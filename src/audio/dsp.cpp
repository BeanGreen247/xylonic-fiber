#include "dsp.h"
#include <complex>

namespace {
// Tables for one transform size, built once per thread: bit-reversal, Hann window, per-stage twiddles.
struct FftPlan {
    size_t n = 0;
    std::vector<uint32_t> rev;
    std::vector<float> hann;
    std::vector<std::complex<float>> tw;   // tw[k] = exp(-2 pi i k / n), k < n/2
    std::vector<std::complex<float>> buf;
    void init(size_t size) {
        n = size;
        rev.assign(n, 0); hann.assign(n, 0); tw.assign(n / 2, {}); buf.assign(n, {});
        for (size_t i = 1, j = 0; i < n; ++i) {
            size_t bit = n >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            rev[i] = static_cast<uint32_t>(j);
        }
        for (size_t i = 0; i < n; ++i) hann[i] = 0.5f - 0.5f * std::cos(6.2831853f * static_cast<float>(i) / static_cast<float>(n - 1));
        for (size_t k = 0; k < n / 2; ++k) {
            const double a = -6.283185307179586 * static_cast<double>(k) / static_cast<double>(n);
            tw[k] = {static_cast<float>(std::cos(a)), static_cast<float>(std::sin(a))};
        }
    }
    // windowed, bit-reversed load then in-place radix-2
    void run(const float* in) {
        for (size_t i = 0; i < n; ++i) buf[rev[i]] = {in[i] * hann[i], 0.0f};
        for (size_t len = 2; len <= n; len <<= 1) {
            const size_t half = len / 2, step = n / len;
            for (size_t i = 0; i < n; i += len)
                for (size_t k = 0; k < half; ++k) {
                    const std::complex<float> u = buf[i + k], v = buf[i + k + half] * tw[k * step];
                    buf[i + k] = u + v;
                    buf[i + k + half] = u - v;
                }
        }
    }
};
}  // namespace

void spectrum_bands(const float* mono, size_t n, double rate, float* bands, int nb) {
    thread_local FftPlan plan;
    if (plan.n != n) plan.init(n);
    plan.run(mono);
    const std::vector<std::complex<float>>& a = plan.buf;
    const double fmin = 40.0, fmax = std::min(16000.0, rate / 2.0);
    for (int b = 0; b < nb; ++b) {
        double f0 = fmin * std::pow(fmax / fmin, static_cast<double>(b) / nb), f1 = fmin * std::pow(fmax / fmin, static_cast<double>(b + 1) / nb);
        size_t k0 = static_cast<size_t>(f0 * static_cast<double>(n) / rate), k1 = static_cast<size_t>(f1 * static_cast<double>(n) / rate);
        if (k1 <= k0) k1 = k0 + 1;
        float peak = 0;
        for (size_t k = k0; k < k1 && k < n / 2; ++k) peak = std::max(peak, std::abs(a[k]));
        float mag = peak / (static_cast<float>(n) / 4.0f);  // 1.0 = full-scale sine
        bands[b] = std::min(1.0f, std::max(0.0f, (20.0f * std::log10(mag + 1e-6f) + 60.0f) / 60.0f));  // -60 dB .. 0 dB -> 0..1
    }
}
