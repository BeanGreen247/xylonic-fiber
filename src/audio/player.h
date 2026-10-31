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

class Player {
public:
    Player();
    ~Player();
    bool load(const std::string& path);   // stops current, opens new, paused
    bool play();
    void pause();
    void stop();
    bool seek_seconds(double s);
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
    std::atomic<uint64_t> played_frames_{0};  // frames handed to the device since last seek base
    std::atomic<uint64_t> base_frame_{0};
    std::atomic<float> volume_{1.0f};
    uint32_t device_ = 0;
    uint32_t channels_ = 2;
    uint32_t rate_ = 44100;
};
