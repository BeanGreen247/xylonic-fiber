// Small JSON-backed persistence layer. The original Electron app uses
// localStorage/IndexedDB rather than a relational database; this keeps native state in one
// atomic JSON manifest while retaining the existing C++ API.
#pragma once
#include <cstdint>
#include <mutex>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "../cache/metadata.h"
#include "../cache/journal.h"
#include "../subsonic/model.h"

class Store {
public:
    ~Store();
    bool open(const std::string& path);  // ":memory:" keeps state in memory for tests
    bool put(const std::string& key, const std::string& value);
    bool get(const std::string& key, std::string& value, int64_t* ts = nullptr);
    void erase_prefix(const std::string& prefix);
    struct Row { std::string id, album_id, path, song_json, content_hash; int state = 0; int64_t bytes = 0, total = 0; };
    bool dl_upsert(const Row& r);
    bool dl_set(const std::string& id, int state, int64_t bytes, int64_t total, const std::string& path, const std::string& content_hash = "");
    bool dl_get(const std::string& id, Row& out);
    std::vector<Row> dl_all();
    bool dl_erase(const std::string& id);
    void index_album(const Album& album);
    std::vector<Song> local_search(const std::string& query, size_t limit = 50) const;

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
    std::unordered_map<std::string, std::pair<std::string, int64_t>> kv_;
    std::unordered_map<std::string, Row> downloads_;
    std::unique_ptr<xf::cache::Journal> journal_;
    uint64_t transaction_ = 0;
    uint64_t generation_ = 0;
    xf::cache::MetadataSnapshot metadata_;
    std::unordered_map<std::string, Song> indexed_songs_;
    void rebuild_index_locked();
    void index_song_locked(const Song& song);
};
