// Registers on the session bus and talks to ourselves with busctl; skipped when there is no session bus or busctl.
#include <cstdio>
#include <cstdlib>
#include <atomic>
#include <chrono>
#include <thread>
#include "../src/app/mpris.h"
static std::string sh(const std::string& cmd) { FILE* f = popen((cmd + " 2>&1").c_str(), "r"); std::string o; if (!f) return o; char b[512]; while (fgets(b, sizeof b, f)) o += b; pclose(f); return o; }
int main() {
    if (std::system("busctl --user list >/dev/null 2>&1") != 0) { std::printf("SKIP no session bus\n"); return 0; }
    std::atomic<int> next{0}, playpause{0}; std::atomic<double> seek{0};
    Mpris m;
    if (!m.start("xylonic-fiber-test", [&](MprisCmd c, double a, const std::string&) { if (c == MprisCmd::Next) ++next; if (c == MprisCmd::PlayPause) ++playpause; if (c == MprisCmd::SeekRelative) seek = a; })) { std::printf("SKIP cannot own bus name (or built without sd-bus)\n"); return 0; }
    MprisState st; st.has_song = true; st.playing = true; st.title = "Tïtle \"x\""; st.artist = "Art"; st.album = "Alb"; st.track_id = "s1-2"; st.length_s = 125.5; st.volume = 0.5;
    m.update(st);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    const std::string B = "busctl --user ", N = " org.mpris.MediaPlayer2.xylonic-fiber-test /org/mpris/MediaPlayer2 ";
    int fails = 0;
    auto check = [&](bool c, const char* what, const std::string& out) { if (!c) { std::printf("FAIL %s: %s\n", what, out.c_str()); ++fails; } };
    std::string o = sh(B + "get-property" + N + "org.mpris.MediaPlayer2 Identity"); check(o.find("\"Xylonic\"") != std::string::npos, "identity", o);
    o = sh(B + "get-property" + N + "org.mpris.MediaPlayer2.Player PlaybackStatus"); check(o.find("Playing") != std::string::npos, "status", o);
    o = sh(B + "get-property" + N + "org.mpris.MediaPlayer2.Player Metadata"); check(o.find("xesam:title") != std::string::npos && o.find("/org/xylonic/track/s1_2") != std::string::npos && o.find("125500000") != std::string::npos, "metadata", o);
    o = sh(B + "get-property" + N + "org.mpris.MediaPlayer2.Player Volume"); check(o.find("0.5") != std::string::npos, "volume", o);
    sh(B + "call" + N + "org.mpris.MediaPlayer2.Player Next"); sh(B + "call" + N + "org.mpris.MediaPlayer2.Player PlayPause"); sh(B + "call" + N + "org.mpris.MediaPlayer2.Player Seek x 5000000");
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    check(next == 1 && playpause == 1 && seek == 5.0, "methods", std::to_string(next) + " " + std::to_string(playpause) + " " + std::to_string(seek));
    st.playing = false; m.update(st); std::this_thread::sleep_for(std::chrono::milliseconds(500));
    o = sh(B + "get-property" + N + "org.mpris.MediaPlayer2.Player PlaybackStatus"); check(o.find("Paused") != std::string::npos, "paused", o);
    m.stop();
    std::printf(fails ? "FAILED\n" : "OK\n");
    return fails ? 1 : 0;
}
