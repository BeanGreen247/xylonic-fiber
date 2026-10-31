#include "store.h"
#include <cstdio>
#include <ctime>
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
}

Store::~Store() {
    std::lock_guard<std::mutex> lk(m_);
    save_locked();
}

bool Store::open(const std::string& path) {
    std::lock_guard<std::mutex> lk(m_);
    path_ = path; memory_ = path == ":memory:"; kv_.clear(); downloads_.clear();
    return load_locked();
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
    }
    yyjson_mut_obj_add_val(d, root, "downloads", dl);
    std::string tmp = path_ + ".tmp";
    bool ok = yyjson_mut_write_file(tmp.c_str(), d, YYJSON_WRITE_NOFLAG, nullptr, nullptr);
    yyjson_mut_doc_free(d);
    return ok && std::rename(tmp.c_str(), path_.c_str()) == 0;
}

bool Store::put(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lk(m_); kv_[key] = {value, static_cast<int64_t>(std::time(nullptr))}; return save_locked();
}
bool Store::get(const std::string& key, std::string& value, int64_t* ts) {
    std::lock_guard<std::mutex> lk(m_); auto it = kv_.find(key); if (it == kv_.end()) return false;
    value = it->second.first; if (ts) *ts = it->second.second; return true;
}
void Store::erase_prefix(const std::string& prefix) {
    std::lock_guard<std::mutex> lk(m_);
    for (auto it = kv_.begin(); it != kv_.end();) { if (it->first.compare(0, prefix.size(), prefix) == 0) it = kv_.erase(it); else ++it; }
    save_locked();
}
bool Store::dl_upsert(const Row& r) { std::lock_guard<std::mutex> lk(m_); downloads_[r.id] = r; return save_locked(); }
bool Store::dl_set(const std::string& id, int state, int64_t bytes, int64_t total, const std::string& path) {
    std::lock_guard<std::mutex> lk(m_); auto it = downloads_.find(id); if (it == downloads_.end()) return false;
    it->second.state = state; it->second.bytes = bytes; it->second.total = total; it->second.path = path; return save_locked();
}
bool Store::dl_get(const std::string& id, Row& out) {
    std::lock_guard<std::mutex> lk(m_); auto it = downloads_.find(id); if (it == downloads_.end()) return false; out = it->second; return true;
}
std::vector<Store::Row> Store::dl_all() {
    std::lock_guard<std::mutex> lk(m_); std::vector<Row> out; out.reserve(downloads_.size()); for (const auto& x : downloads_) out.push_back(x.second); return out;
}
bool Store::dl_erase(const std::string& id) { std::lock_guard<std::mutex> lk(m_); if (!downloads_.erase(id)) return false; return save_locked(); }
