#include "dsp.h"
#include <complex>

namespace {
// Tables for one transform size, built once per thread: bit-reversal, Hann window, per-stage twiddles.
struct FftPlan {
    size_t n = 0, m = 0;   // n real input samples, m = n / 2 complex points
    std::vector<uint32_t> rev;   // bit reversal for m points
    std::vector<float> hann;
    std::vector<std::complex<float>> tw;   // tw[k] = exp(-2 pi i k / n), k < n/2
    std::vector<std::complex<float>> buf;
    void init(size_t size) {
        n = size; m = n / 2;
        rev.assign(m, 0); hann.assign(n, 0); tw.assign(m, {}); buf.assign(m, {});
        for (size_t i = 1, j = 0; i < m; ++i) {
            size_t bit = m >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            rev[i] = static_cast<uint32_t>(j);
        }
        for (size_t i = 0; i < n; ++i) hann[i] = 0.5f - 0.5f * std::cos(6.2831853f * static_cast<float>(i) / static_cast<float>(n - 1));
        for (size_t k = 0; k < m; ++k) {
            const double a = -6.283185307179586 * static_cast<double>(k) / static_cast<double>(n);
            tw[k] = {static_cast<float>(std::cos(a)), static_cast<float>(std::sin(a))};
        }
    }
    // Real input of n samples: even samples as the real part and odd as the imaginary part of an n/2-point complex transform (half the work of a
    // complex transform of the real signal); bin(k) then rebuilds X[k] for k < n/2. Windowed, bit-reversed load, in-place radix-2.
    void run(const float* in) {
        for (size_t i = 0; i < m; ++i) buf[rev[i]] = {in[2 * i] * hann[2 * i], in[2 * i + 1] * hann[2 * i + 1]};
        float* d = reinterpret_cast<float*>(buf.data());   // interleaved re, im: plain float maths (std::complex multiply is a library call with NaN checks)
        const float* w = reinterpret_cast<const float*>(tw.data());
        for (size_t len = 2; len <= m; len <<= 1) {
            const size_t half = len / 2, step = n / len;
            for (size_t i = 0; i < m; i += len)
                for (size_t k = 0; k < half; ++k) {
                    float* a = d + 2 * (i + k);
                    float* b = d + 2 * (i + k + half);
                    const float wr = w[2 * k * step], wi = w[2 * k * step + 1];
                    const float vr = b[0] * wr - b[1] * wi, vi = b[0] * wi + b[1] * wr;
                    b[0] = a[0] - vr; b[1] = a[1] - vi;
                    a[0] += vr; a[1] += vi;
                }
        }
    }
    // |X[k]|^2 of the real signal, k in [0, n/2)
    float power(size_t k) const {
        const std::complex<float> zk = buf[k], zc = std::conj(buf[(m - k) % m]);
        const float er = (zk.real() + zc.real()) * 0.5f, ei = (zk.imag() + zc.imag()) * 0.5f;       // even part
        const float dr = (zk.real() - zc.real()) * 0.5f, di = (zk.imag() - zc.imag()) * 0.5f;       // (Zk - conj Z[m-k]) / 2
        const float orr = di, oi = -dr;                                                              // times -i: the odd part
        const float wr = tw[k].real(), wi = tw[k].imag();
        const float xr = er + orr * wr - oi * wi, xi = ei + orr * wi + oi * wr;
        return xr * xr + xi * xi;
    }
};
}  // namespace

void spectrum_bands(const float* mono, size_t n, double rate, float* bands, int nb) {
    thread_local FftPlan plan;
    // the bin range of every band depends only on (n, rate, nb): computed once, not per frame
    thread_local size_t edge_n = 0; thread_local double edge_rate = 0; thread_local int edge_nb = 0;
    thread_local std::vector<uint32_t> k0s, k1s;
    if (plan.n != n) plan.init(n);
    if (edge_n != n || edge_rate != rate || edge_nb != nb) {
        edge_n = n; edge_rate = rate; edge_nb = nb;
        k0s.assign(static_cast<size_t>(nb), 0); k1s.assign(static_cast<size_t>(nb), 0);
        const double fmin = 40.0, fmax = std::min(16000.0, rate / 2.0);
        for (int b = 0; b < nb; ++b) {
            const double f0 = fmin * std::pow(fmax / fmin, static_cast<double>(b) / nb), f1 = fmin * std::pow(fmax / fmin, static_cast<double>(b + 1) / nb);
            size_t k0 = static_cast<size_t>(f0 * static_cast<double>(n) / rate), k1 = static_cast<size_t>(f1 * static_cast<double>(n) / rate);
            if (k1 <= k0) k1 = k0 + 1;
            k0s[static_cast<size_t>(b)] = static_cast<uint32_t>(k0); k1s[static_cast<size_t>(b)] = static_cast<uint32_t>(std::min(k1, n / 2));
        }
    }
    plan.run(mono);
    const float scale = 1.0f / (static_cast<float>(n) / 4.0f);
    for (int b = 0; b < nb; ++b) {
        float peak2 = 0;
        for (uint32_t k = k0s[static_cast<size_t>(b)]; k < k1s[static_cast<size_t>(b)]; ++k) peak2 = std::max(peak2, plan.power(k));
        const float mag = std::sqrt(peak2) * scale;   // 1.0 = full-scale sine
        bands[b] = std::min(1.0f, std::max(0.0f, (20.0f * std::log10(mag + 1e-6f) + 60.0f) / 60.0f));   // -60 dB .. 0 dB -> 0..1
    }
}
