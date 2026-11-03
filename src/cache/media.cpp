#include "media.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <system_error>
#include "journal.h"

namespace xf::cache {
namespace fs = std::filesystem;
namespace {
const char* kind_dir(MediaKind k) { return k == MediaKind::Audio ? "audio" : "artwork"; }
std::string content_hash(const std::string& data) {
    uint64_t h = 1469598103934665603ull;
    for (unsigned char c : data) { h ^= c; h *= 1099511628211ull; }
    char buf[40]; std::snprintf(buf, sizeof buf, "%016llx%08x", static_cast<unsigned long long>(h), cache_crc32(data.data(), data.size()));
    return buf;
}
bool read_all(const std::string& path, std::string& out) {
    std::FILE* f = std::fopen(path.c_str(), "rb"); if (!f) return false;
    char buf[1 << 16]; size_t n; out.clear(); while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
    std::fclose(f); return true;
}
void put32(std::string& b, uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back(static_cast<char>(v >> (i * 8))); }
void put64(std::string& b, uint64_t v) { for (int i = 0; i < 8; ++i) b.push_back(static_cast<char>(v >> (i * 8))); }
void put_str(std::string& b, const std::string& s) { put32(b, static_cast<uint32_t>(s.size())); b += s; }
struct Reader {
    const std::string& b; size_t p = 0;
    bool u32(uint32_t& v) { if (b.size() - p < 4) return false; v = 0; for (int i = 0; i < 4; ++i) v |= static_cast<uint32_t>(static_cast<unsigned char>(b[p++])) << (i * 8); return true; }
    bool u64(uint64_t& v) { if (b.size() - p < 8) return false; v = 0; for (int i = 0; i < 8; ++i) v |= static_cast<uint64_t>(static_cast<unsigned char>(b[p++])) << (i * 8); return true; }
    bool str(std::string& s) { uint32_t n; if (!u32(n) || n > b.size() - p) return false; s.assign(b, p, n); p += n; return true; }
};
}

std::string MediaCache::path_for(MediaKind kind, const std::string& hash) const { return dir_ + "/" + kind_dir(kind) + "/" + hash; }

bool MediaCache::open() {
    std::error_code ec;
    fs::create_directories(dir_ + "/audio", ec); fs::create_directories(dir_ + "/artwork", ec);
    { std::lock_guard<std::mutex> lk(m_);
      blobs_.clear(); keys_.clear(); clock_ = 0;
      std::string payload;
      if (read_snapshot(manifest(), payload)) {
          Reader r{payload}; uint64_t clock = 0, nb = 0, nk = 0; bool ok = r.u64(clock) && r.u64(nb);
          std::unordered_map<std::string, Blob> blobs; std::unordered_map<std::string, Key> keys;
          for (uint64_t i = 0; ok && i < nb; ++i) { std::string h; Blob b; uint32_t kind = 0; uint64_t used = 0;
              ok = r.str(h) && r.u32(kind) && r.u64(b.size) && r.u64(used) && (kind == 1 || kind == 2); b.kind = static_cast<MediaKind>(kind); b.last_used = used; blobs[h] = b; }
          ok = ok && r.u64(nk);
          for (uint64_t i = 0; ok && i < nk; ++i) { std::string k; Key key; uint32_t pinned = 0; ok = r.str(k) && r.str(key.hash) && r.u32(pinned) && blobs.count(key.hash); key.pinned = pinned != 0; if (ok) { keys[k] = key; ++blobs[key.hash].refs; if (key.pinned) ++blobs[key.hash].pinned; } }
          if (ok) { blobs_ = std::move(blobs); keys_ = std::move(keys); clock_ = clock; }   // else: start empty, reconcile removes stray files
      } }
    reconcile();
    return true;
}

bool MediaCache::save() {
    std::lock_guard<std::mutex> lk(m_);
    std::string payload; put64(payload, clock_); put64(payload, blobs_.size());
    for (const auto& b : blobs_) { put_str(payload, b.first); put32(payload, static_cast<uint32_t>(b.second.kind)); put64(payload, b.second.size); put64(payload, b.second.last_used); }
    put64(payload, keys_.size());
    for (const auto& k : keys_) { put_str(payload, k.first); put_str(payload, k.second.hash); put32(payload, k.second.pinned ? 1 : 0); }
    return write_snapshot(manifest(), payload);
}

void MediaCache::unlink_key_locked(const std::string& key) {
    auto it = keys_.find(key); if (it == keys_.end()) return;
    auto blob = blobs_.find(it->second.hash);
    if (blob != blobs_.end()) { if (blob->second.refs) --blob->second.refs; if (it->second.pinned && blob->second.pinned) --blob->second.pinned; if (blob->second.refs == 0) drop_blob_locked(it->second.hash); }
    keys_.erase(it);
}
void MediaCache::drop_blob_locked(const std::string& hash) {
    auto blob = blobs_.find(hash); if (blob == blobs_.end()) return;
    std::error_code ec; fs::remove(path_for(blob->second.kind, hash), ec); blobs_.erase(blob);
}

bool MediaCache::put(const std::string& key, MediaKind kind, const std::string& data, bool pinned) {
    if (key.empty() || data.empty()) return false;
    const std::string hash = content_hash(data);
    std::lock_guard<std::mutex> lk(m_);
    auto existing = keys_.find(key);
    if (existing != keys_.end() && existing->second.hash == hash) { Blob& same = blobs_[hash]; same.last_used = ++clock_; if (pinned && !existing->second.pinned) { existing->second.pinned = true; ++same.pinned; } return true; }
    auto blob = blobs_.find(hash); const std::string path = path_for(kind, hash);
    bool present = false;
    if (blob != blobs_.end()) { std::string disk; present = blob->second.size == data.size() && read_all(path, disk) && disk == data; if (!present) drop_blob_locked(hash); }
    if (!present) {                                         // not stored yet, or the stored file was damaged: write verified bytes
        const std::string temp = path + ".tmp";
        std::FILE* f = std::fopen(temp.c_str(), "wb"); if (!f) return false;
        const bool ok = std::fwrite(data.data(), 1, data.size(), f) == data.size() && std::fflush(f) == 0;
        if (std::fclose(f) != 0 || !ok || std::rename(temp.c_str(), path.c_str()) != 0) { std::remove(temp.c_str()); return false; }
        Blob b; b.kind = kind; b.size = data.size(); blobs_[hash] = b;
    }
    bind_locked(key, kind, hash, data.size(), pinned);
    return true;
}

void MediaCache::bind_locked(const std::string& key, MediaKind kind, const std::string& hash, uint64_t size, bool pinned) {
    if (keys_.count(key)) unlink_key_locked(key);
    Blob& b = blobs_[hash]; b.kind = kind; b.size = size; ++b.refs; b.last_used = ++clock_; if (pinned) ++b.pinned;
    keys_[key] = {hash, pinned};
}

bool MediaCache::put_file(const std::string& key, MediaKind kind, const std::string& src, bool pinned, std::string* out_path, std::string* out_hash) {
    if (key.empty()) return false;
    std::FILE* f = std::fopen(src.c_str(), "rb"); if (!f) return false;
    uint64_t h = 1469598103934665603ull, size = 0; uint32_t crc = 0xffffffffu; char buf[1 << 16]; size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) {
        for (size_t i = 0; i < n; ++i) { h ^= static_cast<unsigned char>(buf[i]); h *= 1099511628211ull; }
        crc = cache_crc32_update(crc, buf, n); size += n;
    }
    std::fclose(f); if (size == 0) return false;
    char hx[40]; std::snprintf(hx, sizeof hx, "%016llx%08x", static_cast<unsigned long long>(h), ~crc);
    const std::string hash = hx, path = path_for(kind, hash);
    std::lock_guard<std::mutex> lk(m_);
    auto existing = keys_.find(key);
    auto blob = blobs_.find(hash);
    bool present = false;
    if (blob != blobs_.end() && blob->second.size == size) {      // verify the stored file really holds these bytes
        std::string a, b; present = read_all(path, a) && read_all(src, b) && a == b;
        if (!present) drop_blob_locked(hash);
    } else if (blob != blobs_.end()) drop_blob_locked(hash);
    if (existing != keys_.end() && existing->second.hash == hash && present) { if (pinned && !existing->second.pinned) { existing->second.pinned = true; ++blobs_[hash].pinned; } std::remove(src.c_str()); if (out_path) *out_path = path;
        if (out_hash) *out_hash = hash;
        return true; }
    if (present) std::remove(src.c_str());
    else if (std::rename(src.c_str(), path.c_str()) != 0) {      // different filesystem: copy
        std::string data; if (!read_all(src, data)) return false;
        std::FILE* o = std::fopen((path + ".tmp").c_str(), "wb"); if (!o) return false;
        const bool ok = std::fwrite(data.data(), 1, data.size(), o) == data.size(); if (std::fclose(o) != 0 || !ok || std::rename((path + ".tmp").c_str(), path.c_str()) != 0) { std::remove((path + ".tmp").c_str()); return false; }
        std::remove(src.c_str());
    }
    bind_locked(key, kind, hash, size, pinned);
    if (out_path) *out_path = path;
    if (out_hash) *out_hash = hash;
    return true;
}

