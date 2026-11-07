#include "source.h"
#include <chrono>
#include <cstring>
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

bool HttpFileSource::wait_complete(int timeout_ms) {
    std::unique_lock<std::mutex> lk(m_);
    cv_.wait_for(lk, std::chrono::milliseconds(timeout_ms), [this] { return done_.load() || failed_.load() || cancel_.load(); });
    return done_.load() && !failed_.load() && win_start_.load() == 0 && (size_.load() < 0 || avail_.load() == size_.load());
}

bool HttpFileSource::seek(int64_t off, Origin o) {
    int64_t t = o == Set ? off : o == Cur ? pos_ + off : size_.load() + off;
    if (o == End && size_.load() < 0) return false;
    if (t < 0) return false;
    pos_ = t;  // read() decides between waiting for the download and re-aiming it with a Range request
    return true;
}

// The decoder a local file needs, from its first bytes: a download may hold a different format than the song's suffix says (a FLAC fetched as
// MP3, or the reverse). Empty when the header is not one of the formats the player reads, so the caller keeps its own guess.
std::string sniff_audio_kind(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return std::string();
    unsigned char b[96] = {};
    const size_t n = std::fread(b, 1, sizeof b, f);
    std::fclose(f);
    if (n < 4) return std::string();
    auto is = [&](size_t at, const char* s, size_t len) { return at + len <= n && std::memcmp(b + at, s, len) == 0; };
    if (is(0, "fLaC", 4)) return "flac";
    if (is(0, "RIFF", 4) || is(0, "RF64", 4) || is(0, "riff", 4)) return "wav";   // RIFF, RF64, Wave64
    if (is(0, "FORM", 4) && (is(8, "AIFF", 4) || is(8, "AIFC", 4))) return "aiff";
    if (is(4, "ftyp", 4)) return "m4a";
    if (is(0, "OggS", 4) && n >= 28) {
        const size_t off = 27u + b[26];   // first packet follows the page header and its segment table
        if (is(off, "OpusHead", 8)) return "opus";
        if (is(off + 1, "vorbis", 6)) return "vorbis";
        if (off < n && b[off] == 0x7f && is(off + 1, "FLAC", 4)) return "oggflac";
        return std::string();             // other Ogg payloads are not read
    }
    if (is(0, "ID3", 3)) return "mp3";
    if (b[0] == 0xFF && (b[1] & 0xF6) == 0xF0) return "aac";   // ADTS: sync + layer 00
    if (b[0] == 0xFF && (b[1] & 0xE0) == 0xE0) return "mp3";
    return std::string();
}

std::unique_ptr<Decoder> open_track(const std::string& url, const std::string& permanent_path, const std::string& stream_path,
                                    const std::string& kind, const ConnectOptions& opt, std::string& err) {
    if (!permanent_path.empty() && ::access(permanent_path.c_str(), R_OK) == 0) {
        const std::string sk = sniff_audio_kind(permanent_path);
        if (auto d = open_decoder(permanent_path, sk.empty() ? kind : sk)) return d;  // unreadable permanent copy: fall through to the stream
    }
    if (!stream_path.empty() && ::access(stream_path.c_str(), R_OK) == 0) {
        const std::string sk = sniff_audio_kind(stream_path);
        if (auto d = open_decoder(stream_path, sk.empty() ? kind : sk)) return d;
    }
    auto src = std::make_unique<HttpFileSource>(url, stream_path, opt);
    if (!src->start(opt.timeout_ms, err)) return nullptr;
    if (!decoder_kind_streams(kind)) {   // Ogg, Opus, M4A, AAC, AIFF need the whole file (or its index at the end): download it, then decode from disk
        if (!src->wait_complete(180000)) { err = "download did not finish"; return nullptr; }
        src.reset();   // renames the finished .part into stream_path
        const std::string sk = sniff_audio_kind(stream_path);
        auto d = open_decoder(stream_path, sk.empty() ? kind : sk);
        if (!d) err = "unsupported or invalid audio file";
        return d;
    }
    auto d = open_decoder_source(std::move(src), kind);
    if (!d) err = "unsupported or invalid audio stream";
    return d;
}
