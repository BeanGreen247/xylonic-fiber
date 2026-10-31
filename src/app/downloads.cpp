#include "downloads.h"
#include <chrono>
#include <cstdio>
#include <sys/stat.h>
#include <unistd.h>
#include "../net/http.h"
#include "model_json.h"

namespace {
uint32_t now_ms() {
    return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}
}  // namespace

Downloads::Downloads(Store& store, std::string dir, std::function<std::string(const std::string&)> url_fn, ConnectOptions opt, int threads)
    : store_(store), dir_(std::move(dir)), url_fn_(std::move(url_fn)), opt_(std::move(opt)) {
    if (!dir_.empty() && dir_.back() != '/') dir_ += '/';
    for (auto& r : store_.dl_all())  // an interrupted run leaves rows Active: requeue them
        if (r.state == Active) store_.dl_set(r.id, Queued, 0, r.total, r.path);
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
}

std::string Downloads::file_for(const Song& s) const { return dir_ + s.id + "." + (s.suffix.empty() ? "mp3" : s.suffix); }

void Downloads::changed() {
    uint32_t n = now_ms();
    uint32_t l = last_notify_.load();
    if (n - l < 100) return;  // coalesce progress notifications to 10 Hz
    last_notify_ = n;
    if (on_change_) on_change_();
}

void Downloads::enqueue(const std::vector<Song>& songs) {
    for (const Song& s : songs) {
        Store::Row ex;
        if (store_.dl_get(s.id, ex) && (ex.state == Done || ex.state == Queued || ex.state == Active)) continue;
        Store::Row r;
        r.id = s.id; r.album_id = s.album_id; r.state = Queued; r.total = static_cast<int64_t>(s.size);
        r.path = file_for(s); r.song_json = to_json(s);
        store_.dl_upsert(r);
    }
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

void Downloads::remove(const std::string& id) {
    {
        std::lock_guard<std::mutex> lk(m_);
        auto it = cancel_.find(id);
        if (it != cancel_.end()) it->second->store(true);
    }
    Store::Row r;
    if (store_.dl_get(id, r)) {
        ::unlink(r.path.c_str());
        ::unlink((r.path + ".part").c_str());
        store_.dl_erase(id);
    }
    last_notify_ = 0;
    changed();
}

void Downloads::clear_all() {
    for (auto& r : store_.dl_all()) remove(r.id);
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

std::string Downloads::path_if_done(const std::string& id) const {
    Store::Row r;
    if (!store_.dl_get(id, r) || r.state != Done) return "";
    return ::access(r.path.c_str(), R_OK) == 0 ? r.path : "";
}

int Downloads::pending() const {
    int n = 0;
    for (auto& r : store_.dl_all()) if (r.state == Queued || r.state == Active) ++n;
    return n;
}

std::vector<Downloads::Item> Downloads::snapshot() const {
    std::vector<Item> v;
    std::lock_guard<std::mutex> lk(m_);
    for (auto& r : store_.dl_all()) {
        Item it;
        from_json(r.song_json, it.song);
        it.state = static_cast<State>(r.state);
        auto lb = live_bytes_.find(r.id);
        it.bytes = lb != live_bytes_.end() ? lb->second : r.bytes;
        it.total = r.total;
        v.push_back(std::move(it));
    }
    return v;
}

std::vector<Album> Downloads::done_albums() const {
    std::vector<Album> out;
    for (auto& r : store_.dl_all()) {
        if (r.state != Done) continue;
        Song s;
        if (!from_json(r.song_json, s)) continue;
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
        r = http_get(url_fn_(row.id), [&](const uint8_t* p, size_t n) {
            if (cancel.load()) return false;
            if (std::fwrite(p, 1, n, f) != n) return false;
            got += static_cast<int64_t>(n);
            { std::lock_guard<std::mutex> lk(m_); live_bytes_[row.id] = got; }
            changed();
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
        store_.dl_set(row.id, Done, got, got, row.path);
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
    changed();
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
                for (auto& r : store_.dl_all()) {
                    if (r.state == Queued && cancel_.find(r.id) == cancel_.end()) {
                        // claim atomically under m_: mark Active before releasing the lock
                        store_.dl_set(r.id, Active, 0, r.total, r.path);
                        next = r;
                        found = true;
                        break;
                    }
                }
                if (found) break;
                cv_.wait_for(lk, std::chrono::milliseconds(500));
            }
        }
        fetch(next);
    }
}
