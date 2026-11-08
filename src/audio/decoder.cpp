#define DR_MP3_IMPLEMENTATION
#define DR_FLAC_IMPLEMENTATION
#define DR_WAV_IMPLEMENTATION
#define DR_MP3_NO_STDIO_WRITE
#include "decoder.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
#include "../../third_party/dr_flac.h"
#include "../../third_party/dr_mp3.h"
#include "../../third_party/dr_wav.h"
#include <vorbis/vorbisfile.h>

namespace {
std::string lower(std::string s) {
    for (char& c : s) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
    return s;
}

std::string ext_of(const std::string& path) {
    const size_t dot = path.rfind('.');
    return dot == std::string::npos ? std::string() : lower(path.substr(dot + 1));
}

// ---------------------------------------------------------------------------------------------------------------- MP3
class Mp3Decoder final : public Decoder {
public:
    bool open(const std::string& p) {
        if (!drmp3_init_file(&d_, p.c_str(), nullptr)) return false;
        open_ = true;
        total_ = drmp3_get_pcm_frame_count(&d_);
        return true;
    }
    ~Mp3Decoder() override { if (open_) drmp3_uninit(&d_); }
    uint32_t sample_rate() const override { return d_.sampleRate; }
    uint32_t channels() const override { return d_.channels; }
    uint64_t total_frames() const override { return total_; }
    uint64_t read(float* out, uint64_t frames) override { return drmp3_read_pcm_frames_f32(&d_, frames, out); }
    bool seek(uint64_t f) override { return drmp3_seek_to_pcm_frame(&d_, f) != 0; }
    uint32_t bits() const override { return 0; }
    const char* codec() const override { return "MP3"; }
private:
    mutable drmp3 d_{};
    bool open_ = false;
    uint64_t total_ = 0;
};

// ---------------------------------------------------------------------------------------------------------------- FLAC
class FlacDecoder final : public Decoder {
public:
    bool open(const std::string& p) {
        f_ = drflac_open_file(p.c_str(), nullptr);
        return f_ != nullptr;
    }
    ~FlacDecoder() override { if (f_) drflac_close(f_); }
    uint32_t sample_rate() const override { return f_->sampleRate; }
    uint32_t channels() const override { return f_->channels; }
    uint64_t total_frames() const override { return f_->totalPCMFrameCount; }
    uint64_t read(float* out, uint64_t frames) override { return drflac_read_pcm_frames_f32(f_, frames, out); }
    bool seek(uint64_t f) override { return drflac_seek_to_pcm_frame(f_, f) != 0; }
    uint32_t bits() const override { return f_->bitsPerSample; }
    const char* codec() const override { return "FLAC"; }
private:
    drflac* f_ = nullptr;
};

size_t src_read(void* u, void* out, size_t n) {
    long r = static_cast<ByteSource*>(u)->read(out, n);
    return r > 0 ? static_cast<size_t>(r) : 0;
}
bool src_seek_off(void* u, int64_t off, ByteSource::Origin o) { return static_cast<ByteSource*>(u)->seek(off, o); }

class FileSource final : public ByteSource {
public:
    explicit FileSource(const std::string& p) : f_(std::fopen(p.c_str(), "rb")) {}
    ~FileSource() override { if (f_) std::fclose(f_); }
    bool ok() const { return f_ != nullptr; }
    long read(void* b, size_t n) override { return static_cast<long>(std::fread(b, 1, n, f_)); }
    bool seek(int64_t off, Origin o) override { return std::fseek(f_, static_cast<long>(off), o == Set ? SEEK_SET : o == Cur ? SEEK_CUR : SEEK_END) == 0; }
    int64_t tell() const override { return std::ftell(f_); }
private:
    std::FILE* f_ = nullptr;
};

class Mp3StreamDecoder final : public Decoder {
public:
    explicit Mp3StreamDecoder(std::unique_ptr<ByteSource> s) : src_(std::move(s)) {}
    bool open() {
        auto rd = [](void* u, void* o, size_t n) { return src_read(u, o, n); };
        auto sk = [](void* u, int off, drmp3_seek_origin org) -> drmp3_bool32 {
            return src_seek_off(u, off, org == DRMP3_SEEK_SET ? ByteSource::Set : org == DRMP3_SEEK_END ? ByteSource::End : ByteSource::Cur);
        };
        auto tl = [](void* u, drmp3_int64* c) -> drmp3_bool32 { *c = static_cast<ByteSource*>(u)->tell(); return 1; };
        open_ = drmp3_init(&d_, rd, sk, tl, nullptr, src_.get(), nullptr);
        return open_;
    }
    ~Mp3StreamDecoder() override { if (open_) drmp3_uninit(&d_); }
    uint32_t sample_rate() const override { return d_.sampleRate; }
    uint32_t channels() const override { return d_.channels; }
    uint64_t total_frames() const override { return 0; }
    uint64_t read(float* out, uint64_t frames) override { return drmp3_read_pcm_frames_f32(&d_, frames, out); }
    bool seek(uint64_t f) override { return drmp3_seek_to_pcm_frame(&d_, f) != 0; }
    uint32_t bits() const override { return 0; }
    const char* codec() const override { return "MP3"; }
private:
    std::unique_ptr<ByteSource> src_;
    mutable drmp3 d_{};
    bool open_ = false;
};

class FlacStreamDecoder final : public Decoder {
public:
    explicit FlacStreamDecoder(std::unique_ptr<ByteSource> s, bool ogg = false) : src_(std::move(s)), ogg_(ogg) {}
    bool open() {
        auto rd = [](void* u, void* o, size_t n) { return src_read(u, o, n); };
        auto sk = [](void* u, int off, drflac_seek_origin org) -> drflac_bool32 {
            return src_seek_off(u, off, org == DRFLAC_SEEK_SET ? ByteSource::Set : org == DRFLAC_SEEK_END ? ByteSource::End : ByteSource::Cur);
        };
        auto tl = [](void* u, drflac_int64* c) -> drflac_bool32 { *c = static_cast<ByteSource*>(u)->tell(); return 1; };
        f_ = ogg_ ? drflac_open_relaxed(rd, sk, tl, drflac_container_ogg, src_.get(), nullptr) : drflac_open(rd, sk, tl, src_.get(), nullptr);
        return f_ != nullptr;
    }
    ~FlacStreamDecoder() override { if (f_) drflac_close(f_); }
    uint32_t sample_rate() const override { return f_->sampleRate; }
    uint32_t channels() const override { return f_->channels; }
    uint64_t total_frames() const override { return f_->totalPCMFrameCount; }
    uint64_t read(float* out, uint64_t frames) override { return drflac_read_pcm_frames_f32(f_, frames, out); }
    bool seek(uint64_t f) override { return drflac_seek_to_pcm_frame(f_, f) != 0; }
    uint32_t bits() const override { return f_->bitsPerSample; }
    const char* codec() const override { return "FLAC"; }
private:
    std::unique_ptr<ByteSource> src_;
    bool ogg_ = false;
    drflac* f_ = nullptr;
};

// ---------------------------------------------------------------------------------------------------------------- WAV
// RIFF/RF64/W64 PCM (8..32 bit, float32/64), A-law, mu-law, IMA/MS ADPCM, extensible multichannel: all converted by dr_wav.
class WavDecoder final : public Decoder {
public:
    WavDecoder() = default;
    explicit WavDecoder(std::unique_ptr<ByteSource> s) : src_(std::move(s)) {}
    bool open_file(const std::string& p) {
        open_ = drwav_init_file(&w_, p.c_str(), nullptr) != 0;
        return open_ && w_.channels > 0 && w_.sampleRate > 0;
    }
    bool open_stream() {
        auto rd = [](void* u, void* o, size_t n) { return src_read(u, o, n); };
        auto sk = [](void* u, int off, drwav_seek_origin org) -> drwav_bool32 {
            return src_seek_off(u, off, org == DRWAV_SEEK_SET ? ByteSource::Set : org == DRWAV_SEEK_END ? ByteSource::End : ByteSource::Cur);
        };
        auto tl = [](void* u, drwav_int64* c) -> drwav_bool32 { *c = static_cast<ByteSource*>(u)->tell(); return 1; };
        open_ = drwav_init(&w_, rd, sk, tl, src_.get(), nullptr) != 0;
        return open_ && w_.channels > 0 && w_.sampleRate > 0;
    }
    ~WavDecoder() override { if (open_) drwav_uninit(&w_); }
    uint32_t sample_rate() const override { return w_.sampleRate; }
    uint32_t channels() const override { return w_.channels; }
    uint64_t total_frames() const override { return w_.totalPCMFrameCount; }
    uint64_t read(float* out, uint64_t frames) override { return drwav_read_pcm_frames_f32(&w_, frames, out); }
    bool seek(uint64_t f) override { return drwav_seek_to_pcm_frame(&w_, f) != 0; }
    uint32_t bits() const override { return w_.bitsPerSample; }
    const char* codec() const override { return "WAV"; }
private:
    std::unique_ptr<ByteSource> src_;
    mutable drwav w_{};
    bool open_ = false;
};

// ---------------------------------------------------------------------------------------------------------------- AIFF / AIFC
// Big-endian PCM 8..32 bit, AIFC "sowt" (little-endian) and "fl32"/"fl64" floats. Compressed AIFC (ima4, ulaw...) is not read.
class AiffDecoder final : public Decoder {
public:
    ~AiffDecoder() override { if (f_) std::fclose(f_); }
    bool open(const std::string& p) {
        f_ = std::fopen(p.c_str(), "rb");
        if (!f_) return false;
        unsigned char h[12];
        if (std::fread(h, 1, 12, f_) != 12 || std::memcmp(h, "FORM", 4) != 0) return false;
        const bool aifc = std::memcmp(h + 8, "AIFC", 4) == 0;
        if (!aifc && std::memcmp(h + 8, "AIFF", 4) != 0) return false;
        bool have_comm = false, have_ssnd = false;
        for (;;) {
            unsigned char ch[8];
            if (std::fread(ch, 1, 8, f_) != 8) break;
            const uint32_t size = be32(ch + 4);
            long next = std::ftell(f_) + static_cast<long>(size) + static_cast<long>(size & 1);
            if (std::memcmp(ch, "COMM", 4) == 0 && size >= 18) {
                unsigned char c[64] = {};
                const size_t n = std::min<size_t>(size, sizeof c);
                if (std::fread(c, 1, n, f_) != n) return false;
                ch_ = static_cast<uint32_t>(c[0] << 8 | c[1]);
                frames_ = be32(c + 2);
                bits_ = static_cast<uint32_t>(c[6] << 8 | c[7]);
                rate_ = static_cast<uint32_t>(ext80(c + 8));
                if (aifc && n >= 22) {
                    if (std::memcmp(c + 18, "sowt", 4) == 0) little_ = true;
                    else if (std::memcmp(c + 18, "fl32", 4) == 0 || std::memcmp(c + 18, "FL32", 4) == 0) float_ = 32;
                    else if (std::memcmp(c + 18, "fl64", 4) == 0 || std::memcmp(c + 18, "FL64", 4) == 0) float_ = 64;
                    else if (std::memcmp(c + 18, "NONE", 4) != 0 && std::memcmp(c + 18, "twos", 4) != 0) return false;   // compressed
                }
                have_comm = true;
            } else if (std::memcmp(ch, "SSND", 4) == 0) {
                unsigned char o[8];
                if (std::fread(o, 1, 8, f_) != 8) return false;
                data_off_ = std::ftell(f_) + static_cast<long>(be32(o));
                data_size_ = size >= 8 + be32(o) ? size - 8 - be32(o) : 0;
                have_ssnd = true;
            }
            if (have_comm && have_ssnd) break;
            if (std::fseek(f_, next, SEEK_SET) != 0) break;
        }
        if (!have_comm || !have_ssnd || ch_ == 0 || ch_ > 8 || rate_ == 0) return false;
        const uint32_t bps = float_ ? static_cast<uint32_t>(float_) : bits_;
        if (!(bps == 8 || bps == 16 || bps == 24 || bps == 32 || bps == 64)) return false;
        bytes_ = bps / 8;
        const uint64_t avail = data_size_ / (static_cast<uint64_t>(bytes_) * ch_);
        if (frames_ == 0 || frames_ > avail) frames_ = avail;   // a truncated or streamed-length file
        pos_ = 0;
        return std::fseek(f_, data_off_, SEEK_SET) == 0;
    }
    uint32_t sample_rate() const override { return rate_; }
    uint32_t channels() const override { return ch_; }
    uint64_t total_frames() const override { return frames_; }
    uint64_t read(float* out, uint64_t frames) override {
        frames = std::min<uint64_t>(frames, frames_ - pos_);
        if (frames == 0) return 0;
        buf_.resize(static_cast<size_t>(frames) * ch_ * bytes_);
        const size_t got = std::fread(buf_.data(), 1, buf_.size(), f_) / (static_cast<size_t>(bytes_) * ch_);
        const size_t n = got * ch_;
        const unsigned char* p = buf_.data();
        for (size_t i = 0; i < n; ++i, p += bytes_) out[i] = sample(p);
        pos_ += got;
        return got;
    }
    bool seek(uint64_t f) override {
        f = std::min<uint64_t>(f, frames_);
        pos_ = f;
        return std::fseek(f_, data_off_ + static_cast<long>(f * bytes_ * ch_), SEEK_SET) == 0;
    }
    uint32_t bits() const override { return float_ ? static_cast<uint32_t>(float_) : bits_; }
    const char* codec() const override { return "AIFF"; }
private:
    static uint32_t be32(const unsigned char* p) { return static_cast<uint32_t>(p[0]) << 24 | static_cast<uint32_t>(p[1]) << 16 | static_cast<uint32_t>(p[2]) << 8 | p[3]; }
    static double ext80(const unsigned char* p) {   // 80-bit IEEE extended, big endian
        const int exp = ((p[0] & 0x7f) << 8) | p[1];
        uint64_t mant = 0;
        for (int i = 0; i < 8; ++i) mant = (mant << 8) | p[2 + i];
        if (exp == 0 && mant == 0) return 0;
        return std::ldexp(static_cast<double>(mant), exp - 16383 - 63);
    }
    float sample(const unsigned char* p) const {
        if (float_ == 32) { uint32_t v = little_ ? (p[0] | p[1] << 8 | p[2] << 16 | static_cast<uint32_t>(p[3]) << 24) : be32(p); float f; std::memcpy(&f, &v, 4); return f; }
        if (float_ == 64) { uint64_t v = 0; for (int i = 0; i < 8; ++i) v = (v << 8) | p[little_ ? 7 - i : i]; double d; std::memcpy(&d, &v, 8); return static_cast<float>(d); }
        int32_t v = 0;
        switch (bytes_) {
        case 1: return static_cast<float>(static_cast<int8_t>(p[0])) * (1.0f / 128.0f);
        case 2: v = little_ ? static_cast<int16_t>(p[0] | p[1] << 8) : static_cast<int16_t>(p[0] << 8 | p[1]); return static_cast<float>(v) * (1.0f / 32768.0f);
        case 3: v = little_ ? (p[0] << 8 | p[1] << 16 | p[2] << 24) : (p[0] << 24 | p[1] << 16 | p[2] << 8); return static_cast<float>(v) * (1.0f / 2147483648.0f);
        default: v = static_cast<int32_t>(little_ ? (p[0] | p[1] << 8 | p[2] << 16 | static_cast<uint32_t>(p[3]) << 24) : be32(p)); return static_cast<float>(static_cast<double>(v) / 2147483648.0);
        }
    }
    std::FILE* f_ = nullptr;
    uint32_t ch_ = 0, bits_ = 0, rate_ = 0, bytes_ = 0;
    int float_ = 0;
    bool little_ = false;
    uint64_t frames_ = 0, pos_ = 0, data_size_ = 0;
    long data_off_ = 0;
    std::vector<unsigned char> buf_;
};

// ---------------------------------------------------------------------------------------------------------------- Ogg Vorbis
// libvorbisfile: exact seeking, any channel count up to 8 (reordered from Vorbis to WAVE order).
class VorbisDecoder final : public Decoder {
public:
    ~VorbisDecoder() override { if (open_) ov_clear(&vf_); }
    bool open(const std::string& p) {
        if (ov_fopen(p.c_str(), &vf_) != 0) return false;
        open_ = true;
        const vorbis_info* info = ov_info(&vf_, -1);
        if (!info || info->channels < 1 || info->channels > 8 || info->rate <= 0) return false;
        ch_ = static_cast<uint32_t>(info->channels);
        rate_ = static_cast<uint32_t>(info->rate);
        const ogg_int64_t t = ov_pcm_total(&vf_, -1);
        total_ = t > 0 ? static_cast<uint64_t>(t) : 0;
        return true;
    }
    uint32_t sample_rate() const override { return rate_; }
    uint32_t channels() const override { return ch_; }
    uint64_t total_frames() const override { return total_; }
    uint64_t read(float* out, uint64_t frames) override {
        uint64_t done = 0;
        while (done < frames) {
            if (avail_ == 0) {
                float** pcm = nullptr;
                int bitstream = 0;
                const long n = ov_read_float(&vf_, &pcm, 4096, &bitstream);
                if (n == OV_HOLE) continue;   // a damaged page: carry on after it
                if (n <= 0) break;
                const vorbis_info* info = ov_info(&vf_, -1);
                if (info && static_cast<uint32_t>(info->channels) != ch_) break;   // a chained stream that changes layout ends this track
                buf_.resize(static_cast<size_t>(n) * ch_);
                for (long i = 0; i < n; ++i) for (uint32_t c = 0; c < ch_; ++c) buf_[static_cast<size_t>(i) * ch_ + c] = pcm[c][i];
                if (ch_ > 2) reorder(buf_.data(), static_cast<size_t>(n));
                off_ = 0; avail_ = static_cast<uint64_t>(n);
            }
            const uint64_t take = std::min<uint64_t>(frames - done, avail_);
            std::memcpy(out + done * ch_, buf_.data() + off_ * ch_, static_cast<size_t>(take) * ch_ * sizeof(float));
            off_ += take; avail_ -= take; done += take;
        }
        return done;
    }
    bool seek(uint64_t f) override { avail_ = 0; off_ = 0; return ov_pcm_seek(&vf_, static_cast<ogg_int64_t>(f)) == 0; }
    uint32_t bits() const override { return 0; }
    const char* codec() const override { return "Vorbis"; }
private:
    // Vorbis orders 5.1 as L C R Ls Rs LFE; the player expects the WAVE order L R C LFE Ls Rs.
    void reorder(float* p, size_t frames) const {
        for (size_t i = 0; i < frames; ++i, p += ch_) {
            float t[8];
            for (uint32_t c = 0; c < ch_; ++c) t[c] = p[c];
            switch (ch_) {
            case 3: p[0] = t[0]; p[1] = t[2]; p[2] = t[1]; break;
            case 5: p[0] = t[0]; p[1] = t[2]; p[2] = t[1]; p[3] = t[3]; p[4] = t[4]; break;
            case 6: p[0] = t[0]; p[1] = t[2]; p[2] = t[1]; p[3] = t[5]; p[4] = t[3]; p[5] = t[4]; break;
            case 7: p[0] = t[0]; p[1] = t[2]; p[2] = t[1]; p[3] = t[6]; p[4] = t[5]; p[5] = t[3]; p[6] = t[4]; break;
            case 8: p[0] = t[0]; p[1] = t[2]; p[2] = t[1]; p[3] = t[7]; p[4] = t[5]; p[5] = t[6]; p[6] = t[3]; p[7] = t[4]; break;
            default: break;
            }
        }
    }
    OggVorbis_File vf_{};
    bool open_ = false;
    uint32_t ch_ = 0, rate_ = 0;
    uint64_t total_ = 0, avail_ = 0, off_ = 0;
    std::vector<float> buf_;
};
}  // namespace

