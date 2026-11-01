#include "store.h"
#include "model_json.h"
#include <algorithm>
#include <cstdio>
#include <ctime>
#include <cstring>
#include "../../third_party/yyjson.h"

namespace {
std::string str(yyjson_val* o, const char* key) {
    yyjson_val* v = yyjson_obj_get(o, key);
    return v && yyjson_is_str(v) ? std::string(yyjson_get_str(v)) : std::string();
}
int64_t integer(yyjson_val* o, const char* key, int64_t fallback = 0) {
    yyjson_val* v = yyjson_obj_get(o, key);
    return v && yyjson_is_num(v) ? yyjson_get_sint(v) : fallback;
}
void put32(std::string& out, uint32_t v) { for (int i = 0; i < 4; ++i) out.push_back(static_cast<char>(v >> (i * 8))); }
void put64(std::string& out, uint64_t v) { for (int i = 0; i < 8; ++i) out.push_back(static_cast<char>(v >> (i * 8))); }
bool take_string(const std::string& in, size_t& p, std::string& out) {
    if (p + 4 > in.size()) return false;
    uint32_t n = 0; for (int i = 0; i < 4; ++i) n |= static_cast<uint32_t>(static_cast<unsigned char>(in[p++])) << (i * 8);
    if (n > in.size() - p) return false;
    out.assign(in, p, n); p += n; return true;
}
bool take64(const std::string& in, size_t& p, int64_t& out) {
    if (p + 8 > in.size()) return false;
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v |= static_cast<uint64_t>(static_cast<unsigned char>(in[p++])) << (i * 8);
    out = static_cast<int64_t>(v); return true;
}
}

Store::~Store() {
    std::lock_guard<std::mutex> lk(m_);
    save_locked();
}

bool Store::open(const std::string& path) {
    std::lock_guard<std::mutex> lk(m_);
    path_ = path; memory_ = path == ":memory:"; kv_.clear(); downloads_.clear();
    journal_.reset(); transaction_ = generation_ = 0;
    if (!memory_) journal_ = std::make_unique<xf::cache::Journal>(path_ + ".journal");
    bool ok = load_locked();
    if (ok && journal_) {
        std::vector<xf::cache::JournalRecord> records;
        if (!journal_->recover(records)) return false;
        for (const auto& record : records) {
            transaction_ = std::max(transaction_, record.transaction);
            generation_ = std::max(generation_, record.generation);
            apply_journal_locked(record.payload);
        }
        if (!records.empty()) ok = save_locked();
    }
    if (ok) rebuild_index_locked();
    return ok;
}

bool Store::journal_put_locked(const std::string& key, const std::string& value) {
    if (!journal_) return true;
    std::string payload(1, 'P');
    uint32_t n = static_cast<uint32_t>(key.size());
    for (int i = 0; i < 4; ++i) payload.push_back(static_cast<char>(n >> (i * 8)));
    payload += key; payload += value;
    return journal_->append_transaction(++transaction_, ++generation_, {payload});
}

bool Store::journal_erase_locked(const std::string& key) {
    if (!journal_) return true;
    return journal_->append_transaction(++transaction_, ++generation_, {std::string(1, 'E') + key});
}

void Store::apply_journal_locked(const std::string& payload) {
    if (payload.empty()) return;
    if (payload[0] == 'D' || payload[0] == 'X') { apply_download_journal_locked(payload); return; }
    if (payload[0] == 'E') { kv_.erase(payload.substr(1)); return; }
    if (payload[0] != 'P' || payload.size() < 5) return;
    uint32_t n = 0;
    for (int i = 0; i < 4; ++i) n |= static_cast<uint32_t>(static_cast<unsigned char>(payload[1 + i])) << (i * 8);
    if (n > payload.size() - 5) return;
    kv_[payload.substr(5, n)] = {payload.substr(5 + n), static_cast<int64_t>(std::time(nullptr))};
}

bool Store::journal_download_locked(char op, const Row& row) {
    if (!journal_) return true;
    std::string payload(1, op);
    put32(payload, static_cast<uint32_t>(row.id.size())); payload += row.id;
    put32(payload, static_cast<uint32_t>(row.album_id.size())); payload += row.album_id;
    put32(payload, static_cast<uint32_t>(row.path.size())); payload += row.path;
    put32(payload, static_cast<uint32_t>(row.song_json.size())); payload += row.song_json;
    put32(payload, static_cast<uint32_t>(row.content_hash.size())); payload += row.content_hash;
    put64(payload, static_cast<uint64_t>(row.state)); put64(payload, static_cast<uint64_t>(row.bytes)); put64(payload, static_cast<uint64_t>(row.total));
    return journal_->append_transaction(++transaction_, ++generation_, {payload});
}