bool MediaCache::get(const std::string& key, std::string& path) {
    std::lock_guard<std::mutex> lk(m_);
    auto it = keys_.find(key); if (it == keys_.end()) return false;
    auto blob = blobs_.find(it->second.hash); if (blob == blobs_.end()) { keys_.erase(it); return false; }
    const std::string p = path_for(blob->second.kind, it->second.hash);
    std::error_code ec; const auto size = fs::file_size(p, ec);
    if (ec || size != blob->second.size) {                  // damaged media: drop it and every key that pointed at it
        const std::string hash = it->second.hash;
        for (auto k = keys_.begin(); k != keys_.end();) { if (k->second.hash == hash) k = keys_.erase(k); else ++k; }
        drop_blob_locked(hash); return false;
    }
    blob->second.last_used = ++clock_; path = p; return true;
}

bool MediaCache::best(const std::string& base, const std::vector<std::string>& preference, std::string& path, std::string* quality) {
    for (const auto& q : preference) if (get(quality_key(base, q), path)) { if (quality) *quality = q; return true; }
    return false;
}

bool MediaCache::release(const std::string& key) { std::lock_guard<std::mutex> lk(m_); if (!keys_.count(key)) return false; unlink_key_locked(key); return true; }

size_t MediaCache::release_prefix(const std::string& prefix) {
    std::lock_guard<std::mutex> lk(m_);
    std::vector<std::string> hit; for (const auto& k : keys_) if (k.first.compare(0, prefix.size(), prefix) == 0) hit.push_back(k.first);
    for (const auto& k : hit) unlink_key_locked(k);
    return hit.size();
}

