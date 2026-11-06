#pragma once
#include <string>
#include <cstdint>
#include <vector>

struct Config {
    std::string server_url, username, pin_sha256;  // the password is never written to disk here
    std::string cache_root;   // streaming cache base (stream/ below it, and downloads/ unless downloads_root is set); empty = the app data folder
    std::string downloads_root;   // base for downloads/ only; empty = same as cache_root
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
    // One logical server+user, however many addresses reach it (home LAN, another house, VPN, a hostname). `id` names the data
    // folders and files, so downloads, library index, queue and playlists follow the account instead of the URL. `fp` is a
    // fingerprint of the library (see library_fingerprint) used to recognise the same server behind a new address.
    struct Account { std::string id, user; std::vector<std::string> urls, fp; };
    std::vector<Account> accounts;
    Account* find_account(const std::string& url, const std::string& user);
    Account* match_account(const std::string& user, const std::vector<std::string>& fp);   // same user, library overlaps; nullptr when none
    // The folder id for url+user: the stored account's id, or the legacy hash of url|user for a pair never seen before.
    std::string account_id(const std::string& url, const std::string& user);
    bool load(const std::string& path);
    bool save(const std::string& path) const;
};

// Stable sample of the library's artist ids: the ids whose hash falls in a fixed quarter, sorted, at most 128. Membership of an id
// never depends on the other ids, so a library that grew or lost a few artists still overlaps its old sample.
std::vector<std::string> library_fingerprint(const std::vector<std::string>& artist_ids);
// How many sample ids two fingerprints share, and whether that is enough to call them the same library.
size_t fingerprint_common(const std::vector<std::string>& a, const std::vector<std::string>& b);
bool fingerprint_match(const std::vector<std::string>& a, const std::vector<std::string>& b);