bool Store::journal_download_erase_locked(const std::string& id) {
    if (!journal_) return true;
    std::string payload(1, 'X'); put32(payload, static_cast<uint32_t>(id.size())); payload += id;
    return journal_->append_transaction(++transaction_, ++generation_, {payload});
}

void Store::apply_download_journal_locked(const std::string& payload) {
    if (payload[0] == 'X') {
        size_t p = 1; std::string id; if (take_string(payload, p, id)) downloads_.erase(id); return;
    }
    size_t p = 1; Row row; int64_t state = 0;
    if (!take_string(payload, p, row.id) || !take_string(payload, p, row.album_id) || !take_string(payload, p, row.path) ||
        !take_string(payload, p, row.song_json) || !take_string(payload, p, row.content_hash) || !take64(payload, p, state) ||
        !take64(payload, p, row.bytes) || !take64(payload, p, row.total)) return;
    row.state = static_cast<int>(state); downloads_[row.id] = std::move(row);
}

void Store::index_song_locked(const Song& song) {
    if (song.id.empty()) return;
    xf::cache::SongInput in;
    in.id = song.id; in.title = song.title; in.artist_id = song.artist_id; in.artist_name = song.artist;
    in.album_id = song.album_id; in.album_name = song.album; in.album_artist_name = song.artist;
    in.format = song.suffix; in.codec = song.suffix; in.track = song.track; in.year = song.year;
    in.duration = song.duration; in.bitrate = song.bit_rate; in.size = song.size; in.liked = song.starred;
    metadata_.upsert(in);
    indexed_songs_[song.id] = song;
}

void Store::index_album(const Album& album) {
    std::lock_guard<std::mutex> lk(m_);
    for (const Song& song : album.songs) index_song_locked(song);
}

void Store::rebuild_index_locked() {
    metadata_ = xf::cache::MetadataSnapshot{};
    indexed_songs_.clear();
    for (const auto& item : kv_) {
        if (item.first.rfind("album:", 0) != 0) continue;
        Album album;
        if (from_json(item.second.first, album)) for (const Song& song : album.songs) index_song_locked(song);
    }
    for (const auto& item : downloads_) {
        Song song;
        if (from_json(item.second.song_json, song)) index_song_locked(song);
    }
}

std::vector<Song> Store::local_search(const std::string& query, size_t limit) const {
    std::lock_guard<std::mutex> lk(m_);
    std::vector<Song> out;
    for (const std::string& id : metadata_.search_songs(query, limit)) {
        auto it = indexed_songs_.find(id);
        if (it != indexed_songs_.end()) out.push_back(it->second);
    }
    return out;
}

bool Store::load_locked() {
    if (memory_) return true;
    yyjson_read_err err{};
    yyjson_doc* d = yyjson_read_file(path_.c_str(), 0, nullptr, &err);
    if (!d) return true;
    yyjson_val* root = yyjson_doc_get_root(d);
    yyjson_val* kv = yyjson_obj_get(root, "kv");
    if (kv && yyjson_is_obj(kv)) {
        yyjson_obj_iter it = yyjson_obj_iter_with(kv);
        yyjson_val* key;
        while ((key = yyjson_obj_iter_next(&it))) {
            yyjson_val* value = yyjson_obj_iter_get_val(key);
            if (yyjson_is_obj(value)) kv_[yyjson_get_str(key)] = {str(value, "value"), integer(value, "ts")};
        }
    }
    yyjson_val* dl = yyjson_obj_get(root, "downloads");
    size_t i, n; yyjson_val* o;
    yyjson_arr_foreach(dl, i, n, o) {
        Row r;
        r.id = str(o, "id"); r.album_id = str(o, "album_id"); r.path = str(o, "path"); r.song_json = str(o, "song");
        r.content_hash = str(o, "content_hash");
        r.state = static_cast<int>(integer(o, "state")); r.bytes = integer(o, "bytes"); r.total = integer(o, "total");
        if (!r.id.empty()) downloads_[r.id] = std::move(r);
    }
    yyjson_doc_free(d);
    return true;
}

