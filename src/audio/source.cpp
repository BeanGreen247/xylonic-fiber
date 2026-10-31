#include "source.h"
#include <chrono>
#include <cstdio>
#include <fcntl.h>
#include <unistd.h>
#include "../net/http.h"

HttpFileSource::HttpFileSource(std::string url, std::string final_path, ConnectOptions opt)
    : url_(std::move(url)), final_(std::move(final_path)), part_(final_ + ".part"), opt_(std::move(opt)) {}

HttpFileSource::~HttpFileSource() {
    cancel_ = true;
    cv_.notify_all();
    if (th_.joinable()) th_.join();
    if (fd_w_ >= 0) ::close(fd_w_);
    if (fd_r_ >= 0) ::close(fd_r_);
    if (complete()) std::rename(part_.c_str(), final_.c_str());
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

void HttpFileSource::run() {
    HttpResult r = http_get(url_, [this](const uint8_t* p, size_t n) {
        if (cancel_.load()) return false;
        size_t off = 0;
        while (off < n) {
            ssize_t w = ::write(fd_w_, p + off, n - off);
            if (w <= 0) return false;
            off += static_cast<size_t>(w);
        }
        { std::lock_guard<std::mutex> lk(m_); avail_ += static_cast<int64_t>(n); }
        cv_.notify_all();
        return true;
    }, opt_, {}, [this](int status, int64_t len) {
        std::lock_guard<std::mutex> lk(m_);
        status_ = status;
        size_ = len;
        headers_ = true;
        cv_.notify_all();
    });
    {
        std::lock_guard<std::mutex> lk(m_);
        if (!r.error.empty()) { error_ = r.error; failed_ = true; }
        else if (r.status < 200 || r.status >= 300) failed_ = true;
        else if (size_.load() >= 0 && avail_.load() != size_.load()) { error_ = "short body"; failed_ = true; }
        else if (cancel_.load()) failed_ = true;
        done_ = true;
        if (!headers_) headers_ = true;
    }
    cv_.notify_all();
}

long HttpFileSource::read(void* buf, size_t n) {
    std::unique_lock<std::mutex> lk(m_);
    cv_.wait(lk, [&] { return avail_.load() > pos_ || done_.load() || cancel_.load(); });
    int64_t have = avail_.load() - pos_;
    if (have <= 0) return failed_.load() ? -1 : 0;
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
    pos_ = t;  // reads past the downloaded part block until data arrives
    return true;
}
