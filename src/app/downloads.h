// Permanent offline cache: downloads whole tracks to disk, resumable across restarts, with progress.
#pragma once
#include <atomic>
#include <condition_variable>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <memory>
#include "../cache/media.h"
#include "../net/stream.h"
#include "../subsonic/model.h"
#include "store.h"

class Downloads {
public:
    enum State { Queued = 0, Active = 1, Done = 2, Failed = 3, Paused = 4 };
    struct Item { Song song; State state; int64_t bytes = 0, total = 0; int quality = -1; };
    // url_fn builds a fresh authenticated URL for a song id (called on download threads).
    Downloads(Store& store, std::string dir, std::function<std::string(const std::string&)> url_fn, ConnectOptions opt, int threads = 2);
    ~Downloads();
    // Quality label stored with each downloaded file (key song:<id>@<label>), e.g. "original", "320".
    void set_quality_label(std::string q) { quality_ = std::move(q); }
    void set_on_change(std::function<void()> f) { on_change_ = std::move(f); }
    // Called on a download thread after each finished song (the app fetches its cover art so the downloaded library also looks right offline).
    void set_on_done(std::function<void(const Song&)> f) { on_done_ = std::move(f); }

    // quality kbps (0 original); -1 uses the default download setting at fetch time.
    void enqueue(const std::vector<Song>& songs, int quality = -1);
    // URL builder that knows the quality of each queued song (replaces the plain builder once set).
    void set_quality_url_fn(std::function<std::string(const std::string&, int)> f) { url_q_ = std::move(f); }
    void pause(const std::string& id);
    void resume(const std::string& id);
    void retry_failed();
    void pause_all();                            // stops the active songs and keeps the queue from starting new ones
    void resume_all();
    bool paused_all() const { return paused_all_.load(); }
    void clear_queue();                          // cancels and removes every song that is not finished
    int remove_album(const std::string& album_id);   // deletes the finished songs of one album; returns how many
    void remove(const std::string& id);          // cancels and deletes file + row
    void remove_many(const std::vector<std::string>& ids);   // same for a batch: one manifest save and one notification (clearing a 5000-song queue)
    void clear_all();
    int verify();                                // drops rows whose file is gone or short; returns how many (stats on several threads)
    void start_verify();                         // same on a background thread; poll verify_running / verify_dropped
    bool verify_running() const { return verifying_.load(); }
    int verify_dropped() const { return verify_dropped_.load(); }
    void clear_queue_async();                    // clear_queue off the calling thread; the change notification fires when done
    int reconcile();                             // removes unowned files and stale partials
    std::string path_if_done(const std::string& id) const;
    using Snapshot = std::shared_ptr<const std::vector<Item>>;
    // Every download with live byte counts. Cached: rebuilt only after something changed, and songs are parsed once per id, so a
    // 5000-song queue costs a few ms per frame instead of 5000 JSON parses.
    Snapshot snapshot() const;
    // Bytes received so far for a download in progress (the snapshot is not rebuilt for every chunk), else the snapshot value.
    int64_t live_bytes(const std::string& id, int64_t fallback) const;
    uint64_t media_bytes() const { return media_ ? media_->total_bytes() : 0; }
    size_t media_files() const { return media_ ? media_->blob_count() : 0; }
    int pending() const;                         // queued + active
    std::vector<Album> done_albums() const;      // albums that have at least one finished track (songs = finished ones)

private:
    bool remove_one(const std::string& id);   // returns true when the media manifest changed
    void loop();
    bool fetch(const Store::Row& row);
    enum class Change { Progress, Background, User };
    void changed(Change kind = Change::User);
    std::string file_for(const Song& s) const;

    Store& store_;
    std::string dir_;
    std::shared_ptr<xf::cache::MediaCache> media_;   // owns the downloaded bytes: refcount dedup, verify, orphan reconcile
    std::string quality_ = "original";
    std::function<std::string(const std::string&)> url_fn_;
    std::function<std::string(const std::string&, int)> url_q_;
    ConnectOptions opt_;
    std::function<void()> on_change_;
    std::function<void(const Song&)> on_done_;
    mutable std::mutex m_;
    mutable Snapshot snap_;
    mutable std::unordered_map<std::string, Song> songs_;   // parsed song_json by id
    std::atomic<uint64_t> struct_gen_{0}, force_gen_{0};   // snapshot invalidation: any structural change / a user-initiated one
    mutable uint64_t snap_struct_ = ~0ull, snap_force_ = ~0ull;
    mutable uint32_t snap_time_ = 0;
    std::condition_variable cv_;
    std::vector<std::thread> th_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> paused_all_{false};
    std::map<std::string, std::atomic<bool>*> cancel_;  // active id -> cancel flag
    std::map<std::string, int64_t> live_bytes_;          // active id -> bytes so far
    std::atomic<uint32_t> last_notify_{0};
    std::atomic<bool> verifying_{false};
    std::atomic<int> verify_dropped_{0};
    std::thread bg_;   // verify / clear-queue worker, joined by the destructor
    std::mutex bg_m_;
};
