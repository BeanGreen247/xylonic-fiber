#include "mpris.h"
#if defined(XF_MPRIS)
#include <cctype>
#include <systemd/sd-bus.h>
#include <atomic>
#include <cmath>
#include <mutex>
#include <thread>

struct Mpris::Impl {
    sd_bus* bus = nullptr;
    sd_bus_slot *slot_root = nullptr, *slot_player = nullptr;
    std::thread th;
    std::atomic<bool> run{false};
    std::mutex mu;
    MprisState cur, pending, emitted;
    bool dirty = false, seeked = false;
    Mpris::Callback cb;
    static constexpr const char* kPath = "/org/mpris/MediaPlayer2";

    std::string status() const { return !cur.has_song ? "Stopped" : cur.playing ? "Playing" : "Paused"; }
};

namespace {
using Impl = Mpris::Impl;
Impl* I(void* u) { return static_cast<Impl*>(u); }
int64_t us(double s) { return static_cast<int64_t>(std::llround(s * 1e6)); }

int get_status(sd_bus*, const char*, const char*, const char*, sd_bus_message* r, void* u, sd_bus_error*) { Impl* m = I(u); std::lock_guard<std::mutex> lk(m->mu); return sd_bus_message_append(r, "s", m->status().c_str()); }
int get_loop(sd_bus*, const char*, const char*, const char*, sd_bus_message* r, void* u, sd_bus_error*) { Impl* m = I(u); std::lock_guard<std::mutex> lk(m->mu); return sd_bus_message_append(r, "s", m->cur.loop.c_str()); }
int get_rate(sd_bus*, const char*, const char*, const char*, sd_bus_message* r, void*, sd_bus_error*) { return sd_bus_message_append(r, "d", 1.0); }
int get_shuffle(sd_bus*, const char*, const char*, const char*, sd_bus_message* r, void* u, sd_bus_error*) { Impl* m = I(u); std::lock_guard<std::mutex> lk(m->mu); return sd_bus_message_append(r, "b", m->cur.shuffle ? 1 : 0); }
int get_volume(sd_bus*, const char*, const char*, const char*, sd_bus_message* r, void* u, sd_bus_error*) { Impl* m = I(u); std::lock_guard<std::mutex> lk(m->mu); return sd_bus_message_append(r, "d", m->cur.volume); }
int get_position(sd_bus*, const char*, const char*, const char*, sd_bus_message* r, void* u, sd_bus_error*) { Impl* m = I(u); std::lock_guard<std::mutex> lk(m->mu); return sd_bus_message_append(r, "x", us(m->cur.position_s)); }
int get_true(sd_bus*, const char*, const char*, const char*, sd_bus_message* r, void*, sd_bus_error*) { return sd_bus_message_append(r, "b", 1); }
int get_false(sd_bus*, const char*, const char*, const char*, sd_bus_message* r, void*, sd_bus_error*) { return sd_bus_message_append(r, "b", 0); }
int get_has_song(sd_bus*, const char*, const char*, const char*, sd_bus_message* r, void* u, sd_bus_error*) { Impl* m = I(u); std::lock_guard<std::mutex> lk(m->mu); return sd_bus_message_append(r, "b", m->cur.has_song ? 1 : 0); }
int get_identity(sd_bus*, const char*, const char*, const char*, sd_bus_message* r, void*, sd_bus_error*) { return sd_bus_message_append(r, "s", "Xylonic"); }
int get_empty_as(sd_bus*, const char*, const char*, const char*, sd_bus_message* r, void*, sd_bus_error*) { return sd_bus_message_append(r, "as", 0); }

int get_metadata(sd_bus*, const char*, const char*, const char*, sd_bus_message* r, void* u, sd_bus_error*) {
    Impl* m = I(u);
    std::lock_guard<std::mutex> lk(m->mu);
    int rc = sd_bus_message_open_container(r, 'a', "{sv}");
    if (rc < 0) return rc;
    if (m->cur.has_song) {
        auto entry_s = [&](const char* k, const char* v) { sd_bus_message_open_container(r, 'e', "sv"); sd_bus_message_append(r, "s", k); sd_bus_message_open_container(r, 'v', "s"); sd_bus_message_append(r, "s", v); sd_bus_message_close_container(r); sd_bus_message_close_container(r); };
        std::string safe;
        for (char c : m->cur.track_id) safe += (std::isalnum(static_cast<unsigned char>(c)) || c == '_') ? c : '_';
        std::string tid = "/org/xylonic/track/" + (safe.empty() ? std::string("unknown") : safe);
        sd_bus_message_open_container(r, 'e', "sv"); sd_bus_message_append(r, "s", "mpris:trackid"); sd_bus_message_open_container(r, 'v', "o"); sd_bus_message_append(r, "o", tid.c_str()); sd_bus_message_close_container(r); sd_bus_message_close_container(r);
        entry_s("xesam:title", m->cur.title.c_str());
        sd_bus_message_open_container(r, 'e', "sv"); sd_bus_message_append(r, "s", "xesam:artist"); sd_bus_message_open_container(r, 'v', "as"); sd_bus_message_append(r, "as", 1, m->cur.artist.c_str()); sd_bus_message_close_container(r); sd_bus_message_close_container(r);
        entry_s("xesam:album", m->cur.album.c_str());
        if (m->cur.length_s > 0) { sd_bus_message_open_container(r, 'e', "sv"); sd_bus_message_append(r, "s", "mpris:length"); sd_bus_message_open_container(r, 'v', "x"); sd_bus_message_append(r, "x", us(m->cur.length_s)); sd_bus_message_close_container(r); sd_bus_message_close_container(r); }
        if (!m->cur.art_url.empty()) entry_s("mpris:artUrl", m->cur.art_url.c_str());
    }
    return sd_bus_message_close_container(r);
}

int set_loop(sd_bus*, const char*, const char*, const char*, sd_bus_message* v, void* u, sd_bus_error*) { const char* s = nullptr; if (sd_bus_message_read(v, "s", &s) < 0 || !s) return -EINVAL; I(u)->cb(MprisCmd::SetLoop, 0, s); return 0; }
int set_rate(sd_bus*, const char*, const char*, const char*, sd_bus_message* v, void*, sd_bus_error*) { double d; return sd_bus_message_read(v, "d", &d) < 0 ? -EINVAL : 0; }
int set_shuffle(sd_bus*, const char*, const char*, const char*, sd_bus_message* v, void* u, sd_bus_error*) { int b = 0; if (sd_bus_message_read(v, "b", &b) < 0) return -EINVAL; I(u)->cb(MprisCmd::SetShuffle, b ? 1 : 0, ""); return 0; }
int set_volume(sd_bus*, const char*, const char*, const char*, sd_bus_message* v, void* u, sd_bus_error*) { double d = 0; if (sd_bus_message_read(v, "d", &d) < 0) return -EINVAL; I(u)->cb(MprisCmd::SetVolume, std::fmax(0.0, std::fmin(1.0, d)), ""); return 0; }

#define SIMPLE_METHOD(name, cmd) int name(sd_bus_message* m, void* u, sd_bus_error*) { I(u)->cb(cmd, 0, ""); return sd_bus_reply_method_return(m, nullptr); }
SIMPLE_METHOD(m_next, MprisCmd::Next)
SIMPLE_METHOD(m_prev, MprisCmd::Previous)
SIMPLE_METHOD(m_pause, MprisCmd::Pause)
SIMPLE_METHOD(m_playpause, MprisCmd::PlayPause)
SIMPLE_METHOD(m_stop, MprisCmd::Stop)
SIMPLE_METHOD(m_play, MprisCmd::Play)
SIMPLE_METHOD(m_raise, MprisCmd::Raise)
int m_quit(sd_bus_message* m, void*, sd_bus_error*) { return sd_bus_reply_method_return(m, nullptr); }
int m_open(sd_bus_message* m, void*, sd_bus_error*) { return sd_bus_reply_method_return(m, nullptr); }
int m_seek(sd_bus_message* m, void* u, sd_bus_error*) { int64_t off = 0; if (sd_bus_message_read(m, "x", &off) < 0) return -EINVAL; I(u)->cb(MprisCmd::SeekRelative, static_cast<double>(off) / 1e6, ""); return sd_bus_reply_method_return(m, nullptr); }
int m_setpos(sd_bus_message* m, void* u, sd_bus_error*) { const char* t = nullptr; int64_t p = 0; if (sd_bus_message_read(m, "ox", &t, &p) < 0) return -EINVAL; I(u)->cb(MprisCmd::SeekAbsolute, static_cast<double>(p) / 1e6, ""); return sd_bus_reply_method_return(m, nullptr); }

const sd_bus_vtable root_vt[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("Raise", "", "", m_raise, 0),
    SD_BUS_METHOD("Quit", "", "", m_quit, 0),
    SD_BUS_PROPERTY("CanQuit", "b", get_false, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("CanRaise", "b", get_true, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("HasTrackList", "b", get_false, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("Identity", "s", get_identity, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("SupportedUriSchemes", "as", get_empty_as, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("SupportedMimeTypes", "as", get_empty_as, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_VTABLE_END};

const sd_bus_vtable player_vt[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("Next", "", "", m_next, 0),
    SD_BUS_METHOD("Previous", "", "", m_prev, 0),
    SD_BUS_METHOD("Pause", "", "", m_pause, 0),
    SD_BUS_METHOD("PlayPause", "", "", m_playpause, 0),
    SD_BUS_METHOD("Stop", "", "", m_stop, 0),
    SD_BUS_METHOD("Play", "", "", m_play, 0),
    SD_BUS_METHOD("Seek", "x", "", m_seek, 0),
    SD_BUS_METHOD("SetPosition", "ox", "", m_setpos, 0),
    SD_BUS_METHOD("OpenUri", "s", "", m_open, 0),
    SD_BUS_SIGNAL("Seeked", "x", 0),
    SD_BUS_PROPERTY("PlaybackStatus", "s", get_status, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_WRITABLE_PROPERTY("LoopStatus", "s", get_loop, set_loop, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_WRITABLE_PROPERTY("Rate", "d", get_rate, set_rate, 0, 0),
    SD_BUS_WRITABLE_PROPERTY("Shuffle", "b", get_shuffle, set_shuffle, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("Metadata", "a{sv}", get_metadata, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_WRITABLE_PROPERTY("Volume", "d", get_volume, set_volume, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("Position", "x", get_position, 0, 0),
    SD_BUS_PROPERTY("MinimumRate", "d", get_rate, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("MaximumRate", "d", get_rate, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("CanGoNext", "b", get_has_song, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("CanGoPrevious", "b", get_has_song, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("CanPlay", "b", get_has_song, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("CanPause", "b", get_has_song, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("CanSeek", "b", get_true, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("CanControl", "b", get_true, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_VTABLE_END};

void loop(Impl* m) {
    const char* player_if = "org.mpris.MediaPlayer2.Player";
    while (m->run.load()) {
        int r = sd_bus_process(m->bus, nullptr);
        if (r > 0) continue;
        bool st = false, meta = false, vol = false, shuf = false, loopc = false;
        {
            std::lock_guard<std::mutex> lk(m->mu);
            if (m->dirty) {
                const MprisState& a = m->emitted;
                m->cur = m->pending;
                const MprisState& b = m->cur;
                st = a.has_song != b.has_song || a.playing != b.playing;
                meta = a.title != b.title || a.artist != b.artist || a.album != b.album || a.track_id != b.track_id || a.art_url != b.art_url || std::fabs(a.length_s - b.length_s) > 0.5;
                vol = std::fabs(a.volume - b.volume) > 0.001;
                shuf = a.shuffle != b.shuffle;
                loopc = a.loop != b.loop;
                m->emitted = b;
                m->dirty = false;
            }
        }
        // the getters take the lock themselves, so emit with it released
        if (st) sd_bus_emit_properties_changed(m->bus, Impl::kPath, player_if, "PlaybackStatus", "CanGoNext", "CanGoPrevious", "CanPlay", "CanPause", nullptr);
        if (meta) sd_bus_emit_properties_changed(m->bus, Impl::kPath, player_if, "Metadata", nullptr);
        if (vol) sd_bus_emit_properties_changed(m->bus, Impl::kPath, player_if, "Volume", nullptr);
        if (shuf) sd_bus_emit_properties_changed(m->bus, Impl::kPath, player_if, "Shuffle", nullptr);
        if (loopc) sd_bus_emit_properties_changed(m->bus, Impl::kPath, player_if, "LoopStatus", nullptr);
        sd_bus_wait(m->bus, 200000);
    }
}
}  // namespace

Mpris::Mpris() : p_(new Impl) {}
Mpris::~Mpris() { stop(); }

bool Mpris::start(const std::string& suffix, Callback cb) {
    if (p_->bus) return true;
    p_->cb = std::move(cb);
    if (sd_bus_open_user(&p_->bus) < 0) { p_->bus = nullptr; return false; }
    if (sd_bus_add_object_vtable(p_->bus, &p_->slot_root, Impl::kPath, "org.mpris.MediaPlayer2", root_vt, p_.get()) < 0 ||
        sd_bus_add_object_vtable(p_->bus, &p_->slot_player, Impl::kPath, "org.mpris.MediaPlayer2.Player", player_vt, p_.get()) < 0 ||
        sd_bus_request_name(p_->bus, ("org.mpris.MediaPlayer2." + suffix).c_str(), 0) < 0) {
        sd_bus_flush_close_unref(p_->bus);
        p_->bus = nullptr;
        return false;
    }
    p_->run = true;
    p_->th = std::thread(loop, p_.get());
    return true;
}

void Mpris::update(const MprisState& st) {
    std::lock_guard<std::mutex> lk(p_->mu);
    p_->pending = st;
    p_->dirty = true;
}

void Mpris::stop() {
    if (!p_ || !p_->bus) return;
    p_->run = false;
    if (p_->th.joinable()) p_->th.join();
    sd_bus_slot_unref(p_->slot_root);
    sd_bus_slot_unref(p_->slot_player);
    sd_bus_flush_close_unref(p_->bus);
    p_->bus = nullptr;
}
#elif defined(__ANDROID__)
// Android: MediaSession + foreground service in Java (org.xylonic.fiber.MediaService); android_bridge pushes state over JNI and
// transport commands come back through MediaBridge.nativeCommand (called on an Android thread; the app callback only posts to the UI thread).
#include <jni.h>
#include <mutex>
#include "android_bridge.h"

struct Mpris::Impl {};

namespace {
std::mutex g_cb_mu;
Mpris::Callback g_cb;
}  // namespace

extern "C" JNIEXPORT void JNICALL Java_org_xylonic_fiber_MediaBridge_nativeCommand(JNIEnv*, jclass, jint cmd, jdouble arg) {
    Mpris::Callback cb;
    { std::lock_guard<std::mutex> lk(g_cb_mu); cb = g_cb; }
    if (!cb) return;
    switch (cmd) {
    case 0: cb(MprisCmd::Play, 0, ""); break;
    case 1: cb(MprisCmd::Pause, 0, ""); break;
    case 2: cb(MprisCmd::PlayPause, 0, ""); break;
    case 3: cb(MprisCmd::Stop, 0, ""); break;
    case 4: cb(MprisCmd::Next, 0, ""); break;
    case 5: cb(MprisCmd::Previous, 0, ""); break;
    case 6: cb(MprisCmd::SeekAbsolute, arg, ""); break;
    default: break;
    }
}

Mpris::Mpris() : p_(new Impl) {}
Mpris::~Mpris() { stop(); }

bool Mpris::start(const std::string&, Callback cb) {
    std::lock_guard<std::mutex> lk(g_cb_mu);
    g_cb = std::move(cb);
    return true;
}

void Mpris::update(const MprisState& st) { android_bridge::media_update(st); }   // UI thread

void Mpris::stop() {
    std::lock_guard<std::mutex> lk(g_cb_mu);
    g_cb = nullptr;
}
#else
struct Mpris::Impl {};
Mpris::Mpris() : p_(new Impl) {}
Mpris::~Mpris() = default;
bool Mpris::start(const std::string&, Callback) { return false; }
void Mpris::update(const MprisState&) {}
void Mpris::stop() {}
#endif
