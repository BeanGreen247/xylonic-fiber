#include "source.h"
#include <chrono>
#include <cstdio>
#include <fcntl.h>
#include <unistd.h>
#include <vector>
#include "../net/http.h"
#include "decoder.h"

HttpFileSource::HttpFileSource(std::string url, std::string final_path, ConnectOptions opt)
    : url_(std::move(url)), final_(std::move(final_path)), part_(final_ + ".part"), opt_(std::move(opt)) {}

HttpFileSource::~HttpFileSource() {
    const bool whole = complete();   // judged before the cancel below marks the transfer failed
    cancel_ = true;
    cv_.notify_all();
    if (th_.joinable()) th_.join();
    if (fd_w_ >= 0) ::close(fd_w_);
    if (fd_r_ >= 0) ::close(fd_r_);
    if (whole) std::rename(part_.c_str(), final_.c_str());
    else ::unlink(part_.c_str());
}

bool HttpFileSource::start(int header_timeout_ms, std::string& err) {
    fd_w_ = ::open(part_.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    fd_r_ = ::open(part_.c_str(), O_RDONLY);
    if (fd_w_ < 0 || fd_r_ < 0) { err = "cannot create cache file"; return false; }
    th_ = std::thread([this] { run(); });
    std::unique_lock<std::mutex> lk(m_);
    cv_.wait_for(lk, std::chrono::milliseconds(header_timeout_ms), [this] { return headers_.load() || failed_.load(); });
    if (failed_ || !headers_ || status_ < 200 || status_ >= 300) {
        err = !error_.empty() ? error_ : (status_ ? "HTTP " + std::to_string(status_) : "timeout");
        return false;
    }
    return true;
}

namespace { constexpr int64_t kJump = 256 * 1024; }   // a seek this far past the downloaded part is cheaper as a new Range request than waiting

void HttpFileSource::request_restart(int64_t at) {
    restart_at_ = at;
    cv_.notify_all();
}

void HttpFileSource::run() {
    int64_t start = 0;
    for (;;) {
        std::vector<std::string> hdr;
        if (start > 0) hdr.push_back("Range: bytes=" + std::to_string(start) + "-");
        { std::lock_guard<std::mutex> lk(m_); win_start_ = start; avail_ = start; done_ = false; restart_at_ = -1; }
        int64_t wrote = 0;
        bool range_refused = false;
        HttpResult r = http_get(url_, [&](const uint8_t* p, size_t n) {
            if (cancel_.load() || range_refused) return false;
            { std::lock_guard<std::mutex> lk(m_); if (restart_at_ >= 0) return false; }
            size_t off = 0;
            while (off < n) {
                ssize_t w = ::pwrite(fd_w_, p + off, n - off, start + wrote + static_cast<int64_t>(off));
                if (w <= 0) return false;
                off += static_cast<size_t>(w);
            }
            wrote += static_cast<int64_t>(n);
            { std::lock_guard<std::mutex> lk(m_); avail_ = start + wrote; }
            cv_.notify_all();
            return true;
        }, opt_, hdr, [&](int status, int64_t len) {
            std::lock_guard<std::mutex> lk(m_);
            if (start > 0 && status == 200) { range_refused = true; range_ok_ = false; }   // server ignores Range: back to sequential
            else if (!headers_.load()) { status_ = status; size_ = len; }
            else if (len >= 0 && status == 206) size_ = start + len;
            headers_ = true;
            cv_.notify_all();
        });
        std::unique_lock<std::mutex> lk(m_);
        if (cancel_.load()) { failed_ = true; done_ = true; break; }
        if (range_refused) { start = 0; continue; }   // sequential download from the top; seeks wait for it
        if (restart_at_ >= 0) { start = restart_at_; continue; }
        if (!r.error.empty()) { error_ = r.error; failed_ = true; }
        else if (r.status < 200 || r.status >= 300) failed_ = true;
        else if (size_.load() >= 0 && avail_.load() != size_.load()) { error_ = "short body"; failed_ = true; }
        done_ = true;
        if (!headers_) headers_ = true;
        cv_.notify_all();
        if (failed_) break;
        // finished: idle until a seek asks for another window (outside the finished one) or the source is dropped
        cv_.wait(lk, [&] { return cancel_.load() || restart_at_ >= 0; });
        if (cancel_.load()) break;
        start = restart_at_;
    }
    cv_.notify_all();
}

long HttpFileSource::read(void* buf, size_t n) {
    std::unique_lock<std::mutex> lk(m_);
    for (;;) {
        if (cancel_.load()) return failed_.load() ? -1 : 0;
        const int64_t ws = win_start_.load(), we = avail_.load(), sz = size_.load();
        const bool in_win = pos_ >= ws && pos_ < we;
        if (in_win) break;
        if (failed_.load()) return -1;
        const bool at_eof = sz >= 0 && pos_ >= sz;
        if (at_eof) return 0;
        // outside the window: jump when the server takes Range requests and the gap is worth it
        const bool behind = pos_ < ws, far_ahead = pos_ >= we + kJump, finished = done_.load();
        if (range_ok_ && restart_at_ < 0 && sz >= 0 && (behind || far_ahead || finished)) { request_restart(pos_); }
        else if (finished && !range_ok_ && !behind) return sz < 0 ? 0 : -1;
        cv_.wait(lk);
    }
    int64_t have = avail_.load() - pos_;
    if (static_cast<int64_t>(n) > have) n = static_cast<size_t>(have);
    lk.unlock();
    ssize_t r = ::pread(fd_r_, buf, n, pos_);
    if (r < 0) return -1;
    pos_ += r;
    return r;
}

bool HttpFileSource::seek(int64_t off, Origin o) {
    int64_t t = o == Set ? off : o == Cur ? pos_ + off : size_.load() + off;
    if (o == End && size_.load() < 0) return false;
    if (t < 0) return false;
    pos_ = t;  // read() decides between waiting for the download and re-aiming it with a Range request
    return true;
}

std::unique_ptr<Decoder> open_track(const std::string& url, const std::string& permanent_path, const std::string& stream_path,
                                    const std::string& kind, const ConnectOptions& opt, std::string& err) {
    if (!permanent_path.empty() && ::access(permanent_path.c_str(), R_OK) == 0) {
        if (auto d = open_decoder(permanent_path, kind)) return d;  // unreadable permanent copy: fall through to the stream
    }
    if (!stream_path.empty() && ::access(stream_path.c_str(), R_OK) == 0) {
        if (auto d = open_decoder(stream_path, kind)) return d;
    }
    auto src = std::make_unique<HttpFileSource>(url, stream_path, opt);
    if (!src->start(opt.timeout_ms, err)) return nullptr;
    auto d = open_decoder_source(std::move(src), kind);
    if (!d) err = "unsupported or invalid audio stream";
    return d;
}
