#include "player.h"
#include <SDL.h>
#include <chrono>
#include <unistd.h>
#include <vector>

Player::Player() = default;
Player::~Player() { stop(); }

bool Player::attach(std::unique_ptr<Decoder> d, double duration_hint) {
    stop();
    if (!d || d->channels() == 0 || d->channels() > 2) return false;
    dec_ = std::move(d);
    channels_ = dec_->channels();
    rate_ = dec_->sample_rate();
    duration_hint_ = duration_hint;
    eof_ = false;
    seek_pending_ = false;
    played_frames_ = 0;
    base_frame_ = 0;
    ring_.clear();
    return true;
}

bool Player::load(const std::string& path, const std::string& kind) { return attach(open_decoder(path, kind), 0); }

bool Player::load_url(const std::string& url, const std::string& cache_path, const std::string& kind,
                      const ConnectOptions& opt, double duration_hint, std::string& err) {
    stop();
    auto d = open_track(url, cache_path, cache_path, kind, opt, err);
    return d && attach(std::move(d), duration_hint);
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
        if (seek_pending_.load()) {
            uint64_t target = seek_target_.load();
            dec_->seek(target);  // may wait for the download to reach the target
            if (playing_.load()) {
                flush_ack_ = false;
                flush_req_ = true;  // the audio callback owns the ring's read side
                while (run_.load() && !flush_ack_.load()) std::this_thread::sleep_for(std::chrono::milliseconds(2));  // pause() acks if the device closes meanwhile
            } else {
                ring_.clear();
            }
            if (seek_target_.load() == target) { eof_ = false; seek_pending_ = false; }
            continue;
        }
        size_t room = ring_.writable() / channels_;
        if (eof_.load() || room < 4096) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        uint64_t n = dec_->read(tmp.data(), 4096);
        if (n == 0) { eof_ = true; continue; }
        if (seek_pending_.load()) continue;  // a seek arrived while decoding: drop this block
        ring_.write(tmp.data(), n * channels_);
    }
}

void Player::callback(void* user, uint8_t* stream, int len) {
    static_cast<Player*>(user)->fill(reinterpret_cast<int16_t*>(stream), static_cast<size_t>(len) / sizeof(int16_t));
}

void Player::fill(int16_t* out, size_t samples) {
    if (flush_req_.load(std::memory_order_acquire)) {
        ring_.clear();
        played_frames_.store(0, std::memory_order_relaxed);
        flush_req_.store(false, std::memory_order_release);
        flush_ack_.store(true, std::memory_order_release);
    }
    if (seek_pending_.load(std::memory_order_relaxed)) { std::memset(out, 0, samples * sizeof(int16_t)); return; }
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
    close_device();  // no idle device; the callback can no longer run
    if (flush_req_.exchange(false)) {  // a seek flush was waiting on the callback: do it here
        ring_.clear();
        played_frames_ = 0;
        flush_ack_ = true;
    }
}

void Player::stop() {
    playing_ = false;
    close_device();
    stop_thread();
    flush_req_ = false;
    flush_ack_ = true;
    seek_pending_ = false;
    dec_.reset();
}

bool Player::seek_seconds(double s) {
    if (!dec_) return false;
    if (s < 0) s = 0;
    uint64_t f = static_cast<uint64_t>(s * rate_);
    seek_target_ = f;
    base_frame_ = f;
    if (!playing_.load()) played_frames_ = 0;  // when playing, the callback zeroes it at the flush
    seek_pending_ = true;
    start_thread();
    return true;
}

double Player::position_seconds() const {
    return static_cast<double>(base_frame_.load() + played_frames_.load()) / rate_;
}

double Player::duration_seconds() const {
    return dec_ && dec_->total_frames() ? static_cast<double>(dec_->total_frames()) / rate_ : duration_hint_;
}
