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
    if (meta_ && meta_->needs_compact()) meta_->compact();   // fold the journal into a snapshot + persisted search index
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
    if (ok) {
        meta_ = std::make_unique<xf::cache::MetadataStore>(memory_ ? std::string() : path_ + ".meta");
        meta_->durable = false;      // group commit: sync with the JSON batches, not per album
        meta_->open();
        // First run on a store that predates the metadata files: build the index from the cached albums on first use.
        index_stale_ = meta_->current()->song_count() == 0 && (!downloads_.empty() || std::any_of(kv_.begin(), kv_.end(), [](const auto& x) { return x.first.rfind("album:", 0) == 0; }));
    }
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

static std::string download_payload(char op, const Store::Row& row) {
    std::string payload(1, op);
    put32(payload, static_cast<uint32_t>(row.id.size())); payload += row.id;
    put32(payload, static_cast<uint32_t>(row.album_id.size())); payload += row.album_id;
    put32(payload, static_cast<uint32_t>(row.path.size())); payload += row.path;
    put32(payload, static_cast<uint32_t>(row.song_json.size())); payload += row.song_json;
    put32(payload, static_cast<uint32_t>(row.content_hash.size())); payload += row.content_hash;
    put64(payload, static_cast<uint64_t>(row.state)); put64(payload, static_cast<uint64_t>(row.bytes)); put64(payload, static_cast<uint64_t>(row.total)); put64(payload, static_cast<uint64_t>(static_cast<int64_t>(row.quality)));
    return payload;
}

bool Store::journal_download_locked(char op, const Row& row) {
    if (!journal_) return true;
    return journal_->append_transaction(++transaction_, ++generation_, {download_payload(op, row)});
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
    { int64_t q = -1; if (take64(payload, p, q)) row.quality = static_cast<int>(q); }   // absent in journals written before per-download quality
    row.state = static_cast<int>(state); downloads_[row.id] = std::move(row);
}

namespace {
Song song_from_view(const xf::cache::SongView& v) {
    Song s; const auto& p = *v.packed;
    s.id = v.id(); s.title = v.title(); s.album = v.album_name(); s.album_id = v.album_id(); s.artist = v.artist_name(); s.artist_id = v.artist_id();
    s.cover_art = v.cover_art(); s.suffix = v.format(); s.track = static_cast<int>(p.track); s.year = static_cast<int>(p.year); s.duration = static_cast<int>(p.duration);
    s.bit_rate = static_cast<int>(p.bitrate); s.sampling_rate = static_cast<int>(p.sample_rate); s.bit_depth = p.bit_depth; s.channels = p.channels; s.size = p.size; s.starred = p.liked; return s;
}
bool same_song(const xf::cache::SongView& v, const xf::cache::SongInput& in) {
    return v.valid() && v.title() == in.title && v.artist_name() == in.artist_name && v.album_name() == in.album_name && v.album_id() == in.album_id && v.artist_id() == in.artist_id &&
           v.cover_art() == in.cover_art && v.format() == in.format && v.packed->track == in.track && v.packed->year == in.year && v.packed->duration == in.duration &&
           v.packed->bitrate == in.bitrate && v.packed->size == in.size && v.packed->liked == in.liked;
}
}  // namespace

// Journals and publishes only the songs that differ from the live generation.
void Store::index_songs_locked(const std::vector<Song>& songs) const {
    if (!meta_) return;
    auto live = meta_->current();
    xf::cache::UpdateBatch batch;
    for (const Song& song : songs) {
        if (song.id.empty()) continue;
        xf::cache::SongInput in = xf::cache::to_input(song);
        if (!same_song(live->song(song.id), in)) batch.songs.push_back(std::move(in));
    }
    live.reset();
    if (!batch.empty()) meta_->apply(batch);
}

void Store::index_album(const Album& album) {
    std::lock_guard<std::mutex> lk(m_);
    if (!index_stale_) index_songs_locked(album.songs);
}

void Store::index_songs(const std::vector<Song>& songs) {
    std::lock_guard<std::mutex> lk(m_);
    if (!index_stale_) index_songs_locked(songs);
}

