#pragma once
#include <string>
#include <cstdint>
#include <vector>

struct Config {
    std::string server_url, username, pin_sha256;  // the password is never written to disk here
    std::string cache_root;   // permanent cache base (downloads/ and stream/ below it); empty = the app data folder
    int volume_pct = 80;
    int stream_quality = 0;  // max kbps, 0 = original
    int download_quality = 320;   // kbps, 0 = original (the original app defaults to 320)
    int concurrent_downloads = 3;
    int default_view = 0;       // Library default: 0 artists, 1 albums, 2 songs, 3 liked
    int car_minimal = 0;        // 0 auto, 1 always, 2 never
    bool offline_mode = false;   // localStorage "offlineMode": the app starts offline again
    bool car_keep_on = true, car_start = false, car_pulse = false, debug_log = false, render_timer = false, software_render = false;
    int perf_mode = 0;          // 0 balanced, 1 gaming, 2 eco
    bool be_controlled = true, control_others = true;
    bool auto_offline_metered = false;   // switch to offline mode on a metered connection when songs are downloaded
    struct Conn { std::string url, user; int64_t last_used = 0; };   // connectionHistoryService: newest first, max 10
    std::vector<Conn> conns;
    void remember_connection(const std::string& url, const std::string& user);
    void forget_connection(const std::string& url, const std::string& user);
    int theme = 0;           // 0..7 presets, 8..11 custom slots
    int app_icon = 0;        // launcher / window / splash icon: index into kAppIcons (app.h)
    int icon_set = 1;        // 0 classic (solid), 1 GitHub (Octicons), 2 Phosphor, 3 Material
    int theme_mode = 0;      // 0 system, 1 light, 2 dark
    struct Custom { std::string name; uint32_t primary = 0x00bcd4; };
    Custom custom[4] = {{"My Theme 1", 0x00bcd4}, {"My Theme 2", 0x00bcd4}, {"My Theme 3", 0x00bcd4}, {"My Theme 4", 0x00bcd4}};
    bool load(const std::string& path);
    bool save(const std::string& path) const;
};
