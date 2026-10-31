#include "player.h"
#include <SDL.h>
#include <chrono>
#include <unistd.h>
#include <vector>

Player::Player() = default;
Player::~Player() { stop(); }

bool Player::load(const std::string& path, const std::string& kind) {
    duration_hint_ = 0;
    stop();
    auto d = open_decoder(path, kind);
    if (!d || d->channels() == 0 || d->channels() > 2) return false;
    dec_ = std::move(d);
    channels_ = dec_->channels();
    rate_ = dec_->sample_rate();
    eof_ = false;
    played_frames_ = 0;
    base_frame_ = 0;
    ring_.clear();
    return true;
}

bool Player::load_url(const std::string& url, const std::string& cache_path, const std::string& kind,
                      const ConnectOptions& opt, double duration_hint, std::string& err) {
    if (::access(cache_path.c_str(), R_OK) == 0) { duration_hint_ = 0; return load(cache_path, kind); }
    stop();
    auto src = std::make_unique<HttpFileSource>(url, cache_path, opt);
    if (!src->start(opt.timeout_ms, err)) return false;
    auto d = open_decoder_source(std::move(src), kind);
    if (!d || d->channels() == 0 || d->channels() > 2) { err = "unsupported or invalid audio stream"; return false; }
    dec_ = std::move(d);
    channels_ = dec_->channels();
    rate_ = dec_->sample_rate();
    duration_hint_ = duration_hint;
    eof_ = false;
    played_frames_ = 0;
    base_frame_ = 0;
    ring_.clear();
    return true;
}

void Player::start_thread() {
    if (th_.joinable()) return;
    run_ = true;
    th_ = std::thread([this] { decode_loop(); });
}

void Player::stop_thread() {
    run_ = false;
    if (th_.joinable()) th_.join();
}

void Player::decode_loop() {
    std::vector<int16_t> tmp(4096 * channels_);
    while (run_.load()) {
        size_t room = ring_.writable() / channels_;
        if (eof_.load() || room < 4096) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        uint64_t n = dec_->read(tmp.data(), 4096);
        if (n == 0) { eof_ = true; continue; }
        ring_.write(tmp.data(), n * channels_);
    }
}

void Player::callback(void* user, uint8_t* stream, int len) {
    static_cast<Player*>(user)->fill(reinterpret_cast<int16_t*>(stream), static_cast<size_t>(len) / sizeof(int16_t));
}

void Player::fill(int16_t* out, size_t samples) {
    size_t got = ring_.read(out, samples);
    float v = volume_.load(std::memory_order_relaxed);
    if (v < 0.999f)
        for (size_t i = 0; i < got; ++i) out[i] = static_cast<int16_t>(out[i] * v);
    if (got < samples) std::memset(out + got, 0, (samples - got) * sizeof(int16_t));
    played_frames_.fetch_add(got / channels_, std::memory_order_relaxed);
}

bool Player::play() {
    if (!dec_) return false;
    if (playing_.load()) return true;
    if (!(SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) && SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) return false;
    start_thread();
    SDL_AudioSpec want{}, have{};
    want.freq = static_cast<int>(rate_);
    want.format = AUDIO_S16SYS;
    want.channels = static_cast<Uint8>(channels_);
    want.samples = 1024;
    want.callback = &Player::callback;
    want.userdata = this;
    SDL_AudioDeviceID id = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);  // 0 = SDL converts if needed
    if (id == 0) return false;
    device_ = id;
    playing_ = true;
    SDL_PauseAudioDevice(id, 0);
    return true;
}

void Player::close_device() {
    if (device_) {
        SDL_CloseAudioDevice(device_);
        device_ = 0;
    }
}

void Player::pause() {
    if (!playing_.exchange(false)) return;
    close_device();  // no idle device
}

void Player::stop() {
    playing_ = false;
    close_device();
    stop_thread();
    dec_.reset();
}

bool Player::seek_seconds(double s) {
    if (!dec_) return false;
    bool was = playing_.load();
    if (was) pause();
    stop_thread();
    uint64_t f = static_cast<uint64_t>(s * rate_);
    if (!dec_->seek(f)) return false;
    ring_.clear();
    eof_ = false;
    base_frame_ = f;
    played_frames_ = 0;
    start_thread();
    return was ? play() : true;
}

double Player::position_seconds() const {
    return static_cast<double>(base_frame_.load() + played_frames_.load()) / rate_;
}

double Player::duration_seconds() const {
    return dec_ && dec_->total_frames() ? static_cast<double>(dec_->total_frames()) / rate_ : duration_hint_;
}
