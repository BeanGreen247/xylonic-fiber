// Small JSON-backed persistence layer. The original Electron app uses
// localStorage/IndexedDB rather than a relational database; this keeps native state in one
// atomic JSON manifest while retaining the existing C++ API.
#pragma once
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

class Store {
public:
    ~Store();
    bool open(const std::string& path);  // ":memory:" keeps state in memory for tests
    bool put(const std::string& key, const std::string& value);
    bool get(const std::string& key, std::string& value, int64_t* ts = nullptr);
    void erase_prefix(const std::string& prefix);
    struct Row { std::string id, album_id, path, song_json; int state = 0; int64_t bytes = 0, total = 0; };
    bool dl_upsert(const Row& r);
    bool dl_set(const std::string& id, int state, int64_t bytes, int64_t total, const std::string& path);
    bool dl_get(const std::string& id, Row& out);
    std::vector<Row> dl_all();
    bool dl_erase(const std::string& id);

private:
    bool load_locked();
    bool save_locked() const;
    std::string path_;
    bool memory_ = false;
    mutable std::mutex m_;
    std::unordered_map<std::string, std::pair<std::string, int64_t>> kv_;
    std::unordered_map<std::string, Row> downloads_;
};
