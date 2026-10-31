#pragma once
#include <string>
#include <cstdint>

struct Config {
    std::string server_url, username, pin_sha256;  // the password is never written to disk here
    int volume_pct = 80;
    int stream_quality = 0;  // max kbps, 0 = original
    int theme = 0;           // 0..7 presets, 8..11 custom slots
    int theme_mode = 0;      // 0 system, 1 light, 2 dark
    struct Custom { std::string name; uint32_t primary = 0x00bcd4; };
    Custom custom[4] = {{"My Theme 1", 0x00bcd4}, {"My Theme 2", 0x00bcd4}, {"My Theme 3", 0x00bcd4}, {"My Theme 4", 0x00bcd4}};
    bool load(const std::string& path);
    bool save(const std::string& path) const;
};
