#pragma once
#include <string>
#include <cstdint>
#include <vector>

struct Config {
    std::string server_url, username, pin_sha256;  // the password is never written to disk here
    int volume_pct = 80;
    int stream_quality = 0;  // max kbps, 0 = original
    int download_quality = 0;   // kbps, 0 = original
    int concurrent_downloads = 3;
    int default_view = 0;       // Library default: 0 artists, 1 albums, 2 songs, 3 liked
    int car_minimal = 0;        // 0 auto, 1 always, 2 never
    bool car_keep_on = true, car_start = false, car_pulse = false, debug_log = false, render_timer = false;
    int perf_mode = 0;          // 0 balanced, 1 gaming, 2 eco
    bool be_controlled = true, control_others = true;
    struct Conn { std::string url, user; int64_t last_used = 0; };   // connectionHistoryService: newest first, max 10
    std::vector<Conn> conns;
    void remember_connection(const std::string& url, const std::string& user);
    void forget_connection(const std::string& url, const std::string& user);
    int theme = 0;           // 0..7 presets, 8..11 custom slots
    int theme_mode = 0;      // 0 system, 1 light, 2 dark
    struct Custom { std::string name; uint32_t primary = 0x00bcd4; };
    Custom custom[4] = {{"My Theme 1", 0x00bcd4}, {"My Theme 2", 0x00bcd4}, {"My Theme 3", 0x00bcd4}, {"My Theme 4", 0x00bcd4}};
    bool load(const std::string& path);
    bool save(const std::string& path) const;
};
