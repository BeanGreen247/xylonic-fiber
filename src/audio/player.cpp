#include "player.h"
#include <SDL.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <unistd.h>
#include <vector>

Player::Player() = default;
Player::~Player() { stop(); }

bool Player::attach(std::unique_ptr<Decoder> d, double duration_hint) {
    stop();
    if (!d || d->channels() == 0 || d->channels() > 8) return false;
    dec_ = std::move(d);
    set_info(*dec_);
    channels_ = dec_->channels();
    src_rate_ = dec_->sample_rate();
    cur_total_ = dec_->total_frames();
    dev_rate_ = pref_rate_ ? pref_rate_ : src_rate_.load();
    duration_hint_ = duration_hint;
    eof_ = false;
    seek_pending_ = false;
    consumed_ = 0; out_total_ = 0; track_start_ = 0; base_seconds_ = 0;
    boundary_pending_ = false; advanced_ = false;
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

void Player::set_speed(double v) {
    v = std::min(2.0, std::max(0.5, v));
    if (v == speed_.load()) return;
    double pos = position_seconds();
    speed_ = v;
    if (dec_) seek_seconds(pos);  // flush so the new rate is heard now and the timeline restarts from here
}

void Player::set_eq(double bass, double treble) {
    eq_bass_ = std::min(12.0, std::max(-12.0, bass));
    eq_treble_ = std::min(12.0, std::max(-12.0, treble));
    ++eq_gen_;
}

void Player::set_next(std::unique_ptr<Decoder> d, double hint) {
    std::lock_guard<std::mutex> lk(next_m_);
    next_dec_ = std::move(d);
    next_duration_ = hint;
}

void Player::clear_next() {
    std::lock_guard<std::mutex> lk(next_m_);
    next_dec_.reset();
}

// Any channel count to stereo (WAVE order: L R C LFE Ls Rs Lrs Rrs), the ITU-R BS.775 style fold-down, normalised so a full-scale
// multichannel mix cannot clip.
static void downmix(const float* raw, uint64_t n, uint32_t ch, float* in) {
    if (ch == 2) { std::memcpy(in, raw, static_cast<size_t>(n) * 2 * sizeof(float)); return; }
    if (ch == 1) { for (uint64_t i = 0; i < n; ++i) in[i * 2] = in[i * 2 + 1] = raw[i]; return; }
    const float k = 0.7071f;
    for (uint64_t i = 0; i < n; ++i) {
        const float* p = raw + i * ch;
        float l = p[0], r = p[1], norm = 1.0f;
        switch (ch) {
        case 3: l += k * p[2]; r += k * p[2]; norm += k; break;                                          // FL FR C
        case 4: l += k * p[2]; r += k * p[3]; norm += k; break;                                          // FL FR BL BR
        case 5: l += k * p[2] + k * p[3]; r += k * p[2] + k * p[4]; norm += 2 * k; break;                // FL FR C BL BR
        case 6: l += k * p[2] + 0.5f * p[3] + k * p[4]; r += k * p[2] + 0.5f * p[3] + k * p[5]; norm += 2 * k + 0.5f; break;   // + LFE at -6 dB
        case 7: l += k * p[2] + 0.5f * p[3] + k * p[4] + 0.5f * p[6]; r += k * p[2] + 0.5f * p[3] + k * p[5] + 0.5f * p[6]; norm += 2 * k + 1.0f; break;
        default: l += k * p[2] + 0.5f * p[3] + k * p[4] + k * p[6]; r += k * p[2] + 0.5f * p[3] + k * p[5] + k * p[7]; norm += 3 * k + 0.5f; break;   // 7.1
        }
        in[i * 2] = l / norm; in[i * 2 + 1] = r / norm;
    }
}

void Player::decode_loop() {
    std::vector<float> raw(2048 * 8);
    std::vector<float> in(2048 * 2), out;
    out.reserve(8192);
    Resampler rs;
    Shelf lo, hi;
    int eq_seen = -1;
    bool eq_on = false;
    while (run_.load()) {
        if (seek_pending_.load()) {
            uint64_t target = seek_target_.load();
            dec_->seek(target);  // may wait for the download to reach the target
            rs.reset(); lo.clear(); hi.clear();
            if (playing_.load()) {
                flush_ack_ = false;
                flush_req_ = true;  // the audio callback owns the ring's read side
                while (run_.load() && !flush_ack_.load()) std::this_thread::sleep_for(std::chrono::milliseconds(2));
            } else {
                ring_.clear();
                track_start_ = consumed_.load();
            }
            out_total_ = consumed_.load();
            if (seek_target_.load() == target) { eof_ = false; seek_pending_ = false; }
            continue;
        }
        if (eq_seen != eq_gen_.load()) {
            eq_seen = eq_gen_.load();
            double b = eq_bass_.load(), t = eq_treble_.load();
            eq_on = b != 0 || t != 0;
            lo.design(false, dev_rate_, 100, b); hi.design(true, dev_rate_, 8000, t);
            lo.clear(); hi.clear();
        }
        if (ring_.writable() < 8192) { std::this_thread::sleep_for(std::chrono::milliseconds(20)); continue; }  // 0.74 s ring: waking at 50 Hz is plenty
        if (eof_.load()) {
            std::unique_ptr<Decoder> nx;
            double hint = 0;
            { std::lock_guard<std::mutex> lk(next_m_); nx = std::move(next_dec_); hint = next_duration_; }
            if (!nx || nx->channels() == 0 || nx->channels() > 8) { std::this_thread::sleep_for(std::chrono::milliseconds(10)); continue; }
            { std::lock_guard<std::mutex> lk(tl_m_); dec_ = std::move(nx); set_info(*dec_); channels_ = dec_->channels(); src_rate_ = dec_->sample_rate(); cur_total_ = dec_->total_frames();
              boundary_pending_ = true; boundary_out_ = out_total_.load(); next_duration_ = hint; }
            rs.reset();
            eof_ = false;
            continue;
        }
        uint64_t n = dec_->read(raw.data(), 2048);
        if (n == 0) {   // end of stream: push the frames the resampler's kernel was still holding back, then flag EOF
            std::vector<float> tail;
            rs.finish(static_cast<double>(src_rate_.load()) * speed_.load() / static_cast<double>(dev_rate_), tail);
            size_t toff = 0;
            while (toff < tail.size() && run_.load() && !seek_pending_.load()) { toff += ring_.write(tail.data() + toff, tail.size() - toff); if (toff < tail.size()) std::this_thread::sleep_for(std::chrono::milliseconds(2)); }
            out_total_ += tail.size() / 2;
            eof_ = true;
            continue;
        }
        if (seek_pending_.load()) continue;  // a seek arrived while decoding: drop this block
        downmix(raw.data(), n, channels_, in.data());
        out.clear();
        double step = static_cast<double>(src_rate_.load()) * speed_.load() / static_cast<double>(dev_rate_);
        rs.process(in.data(), n, step, out);
        size_t frames = out.size() / 2;
        if (eq_on) { lo.process(out.data(), frames); hi.process(out.data(), frames); }
        size_t off = 0;
        while (off < out.size() && run_.load() && !seek_pending_.load()) {
            size_t w = ring_.write(out.data() + off, out.size() - off);
            off += w;
            if (off < out.size()) std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        out_total_ += frames;
    }
}

void Player::callback(void* user, uint8_t* stream, int len) {
    Player* p = static_cast<Player*>(user);
    if (!p->out_s16_) { p->fill(reinterpret_cast<float*>(stream), static_cast<size_t>(len) / sizeof(float)); return; }
    // 16-bit device: the float mix is rounded with TPDF dither (so quiet passages of 24-bit sources do not turn into truncation distortion)
    const size_t n = static_cast<size_t>(len) / sizeof(int16_t);
    if (p->s16_tmp_.size() < n) p->s16_tmp_.resize(n);
    p->fill(p->s16_tmp_.data(), n);
    int16_t* o = reinterpret_cast<int16_t*>(stream);
    uint32_t r = p->dither_state_;
    for (size_t i = 0; i < n; ++i) {
        r = r * 1664525u + 1013904223u; const float d1 = static_cast<float>(r >> 8) * (1.0f / 16777216.0f);
        r = r * 1664525u + 1013904223u; const float d2 = static_cast<float>(r >> 8) * (1.0f / 16777216.0f);
        const float v = p->s16_tmp_[i] * 32767.0f + (d1 - d2);
        o[i] = static_cast<int16_t>(v > 32767.0f ? 32767.0f : v < -32768.0f ? -32768.0f : std::lrintf(v));
    }
    p->dither_state_ = r;
}

void Player::fill(float* out, size_t samples) {
    if (flush_req_.load(std::memory_order_acquire)) {
        ring_.clear();
        track_start_.store(consumed_.load(std::memory_order_relaxed), std::memory_order_relaxed);
        flush_req_.store(false, std::memory_order_release);
        flush_ack_.store(true, std::memory_order_release);
    }
    if (seek_pending_.load(std::memory_order_relaxed)) { std::memset(out, 0, samples * sizeof(float)); return; }
    size_t got = ring_.read(out, samples);
    float v = volume_.load(std::memory_order_relaxed);
    if (v < 0.999f)
        for (size_t i = 0; i < got; ++i) out[i] *= v;
    if (got < samples) std::memset(out + got, 0, (samples - got) * sizeof(float));
    consumed_.fetch_add(got / 2, std::memory_order_relaxed);
    // spectrum tap (mono)
    if (tap_on_.load(std::memory_order_relaxed)) {
    size_t tp = tap_pos_.load(std::memory_order_relaxed);
    for (size_t i = 0; i + 1 < got; i += 2) { tap_[tp % kTap] = (out[i] + out[i + 1]) * 0.5f; ++tp; }
    tap_pos_.store(tp, std::memory_order_release);
    }
}

bool Player::play() {
    if (!dec_) return false;
    if (playing_.load()) return true;
    if (!(SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) && SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) return false;
    start_thread();
#if defined(__ANDROID__)
    if (AAudioOut::available() && aaudio_.open(dev_rate_, [this](float* out, int frames) { fill(out, static_cast<size_t>(frames) * 2); })) {
        use_aaudio_ = true;
        playing_ = true;
        return true;
    }
    use_aaudio_ = false;
#endif
    SDL_AudioSpec want{}, have{};
    want.freq = static_cast<int>(dev_rate_);
    out_s16_ = s16_device_;   // float inside; only the SDL fallback on Android hands 16-bit to the device (dithered from the float mix)
    want.format = out_s16_ ? AUDIO_S16SYS : AUDIO_F32SYS;
    want.channels = 2;
    want.samples = 1024;
    want.callback = &Player::callback;
    want.userdata = this;
    SDL_AudioDeviceID id = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);  // 0 = SDL converts if the device differs
    if (id == 0) return false;
    device_ = id;
    playing_ = true;
    SDL_PauseAudioDevice(id, 0);
    return true;
}

void Player::close_device() {
#if defined(__ANDROID__)
    if (use_aaudio_) { aaudio_.close(); use_aaudio_ = false; }
#endif
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
        track_start_ = consumed_.load();
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
    clear_next();
    dec_.reset();
    { std::lock_guard<std::mutex> lk(info_m_); info_ = Info{}; }
}

bool Player::seek_seconds(double s) {
    if (!dec_) return false;
    if (s < 0) s = 0;
    double d = duration_seconds();
    if (d > 0 && s > d) s = d;
    uint64_t f = static_cast<uint64_t>(s * src_rate_.load());
    seek_target_ = f;
    base_seconds_ = s;
    seek_pending_ = true;
    start_thread();
    return true;
}

void Player::settle_boundary() {
    std::lock_guard<std::mutex> lk(tl_m_);
    if (boundary_pending_ && consumed_.load() >= boundary_out_) {
        track_start_ = boundary_out_;
        base_seconds_ = 0;
        duration_hint_ = next_duration_;
        boundary_pending_ = false;
        advanced_ = true;
    }
}

double Player::position_seconds() {
    settle_boundary();
    uint64_t c = consumed_.load(), t = track_start_.load();
    return base_seconds_.load() + static_cast<double>(c > t ? c - t : 0) * speed_.load() / static_cast<double>(dev_rate_);
}

bool Player::take_advanced() {
    settle_boundary();
    return advanced_.exchange(false);
}

double Player::duration_seconds() const {
    uint64_t t = cur_total_.load();
    if (t && !boundary_pending_) return static_cast<double>(t) / src_rate_.load();
    return boundary_pending_ ? 0.0 : duration_hint_;
}

void Player::spectrum(float* bands, int n) {
    if (!playing_.load()) { for (int i = 0; i < n; ++i) bands[i] = 0; return; }
    const size_t N = 2048;
    float buf[N] = {};
    size_t tp = tap_pos_.load(std::memory_order_acquire);
    for (size_t i = 0; i < N; ++i) buf[i] = tap_[(tp - N + i) % kTap];
    spectrum_bands(buf, N, dev_rate_, bands, n);
}