bool Store::save_locked() const {
    if (memory_ || path_.empty()) return true;
    yyjson_mut_doc* d = yyjson_mut_doc_new(nullptr);
    yyjson_mut_val* root = yyjson_mut_obj(d); yyjson_mut_doc_set_root(d, root);
    yyjson_mut_val* kv = yyjson_mut_obj(d);
    for (const auto& item : kv_) {
        yyjson_mut_val* entry = yyjson_mut_obj(d);
        yyjson_mut_obj_add_strcpy(d, entry, "value", item.second.first.c_str());
        yyjson_mut_obj_add_sint(d, entry, "ts", item.second.second);
        yyjson_mut_obj_add_val(d, kv, item.first.c_str(), entry);
    }
    yyjson_mut_obj_add_val(d, root, "kv", kv);
    yyjson_mut_val* dl = yyjson_mut_arr(d);
    for (const auto& item : downloads_) {
        const Row& r = item.second;
        yyjson_mut_val* o = yyjson_mut_arr_add_obj(d, dl);
        yyjson_mut_obj_add_strcpy(d, o, "id", r.id.c_str());
        yyjson_mut_obj_add_strcpy(d, o, "album_id", r.album_id.c_str());
        yyjson_mut_obj_add_int(d, o, "state", r.state);
        yyjson_mut_obj_add_sint(d, o, "bytes", r.bytes);
        yyjson_mut_obj_add_sint(d, o, "total", r.total);
        yyjson_mut_obj_add_strcpy(d, o, "path", r.path.c_str());
        yyjson_mut_obj_add_strcpy(d, o, "song", r.song_json.c_str());
        yyjson_mut_obj_add_strcpy(d, o, "content_hash", r.content_hash.c_str());
    }
    yyjson_mut_obj_add_val(d, root, "downloads", dl);
    std::string tmp = path_ + ".tmp";
    bool ok = yyjson_mut_write_file(tmp.c_str(), d, YYJSON_WRITE_NOFLAG, nullptr, nullptr);
    yyjson_mut_doc_free(d);
    ok = ok && std::rename(tmp.c_str(), path_.c_str()) == 0;
    if (ok && journal_) journal_->compact();
    return ok;
}

bool Store::put(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lk(m_);
    if (!journal_put_locked(key, value)) return false;
    kv_[key] = {value, static_cast<int64_t>(std::time(nullptr))};
    if (key.rfind("album:", 0) == 0) rebuild_index_locked();
    return save_locked();
}
bool Store::get(const std::string& key, std::string& value, int64_t* ts) {
    std::lock_guard<std::mutex> lk(m_); auto it = kv_.find(key); if (it == kv_.end()) return false;
    value = it->second.first; if (ts) *ts = it->second.second; return true;
}
void Store::erase_prefix(const std::string& prefix) {
    std::lock_guard<std::mutex> lk(m_);
    std::vector<std::string> removed;
    for (const auto& item : kv_) if (item.first.compare(0, prefix.size(), prefix) == 0) removed.push_back(item.first);
    for (const auto& key : removed) if (!journal_erase_locked(key)) return;
    for (auto it = kv_.begin(); it != kv_.end();) { if (it->first.compare(0, prefix.size(), prefix) == 0) it = kv_.erase(it); else ++it; }
    if (prefix.empty() || prefix.rfind("album", 0) == 0) rebuild_index_locked();
    save_locked();
}
bool Store::dl_upsert(const Row& r) { std::lock_guard<std::mutex> lk(m_); if (!journal_download_locked('D', r)) return false; downloads_[r.id] = r; return save_locked(); }
bool Store::dl_set(const std::string& id, int state, int64_t bytes, int64_t total, const std::string& path, const std::string& content_hash) {
    std::lock_guard<std::mutex> lk(m_); auto it = downloads_.find(id); if (it == downloads_.end()) return false;
    Row next = it->second; next.state = state; next.bytes = bytes; next.total = total; next.path = path; if (!content_hash.empty()) next.content_hash = content_hash;
    if (!journal_download_locked('D', next)) return false;
    it->second = std::move(next); return save_locked();
}
bool Store::dl_get(const std::string& id, Row& out) {
    std::lock_guard<std::mutex> lk(m_); auto it = downloads_.find(id); if (it == downloads_.end()) return false; out = it->second; return true;
}
std::vector<Store::Row> Store::dl_all() {
    std::lock_guard<std::mutex> lk(m_); std::vector<Row> out; out.reserve(downloads_.size()); for (const auto& x : downloads_) out.push_back(x.second); return out;
}
bool Store::dl_erase(const std::string& id) {
    std::lock_guard<std::mutex> lk(m_);
    auto it = downloads_.find(id); if (it == downloads_.end()) return false;
    if (!journal_download_erase_locked(id)) return false;
    downloads_.erase(it);
    rebuild_index_locked();
    return save_locked();
}