std::string decoder_kind_for_ext(const std::string& ext_in) {
    const std::string e = lower(ext_in);
    if (e == "mp3" || e == "mp2" || e == "mpga") return "mp3";
    if (e == "flac") return "flac";
    if (e == "wav" || e == "wave" || e == "w64" || e == "rf64") return "wav";
    if (e == "aiff" || e == "aif" || e == "aifc") return "aiff";
    if (e == "ogg" || e == "oga") return "ogg";
    if (e == "opus") return "opus";
    if (e == "m4a" || e == "mp4" || e == "m4b" || e == "alac" || e == "m4r" || e == "3gp") return "m4a";
    if (e == "aac") return "aac";
    return std::string();
}

bool decoder_kind_streams(const std::string& kind) { return kind == "mp3" || kind == "flac" || kind == "wav"; }

std::unique_ptr<Decoder> open_decoder_source(std::unique_ptr<ByteSource> src, const std::string& kind) {
    if (kind == "mp3") { auto d = std::make_unique<Mp3StreamDecoder>(std::move(src)); if (d->open()) return d; }
    else if (kind == "flac") { auto d = std::make_unique<FlacStreamDecoder>(std::move(src)); if (d->open()) return d; }
    else if (kind == "wav") { auto d = std::make_unique<WavDecoder>(std::move(src)); if (d->open_stream()) return d; }
    return nullptr;
}

