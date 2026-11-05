#include "downloads.h"
#include <chrono>
#include <cstdio>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <set>
#include "../net/http.h"
#include "model_json.h"
#include <mbedtls/sha256.h>

namespace {
uint32_t now_ms() {
    return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}
}  // namespace

Downloads::Downloads(Store& store, std::string dir, std::function<std::string(const std::string&)> url_fn, ConnectOptions opt, int threads)
    : store_(store), dir_(std::move(dir)), url_fn_(std::move(url_fn)), opt_(std::move(opt)) {
    if (!dir_.empty() && dir_.back() != '/') dir_ += '/';
    if (!dir_.empty()) { media_ = std::make_shared<xf::cache::MediaCache>(dir_ + "media"); media_->open(); }
    for (auto& r : store_.dl_all())  // an interrupted run leaves rows Active: requeue them
        if (r.state == Active) store_.dl_set(r.id, Queued, 0, r.total, r.path);
    reconcile();
    verify();
    for (int i = 0; i < threads; ++i) th_.emplace_back([this] { loop(); });
}

Downloads::~Downloads() {
    {
        std::lock_guard<std::mutex> lk(m_);
        stop_ = true;
        for (auto& kv : cancel_) kv.second->store(true);
    }
    cv_.notify_all();
    for (auto& t : th_) t.join();
    if (media_) media_->save();
}

std::string Downloads::file_for(const Song& s) const { return dir_ + s.id + "." + (s.suffix.empty() ? "mp3" : s.suffix); }

void Downloads::changed(Change kind) {
    if (kind != Change::Progress) ++struct_gen_;   // chunks arriving do not change the list, only the live byte counts
    if (kind == Change::User) ++force_gen_;
    uint32_t n = now_ms();
    uint32_t l = last_notify_.load();
    if (n - l < 100) return;  // coalesce progress notifications to 10 Hz
    last_notify_ = n;
    if (on_change_) on_change_();
}

void Downloads::enqueue(const std::vector<Song>& songs, int quality) {
    std::vector<Store::Row> rows;
    for (const Song& s : songs) {
        Store::Row ex;
        if (store_.dl_get(s.id, ex) && (ex.state == Done || ex.state == Queued || ex.state == Active)) continue;
        Store::Row r;
        r.id = s.id; r.album_id = s.album_id; r.state = Queued; r.total = static_cast<int64_t>(s.size);
        r.path = file_for(s); r.song_json = to_json(s); r.quality = quality;
        rows.push_back(std::move(r));
    }
    store_.dl_upsert_many(rows);
    cv_.notify_all();
    last_notify_ = 0;
    changed();
}

void Downloads::pause(const std::string& id) {
    std::lock_guard<std::mutex> lk(m_);
    auto it = cancel_.find(id);
    Store::Row r;
    if (it != cancel_.end()) it->second->store(true);  // fetch() sees the flag and stores Paused
    else if (store_.dl_get(id, r) && r.state == Queued) store_.dl_set(id, Paused, 0, r.total, r.path);
    last_notify_ = 0;
}

void Downloads::resume(const std::string& id) {
    Store::Row r;
    if (store_.dl_get(id, r) && (r.state == Paused || r.state == Failed)) store_.dl_set(id, Queued, 0, r.total, r.path);
    cv_.notify_all();
    last_notify_ = 0;
    changed();
}

void Downloads::retry_failed() {
    for (auto& r : store_.dl_all()) if (r.state == Failed) store_.dl_set(r.id, Queued, 0, r.total, r.path);
    cv_.notify_all();
    last_notify_ = 0;
    changed();
}

bool Downloads::remove_one(const std::string& id, std::vector<std::string>* defer) {
    bool media_dirty = false;
    {
        std::lock_guard<std::mutex> lk(m_);
        auto it = cancel_.find(id);
        if (it != cancel_.end()) it->second->store(true);
    }
    Store::Row r;
    if (store_.dl_get(id, r)) {
        const bool managed = media_ && r.path.compare(0, media_->root().size(), media_->root()) == 0;
        if (managed) { media_->release_prefix("song:" + id + "@"); media_dirty = true; }   // file goes when its last owner goes
        else {                                                                           // downloaded before the manifest existed, or still queued
            bool shared = false;
            store_.dl_each([&](const Store::Row& other) { if (!shared && other.id != id && other.path == r.path) shared = true; });
            if (!shared) ::unlink(r.path.c_str());
        }
        ::unlink((r.path + ".part").c_str());
        if (defer) defer->push_back(id); else store_.dl_erase(id);
    }
    return media_dirty;
}

void Downloads::remove(const std::string& id) {
    if (remove_one(id) && media_) media_->save();
    last_notify_ = 0;
    changed();
}

void Downloads::remove_many(const std::vector<std::string>& ids) {
    bool dirty = false;
    std::vector<std::string> erase;
    for (const std::string& id : ids) dirty |= remove_one(id, &erase);
    store_.dl_erase_many(erase);
    if (dirty && media_) media_->save();
    last_notify_ = 0;
    changed();
}

