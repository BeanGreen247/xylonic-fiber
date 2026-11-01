#define DR_MP3_IMPLEMENTATION
#define DR_FLAC_IMPLEMENTATION
#define DR_MP3_NO_STDIO_WRITE
#include "decoder.h"
#include "../../third_party/dr_flac.h"
#include "../../third_party/dr_mp3.h"

namespace {
bool ends_with(const std::string& s, const char* ext) {
    size_t n = std::char_traits<char>::length(ext);
    if (s.size() < n) return false;
    for (size_t i = 0; i < n; ++i) {
        char a = s[s.size() - n + i];
        if (a >= 'A' && a <= 'Z') a = static_cast<char>(a + 32);
        if (a != ext[i]) return false;
    }
    return true;
}

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
    uint64_t read(int16_t* out, uint64_t frames) override { return drmp3_read_pcm_frames_s16(&d_, frames, out); }
    bool seek(uint64_t f) override { return drmp3_seek_to_pcm_frame(&d_, f) != 0; }
private:
    mutable drmp3 d_{};
    bool open_ = false;
    uint64_t total_ = 0;
};

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
    uint64_t read(int16_t* out, uint64_t frames) override { return drflac_read_pcm_frames_s16(f_, frames, out); }
    bool seek(uint64_t f) override { return drflac_seek_to_pcm_frame(f_, f) != 0; }
private:
    drflac* f_ = nullptr;
};

size_t src_read(void* u, void* out, size_t n) {
    long r = static_cast<ByteSource*>(u)->read(out, n);
    return r > 0 ? static_cast<size_t>(r) : 0;
}
bool src_seek_off(void* u, int64_t off, ByteSource::Origin o) { return static_cast<ByteSource*>(u)->seek(off, o); }

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
    uint64_t read(int16_t* out, uint64_t frames) override { return drmp3_read_pcm_frames_s16(&d_, frames, out); }
    bool seek(uint64_t f) override { return drmp3_seek_to_pcm_frame(&d_, f) != 0; }
private:
    std::unique_ptr<ByteSource> src_;
    mutable drmp3 d_{};
    bool open_ = false;
};

class FlacStreamDecoder final : public Decoder {
public:
    explicit FlacStreamDecoder(std::unique_ptr<ByteSource> s) : src_(std::move(s)) {}
    bool open() {
        auto rd = [](void* u, void* o, size_t n) { return src_read(u, o, n); };
        auto sk = [](void* u, int off, drflac_seek_origin org) -> drflac_bool32 {
            return src_seek_off(u, off, org == DRFLAC_SEEK_SET ? ByteSource::Set : org == DRFLAC_SEEK_END ? ByteSource::End : ByteSource::Cur);
        };
        auto tl = [](void* u, drflac_int64* c) -> drflac_bool32 { *c = static_cast<ByteSource*>(u)->tell(); return 1; };
        f_ = drflac_open(rd, sk, tl, src_.get(), nullptr);
        return f_ != nullptr;
    }
    ~FlacStreamDecoder() override { if (f_) drflac_close(f_); }
    uint32_t sample_rate() const override { return f_->sampleRate; }
    uint32_t channels() const override { return f_->channels; }
    uint64_t total_frames() const override { return f_->totalPCMFrameCount; }
    uint64_t read(int16_t* out, uint64_t frames) override { return drflac_read_pcm_frames_s16(f_, frames, out); }
    bool seek(uint64_t f) override { return drflac_seek_to_pcm_frame(f_, f) != 0; }
private:
    std::unique_ptr<ByteSource> src_;
    drflac* f_ = nullptr;
};
}  // namespace

std::unique_ptr<Decoder> open_decoder_source(std::unique_ptr<ByteSource> src, const std::string& kind) {
    if (kind == "mp3") { auto d = std::make_unique<Mp3StreamDecoder>(std::move(src)); if (d->open()) return d; }
    else if (kind == "flac") { auto d = std::make_unique<FlacStreamDecoder>(std::move(src)); if (d->open()) return d; }
    return nullptr;
}

std::unique_ptr<Decoder> open_decoder(const std::string& path, const std::string& kind) {
    if (kind == "mp3" || (kind.empty() && ends_with(path, ".mp3"))) {
        auto d = std::make_unique<Mp3Decoder>();
        if (d->open(path)) return d;
    } else if (kind == "flac" || (kind.empty() && ends_with(path, ".flac"))) {
        auto d = std::make_unique<FlacDecoder>();
        if (d->open(path)) return d;
    }
    return nullptr;
}
