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
#include "../net/stream.h"
#include "../subsonic/model.h"
#include "store.h"

class Downloads {
public:
    enum State { Queued = 0, Active = 1, Done = 2, Failed = 3, Paused = 4 };
    struct Item { Song song; State state; int64_t bytes, total; };
    // url_fn builds a fresh authenticated URL for a song id (called on download threads).
    Downloads(Store& store, std::string dir, std::function<std::string(const std::string&)> url_fn, ConnectOptions opt, int threads = 2);
    ~Downloads();
    void set_on_change(std::function<void()> f) { on_change_ = std::move(f); }

    void enqueue(const std::vector<Song>& songs);
    void pause(const std::string& id);
    void resume(const std::string& id);
    void retry_failed();
    void remove(const std::string& id);          // cancels and deletes file + row
    void clear_all();
    int verify();                                // drops rows whose file is gone or short; returns how many
    std::string path_if_done(const std::string& id) const;
    std::vector<Item> snapshot() const;
    int pending() const;                         // queued + active
    std::vector<Album> done_albums() const;      // albums that have at least one finished track (songs = finished ones)

private:
    void loop();
    bool fetch(const Store::Row& row);
    void changed();
    std::string file_for(const Song& s) const;

    Store& store_;
    std::string dir_;
    std::function<std::string(const std::string&)> url_fn_;
    ConnectOptions opt_;
    std::function<void()> on_change_;
    mutable std::mutex m_;
    std::condition_variable cv_;
    std::vector<std::thread> th_;
    std::atomic<bool> stop_{false};
    std::map<std::string, std::atomic<bool>*> cancel_;  // active id -> cancel flag
    std::map<std::string, int64_t> live_bytes_;          // active id -> bytes so far
    std::atomic<uint32_t> last_notify_{0};
};