void Downloads::pause_all() {
    paused_all_ = true;
    std::lock_guard<std::mutex> lk(m_);
    for (auto& kv : cancel_) kv.second->store(true);   // fetch() stores Paused for these
    last_notify_ = 0;
}

void Downloads::resume_all() {
    paused_all_ = false;
    for (auto& r : store_.dl_all()) if (r.state == Paused) store_.dl_set(r.id, Queued, 0, r.total, r.path);
    cv_.notify_all();
    last_notify_ = 0;
    changed();
}

void Downloads::clear_queue() {
    std::vector<std::string> ids;
    store_.dl_each([&](const Store::Row& r) { if (r.state != Done) ids.push_back(r.id); });
    remove_many(ids);
}

int Downloads::remove_album(const std::string& album_id) {
    std::vector<std::string> ids;
    store_.dl_each([&](const Store::Row& r) { if (r.album_id == album_id && r.state == Done) ids.push_back(r.id); });
    remove_many(ids);
    return static_cast<int>(ids.size());
}

void Downloads::clear_all() {
    std::vector<std::string> ids;
    store_.dl_each([&](const Store::Row& r) { ids.push_back(r.id); });
    remove_many(ids);
}

int Downloads::verify() {
    int dropped = 0;
    for (auto& r : store_.dl_all()) {
        if (r.state != Done) continue;
        struct stat st{};
        if (::stat(r.path.c_str(), &st) != 0 || (r.total > 0 && st.st_size != r.total)) { store_.dl_erase(r.id); ++dropped; }
    }
    if (dropped) { last_notify_ = 0; changed(); }
    return dropped;
}

int Downloads::reconcile() {
    if (dir_.empty()) return 0;
    DIR* dir = ::opendir(dir_.c_str());
    if (!dir) return 0;
    std::set<std::string> owned;
    for (const auto& r : store_.dl_all()) {
        owned.insert(r.path);
        owned.insert(r.path + ".part");
    }
    int removed = 0;
    while (dirent* e = ::readdir(dir)) {
        if (e->d_name[0] == '.') continue;
        std::string path = dir_ + e->d_name;
        struct stat st{};
        if (::stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) continue;
        if (owned.find(path) == owned.end() && ::unlink(path.c_str()) == 0) ++removed;
    }
    ::closedir(dir);
    return removed;
}

std::string Downloads::path_if_done(const std::string& id) const {
    Store::Row r;
    if (!store_.dl_get(id, r) || r.state != Done) return "";
    return ::access(r.path.c_str(), R_OK) == 0 ? r.path : "";
}

int Downloads::pending() const {
    int n = 0;
    store_.dl_each([&](const Store::Row& r) { if (r.state == Queued || r.state == Active) ++n; });
    return n;
}

Downloads::Snapshot Downloads::snapshot() const {
    std::lock_guard<std::mutex> lk(m_);
    const uint64_t sg = store_.dl_version(), fg = force_gen_.load();
    const uint32_t now = now_ms();
    // unchanged, or only background state changes of a long list within the last 400 ms: serve the cached list
    if (snap_ && (snap_struct_ == sg || (snap_force_ == fg && now - snap_time_ < 400))) return snap_;
    auto v = std::make_shared<std::vector<Item>>();
    store_.dl_each([&](const Store::Row& r) {
        Item it;
        auto sc = songs_.find(r.id);
        if (sc == songs_.end()) { Song sg; from_json(r.song_json, sg); sc = songs_.emplace(r.id, std::move(sg)).first; }
        it.song = sc->second;
        it.state = static_cast<State>(r.state);
        auto lb = live_bytes_.find(r.id);
        it.bytes = lb != live_bytes_.end() ? lb->second : r.bytes;
        it.total = r.total; it.quality = r.quality;
        v->push_back(std::move(it));
    });
    if (songs_.size() > v->size() + 64) {   // forget songs of removed downloads
        std::unordered_map<std::string, Song> keep;
        for (const Item& it : *v) keep.emplace(it.song.id, it.song);
        songs_.swap(keep);
    }
    snap_ = v; snap_struct_ = sg; snap_force_ = fg; snap_time_ = now;
    return snap_;
}

int64_t Downloads::live_bytes(const std::string& id, int64_t fallback) const {
    std::lock_guard<std::mutex> lk(m_);
    auto lb = live_bytes_.find(id);
    return lb != live_bytes_.end() ? lb->second : fallback;
}

std::vector<Album> Downloads::done_albums() const {
    std::vector<Album> out;
    const auto snap = snapshot();   // songs are parsed once per id
    for (const auto& it : *snap) {
        if (it.state != Done) continue;
        const Song& s = it.song;
        Album* a = nullptr;
        for (auto& x : out) if (x.id == s.album_id) { a = &x; break; }
        if (!a) {
            out.emplace_back();
            a = &out.back();
            a->id = s.album_id; a->name = s.album; a->artist = s.artist; a->artist_id = s.artist_id; a->cover_art = s.cover_art; a->year = s.year;
        }
        a->songs.push_back(s);
        a->duration += s.duration;
        a->song_count = static_cast<int>(a->songs.size());
    }
    return out;
}

