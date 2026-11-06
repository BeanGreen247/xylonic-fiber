// Single-producer single-consumer ring of int16 samples. Lock-free so the audio callback never blocks.
#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

class SampleRing {
public:
    explicit SampleRing(size_t capacity_pow2) : buf_(capacity_pow2), mask_(capacity_pow2 - 1) {}
    size_t capacity() const { return buf_.size(); }
    size_t readable() const { return head_.load(std::memory_order_acquire) - tail_.load(std::memory_order_relaxed); }
    size_t writable() const { return buf_.size() - (head_.load(std::memory_order_relaxed) - tail_.load(std::memory_order_acquire)); }
    // Producer only.
    size_t write(const int16_t* src, size_t n) {
        size_t h = head_.load(std::memory_order_relaxed);
        size_t w = writable();
        if (n > w) n = w;
        for (size_t i = 0; i < n; ++i) buf_[(h + i) & mask_] = src[i];
        head_.store(h + n, std::memory_order_release);
        return n;
    }
    // Consumer only.
    size_t read(int16_t* dst, size_t n) {
        size_t t = tail_.load(std::memory_order_relaxed);
        size_t r = readable();
        if (n > r) n = r;
        for (size_t i = 0; i < n; ++i) dst[i] = buf_[(t + i) & mask_];
        tail_.store(t + n, std::memory_order_release);
        return n;
    }
    // Consumer side clear (used under a stopped producer).
    void clear() { tail_.store(head_.load(std::memory_order_acquire), std::memory_order_release); }

private:
    std::vector<int16_t> buf_;
    size_t mask_ = 0;
    std::atomic<size_t> head_{0};
    std::atomic<size_t> tail_{0};
};
