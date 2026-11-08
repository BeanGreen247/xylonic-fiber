// Verified audio/artwork cache. Bytes live in <dir>/<audio|artwork>/<content-hash>; this manifest maps
// logical keys (song or cover plus quality) to content, deduplicates identical content by refcount,
// reconciles against the files on disk, and evicts unpinned content least-recently-used first.
// A damaged or missing media file only drops its own entries; it never touches the metadata store.
#pragma once
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_set>
#include <unordered_map>
#include <vector>

namespace xf::cache {
enum class MediaKind : uint8_t { Audio = 1, Artwork = 2 };

class MediaCache {
public:
    explicit MediaCache(std::string dir) : dir_(std::move(dir)) {}
    bool open(bool reconcile_now = true);   // reconcile_now false: the caller runs reconcile() later (a big blob table on a slow card)                                           // load manifest (a corrupt one starts empty), then reconcile
    bool save();
    // Stores bytes through a temp file and atomic rename. Identical content is stored once.
    bool put(const std::string& key, MediaKind kind, const std::string& data, bool pinned = false);
    // Adopts a finished file (e.g. a completed download) without loading it into memory: hashes it while
    // streaming, then renames it into the cache, or deletes it when identical content is already stored.
    // path/hash receive the canonical file and content hash.
    bool put_file(const std::string& key, MediaKind kind, const std::string& src, bool pinned, std::string* path = nullptr, std::string* hash = nullptr);
    bool get(const std::string& key, std::string& path);   // touches LRU; drops entries whose file is gone or resized
    // Quality picker: first preference that is cached, keys are base + "@" + quality.
    bool best(const std::string& base, const std::vector<std::string>& preference, std::string& path, std::string* quality = nullptr);
    bool release(const std::string& key);                  // refcount--, file removed at zero
    size_t release_prefix(const std::string& prefix);
    size_t release_songs(const std::unordered_set<std::string>& ids);   // release_prefix("song:<id>@") for every id in one pass over the keys
    size_t prune_prefix(const std::string& prefix, size_t keep);   // keep only the `keep` most recently used keys with this prefix     // every key starting with prefix (all qualities of one song)
    bool pin(const std::string& key, bool pinned);
    size_t clear_all();                                    // drops every blob and key (pinned too) with parallel unlinks; returns the blob count
    size_t evict_to(uint64_t budget_bytes);                // returns bytes freed; pinned content never evicted
    struct Report { size_t orphan_files = 0, missing_files = 0, dropped_keys = 0; };
    Report reconcile();
    uint64_t total_bytes() const;
    size_t blob_count() const;
    uint32_t refs(const std::string& key) const;
    const std::string& root() const { return dir_; }
    static std::string quality_key(const std::string& base, const std::string& quality) { return base + "@" + quality; }
private:
    struct Blob { MediaKind kind = MediaKind::Audio; uint64_t size = 0, last_used = 0; uint32_t refs = 0, pinned = 0; };
    struct Key { std::string hash; bool pinned = false; };
    std::string path_for(MediaKind kind, const std::string& hash) const;
    void bind_locked(const std::string& key, MediaKind kind, const std::string& hash, uint64_t size, bool pinned);
    void unlink_key_locked(const std::string& key);
    void drop_blob_locked(const std::string& hash);
    std::string manifest() const { return dir_ + "/cache.manifest"; }
    std::string dir_;
    mutable std::mutex m_;
    std::unordered_map<std::string, Blob> blobs_;
    std::unordered_map<std::string, Key> keys_;
    uint64_t clock_ = 0;
};
} // namespace xf::cache
