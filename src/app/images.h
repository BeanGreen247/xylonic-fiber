// Cover art cache, modelled on the original imageCacheService/AlbumArt:
//  - disk cache keyed by cover-art id (one 300 px copy per id; "hi:<id>" holds the 2000 px Car Mode copy),
//  - a bounded LRU of GPU textures per (id, display size),
//  - Navidrome's 546-byte "no image" artist star recognised by size + SHA-256 and never cached or drawn,
//  - artist photos older than 7 days refreshed once per session, albums valid for a year,
//  - network fetches limited per performance mode (gaming 1, balanced 4, eco 1) with 429 back-off (2 s doubling to 30 s),
//  - iTunes lookup only for items that have no cover id at all (term "artist album").
#pragma once
#include <SDL.h>
#include <string>

// Loads a bundled PNG (app icon, splash art) as a texture; null when missing or undecodable.
SDL_Texture* load_png_texture(SDL_Renderer* r, const std::string& path);
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "../cache/media.h"
#include "../net/stream.h"
#include "worker.h"

class ImageCache {
public:
    using UrlFn = std::function<std::string(int size)>;   // server URL for a cover at the given edge size
    enum class Fetch { Ok, Placeholder, RateLimited, Failed };
    static constexpr int kStdSize = 300;      // size every library image is requested at
    static constexpr int kHiSize = 2000;      // Car Mode art
    static constexpr size_t kHiMaxTextures = 4, kHiMaxDisk = 40;

    // True for Navidrome's "no image" artist star (id "ar-...", 546 bytes, known SHA-256).
    static bool is_placeholder(const std::string& cover_id, const std::string& bytes);
    bool fallback_id(const std::string& cover_id) const;   // any size of this cover shows the icon
    ImageCache(SDL_Renderer* r, Worker& w, std::string dir, size_t max_textures);
    ~ImageCache();
    void set_disk_budget(uint64_t bytes) { budget_ = bytes; }
    void set_connect_options(ConnectOptions o) { opt_ = std::move(o); }
    void set_on_ready(std::function<void()> f) { on_ready_ = std::move(f); }
    void set_itunes_fallback(bool on) { itunes_ = on; }
    void set_blackout(bool on) { blackout_ = on; }   // no on-demand fetches while the bulk precache runs
    void set_max_textures(size_t n) { max_ = n; evict(); }
    void set_concurrency(int n) { std::lock_guard<std::mutex> lk(qm_); max_active_ = n < 1 ? 1 : n > 4 ? 4 : n; qcv_.notify_all(); }
    // Song cover ids (mf-...) that are really their album's art (al-...): lets a list reuse the album image.
    void set_alias(std::unordered_map<std::string, std::string> m) { std::lock_guard<std::mutex> lk(am_); alias_ = std::move(m); }

    // Non-blocking. key = "<cover id>@<display px>", "hi:<cover id>@<px>" or "net:<artist>::<album>@<px>".
    // Returns the texture when resident, else schedules the load and returns nullptr.
    SDL_Texture* get(const std::string& key, UrlFn url_fn, const std::string& artist = "", const std::string& album = "");
    bool fallback(const std::string& key) const;   // draw the icon instead: server has no photo, or loading gave up
    bool average(const std::string& key, uint32_t& rgb) const;
    std::string disk_path(const std::string& cover_id) const;

    // Synchronous (caller's thread, not gated): downloads one cover into the disk cache. Used by the precache.
    Fetch fetch_to_disk(const std::string& cover_id, const UrlFn& url_fn, bool hi = false);
    bool on_disk(const std::string& cover_id) const;

    struct Stats { uint64_t mem_hits = 0, disk_hits = 0, server_fetches = 0, internet_fetches = 0; };
    Stats stats() const { return {mem_hits_.load(), disk_hits_.load(), server_fetches_.load(), internet_fetches_.load()}; }
    void reset_stats() { mem_hits_ = disk_hits_ = server_fetches_ = internet_fetches_ = 0; }

    size_t resident() const { return tex_.size(); }
    uint64_t disk_bytes() const { return media_->total_bytes(); }
    size_t disk_files() const { return media_->blob_count(); }
    uint64_t disk_budget() const { return budget_; }
    void clear_memory();
    int clear_disk();                 // everything: media files, placeholder memory, failures
    void save() { media_->save(); }

private:
    struct Entry { SDL_Texture* t = nullptr; uint64_t tick; uint32_t stat_ms; };
    struct Key { std::string id, term_artist, term_album; int dev = 0; bool hi = false, net = false; };
    static Key parse(const std::string& key);
    std::string disk_id(const Key& k) const;
    std::string resolve_alias(const std::string& id) const;
    void evict();
    void run_job(const std::string& key, Key k, UrlFn url_fn, std::string artist, std::string album);
    Fetch fetch_bytes(const std::string& disk_id, const std::string& url, bool placeholder_check, std::string& bytes);
    void store(const std::string& disk_id, const std::string& bytes);
    void worker_loop();

    SDL_Renderer* r_ = nullptr;
    Worker& w_;
    std::string dir_;
    size_t max_ = 0;
    std::shared_ptr<xf::cache::MediaCache> media_;
    uint64_t budget_ = 64ull << 20;
    std::atomic<int> puts_{0};
    ConnectOptions opt_;
    std::function<void()> on_ready_;
    bool itunes_ = true;
    std::atomic<bool> blackout_{false};
    uint64_t tick_ = 0;
    // UI-thread state
    std::unordered_map<std::string, Entry> tex_;
    std::unordered_map<std::string, uint32_t> avg_;
    std::unordered_set<std::string> pending_;
    std::unordered_map<std::string, std::pair<int, uint32_t>> retry_;   // key -> (attempts, retry not before tick)
    std::unordered_set<std::string> fallback_, fallback_ids_;         // keys / ids showing the icon
    std::unordered_set<std::string> revalidated_try_;                 // artist ids refreshed (or tried) this session (guarded by pm_)
    // shared
    mutable std::mutex am_;
    std::unordered_map<std::string, std::string> alias_;
    std::mutex pm_;
    std::unordered_set<std::string> placeholder_ids_;
    std::unordered_map<std::string, std::string> itunes_urls_;        // "artist::album" -> art URL ("" = none)
    // network queue
    std::mutex qm_;
    std::condition_variable qcv_;
    std::deque<std::function<void()>> queue_;
    std::vector<std::thread> threads_;
    int max_active_ = 4, active_ = 0;
    bool stop_ = false;
    std::atomic<uint32_t> backoff_until_{0};
    std::atomic<int> rate_hits_{0};
    std::atomic<uint64_t> mem_hits_{0}, disk_hits_{0}, server_fetches_{0}, internet_fetches_{0};
};
