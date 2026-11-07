// Small DSP blocks used by the playback chain: sinc resampler (speed and rate conversion), RBJ shelf filters, spectrum.
#pragma once
#include <cmath>
#include <cstdint>
#include <vector>

// Streaming windowed-sinc resampler on interleaved stereo float (speed and rate conversion). step = input frames consumed per output
// frame (source_rate * speed / device_rate). Below 1 (upsampling) the kernel is a plain band-limited interpolator; above 1 its cutoff
// drops to the output Nyquist, so 96/192 kHz sources downsample without folding ultrasonic content into the audible band.
// Kernel: Kaiser-windowed sinc, 16 zero crossings each side, tabulated at 256 points per crossing with linear interpolation.
class Resampler {
public:
    Resampler() { build_table(); reset(); }
    void reset() { buf_.assign(kHalf * 2 * 2, 0.0f); base_ = -static_cast<int64_t>(kHalf * 2); pos_ = 0; primed_ = false; }
    // Appends output frames to `out`. `in` holds `frames` stereo frames.
    void process(const float* in, size_t frames, double step, std::vector<float>& out) {
        if (step == 1.0 && !primed_) { out.insert(out.end(), in, in + frames * 2); return; }   // identical rates: pass through bit-exact
        primed_ = true;
        buf_.insert(buf_.end(), in, in + frames * 2);
        const double fc = step > 1.0 ? 1.0 / step : 1.0;                // cutoff relative to the input rate
        const double halfw = static_cast<double>(kHalf) / fc;           // kernel half width in input samples
        const int64_t end = base_ + static_cast<int64_t>(buf_.size() / 2);
        while (pos_ + halfw + 1.0 < static_cast<double>(end)) {
            const int64_t first = static_cast<int64_t>(std::ceil(pos_ - halfw)), last = static_cast<int64_t>(std::floor(pos_ + halfw));
            float l = 0, r = 0;
            for (int64_t k = first; k <= last; ++k) {
                if (k < base_) continue;   // before the stream began: silence
                const double x = (static_cast<double>(k) - pos_) * fc;   // distance in kernel units (zero crossings)
                const float w = weight(x) * static_cast<float>(fc);
                const size_t i = static_cast<size_t>(k - base_) * 2;
                l += buf_[i] * w; r += buf_[i + 1] * w;
            }
            out.push_back(l); out.push_back(r);
            pos_ += step;
        }
        // drop what no future output can touch
        const int64_t keep_from = static_cast<int64_t>(std::floor(pos_ - halfw)) - 1;
        if (keep_from > base_ + 4096) {
            const size_t drop = static_cast<size_t>(keep_from - base_) * 2;
            buf_.erase(buf_.begin(), buf_.begin() + static_cast<long>(drop));
            base_ = keep_from;
        }
    }
    // Zeros through the kernel: the last real samples come out instead of being held back (end of a track).
    void finish(double step, std::vector<float>& out) {
        if (!primed_) return;
        std::vector<float> zeros(static_cast<size_t>(std::ceil(kHalf / std::min(1.0, 1.0 / std::max(step, 1.0)))) * 2 + 8, 0.0f);
        process(zeros.data(), zeros.size() / 2, step, out);
    }
    bool primed() const { return primed_; }
private:
    static constexpr int kHalf = 16, kPer = 256;
    void build_table() {
        tab_.assign(static_cast<size_t>(kHalf) * kPer + 2, 0.0f);
        const double beta = 8.6;   // ~ -90 dB stopband
        auto i0 = [](double x) { double s = 1, t = 1; for (int k = 1; k < 40; ++k) { t *= (x / (2.0 * k)) * (x / (2.0 * k)); s += t; } return s; };
        const double norm = i0(beta);
        for (size_t i = 0; i < tab_.size(); ++i) {
            const double x = static_cast<double>(i) / kPer;   // 0..kHalf zero crossings
            const double sinc = x == 0 ? 1.0 : std::sin(3.14159265358979323846 * x) / (3.14159265358979323846 * x);
            const double u = x / kHalf, win = u >= 1.0 ? 0.0 : i0(beta * std::sqrt(1.0 - u * u)) / norm;
            tab_[i] = static_cast<float>(sinc * win);
        }
    }
    float weight(double x) const {
        const double a = std::fabs(x) * kPer;
        const size_t i = static_cast<size_t>(a);
        if (i + 1 >= tab_.size()) return 0.0f;
        const float f = static_cast<float>(a - static_cast<double>(i));
        return tab_[i] + (tab_[i + 1] - tab_[i]) * f;
    }
    std::vector<float> tab_, buf_;
    int64_t base_ = 0;       // absolute index of buf_[0]
    double pos_ = 0;         // absolute input position of the next output sample
    bool primed_ = false;
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
