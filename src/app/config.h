#pragma once
#include <string>

struct Config {
    std::string server_url, username, pin_sha256;  // the password is never written to disk here
    int volume_pct = 80;
    int stream_quality = 0;  // max kbps, 0 = original
    bool load(const std::string& path);
    bool save(const std::string& path) const;
};
