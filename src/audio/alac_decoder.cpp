// ALAC (Apple Lossless, up to 32-bit) from MP4/M4A files, through Apple's open-source decoder.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
#include "../../third_party/alac/ALACBitUtilities.h"
#include "../../third_party/alac/ALACDecoder.h"
#include "decoder.h"
#include "mp4.h"

namespace {
using mp4::Mp4Source;
using mp4::Mp4Track;
// Source channel order -> the WAVE order the player expects, for layouts whose order differs.
void alac_reorder(float* p, size_t frames, uint32_t ch) {
    if (ch != 3 && ch != 5 && ch != 6) return;
    for (size_t i = 0; i < frames; ++i, p += ch) {
        const float t[6] = {p[0], p[1], p[2], ch > 3 ? p[3] : 0, ch > 4 ? p[4] : 0, ch > 5 ? p[5] : 0};   // C L R (Ls Rs LFE)
        p[0] = t[1]; p[1] = t[2]; p[2] = t[0];
        if (ch == 5) { p[3] = t[3]; p[4] = t[4]; }
        if (ch == 6) { p[3] = t[5]; p[4] = t[3]; p[5] = t[4]; }
    }
}

// ---------------------------------------------------------------------------------------------------------------- ALAC (MP4)
class AlacDecoder final : public Decoder {
public:
    bool open(const std::string& path) {
        if (!src_.open(path) || !src_.trk_.alac) return false;
        std::vector<uint8_t> cookie = src_.trk_.cfg;
        if (dec_.Init(cookie.data(), static_cast<uint32_t>(cookie.size())) != 0) return false;
        const ALACSpecificConfig& c = dec_.mConfig;
        ch_ = c.numChannels; rate_ = c.sampleRate; bits_ = c.bitDepth; frame_len_ = c.frameLength;
        if (ch_ == 0 || ch_ > 8 || rate_ == 0 || frame_len_ == 0) return false;
        bytes_ = bits_ == 16 ? 2 : bits_ == 32 ? 4 : 3;
        buf_.resize(static_cast<size_t>(frame_len_) * ch_ * bytes_);
        pcm_.resize(static_cast<size_t>(frame_len_) * ch_);
        const Mp4Track& t = src_.trk_;
        const double ts = static_cast<double>(t.timescale);
        skip_start_ = static_cast<uint64_t>(static_cast<double>(t.edit_start) * rate_ / ts);
        total_ = static_cast<uint64_t>(static_cast<double>(t.edit_len ? t.edit_len : t.duration - std::min<uint64_t>(t.duration, static_cast<uint64_t>(t.edit_start))) * rate_ / ts);
        to_skip_ = skip_start_;
        return true;
    }
    uint32_t sample_rate() const override { return rate_; }
    uint32_t channels() const override { return ch_; }
    uint64_t total_frames() const override { return total_; }
    uint32_t bits() const override { return bits_; }
    const char* codec() const override { return "ALAC"; }
    uint64_t read(float* out, uint64_t frames) override {
        uint64_t done = 0;
        while (done < frames) {
            if (avail_ == 0) { if (!next_packet()) break; continue; }
            const uint64_t take = std::min<uint64_t>(frames - done, avail_);
            std::memcpy(out + done * ch_, pcm_.data() + off_ * ch_, static_cast<size_t>(take) * ch_ * sizeof(float));
            off_ += take; avail_ -= take; done += take; played_ += take;
        }
        if (total_ && played_ > total_) done -= std::min<uint64_t>(done, played_ - total_);
        return done;
    }
    bool seek(uint64_t frame) override {
        const uint64_t abs = frame + skip_start_;
        next_ = static_cast<size_t>(abs / frame_len_);
        to_skip_ = abs % frame_len_;
        avail_ = 0; off_ = 0;
        played_ = frame;
        return true;
    }
private:
    bool next_packet() {
        for (;;) {
            if (next_ >= src_.trk_.offset.size()) return false;
            if (!src_.sample(next_++, in_)) return false;
            BitBuffer bits;
            BitBufferInit(&bits, in_.data(), static_cast<uint32_t>(in_.size()));
            uint32_t n = 0;
            if (dec_.Decode(&bits, buf_.data(), frame_len_, ch_, &n) != 0 || n == 0) continue;
            convert(n);
            const uint64_t skip = std::min<uint64_t>(to_skip_, n);
            to_skip_ -= skip;
            off_ = skip; avail_ = n - skip;
            if (avail_ > 0) return true;
        }
    }
    void convert(uint32_t frames) {
        const size_t n = static_cast<size_t>(frames) * ch_;
        const uint8_t* p = buf_.data();
        if (bytes_ == 2) for (size_t i = 0; i < n; ++i) pcm_[i] = static_cast<float>(static_cast<int16_t>(p[i * 2] | p[i * 2 + 1] << 8)) * (1.0f / 32768.0f);
        else if (bytes_ == 4) for (size_t i = 0; i < n; ++i) pcm_[i] = static_cast<float>(static_cast<double>(static_cast<int32_t>(p[i * 4] | p[i * 4 + 1] << 8 | p[i * 4 + 2] << 16 | static_cast<uint32_t>(p[i * 4 + 3]) << 24)) / 2147483648.0);
        else for (size_t i = 0; i < n; ++i) {   // 20/24 bit, packed little endian
            int32_t v = (p[i * 3] | p[i * 3 + 1] << 8 | p[i * 3 + 2] << 16);
            if (v & 0x800000) v |= static_cast<int32_t>(0xff000000u);
            pcm_[i] = static_cast<float>(v) * (1.0f / 8388608.0f) * (bits_ == 20 ? 16.0f : 1.0f);   // 20-bit samples arrive left-aligned in 24
        }
        alac_reorder(pcm_.data(), frames, ch_);
    }
    Mp4Source src_;
    ::ALACDecoder dec_;
    std::vector<uint8_t> in_, buf_;
    std::vector<float> pcm_;
    uint32_t ch_ = 0, rate_ = 0, bits_ = 0, bytes_ = 0, frame_len_ = 0;
    uint64_t total_ = 0, skip_start_ = 0, to_skip_ = 0, played_ = 0, avail_ = 0, off_ = 0;
    size_t next_ = 0;
};

}  // namespace

std::unique_ptr<Decoder> open_alac_mp4_file(const std::string& path) {
    auto d = std::make_unique<AlacDecoder>();
    if (!d->open(path)) return nullptr;
    return d;
}