void Store::rebuild_index_locked() const {
    if (!meta_) return;
    xf::cache::UpdateBatch batch; batch.reset = true;
    for (const auto& item : kv_) {
        if (item.first.rfind("album:", 0) != 0) continue;
        Album album;
        if (from_json(item.second.first, album)) xf::cache::add_album(batch, album);
    }
    for (const auto& item : downloads_) {
        Song song;
        if (from_json(item.second.song_json, song) && !song.id.empty()) batch.songs.push_back(xf::cache::to_input(song));
    }
    meta_->apply(batch); index_stale_ = false;
}

std::vector<Song> Store::local_search(const std::string& query, size_t limit) const {
    std::lock_guard<std::mutex> lk(m_);
    ensure_index_locked();
    std::vector<Song> out;
    if (!meta_) return out;
    auto g = meta_->current();
    for (const std::string& id : g->search_songs(query, limit)) { xf::cache::SongView v = g->song(id); if (v.valid()) out.push_back(song_from_view(v)); }
    return out;
}

bool Store::song(const std::string& id, Song& out) const {
    std::lock_guard<std::mutex> lk(m_); ensure_index_locked();
    if (!meta_) return false;
    auto g = meta_->current(); xf::cache::SongView v = g->song(id); if (!v.valid()) return false; out = song_from_view(v); return true;
}
void Store::set_liked(const std::string& id, bool liked) {
    std::lock_guard<std::mutex> lk(m_); if (!meta_ || id.empty()) return;
    xf::cache::UpdateBatch b; b.likes.emplace_back(id, liked); meta_->apply(b);
}
bool Store::liked(const std::string& id) const { std::lock_guard<std::mutex> lk(m_); return meta_ && meta_->current()->liked(id); }
std::vector<Song> Store::liked_songs() const {
    std::lock_guard<std::mutex> lk(m_); ensure_index_locked(); std::vector<Song> out; if (!meta_) return out;
    auto g = meta_->current(); for (const std::string& id : g->liked_songs()) { xf::cache::SongView v = g->song(id); if (v.valid()) out.push_back(song_from_view(v)); } return out;
}
void Store::set_playlist(const std::string& id, const std::vector<Song>& songs) {
    std::lock_guard<std::mutex> lk(m_); if (!meta_ || id.empty()) return;
    xf::cache::UpdateBatch b; xf::cache::add_playlist(b, id, songs);
    // Playlist copies of songs are not authoritative: add songs the index has never seen, never overwrite known ones (likes!).
    auto live = meta_->current(); std::vector<xf::cache::SongInput> changed;
    for (auto& in : b.songs) if (!live->song(in.id).valid()) changed.push_back(std::move(in));
    b.songs = std::move(changed); live.reset(); meta_->apply(b);
}
bool Store::playlist_contains(const std::string& playlist_id, const std::string& song_id) const { std::lock_guard<std::mutex> lk(m_); return meta_ && meta_->current()->playlist_contains(playlist_id, song_id); }
std::unordered_map<std::string, std::string> Store::cover_alias() const {
    std::vector<std::string> jsons;
    { std::lock_guard<std::mutex> lk(m_); for (const auto& item : kv_) if (item.first.compare(0, 6, "album:") == 0) jsons.push_back(item.second.first); }
    std::unordered_map<std::string, std::string> alias;
    for (const std::string& j : jsons) { Album a; if (!from_json(j, a) || a.cover_art.empty()) continue; for (const Song& s : a.songs) if (!s.cover_art.empty() && s.cover_art != a.cover_art) alias[s.cover_art] = a.cover_art; }
    return alias;
}

void Store::clear_library() {
    std::lock_guard<std::mutex> lk(m_);
    std::vector<std::string> gone;
    for (const auto& item : kv_) if (item.first.compare(0, 6, "album:") == 0 || item.first.compare(0, 7, "artist:") == 0 || item.first == "artists") gone.push_back(item.first);
    for (const auto& key : gone) { if (!journal_erase_locked(key)) break; kv_.erase(key); }
    if (meta_) { xf::cache::UpdateBatch reset; reset.reset = true; meta_->apply(reset); }
    index_stale_ = false;
    save_locked();
}

void Store::compact_metadata() { std::lock_guard<std::mutex> lk(m_); if (meta_) meta_->compact(); }

