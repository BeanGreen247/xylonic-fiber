#include "dsp.h"
#include <complex>

namespace {
void fft(std::vector<std::complex<float>>& a) {
    size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        float ang = -6.2831853f / static_cast<float>(len);
        std::complex<float> wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            std::complex<float> w(1, 0);
            for (size_t k = 0; k < len / 2; ++k) {
                std::complex<float> u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}
}  // namespace

void spectrum_bands(const float* mono, size_t n, double rate, float* bands, int nb) {
    std::vector<std::complex<float>> a(n);
    for (size_t i = 0; i < n; ++i) {
        float w = 0.5f - 0.5f * std::cos(6.2831853f * static_cast<float>(i) / static_cast<float>(n - 1));  // Hann
        a[i] = std::complex<float>(mono[i] * w, 0);
    }
    fft(a);
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
