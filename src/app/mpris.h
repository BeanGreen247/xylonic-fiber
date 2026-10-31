// MPRIS2 media controls on Linux (public/mpris.js): org.mpris.MediaPlayer2[.Player] on the session bus via sd-bus.
// All sd-bus calls run on the service's own thread; commands are delivered through the callback from that thread.
#pragma once
#include <functional>
#include <memory>
#include <string>

struct MprisState {
    bool has_song = false, playing = false, shuffle = false;
    std::string title, artist, album, track_id, art_url, loop = "None";  // loop: None | Track | Playlist
    double length_s = 0, position_s = 0, volume = 1.0;
};

enum class MprisCmd { Play, Pause, PlayPause, Stop, Next, Previous, SeekRelative, SeekAbsolute, SetVolume, SetShuffle, SetLoop, Raise };

class Mpris {
public:
    using Callback = std::function<void(MprisCmd cmd, double arg, const std::string& sarg)>;
    Mpris();
    ~Mpris();
    bool start(const std::string& bus_name_suffix, Callback cb);  // false when no session bus / name taken
    void update(const MprisState& st);                            // thread-safe
    void stop();
    struct Impl;  // public so the implementation file can name it
private:
    std::unique_ptr<Impl> p_;
};
