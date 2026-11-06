// Byte sources for decoders. HttpFileSource downloads a URL into a cache file while it is being decoded;
// on completion the file is renamed into place, so a streamed track becomes an offline-cached one.
#pragma once
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include "../net/stream.h"
class Decoder;

class ByteSource {
public:
    virtual ~ByteSource() = default;
    virtual long read(void* buf, size_t n) = 0;  // 0 = EOF, -1 = error
    enum Origin { Set, Cur, End };
    virtual bool seek(int64_t off, Origin o) = 0;
    virtual int64_t tell() const = 0;
};

class HttpFileSource final : public ByteSource {
public:
    // final_path: where the finished file lands. Downloads to final_path + ".part".
    HttpFileSource(std::string url, std::string final_path, ConnectOptions opt);
    ~HttpFileSource() override;
    bool start(int header_timeout_ms, std::string& err);  // returns once the status line and length are known
    long read(void* buf, size_t n) override;
    bool seek(int64_t off, Origin o) override;
    int64_t tell() const override { return pos_; }
    int64_t size() const { return size_.load(); }
    // Whole file present from byte 0 (a track entered by a Range jump never completes, so it is not kept as a cache file).
    bool complete() const { return !failed_.load() && win_start_.load() == 0 && size_.load() >= 0 ? avail_.load() == size_.load() : (done_.load() && !failed_.load() && win_start_.load() == 0); }

private:
    void run();
    void request_restart(int64_t at);   // caller holds m_: re-aim the download at `at` with a Range request
    std::string url_, final_, part_;
    ConnectOptions opt_;
    std::thread th_;
    std::mutex m_;
    std::condition_variable cv_;
    int fd_w_ = -1, fd_r_ = -1;
    // The download is one contiguous window [win_start_, avail_) of the file; a seek far outside it restarts the request with Range.
    std::atomic<int64_t> avail_{0}, size_{-1}, win_start_{0};
    int64_t restart_at_ = -1;
    bool range_ok_ = true;
    std::atomic<bool> done_{false}, failed_{false}, cancel_{false}, headers_{false};
    int status_ = 0;
    std::string error_;
    int64_t pos_ = 0;
};

// Opens a track for playback: `permanent_path` (downloaded copy) if it exists, else streams `url` into `stream_path`.
// Blocks on the network, so call it from a worker thread and hand the result to Player::attach.
std::unique_ptr<Decoder> open_track(const std::string& url, const std::string& permanent_path, const std::string& stream_path,
                                    const std::string& kind, const ConnectOptions& opt, std::string& err);
