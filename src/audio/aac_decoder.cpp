// AAC (LC, HE-AAC v1/v2 through fdk-aac) from MP4/M4A files, plus raw ADTS .aac.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
#include <aacdecoder_lib.h>
#include "decoder.h"
#include "mp4.h"

namespace {
using mp4::Mp4Source;
using mp4::Mp4Track;
void to_float16(const int16_t* in, size_t n, float* out) { for (size_t i = 0; i < n; ++i) out[i] = static_cast<float>(in[i]) * (1.0f / 32768.0f); }

// ---------------------------------------------------------------------------------------------------------------- AAC (MP4)
class AacMp4Decoder final : public Decoder {
public:
    ~AacMp4Decoder() override { if (dec_) aacDecoder_Close(dec_); }
    bool open(const std::string& path) {
        if (!src_.open(path) || !src_.trk_.aac) return false;
        dec_ = aacDecoder_Open(TT_MP4_RAW, 1);
        if (!dec_) return false;
        UCHAR* conf[1] = {src_.trk_.cfg.data()};
        UINT sz = static_cast<UINT>(src_.trk_.cfg.size());
        if (aacDecoder_ConfigRaw(dec_, conf, &sz) != AAC_DEC_OK) return false;
        aacDecoder_SetParam(dec_, AAC_PCM_MAX_OUTPUT_CHANNELS, 8);
        // decode the first access unit to learn the output rate and channel count (HE-AAC doubles the rate; PS adds a channel)
        if (!decode_au(0)) return false;
        const CStreamInfo* info = aacDecoder_GetStreamInfo(dec_);
        if (!info || info->sampleRate <= 0 || info->numChannels <= 0) return false;
        rate_ = static_cast<uint32_t>(info->sampleRate); ch_ = static_cast<uint32_t>(info->numChannels);
        const Mp4Track& t = src_.trk_;
        const double ts = static_cast<double>(t.timescale);
        skip_start_ = static_cast<uint64_t>(static_cast<double>(t.edit_start) * rate_ / ts);
        const uint64_t media = static_cast<uint64_t>(static_cast<double>(t.edit_len ? t.edit_len : t.duration - std::min<uint64_t>(t.duration, static_cast<uint64_t>(t.edit_start))) * rate_ / ts);
        total_ = media;
        reset_to(0);
        return true;
    }
    uint32_t sample_rate() const override { return rate_; }
    uint32_t channels() const override { return ch_; }
    uint64_t total_frames() const override { return total_; }
    uint32_t bits() const override { return 0; }
    const char* codec() const override { return "AAC"; }
    uint64_t read(float* out, uint64_t frames) override {
        uint64_t done = 0;
        while (done < frames) {
            if (avail_ == 0) { if (!next_au()) break; continue; }
            const uint64_t take = std::min<uint64_t>(frames - done, avail_);
            std::memcpy(out + done * ch_, pcm_.data() + off_ * ch_, static_cast<size_t>(take) * ch_ * sizeof(float));
            off_ += take; avail_ -= take; done += take; played_ += take;
        }
        if (total_ && played_ > total_) done -= std::min<uint64_t>(done, played_ - total_);
        return done;
    }
    bool seek(uint64_t frame) override {
        const Mp4Track& t = src_.trk_;
        const uint64_t target_abs = frame + skip_start_;
        const uint64_t tdts = static_cast<uint64_t>(static_cast<double>(target_abs) * t.timescale / rate_);
        size_t j = static_cast<size_t>(std::upper_bound(t.dts.begin(), t.dts.end() - 1, tdts) - t.dts.begin());
        j = j > 0 ? j - 1 : 0;
        const size_t start = j >= 3 ? j - 3 : 0;   // a few access units of preroll so the filterbank state is right
        reset_to(start);
        const uint64_t at = static_cast<uint64_t>(static_cast<double>(t.dts[start]) * rate_ / t.timescale);
        to_skip_ = target_abs > at ? target_abs - at : 0;
        played_ = frame;
        return true;
    }
private:
    void reset_to(size_t au) {
        // flush by reopening the config: fdk keeps overlap state that a jump must not reuse
        UCHAR* conf[1] = {src_.trk_.cfg.data()};
        UINT sz = static_cast<UINT>(src_.trk_.cfg.size());
        aacDecoder_ConfigRaw(dec_, conf, &sz);
        aacDecoder_SetParam(dec_, AAC_TPDEC_CLEAR_BUFFER, 1);
        next_ = au; avail_ = 0; off_ = 0;
        to_skip_ = skip_start_;
        played_ = 0;
    }
    bool decode_au(size_t i) {
        if (!src_.sample(i, au_)) return false;
        UCHAR* in[1] = {au_.data()};
        UINT sz = static_cast<UINT>(au_.size()), valid = sz;
        if (aacDecoder_Fill(dec_, in, &sz, &valid) != AAC_DEC_OK) return false;
        raw_.resize(2048 * 8 * 2);
        const AAC_DECODER_ERROR e = aacDecoder_DecodeFrame(dec_, reinterpret_cast<INT_PCM*>(raw_.data()), static_cast<INT>(raw_.size()), 0);
        return e == AAC_DEC_OK || e == AAC_DEC_NOT_ENOUGH_BITS;
    }
    bool next_au() {
        for (;;) {
            if (next_ >= src_.trk_.offset.size()) return false;
            const size_t i = next_++;
            if (!decode_au(i)) continue;   // a damaged access unit is skipped
            const CStreamInfo* info = aacDecoder_GetStreamInfo(dec_);
            if (!info || info->frameSize <= 0) continue;
            const size_t n = static_cast<size_t>(info->frameSize);
            pcm_.resize(n * ch_);
            to_float16(reinterpret_cast<const int16_t*>(raw_.data()), n * ch_, pcm_.data());
            const uint64_t skip = std::min<uint64_t>(to_skip_, n);
            to_skip_ -= skip;
            off_ = skip; avail_ = n - skip;
            if (avail_ > 0) return true;
        }
    }
    Mp4Source src_;
    HANDLE_AACDECODER dec_ = nullptr;
    std::vector<uint8_t> au_;
    std::vector<int16_t> raw_;
    std::vector<float> pcm_;
    uint32_t rate_ = 0, ch_ = 0;
    uint64_t total_ = 0, skip_start_ = 0, to_skip_ = 0, played_ = 0, avail_ = 0, off_ = 0;
    size_t next_ = 0;
};

// ---------------------------------------------------------------------------------------------------------------- raw AAC (ADTS)
class AdtsDecoder final : public Decoder {
public:
    ~AdtsDecoder() override { if (dec_) aacDecoder_Close(dec_); if (f_) std::fclose(f_); }
    bool open(const std::string& path) {
        f_ = std::fopen(path.c_str(), "rb");
        if (!f_) return false;
        // index frames: offset + length from each 7/9 byte header
        std::fseek(f_, 0, SEEK_END);
        const long fsize = std::ftell(f_);
        long pos = 0;
        uint8_t h[9];
        while (pos + 7 <= fsize) {
            std::fseek(f_, pos, SEEK_SET);
            if (std::fread(h, 1, 7, f_) != 7) break;
            if (h[0] != 0xFF || (h[1] & 0xF6) != 0xF0) {   // junk (an ID3v2 tag at the start, or damage): resync byte by byte
                ++pos; continue;
            }
            const uint32_t len = static_cast<uint32_t>((h[3] & 3) << 11 | h[4] << 3 | h[5] >> 5);
            if (len < 7 || pos + len > fsize) break;
            frames_.push_back({pos, len});
            pos += len;
        }
        if (frames_.empty()) return false;
        dec_ = aacDecoder_Open(TT_MP4_ADTS, 1);
        if (!dec_) return false;
        aacDecoder_SetParam(dec_, AAC_PCM_MAX_OUTPUT_CHANNELS, 8);
        if (!decode(0)) return false;
        const CStreamInfo* info = aacDecoder_GetStreamInfo(dec_);
        if (!info || info->sampleRate <= 0 || info->numChannels <= 0) return false;
        rate_ = static_cast<uint32_t>(info->sampleRate); ch_ = static_cast<uint32_t>(info->numChannels);
        unit_ = static_cast<uint64_t>(info->frameSize > 0 ? info->frameSize : 1024);
        total_ = unit_ * frames_.size();
        rewind(0);
        return true;
    }
    uint32_t sample_rate() const override { return rate_; }
    uint32_t channels() const override { return ch_; }
    uint64_t total_frames() const override { return total_; }
    uint32_t bits() const override { return 0; }
    const char* codec() const override { return "AAC"; }
    uint64_t read(float* out, uint64_t frames) override {
        uint64_t done = 0;
        while (done < frames) {
            if (avail_ == 0) { if (!next_frame()) break; continue; }
            const uint64_t take = std::min<uint64_t>(frames - done, avail_);
            std::memcpy(out + done * ch_, pcm_.data() + off_ * ch_, static_cast<size_t>(take) * ch_ * sizeof(float));
            off_ += take; avail_ -= take; done += take;
        }
        return done;
    }
    bool seek(uint64_t frame) override {
        const size_t j = std::min<size_t>(static_cast<size_t>(frame / unit_), frames_.size() - 1);
        const size_t start = j >= 3 ? j - 3 : 0;
        rewind(start);
        to_skip_ = frame - static_cast<uint64_t>(start) * unit_;
        return true;
    }
private:
    struct Frame { long off; uint32_t len; };
    void rewind(size_t i) {
        aacDecoder_SetParam(dec_, AAC_TPDEC_CLEAR_BUFFER, 1);
        next_ = i; avail_ = 0; off_ = 0; to_skip_ = 0;
    }
    bool decode(size_t i) {
        buf_.resize(frames_[i].len);
        if (std::fseek(f_, frames_[i].off, SEEK_SET) != 0 || std::fread(buf_.data(), 1, buf_.size(), f_) != buf_.size()) return false;
        UCHAR* in[1] = {buf_.data()};
        UINT sz = static_cast<UINT>(buf_.size()), valid = sz;
        if (aacDecoder_Fill(dec_, in, &sz, &valid) != AAC_DEC_OK) return false;
        raw_.resize(2048 * 8 * 2);
        const AAC_DECODER_ERROR e = aacDecoder_DecodeFrame(dec_, reinterpret_cast<INT_PCM*>(raw_.data()), static_cast<INT>(raw_.size()), 0);
        return e == AAC_DEC_OK || e == AAC_DEC_NOT_ENOUGH_BITS;
    }
    bool next_frame() {
        for (;;) {
            if (next_ >= frames_.size()) return false;
            if (!decode(next_++)) continue;
            const CStreamInfo* info = aacDecoder_GetStreamInfo(dec_);
            if (!info || info->frameSize <= 0) continue;
            const size_t n = static_cast<size_t>(info->frameSize);
            pcm_.resize(n * ch_);
            to_float16(reinterpret_cast<const int16_t*>(raw_.data()), n * ch_, pcm_.data());
            const uint64_t skip = std::min<uint64_t>(to_skip_, n);
            to_skip_ -= skip;
            off_ = skip; avail_ = n - skip;
            if (avail_ > 0) return true;
        }
    }
    std::FILE* f_ = nullptr;
    HANDLE_AACDECODER dec_ = nullptr;
    std::vector<Frame> frames_;
    std::vector<uint8_t> buf_;
    std::vector<int16_t> raw_;
    std::vector<float> pcm_;
    uint32_t rate_ = 0, ch_ = 0;
    uint64_t total_ = 0, unit_ = 1024, to_skip_ = 0, avail_ = 0, off_ = 0;
    size_t next_ = 0;
};
}  // namespace

std::unique_ptr<Decoder> open_aac_mp4_file(const std::string& path) {
    auto d = std::make_unique<AacMp4Decoder>();
    if (!d->open(path)) return nullptr;
    return d;
}

std::unique_ptr<Decoder> open_adts_file(const std::string& path) {
    auto d = std::make_unique<AdtsDecoder>();
    if (!d->open(path)) return nullptr;
    return d;
}
