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
    bool complete() const { return done_.load() && !failed_.load(); }

private:
    void run();
    std::string url_, final_, part_;
    ConnectOptions opt_;
    std::thread th_;
    std::mutex m_;
    std::condition_variable cv_;
    int fd_w_ = -1, fd_r_ = -1;
    std::atomic<int64_t> avail_{0}, size_{-1};
    std::atomic<bool> done_{false}, failed_{false}, cancel_{false}, headers_{false};
    int status_ = 0;
    std::string error_;
    int64_t pos_ = 0;
};
