// SQLite-backed key/value metadata cache (stale-while-revalidate) and the downloads table.
#pragma once
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

struct sqlite3;

class Store {
public:
    ~Store();
    bool open(const std::string& path);  // ":memory:" works for tests
    // Metadata cache
    bool put(const std::string& key, const std::string& value);
    bool get(const std::string& key, std::string& value, int64_t* ts = nullptr);
    void erase_prefix(const std::string& prefix);

    // Downloads: state 0 queued, 1 active, 2 done, 3 failed, 4 paused
    struct Row { std::string id, album_id, path, song_json; int state = 0; int64_t bytes = 0, total = 0; };
    bool dl_upsert(const Row& r);
    bool dl_set(const std::string& id, int state, int64_t bytes, int64_t total, const std::string& path);
    bool dl_get(const std::string& id, Row& out);
    std::vector<Row> dl_all();
    bool dl_erase(const std::string& id);

private:
    bool exec(const char* sql);
    sqlite3* db_ = nullptr;
    std::mutex m_;
};
