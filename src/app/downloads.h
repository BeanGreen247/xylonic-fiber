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
    struct Item { Song song; State state; int64_t bytes, total; int quality = -1; };
    // url_fn builds a fresh authenticated URL for a song id (called on download threads).
    Downloads(Store& store, std::string dir, std::function<std::string(const std::string&)> url_fn, ConnectOptions opt, int threads = 2);
    ~Downloads();
    // Quality label stored with each downloaded file (key song:<id>@<label>), e.g. "original", "320".
    void set_quality_label(std::string q) { quality_ = std::move(q); }
    void set_on_change(std::function<void()> f) { on_change_ = std::move(f); }

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
    void clear_all();
    int verify();                                // drops rows whose file is gone or short; returns how many
    int reconcile();                             // removes unowned files and stale partials
    std::string path_if_done(const std::string& id) const;
    std::vector<Item> snapshot() const;
    uint64_t media_bytes() const { return media_ ? media_->total_bytes() : 0; }
    size_t media_files() const { return media_ ? media_->blob_count() : 0; }
    int pending() const;                         // queued + active
    std::vector<Album> done_albums() const;      // albums that have at least one finished track (songs = finished ones)

private:
    void loop();
    bool fetch(const Store::Row& row);
    void changed();
    std::string file_for(const Song& s) const;

    Store& store_;
    std::string dir_;
    std::shared_ptr<xf::cache::MediaCache> media_;   // owns the downloaded bytes: refcount dedup, verify, orphan reconcile
    std::string quality_ = "original";
    std::function<std::string(const std::string&)> url_fn_;
    std::function<std::string(const std::string&, int)> url_q_;
    ConnectOptions opt_;
    std::function<void()> on_change_;
    mutable std::mutex m_;
    std::condition_variable cv_;
    std::vector<std::thread> th_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> paused_all_{false};
    std::map<std::string, std::atomic<bool>*> cancel_;  // active id -> cancel flag
    std::map<std::string, int64_t> live_bytes_;          // active id -> bytes so far
    std::atomic<uint32_t> last_notify_{0};
};