bool Downloads::fetch(const Store::Row& row) {
    std::atomic<bool> cancel{false};
    {
        std::lock_guard<std::mutex> lk(m_);
        cancel_[row.id] = &cancel;
        live_bytes_[row.id] = 0;
    }
    store_.dl_set(row.id, Active, 0, row.total, row.path);
    std::string part = row.path + ".part";
    FILE* f = std::fopen(part.c_str(), "wb");
    int64_t got = 0, total = row.total;
    bool ok = f != nullptr;
    HttpResult r;
    if (ok) {
        r = http_get(url_q_ ? url_q_(row.id, row.quality) : url_fn_(row.id), [&](const uint8_t* p, size_t n) {
            if (cancel.load()) return false;
            if (std::fwrite(p, 1, n, f) != n) return false;
            got += static_cast<int64_t>(n);
            { std::lock_guard<std::mutex> lk(m_); live_bytes_[row.id] = got; }
            changed(Change::Progress);
            return true;
        }, opt_, {}, [&](int, int64_t len) { if (len > 0) total = len; });
        ok = r.error.empty() && r.status == 200 && (total <= 0 || got == total) && got > 0;
        std::fclose(f);
    }
    bool canceled = cancel.load();
    {
        std::lock_guard<std::mutex> lk(m_);
        cancel_.erase(row.id);
        live_bytes_.erase(row.id);
    }
    Store::Row now;
    bool still = store_.dl_get(row.id, now);  // may have been removed meanwhile
    if (!still) { ::unlink(part.c_str()); changed(); return false; }
    if (ok && !canceled) {
        ::rename(part.c_str(), row.path.c_str());
        std::string hash, final_path = row.path;
        if (media_) {
            // Streams the file into the manifest (hashing happens outside any lock): stored once per content, refcounted per song, pinned against eviction.
            if (media_->put_file("song:" + row.id + "@" + (row.quality < 0 ? quality_ : row.quality == 0 ? std::string("original") : std::to_string(row.quality)), xf::cache::MediaKind::Audio, row.path, true, &final_path, &hash)) media_->save();
            else final_path = row.path;
        } else {
            unsigned char digest[32]{};
            mbedtls_sha256_context sha; mbedtls_sha256_init(&sha); mbedtls_sha256_starts(&sha, 0);
            FILE* in = std::fopen(row.path.c_str(), "rb");
            char buf[16384]; size_t n = 0;
            while (in && (n = std::fread(buf, 1, sizeof buf, in)) > 0) mbedtls_sha256_update(&sha, reinterpret_cast<const unsigned char*>(buf), n);
            if (in) std::fclose(in);
            mbedtls_sha256_finish(&sha, digest); mbedtls_sha256_free(&sha);
            char hex[65]; for (int i = 0; i < 32; ++i) std::snprintf(hex + i * 2, 3, "%02x", digest[i]); hex[64] = 0;
            hash = hex;
            // Serialize the hash lookup and publication so two workers finishing
            // identical content cannot both become canonical files.
            std::lock_guard<std::mutex> publish_lk(m_);
            for (const auto& other : store_.dl_all()) {
                if (other.id == row.id || other.state != Done || other.content_hash != hash || other.path == row.path) continue;
                if (::access(other.path.c_str(), R_OK) == 0) { final_path = other.path; break; }
            }
            if (final_path != row.path) ::unlink(row.path.c_str());
        }
        store_.dl_set(row.id, Done, got, got, final_path, hash);
    } else if (canceled && !stop_) {
        store_.dl_set(row.id, Paused, 0, total, row.path);
        ::unlink(part.c_str());
    } else if (canceled) {
        store_.dl_set(row.id, Queued, 0, total, row.path);  // shutting down: retry next launch
        ::unlink(part.c_str());
    } else {
        store_.dl_set(row.id, Failed, 0, total, row.path);
        ::unlink(part.c_str());
    }
    last_notify_ = 0;
    changed(Change::Background);
    return ok;
}

void Downloads::loop() {
    for (;;) {
        Store::Row next;
        bool found = false;
        {
            std::unique_lock<std::mutex> lk(m_);
            for (;;) {
                if (stop_) return;
                if (!paused_all_ && store_.dl_first([&](const Store::Row& r) { return r.state == Queued && cancel_.find(r.id) == cancel_.end(); }, next)) {
                    // claim atomically under m_: mark Active before releasing the lock
                    store_.dl_set(next.id, Active, 0, next.total, next.path);
                    found = true;
                }
                if (found) break;
                cv_.wait_for(lk, std::chrono::milliseconds(500));
            }
        }
        fetch(next);
    }
}
