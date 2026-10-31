#include "store.h"
#include <ctime>
#include <sqlite3.h>

Store::~Store() { if (db_) sqlite3_close(db_); }

bool Store::exec(const char* sql) { return sqlite3_exec(db_, sql, nullptr, nullptr, nullptr) == SQLITE_OK; }

bool Store::open(const std::string& path) {
    if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK) return false;
    sqlite3_busy_timeout(db_, 2000);
    return exec("PRAGMA journal_mode=WAL;") && exec("PRAGMA synchronous=NORMAL;") &&
           exec("CREATE TABLE IF NOT EXISTS kv(k TEXT PRIMARY KEY, v BLOB NOT NULL, ts INTEGER NOT NULL) WITHOUT ROWID;") &&
           exec("CREATE TABLE IF NOT EXISTS dl(id TEXT PRIMARY KEY, album_id TEXT NOT NULL, state INTEGER NOT NULL, bytes INTEGER NOT NULL, total INTEGER NOT NULL, path TEXT NOT NULL, song TEXT NOT NULL);") &&
           exec("CREATE INDEX IF NOT EXISTS dl_album ON dl(album_id);");
}

bool Store::put(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lk(m_);
    sqlite3_stmt* st;
    if (sqlite3_prepare_v2(db_, "INSERT OR REPLACE INTO kv(k,v,ts) VALUES(?,?,?)", -1, &st, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(st, 1, key.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_blob(st, 2, value.data(), static_cast<int>(value.size()), SQLITE_STATIC);
    sqlite3_bind_int64(st, 3, static_cast<int64_t>(std::time(nullptr)));
    bool ok = sqlite3_step(st) == SQLITE_DONE;
    sqlite3_finalize(st);
    return ok;
}

bool Store::get(const std::string& key, std::string& value, int64_t* ts) {
    std::lock_guard<std::mutex> lk(m_);
    sqlite3_stmt* st;
    if (sqlite3_prepare_v2(db_, "SELECT v,ts FROM kv WHERE k=?", -1, &st, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(st, 1, key.c_str(), -1, SQLITE_STATIC);
    bool ok = sqlite3_step(st) == SQLITE_ROW;
    if (ok) {
        value.assign(static_cast<const char*>(sqlite3_column_blob(st, 0)), static_cast<size_t>(sqlite3_column_bytes(st, 0)));
        if (ts) *ts = sqlite3_column_int64(st, 1);
    }
    sqlite3_finalize(st);
    return ok;
}

void Store::erase_prefix(const std::string& prefix) {
    std::lock_guard<std::mutex> lk(m_);
    sqlite3_stmt* st;
    if (sqlite3_prepare_v2(db_, "DELETE FROM kv WHERE substr(k,1,length(?1))=?1", -1, &st, nullptr) != SQLITE_OK) return;
    sqlite3_bind_text(st, 1, prefix.c_str(), -1, SQLITE_STATIC);
    sqlite3_step(st);
    sqlite3_finalize(st);
}

bool Store::dl_upsert(const Row& r) {
    std::lock_guard<std::mutex> lk(m_);
    sqlite3_stmt* st;
    if (sqlite3_prepare_v2(db_, "INSERT OR REPLACE INTO dl(id,album_id,state,bytes,total,path,song) VALUES(?,?,?,?,?,?,?)", -1, &st, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(st, 1, r.id.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 2, r.album_id.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(st, 3, r.state);
    sqlite3_bind_int64(st, 4, r.bytes);
    sqlite3_bind_int64(st, 5, r.total);
    sqlite3_bind_text(st, 6, r.path.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 7, r.song_json.c_str(), -1, SQLITE_STATIC);
    bool ok = sqlite3_step(st) == SQLITE_DONE;
    sqlite3_finalize(st);
    return ok;
}

bool Store::dl_set(const std::string& id, int state, int64_t bytes, int64_t total, const std::string& path) {
    std::lock_guard<std::mutex> lk(m_);
    sqlite3_stmt* st;
    if (sqlite3_prepare_v2(db_, "UPDATE dl SET state=?,bytes=?,total=?,path=? WHERE id=?", -1, &st, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int(st, 1, state);
    sqlite3_bind_int64(st, 2, bytes);
    sqlite3_bind_int64(st, 3, total);
    sqlite3_bind_text(st, 4, path.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 5, id.c_str(), -1, SQLITE_STATIC);
    bool ok = sqlite3_step(st) == SQLITE_DONE;
    sqlite3_finalize(st);
    return ok;
}

static Store::Row read_row(sqlite3_stmt* st) {
    Store::Row r;
    auto t = [&](int c) { const unsigned char* p = sqlite3_column_text(st, c); return std::string(p ? reinterpret_cast<const char*>(p) : ""); };
    r.id = t(0); r.album_id = t(1); r.state = sqlite3_column_int(st, 2); r.bytes = sqlite3_column_int64(st, 3);
    r.total = sqlite3_column_int64(st, 4); r.path = t(5); r.song_json = t(6);
    return r;
}

bool Store::dl_get(const std::string& id, Row& out) {
    std::lock_guard<std::mutex> lk(m_);
    sqlite3_stmt* st;
    if (sqlite3_prepare_v2(db_, "SELECT id,album_id,state,bytes,total,path,song FROM dl WHERE id=?", -1, &st, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(st, 1, id.c_str(), -1, SQLITE_STATIC);
    bool ok = sqlite3_step(st) == SQLITE_ROW;
    if (ok) out = read_row(st);
    sqlite3_finalize(st);
    return ok;
}

std::vector<Store::Row> Store::dl_all() {
    std::lock_guard<std::mutex> lk(m_);
    std::vector<Row> v;
    sqlite3_stmt* st;
    if (sqlite3_prepare_v2(db_, "SELECT id,album_id,state,bytes,total,path,song FROM dl ORDER BY rowid", -1, &st, nullptr) != SQLITE_OK) return v;
    while (sqlite3_step(st) == SQLITE_ROW) v.push_back(read_row(st));
    sqlite3_finalize(st);
    return v;
}

bool Store::dl_erase(const std::string& id) {
    std::lock_guard<std::mutex> lk(m_);
    sqlite3_stmt* st;
    if (sqlite3_prepare_v2(db_, "DELETE FROM dl WHERE id=?", -1, &st, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(st, 1, id.c_str(), -1, SQLITE_STATIC);
    bool ok = sqlite3_step(st) == SQLITE_DONE;
    sqlite3_finalize(st);
    return ok;
}
