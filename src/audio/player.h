// Single owned audio pipeline: decoder thread -> SPSC ring -> SDL audio callback.
// The SDL audio device is opened only while playing (an open idle device costs ~1.9% CPU, see docs/baseline.md).
#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include "decoder.h"
#include "ring.h"
#include "source.h"

class Player {
public:
    Player();
    ~Player();
    bool load(const std::string& path, const std::string& kind = "");   // stops current, opens new, paused
    // Streams url into cache_path while decoding; if cache_path already exists it plays that file (offline-first).
    // kind: "mp3" or "flac". duration_hint (seconds) is used while the length is unknown.
    bool load_url(const std::string& url, const std::string& cache_path, const std::string& kind,
                  const ConnectOptions& opt, double duration_hint, std::string& err);
    // Takes a decoder prepared on another thread (see open_track), so the UI thread never waits on the network.
    bool attach(std::unique_ptr<Decoder> d, double duration_hint);
    bool play();
    void pause();
    void stop();
    bool seek_seconds(double s);  // asynchronous: returns at once, the decode thread performs the seek
    double position_seconds() const;
    double duration_seconds() const;
    bool playing() const { return playing_.load(); }
    bool finished() const { return eof_.load() && ring_.readable() == 0; }
    void set_volume(float v) { volume_.store(v); }

private:
    static void callback(void* user, uint8_t* stream, int len);
    void fill(int16_t* out, size_t samples);
    void decode_loop();
    void start_thread();
    void stop_thread();
    void close_device();

    std::unique_ptr<Decoder> dec_;
    SampleRing ring_{1u << 18};  // 256k samples ~ 3s at 44.1k stereo
    std::thread th_;
    std::atomic<bool> run_{false};
    std::atomic<bool> eof_{false};
    std::atomic<bool> playing_{false};
    std::atomic<bool> seek_pending_{false};
    std::atomic<uint64_t> seek_target_{0};
    std::atomic<bool> flush_req_{false};  // consumer (callback or pause path) clears the ring, then acks
    std::atomic<bool> flush_ack_{true};
    std::atomic<uint64_t> played_frames_{0};  // frames handed to the device since last seek base
    std::atomic<uint64_t> base_frame_{0};
    std::atomic<float> volume_{1.0f};
    double duration_hint_ = 0;
    uint32_t device_ = 0;
    uint32_t channels_ = 2;
    uint32_t rate_ = 44100;
};