std::unique_ptr<Decoder> open_decoder(const std::string& path, const std::string& kind_in) {
    std::string kind = kind_in.empty() ? decoder_kind_for_ext(ext_of(path)) : kind_in;
    if (kind == "ogg") {   // Vorbis, Opus or FLAC in Ogg: the first packet says which
        const std::string sk = sniff_audio_kind(path);
        kind = sk == "opus" ? "opus" : sk == "oggflac" ? "oggflac" : "vorbis";
    }
    if (kind == "mp3") { auto d = std::make_unique<Mp3Decoder>(); if (d->open(path)) return d; }
    else if (kind == "flac") { auto d = std::make_unique<FlacDecoder>(); if (d->open(path)) return d; }
    else if (kind == "wav") { auto d = std::make_unique<WavDecoder>(); if (d->open_file(path)) return d; }
    else if (kind == "oggflac") {
        auto fs = std::make_unique<FileSource>(path);
        if (!fs->ok()) return nullptr;
        auto d = std::make_unique<FlacStreamDecoder>(std::move(fs), true);
        if (d->open()) return d;
    }
    else if (kind == "aiff") { auto d = std::make_unique<AiffDecoder>(); if (d->open(path)) return d; }
    else if (kind == "vorbis") { auto d = std::make_unique<VorbisDecoder>(); if (d->open(path)) return d; }
    else if (kind == "opus") return open_opus_file(path);
    else if (kind == "m4a") { if (auto d = open_aac_mp4_file(path)) return d; return open_alac_mp4_file(path); }
    else if (kind == "aac") return open_adts_file(path);
    return nullptr;
}
