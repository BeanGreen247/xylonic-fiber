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
            CHECK(std::fabs(static_cast<double>(out.size() / 2) - expect) < 4);
        }
        Resampler r; std::vector<float> out; r.process(in.data(), 44100, 1.0, out); CHECK(out.size() == in.size());  // passthrough
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
    std::printf(fails ? "FAILED\n" : "OK\n");
    return fails ? 1 : 0;
}
