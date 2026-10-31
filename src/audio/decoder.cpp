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
}  // namespace

std::unique_ptr<Decoder> open_decoder(const std::string& path) {
    if (ends_with(path, ".mp3")) {
        auto d = std::make_unique<Mp3Decoder>();
        if (d->open(path)) return d;
    } else if (ends_with(path, ".flac")) {
        auto d = std::make_unique<FlacDecoder>();
        if (d->open(path)) return d;
    }
    return nullptr;
}
