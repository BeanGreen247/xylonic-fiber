#if defined(__ANDROID__)
#include "aaudio_out.h"
#include <android/log.h>
#include <algorithm>
#include <chrono>
#include <dlfcn.h>
#include <cstring>
#include <thread>

namespace {
// The slice of <aaudio/AAudio.h> used here, resolved with dlopen (values from the NDK header).
using Builder = void;
using Stream = void;
enum : int32_t { kOutput = 0, kFormatFloat = 2, kSharingShared = 1, kPerfNone = 10, kCallbackContinue = 0, kUsageMedia = 1, kContentMusic = 2 };
struct Api {
    void* lib = nullptr;
    int32_t (*createBuilder)(Builder**) = nullptr;
    void (*setDirection)(Builder*, int32_t) = nullptr;
    void (*setFormat)(Builder*, int32_t) = nullptr;
    void (*setChannelCount)(Builder*, int32_t) = nullptr;
    void (*setSampleRate)(Builder*, int32_t) = nullptr;
    void (*setSharing)(Builder*, int32_t) = nullptr;
    void (*setPerf)(Builder*, int32_t) = nullptr;
    void (*setUsage)(Builder*, int32_t) = nullptr;           // API 28
    void (*setContentType)(Builder*, int32_t) = nullptr;     // API 28
    void (*setDataCb)(Builder*, int32_t (*)(Stream*, void*, void*, int32_t), void*) = nullptr;
    void (*setErrCb)(Builder*, void (*)(Stream*, void*, int32_t), void*) = nullptr;
    int32_t (*openStream)(Builder*, Stream**) = nullptr;
    int32_t (*deleteBuilder)(Builder*) = nullptr;
    int32_t (*start)(Stream*) = nullptr;
    int32_t (*stop)(Stream*) = nullptr;
    int32_t (*closeStream)(Stream*) = nullptr;
    int32_t (*getRate)(Stream*) = nullptr;
    int32_t (*getBufferCapacity)(Stream*) = nullptr;
    int32_t (*setBufferSize)(Stream*, int32_t) = nullptr;
    int32_t (*getFramesPerBurst)(Stream*) = nullptr;
    bool ok = false;
};
Api g_api;
std::once_flag g_once;

template <class F> bool sym(void* lib, const char* name, F& out, bool required = true) {
    out = reinterpret_cast<F>(dlsym(lib, name));
    return out != nullptr || !required;
}

void load() {
    g_api.lib = dlopen("libaaudio.so", RTLD_NOW);
    if (!g_api.lib) return;
    void* l = g_api.lib;
    bool ok = sym(l, "AAudio_createStreamBuilder", g_api.createBuilder) && sym(l, "AAudioStreamBuilder_setDirection", g_api.setDirection) &&
              sym(l, "AAudioStreamBuilder_setFormat", g_api.setFormat) && sym(l, "AAudioStreamBuilder_setChannelCount", g_api.setChannelCount) &&
              sym(l, "AAudioStreamBuilder_setSampleRate", g_api.setSampleRate) && sym(l, "AAudioStreamBuilder_setSharingMode", g_api.setSharing) &&
              sym(l, "AAudioStreamBuilder_setPerformanceMode", g_api.setPerf) && sym(l, "AAudioStreamBuilder_setDataCallback", g_api.setDataCb) &&
              sym(l, "AAudioStreamBuilder_setErrorCallback", g_api.setErrCb) && sym(l, "AAudioStreamBuilder_openStream", g_api.openStream) &&
              sym(l, "AAudioStreamBuilder_delete", g_api.deleteBuilder) && sym(l, "AAudioStream_requestStart", g_api.start) &&
              sym(l, "AAudioStream_requestStop", g_api.stop) && sym(l, "AAudioStream_close", g_api.closeStream) &&
              sym(l, "AAudioStream_getSampleRate", g_api.getRate) && sym(l, "AAudioStream_getBufferCapacityInFrames", g_api.getBufferCapacity) &&
              sym(l, "AAudioStream_setBufferSizeInFrames", g_api.setBufferSize) && sym(l, "AAudioStream_getFramesPerBurst", g_api.getFramesPerBurst);
    sym(l, "AAudioStreamBuilder_setUsage", g_api.setUsage, false);
    sym(l, "AAudioStreamBuilder_setContentType", g_api.setContentType, false);
    g_api.ok = ok;
}
}  // namespace

