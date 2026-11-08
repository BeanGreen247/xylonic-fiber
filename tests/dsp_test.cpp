#include <cmath>
#include <cstdio>
#include <vector>
#include "../src/audio/dsp.h"
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)
static float rms(const std::vector<float>& v, size_t from) { double s = 0; size_t n = 0; for (size_t i = from; i < v.size(); ++i) { s += v[i] * v[i]; ++n; } return n ? std::sqrt(s / n) : 0; }
static std::vector<float> sine(double f, double rate, size_t frames) { std::vector<float> v(frames * 2); for (size_t i = 0; i < frames; ++i) v[2 * i] = v[2 * i + 1] = 0.5f * std::sin(6.283185307 * f * i / rate); return v; }
int main() {
    { // resampler: output length scales with 1/step, tone stays continuous
        auto in = sine(440, 44100, 44100);
        for (double step : {0.5, 1.0, 2.0, 1.0881}) {
            Resampler r; std::vector<float> out; r.process(in.data(), 44100, step, out);
            double expect = 44100.0 / step;
            CHECK(static_cast<double>(out.size() / 2) <= expect + 1 && static_cast<double>(out.size() / 2) > expect - 70);   // the kernel holds back its last half width
            r.finish(step, out);   // end of track: the held-back tail is delivered
            CHECK(static_cast<double>(out.size() / 2) >= expect - 2);
        }
        Resampler r; std::vector<float> out; r.process(in.data(), 44100, 1.0, out); CHECK(out.size() == in.size());  // passthrough
    }
    { // sinc resampler quality: in-band tones pass at unity gain, tones above the output Nyquist are removed instead of folding down
        auto tone = [&](double f, double rate, double step) {
            auto in = sine(f, rate, static_cast<size_t>(rate));
            Resampler r; std::vector<float> out;
            for (size_t off = 0; off < in.size() / 2; off += 1024) r.process(in.data() + off * 2, std::min<size_t>(1024, in.size() / 2 - off), step, out);   // streamed in blocks
            std::vector<float> left; for (size_t i = 0; i + 1 < out.size(); i += 2) left.push_back(out[i]);
            return rms(left, left.size() / 4) * 1.4142136f;   // amplitude of the steady part
        };
        const float pass = tone(1000, 96000, 2.0), pass2 = tone(10000, 44100, 44100.0 / 48000.0), alias = tone(30000, 96000, 2.0), alias2 = tone(40000, 192000, 4.0);
        CHECK(pass > 0.48f && pass < 0.52f);      // 0.5 in, 0.5 out (96k -> 48k)
        CHECK(pass2 > 0.48f && pass2 < 0.52f);    // 44.1k -> 48k keeps a 10 kHz tone
        CHECK(alias < 0.01f);                      // 30 kHz at 96k would fold to 18 kHz at 48k: must be filtered away
        CHECK(alias2 < 0.01f);                     // 40 kHz at 192k -> 48k likewise
    }
    { // shelves: +12 dB low shelf boosts 60 Hz, leaves 5 kHz; high shelf the opposite
        auto lo = sine(60, 44100, 44100), hi = sine(5000, 44100, 44100);
        Shelf l; l.design(false, 44100, 100, 12);
        auto lo2 = lo; l.process(lo2.data(), 44100);
        float g = rms(lo2, 20000) / rms(lo, 20000); CHECK(g > 3.0f && g < 4.5f);  // ~ +12 dB = x3.98
        l.clear(); auto hi2 = hi; l.process(hi2.data(), 44100); float g2 = rms(hi2, 20000) / rms(hi, 20000); CHECK(g2 > 0.9f && g2 < 1.1f);
        Shelf h; h.design(true, 44100, 8000, -12);
        auto hi3 = sine(15000, 44100, 44100); auto hi4 = hi3; h.process(hi4.data(), 44100);
        float g3 = rms(hi4, 20000) / rms(hi3, 20000); CHECK(g3 < 0.4f);
    }
    { // spectrum: a 1 kHz sine lights the band containing 1 kHz most
        const size_t n = 2048; std::vector<float> m(n);
        for (size_t i = 0; i < n; ++i) m[i] = 0.8f * std::sin(6.283185307 * 1000.0 * i / 44100.0);
        float b[24]; spectrum_bands(m.data(), n, 44100, b, 24);
        int best = 0; for (int i = 1; i < 24; ++i) if (b[i] > b[best]) best = i;
        double lo = 40.0 * std::pow(16000.0 / 40.0, best / 24.0), hi = 40.0 * std::pow(16000.0 / 40.0, (best + 1) / 24.0);
        CHECK(lo <= 1000.0 && 1000.0 <= hi * 1.05 && b[best] > 0.7f);
        std::vector<float> z(n, 0.0f); spectrum_bands(z.data(), n, 44100, b, 24); CHECK(b[5] < 0.01f);
    }
    { // the half-size real transform must agree with a brute-force windowed DFT on a noisy multi-tone signal
        const size_t n = 512; const int nb = 12; const double rate = 44100;
        std::vector<float> m(n); unsigned seed = 12345;
        for (size_t i = 0; i < n; ++i) { seed = seed * 1664525u + 1013904223u; m[i] = 0.3f * std::sin(6.283185307 * 700.0 * i / rate) + 0.2f * std::sin(6.283185307 * 5200.0 * i / rate) + 0.05f * (static_cast<float>(seed >> 8) / 8388608.0f - 1.0f); }
        float got[12]; spectrum_bands(m.data(), n, rate, got, nb);
        const double fmin = 40.0, fmax = 16000.0;
        double worst = 0;
        for (int b = 0; b < nb; ++b) {
            const double f0 = fmin * std::pow(fmax / fmin, double(b) / nb), f1 = fmin * std::pow(fmax / fmin, double(b + 1) / nb);
            size_t k0 = size_t(f0 * n / rate), k1 = size_t(f1 * n / rate); if (k1 <= k0) k1 = k0 + 1; if (k1 > n / 2) k1 = n / 2;
            double peak2 = 0;
            for (size_t k = k0; k < k1; ++k) {
                double re = 0, im = 0;
                for (size_t i = 0; i < n; ++i) { const double w = 0.5 - 0.5 * std::cos(6.283185307179586 * i / double(n - 1)); const double a2 = -6.283185307179586 * double(k) * double(i) / double(n); re += m[i] * w * std::cos(a2); im += m[i] * w * std::sin(a2); }
                peak2 = std::max(peak2, re * re + im * im);
            }
            const double mag = std::sqrt(peak2) / (n / 4.0), ref = std::min(1.0, std::max(0.0, (20.0 * std::log10(mag + 1e-6) + 60.0) / 60.0));
            worst = std::max(worst, std::fabs(ref - double(got[b])));
        }
        CHECK(worst < 0.01);
    }
    std::printf(fails ? "FAILED\n" : "OK\n");
    return fails ? 1 : 0;
}
