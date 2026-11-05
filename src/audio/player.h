// Single owned audio pipeline: decoder thread -> resample/speed -> EQ -> SPSC ring -> SDL audio callback.
// The SDL audio device is opened only while playing (an open idle device costs ~1.9% CPU, see docs/baseline.md).
// Gapless: a prepared next decoder is switched in at end of stream without a gap (rate converted to the device rate).
#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include "decoder.h"
#include "dsp.h"
#include "ring.h"
#include "source.h"

class Player {
public:
    Player();
    ~Player();
    bool load(const std::string& path, const std::string& kind = "");   // stops current, opens new, paused
    // Streams url into cache_path while decoding; if cache_path already exists it plays that file (offline-first).
    bool load_url(const std::string& url, const std::string& cache_path, const std::string& kind,
                  const ConnectOptions& opt, double duration_hint, std::string& err);
    // Takes a decoder prepared on another thread (see open_track), so the UI thread never waits on the network.
    bool attach(std::unique_ptr<Decoder> d, double duration_hint);
    bool play();
    void pause();
    void stop();
    bool seek_seconds(double s);  // asynchronous: returns at once, the decode thread performs the seek
    double position_seconds();
    double duration_seconds() const;
    bool playing() const { return playing_.load(); }
    bool finished() const { return eof_.load() && !has_next() && ring_.readable() == 0; }
    void set_volume(float v) { volume_.store(v); }
    // Output rate to open the device at (0 = the track's own rate). Phones need their native rate: OpenSL ES refuses others on some devices.
    void set_preferred_device_rate(uint32_t hz) { pref_rate_ = hz; }

    // Playback speed 0.5..2 (changes the pitch with the speed, like HTMLMediaElement.playbackRate with preservesPitch off).
    void set_speed(double v);
    double speed() const { return speed_.load(); }
    // Per-song bass/treble in dB (-12..12); 0/0 bypasses the filters.
    void set_eq(double bass_db, double treble_db);
    // Gapless: queue the next track's decoder. When the current one ends it is switched in seamlessly.
    void set_next(std::unique_ptr<Decoder> d, double duration_hint);
    void clear_next();
    bool has_next() const { std::lock_guard<std::mutex> lk(next_m_); return next_dec_ != nullptr; }
    // True once playback has actually crossed into the track passed to set_next (poll from the UI thread; clears itself).
    bool take_advanced();
    void enable_spectrum(bool on) { tap_on_.store(on); }  // the tap costs a pass over the output, so it is off by default
    // Latest spectrum as `n` log-spaced bands in 0..1 (empty/zero when nothing plays).
    void spectrum(float* bands, int n);

private:
    static void callback(void* user, uint8_t* stream, int len);
    void fill(int16_t* out, size_t samples);
    void decode_loop();
    void start_thread();
    void stop_thread();
    void close_device();
    void settle_boundary();

    std::unique_ptr<Decoder> dec_;
    SampleRing ring_{1u << 16};  // 64k samples = 0.74 s at 44.1k stereo: short enough that EQ/speed edits are heard quickly
    std::thread th_;
    std::atomic<bool> run_{false}, eof_{false}, playing_{false};
    std::atomic<bool> seek_pending_{false};
    std::atomic<uint64_t> seek_target_{0};
    std::atomic<bool> flush_req_{false}, flush_ack_{true};
    std::atomic<float> volume_{1.0f};
    std::atomic<double> speed_{1.0};
    std::atomic<int> eq_gen_{0};
    std::atomic<double> eq_bass_{0}, eq_treble_{0};
    // timeline (device-domain frames)
    std::atomic<uint64_t> consumed_{0};       // frames handed to the device
    std::atomic<uint64_t> out_total_{0};      // frames written into the ring
    std::atomic<uint64_t> track_start_{0};    // consumed_ value at which the current track's position was 0
    std::atomic<double> base_seconds_{0};     // track position at track_start_
    std::mutex tl_m_;                          // guards boundary bookkeeping
    bool boundary_pending_ = false;
    uint64_t boundary_out_ = 0;
    double next_duration_ = 0, duration_hint_ = 0;
    std::atomic<bool> advanced_{false};
    mutable std::mutex next_m_;
    std::unique_ptr<Decoder> next_dec_;
    // device
    uint32_t device_ = 0;
    uint32_t dev_rate_ = 44100, pref_rate_ = 0;
    std::atomic<uint32_t> src_rate_{44100};
    uint32_t channels_ = 2;
    std::atomic<uint64_t> cur_total_{0};  // total frames of the current decoder (0 = unknown), readable from the UI thread
    // spectrum tap (callback writes, UI reads)
    static constexpr size_t kTap = 4096;
    float tap_[kTap] = {};
    std::atomic<size_t> tap_pos_{0};
    std::atomic<bool> tap_on_{false};
};
