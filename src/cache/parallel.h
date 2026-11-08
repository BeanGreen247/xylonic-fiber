// Spreads I/O-bound work (unlink, stat) across the CPU cores. fn(i) runs for i in [0, n) and must be thread-safe.
#pragma once
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <thread>
#include <vector>

namespace xf::cache {

template <class F> void parallel_for(size_t n, F&& fn, size_t min_parallel = 256, size_t max_threads = 8) {
    const size_t cores = std::max<size_t>(1, std::thread::hardware_concurrency());
    const size_t t = std::min<size_t>(max_threads, std::max<size_t>(2, cores));
    if (n < min_parallel || t < 2) { for (size_t i = 0; i < n; ++i) fn(i); return; }
    std::atomic<size_t> next{0};
    auto work = [&] { for (size_t b = 0; (b = next.fetch_add(64)) < n;) for (size_t i = b; i < std::min(n, b + 64); ++i) fn(i); };
    std::vector<std::thread> th;
    th.reserve(t - 1);
    for (size_t k = 1; k < t; ++k) th.emplace_back(work);
    work();
    for (auto& x : th) x.join();
}

}  // namespace xf::cache
