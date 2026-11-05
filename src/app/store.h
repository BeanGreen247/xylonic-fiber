// Small JSON-backed persistence layer. The original Electron app uses
// localStorage/IndexedDB rather than a relational database; this keeps native state in one
// atomic JSON manifest while retaining the existing C++ API.
#pragma once
#include <cstdint>
#include <atomic>
#include <mutex>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "../cache/batch.h"
#include "../cache/generation.h"
#include "../cache/journal.h"
#include "../subsonic/model.h"

class Store {
public:
    ~Store();
    bool open(const std::string& path);  // ":memory:" keeps state in memory for tests
    bool put(const std::string& key, const std::string& value);
    bool get(const std::string& key, std::string& value, int64_t* ts = nullptr);
    void erase_prefix(const std::string& prefix);
    struct Row { std::string id, album_id, path, song_json, content_hash; int state = 0; int64_t bytes = 0, total = 0; int quality = -1; };   // quality kbps, 0 original, -1 default setting
    bool dl_upsert(const Row& r);
    uint64_t dl_version() const { return dl_ver_.load(); }   // bumps on every change to the download rows
    bool dl_upsert_many(const std::vector<Row>& rows);          // one journal transaction and one save for a whole batch (queueing a library)
    // Visits every row under the lock without copying the (large) song JSON; fn must not call back into the store.
    // First row (in id order) for which pred is true, copied into out; false when none. Cheap next-job pick for a big queue.
    template <class P> bool dl_first(P&& pred, Row& out) const { std::lock_guard<std::mutex> lk(m_); for (const auto& x : downloads_) if (pred(x.second)) { out = x.second; return true; } return false; }
    template <class F> void dl_each(F&& fn) const { std::lock_guard<std::mutex> lk(m_); for (const auto& x : downloads_) fn(x.second); }
    bool dl_set(const std::string& id, int state, int64_t bytes, int64_t total, const std::string& path, const std::string& content_hash = "");
    bool dl_get(const std::string& id, Row& out);
    std::vector<Row> dl_all();
    bool dl_erase(const std::string& id);
    bool dl_erase_many(const std::vector<std::string>& ids);   // one journal transaction and one save for the whole batch
    void index_album(const Album& album);
    void index_songs(const std::vector<Song>& songs);   // e.g. the starred list from the server
    std::vector<Song> local_search(const std::string& query, size_t limit = 50) const;
    // Query API over the indexed metadata (likes, playlists, per-song technical fields).
    bool song(const std::string& id, Song& out) const;
    void set_liked(const std::string& id, bool liked);
    bool liked(const std::string& id) const;
    std::vector<Song> liked_songs() const;
    void set_playlist(const std::string& id, const std::vector<Song>& songs);
    bool playlist_contains(const std::string& playlist_id, const std::string& song_id) const;
    uint64_t metadata_generation() const;
    struct Stats { size_t songs = 0, liked = 0, downloads = 0, kv_entries = 0, artists = 0, albums = 0; uint64_t generation = 0, index_bytes = 0; };
    std::unordered_map<std::string, std::string> cover_alias() const;   // song cover id -> album cover id, from the cached albums
    void clear_library();            // cached artists/albums and the search index (downloads and flags stay)
    void compact_metadata();         // fold the journal into the snapshot and persist the search index
    Stats stats() const;

private:
    bool load_locked();
    bool save_locked() const;
    bool journal_put_locked(const std::string& key, const std::string& value);
    bool journal_erase_locked(const std::string& key);
    bool journal_download_locked(char op, const Row& row);
    bool journal_download_erase_locked(const std::string& id);
    void apply_journal_locked(const std::string& payload);
    void apply_download_journal_locked(const std::string& payload);
    std::string path_;
    bool memory_ = false;
    mutable std::mutex m_;
    std::atomic<uint64_t> dl_ver_{0};
    uint32_t last_save_ms_ = 0;
    std::unordered_map<std::string, std::pair<std::string, int64_t>> kv_;
    std::unordered_map<std::string, Row> downloads_;
    std::unique_ptr<xf::cache::Journal> journal_;
    uint64_t transaction_ = 0;
    uint64_t generation_ = 0;
    std::unique_ptr<xf::cache::MetadataStore> meta_;
    void index_songs_locked(const std::vector<Song>& songs) const;
    mutable bool index_stale_ = false;   // set when an album refresh dropped songs; rebuilt on the next search
    int unsaved_ops_ = 0;                // journal-covered mutations since the last full JSON write
    void rebuild_index_locked() const;
    void ensure_index_locked() const { if (index_stale_) rebuild_index_locked(); }
    bool maybe_save_locked();
};
