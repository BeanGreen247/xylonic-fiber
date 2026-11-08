// Android audio output straight on AAudio with a data callback (libaaudio loaded at run time: it exists from API 26, the app supports 24).
// SDL 2.30's AAudio backend writes each buffer with a 1 ms timeout and drops whatever did not fit, which on slow devices plays the music
// at a fraction of real time; this talks to AAudio the way the NDK samples do. Float PCM, so 24/32-bit sources reach the mixer intact.
#pragma once
#if defined(__ANDROID__)
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <vector>

class AAudioOut {
public:
    // fill(out, frames): write `frames` stereo float frames (silence on underrun). Called on the AAudio thread: no allocation, no blocking.
    using Fill = std::function<void(float* out, int frames)>;
    AAudioOut() = default;
    ~AAudioOut() { close(); }
    bool open(uint32_t rate, Fill fill);   // false when AAudio is unavailable or refuses; the caller falls back to SDL
    void close();
    uint32_t rate() const { return rate_; }
    static bool available();

private:
    struct Impl;
    static int cb(void* stream, void* user, void* data, int32_t frames);
    static void err(void* stream, void* user, int32_t error);
    bool start_stream();
    void reopen_async();
    Fill fill_;
    uint32_t rate_ = 0;
    void* stream_ = nullptr;
    std::mutex m_;
    std::atomic<bool> closing_{false}, reopening_{false};
};
#endif