bool AAudioOut::available() {
    std::call_once(g_once, load);
    return g_api.ok;
}

bool AAudioOut::open(uint32_t rate, Fill fill) {
    if (!available()) return false;
    close();
    closing_ = false;
    fill_ = std::move(fill);
    rate_ = rate;
    return start_stream();
}

bool AAudioOut::start_stream() {
    std::lock_guard<std::mutex> lk(m_);
    Builder* b = nullptr;
    if (g_api.createBuilder(&b) != 0 || !b) return false;
    g_api.setDirection(b, kOutput);
    g_api.setFormat(b, kFormatFloat);
    g_api.setChannelCount(b, 2);
    g_api.setSampleRate(b, static_cast<int32_t>(rate_));
    g_api.setSharing(b, kSharingShared);
    g_api.setPerf(b, kPerfNone);   // music: a deeper buffer and fewer wake-ups beat the lowest latency
    if (g_api.setUsage) g_api.setUsage(b, kUsageMedia);
    if (g_api.setContentType) g_api.setContentType(b, kContentMusic);
    g_api.setDataCb(b, reinterpret_cast<int32_t (*)(Stream*, void*, void*, int32_t)>(&AAudioOut::cb), this);
    g_api.setErrCb(b, reinterpret_cast<void (*)(Stream*, void*, int32_t)>(&AAudioOut::err), this);
    Stream* s = nullptr;
    const int32_t r = g_api.openStream(b, &s);
    g_api.deleteBuilder(b);
    if (r != 0 || !s) { __android_log_print(ANDROID_LOG_WARN, "xylonic", "AAudio open failed: %d", r); return false; }
    // a buffer of a few bursts rides out scheduling hiccups; the default of two bursts is the glitchiest setting on weak CPUs
    const int32_t burst = g_api.getFramesPerBurst(s), cap = g_api.getBufferCapacity(s);
    if (burst > 0) g_api.setBufferSize(s, std::min(cap > 0 ? cap : burst * 8, burst * 8));
    if (g_api.start(s) != 0) { g_api.closeStream(s); return false; }
    stream_ = s;
    return true;
}

int AAudioOut::cb(void*, void* user, void* data, int32_t frames) {
    static_cast<AAudioOut*>(user)->fill_(static_cast<float*>(data), frames);
    return kCallbackContinue;
}

// A route change (headphones, Bluetooth) disconnects the stream: reopen it from another thread (a stream cannot be closed from its own callback).
void AAudioOut::err(void*, void* user, int32_t error) {
    AAudioOut* o = static_cast<AAudioOut*>(user);
    __android_log_print(ANDROID_LOG_INFO, "xylonic", "AAudio stream error %d", error);
    o->reopen_async();
}

void AAudioOut::reopen_async() {
    if (closing_.load() || reopening_.exchange(true)) return;
    std::thread([this] {
        Stream* old = nullptr;
        { std::lock_guard<std::mutex> lk(m_); old = static_cast<Stream*>(stream_); stream_ = nullptr; }
        if (old) { g_api.stop(old); g_api.closeStream(old); }
        for (int i = 0; i < 20 && !closing_.load(); ++i) {   // the new route can take a moment to appear
            if (start_stream()) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        reopening_ = false;
    }).detach();
}

void AAudioOut::close() {
    closing_ = true;
    Stream* s = nullptr;
    { std::lock_guard<std::mutex> lk(m_); s = static_cast<Stream*>(stream_); stream_ = nullptr; }
    if (s) { g_api.stop(s); g_api.closeStream(s); }
    while (reopening_.load()) std::this_thread::sleep_for(std::chrono::milliseconds(5));   // a reopen in flight must finish before the object goes
    { std::lock_guard<std::mutex> lk(m_); s = static_cast<Stream*>(stream_); stream_ = nullptr; }
    if (s) { g_api.stop(s); g_api.closeStream(s); }
}
#endif