Store::Stats Store::stats() const {
    std::lock_guard<std::mutex> lk(m_); ensure_index_locked(); Stats st; st.kv_entries = kv_.size(); st.downloads = downloads_.size();
    for (const auto& item : kv_) { if (item.first.compare(0, 6, "album:") == 0) ++st.albums; else if (item.first.compare(0, 7, "artist:") == 0) ++st.artists; }
    if (meta_) st.index_bytes = meta_->disk_bytes();
    if (meta_) { auto g = meta_->current(); st.songs = g->song_count(); st.liked = g->liked_songs().size(); st.generation = meta_->generation(); }
    return st;
}
uint64_t Store::metadata_generation() const { std::lock_guard<std::mutex> lk(m_); return meta_ ? meta_->generation() : 0; }

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
        { yyjson_val* q = yyjson_obj_get(o, "quality"); r.quality = q ? static_cast<int>(yyjson_get_sint(q)) : -1; }
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
        yyjson_mut_obj_add_int(d, o, "quality", r.quality);
    }
    yyjson_mut_obj_add_val(d, root, "downloads", dl);
    std::string tmp = path_ + ".tmp";
    bool ok = yyjson_mut_write_file(tmp.c_str(), d, YYJSON_WRITE_NOFLAG, nullptr, nullptr);
    yyjson_mut_doc_free(d);
    ok = ok && std::rename(tmp.c_str(), path_.c_str()) == 0;
    if (ok && journal_) journal_->compact();
    return ok;
}

// The journal already makes each mutation durable; the full JSON rewrite only compacts it, so batch it.
bool Store::maybe_save_locked() {
    if (++unsaved_ops_ < 64) return true;
    unsaved_ops_ = 0;
    if (meta_) meta_->sync();
    return save_locked();
}

bool Store::put(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lk(m_);
    if (!journal_put_locked(key, value)) return false;
    if (key.rfind("album:", 0) == 0 && !index_stale_) {
        // Index only this album. If the refresh dropped songs, defer the full rebuild to the next query.
        Album next; auto old = kv_.find(key);
        if (from_json(value, next)) {
            if (old != kv_.end()) {
                Album prev;
                if (from_json(old->second.first, prev))
                    for (const Song& s : prev.songs) { bool kept = false; for (const Song& n : next.songs) if (n.id == s.id) { kept = true; break; } if (!kept) { index_stale_ = true; break; } }
            }
            if (!index_stale_) index_songs_locked(next.songs);
        }
    }
    kv_[key] = {value, static_cast<int64_t>(std::time(nullptr))};
    return maybe_save_locked();
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
    if (prefix.empty() || prefix.rfind("album", 0) == 0) index_stale_ = true;
    save_locked();
}
bool Store::dl_upsert_many(const std::vector<Row>& rows) {
    if (rows.empty()) return true;
    std::lock_guard<std::mutex> lk(m_);
    if (journal_) {
        std::vector<std::string> payloads;
        payloads.reserve(rows.size());
        for (const Row& r : rows) payloads.push_back(download_payload('D', r));
        if (!journal_->append_transaction(++transaction_, ++generation_, payloads)) return false;
    }
    for (const Row& r : rows) downloads_[r.id] = r;
    ++dl_ver_;
    unsaved_ops_ = 0;
    return save_locked();   // compacts the journal: one rewrite for the whole batch instead of one per 64 rows
}

bool Store::dl_upsert(const Row& r) { std::lock_guard<std::mutex> lk(m_); if (!journal_download_locked('D', r)) return false; downloads_[r.id] = r; ++dl_ver_; return maybe_save_locked(); }
bool Store::dl_set(const std::string& id, int state, int64_t bytes, int64_t total, const std::string& path, const std::string& content_hash) {
    std::lock_guard<std::mutex> lk(m_); auto it = downloads_.find(id); if (it == downloads_.end()) return false;
    Row next = it->second; next.state = state; next.bytes = bytes; next.total = total; next.path = path; if (!content_hash.empty()) next.content_hash = content_hash;
    if (!journal_download_locked('D', next)) return false;
    it->second = std::move(next); ++dl_ver_; return maybe_save_locked();
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
    ++dl_ver_;
    index_stale_ = true;
    return maybe_save_locked();   // the journal already made the erase durable; a full rewrite per row made clearing a big queue O(n^2) in file writes
}