size_t MediaCache::prune_prefix(const std::string& prefix, size_t keep) {
    std::lock_guard<std::mutex> lk(m_);
    std::vector<std::pair<uint64_t, std::string>> hit;
    for (const auto& k : keys_) if (k.first.compare(0, prefix.size(), prefix) == 0) { auto b = blobs_.find(k.second.hash); hit.emplace_back(b == blobs_.end() ? 0 : b->second.last_used, k.first); }
    if (hit.size() <= keep) return 0;
    std::sort(hit.begin(), hit.end());
    const size_t drop = hit.size() - keep;
    for (size_t i = 0; i < drop; ++i) unlink_key_locked(hit[i].second);
    return drop;
}

bool MediaCache::pin(const std::string& key, bool pinned) {
    std::lock_guard<std::mutex> lk(m_);
    auto it = keys_.find(key); if (it == keys_.end()) return false;
    if (it->second.pinned == pinned) return true;
    Blob& b = blobs_[it->second.hash]; it->second.pinned = pinned; if (pinned) ++b.pinned; else if (b.pinned) --b.pinned; return true;
}

size_t MediaCache::evict_to(uint64_t budget) {
    std::lock_guard<std::mutex> lk(m_);
    uint64_t total = 0; for (const auto& b : blobs_) total += b.second.size;
    std::vector<std::pair<uint64_t, std::string>> order;
    for (const auto& b : blobs_) if (!b.second.pinned) order.emplace_back(b.second.last_used, b.first);
    std::sort(order.begin(), order.end());
    uint64_t freed = 0;
    for (const auto& item : order) {
        if (total <= budget) break;
        const uint64_t size = blobs_[item.second].size;
        for (auto k = keys_.begin(); k != keys_.end();) { if (k->second.hash == item.second) k = keys_.erase(k); else ++k; }
        drop_blob_locked(item.second); total -= size; freed += size;
    }
    return freed;
}

MediaCache::Report MediaCache::reconcile() {
    Report report; std::lock_guard<std::mutex> lk(m_); std::error_code ec;
    std::vector<std::string> missing;
    for (const auto& b : blobs_) { const auto size = fs::file_size(path_for(b.second.kind, b.first), ec); if (ec || size != b.second.size) missing.push_back(b.first); ec.clear(); }
    for (const auto& hash : missing) {
        for (auto k = keys_.begin(); k != keys_.end();) { if (k->second.hash == hash) { k = keys_.erase(k); ++report.dropped_keys; } else ++k; }
        drop_blob_locked(hash); ++report.missing_files;
    }
    for (MediaKind kind : {MediaKind::Audio, MediaKind::Artwork}) {
        const std::string dir = dir_ + "/" + kind_dir(kind); std::vector<fs::path> stray;
        for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
            const std::string name = it->path().filename().string(); auto b = blobs_.find(name);
            if (b == blobs_.end() || b->second.kind != kind) stray.push_back(it->path());
        }
        ec.clear(); for (const auto& p : stray) { fs::remove(p, ec); ++report.orphan_files; }
    }
    return report;
}

uint64_t MediaCache::total_bytes() const { std::lock_guard<std::mutex> lk(m_); uint64_t t = 0; for (const auto& b : blobs_) t += b.second.size; return t; }
size_t MediaCache::blob_count() const { std::lock_guard<std::mutex> lk(m_); return blobs_.size(); }
uint32_t MediaCache::refs(const std::string& key) const { std::lock_guard<std::mutex> lk(m_); auto it = keys_.find(key); if (it == keys_.end()) return 0; auto b = blobs_.find(it->second.hash); return b == blobs_.end() ? 0 : b->second.refs; }
} // namespace xf::cache
