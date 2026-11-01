// Small DSP blocks used by the playback chain: linear resampler (speed and rate conversion), RBJ shelf filters, spectrum.
#pragma once
#include <cmath>
#include <cstdint>
#include <vector>

// Streaming linear resampler on interleaved stereo float. step = input frames consumed per output frame
// (source_rate * speed / device_rate). Carries its fractional position and one frame of history across calls.
class Resampler {
public:
    void reset() { pos_ = 0; have_prev_ = false; }
    // Appends output frames to `out`. `in` holds `frames` stereo frames.
    void process(const float* in, size_t frames, double step, std::vector<float>& out) {
        if (step == 1.0) { out.insert(out.end(), in, in + frames * 2); reset(); return; }  // integer positions: no interpolation needed
        for (size_t i = 0; i < frames; ++i) {
            const float* cur = in + i * 2;
            if (!have_prev_) { prev_[0] = cur[0]; prev_[1] = cur[1]; have_prev_ = true; continue; }
            // pos_ is the position of the next output sample measured from prev_ (0..1 between prev_ and cur)
            while (pos_ < 1.0) {
                float f = static_cast<float>(pos_);
                out.push_back(prev_[0] + (cur[0] - prev_[0]) * f);
                out.push_back(prev_[1] + (cur[1] - prev_[1]) * f);
                pos_ += step;
            }
            pos_ -= 1.0;
            prev_[0] = cur[0]; prev_[1] = cur[1];
        }
    }
private:
    double pos_ = 0;
    bool have_prev_ = false;
    float prev_[2] = {0, 0};
};

// Biquad shelf (RBJ cookbook), stereo.
class Shelf {
public:
    void design(bool high, double rate, double freq, double gain_db) {
        double A = std::pow(10.0, gain_db / 40.0), w0 = 6.283185307179586 * freq / rate, c = std::cos(w0), s = std::sin(w0);
        double alpha = s / 2.0 * std::sqrt(2.0);  // shelf slope 1
        double sq = 2.0 * std::sqrt(A) * alpha, a0, a1, a2, b0, b1, b2;
        if (!high) {
            b0 = A * ((A + 1) - (A - 1) * c + sq); b1 = 2 * A * ((A - 1) - (A + 1) * c); b2 = A * ((A + 1) - (A - 1) * c - sq);
            a0 = (A + 1) + (A - 1) * c + sq; a1 = -2 * ((A - 1) + (A + 1) * c); a2 = (A + 1) + (A - 1) * c - sq;
        } else {
            b0 = A * ((A + 1) + (A - 1) * c + sq); b1 = -2 * A * ((A - 1) + (A + 1) * c); b2 = A * ((A + 1) + (A - 1) * c - sq);
            a0 = (A + 1) - (A - 1) * c + sq; a1 = 2 * ((A - 1) - (A + 1) * c); a2 = (A + 1) - (A - 1) * c - sq;
        }
        b0_ = static_cast<float>(b0 / a0); b1_ = static_cast<float>(b1 / a0); b2_ = static_cast<float>(b2 / a0);
        a1_ = static_cast<float>(a1 / a0); a2_ = static_cast<float>(a2 / a0);
    }
    void process(float* x, size_t frames) {  // interleaved stereo, in place
        for (size_t i = 0; i < frames; ++i)
            for (int ch = 0; ch < 2; ++ch) {
                float in = x[i * 2 + ch];
                float out = b0_ * in + z1_[ch];
                z1_[ch] = b1_ * in - a1_ * out + z2_[ch];
                z2_[ch] = b2_ * in - a2_ * out;
                x[i * 2 + ch] = out;
            }
    }
    void clear() { z1_[0] = z1_[1] = z2_[0] = z2_[1] = 0; }
private:
    float b0_ = 1, b1_ = 0, b2_ = 0, a1_ = 0, a2_ = 0, z1_[2] = {0, 0}, z2_[2] = {0, 0};
};

// Magnitude spectrum of the latest samples folded into log-spaced bands (Pulse Deck: 24 bands, 40 Hz .. 16 kHz).
void spectrum_bands(const float* mono, size_t n /* power of two */, double rate, float* bands, int nbands);
