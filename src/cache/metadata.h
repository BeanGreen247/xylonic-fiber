// Compact, platform-neutral metadata/index foundation.
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace xf::cache {
using Id = uint32_t;
constexpr Id kInvalidId = 0;

struct CacheProfile {
    const char* name = "desktop";
    size_t hot_song_limit = 512;
    size_t cold_page_limit = 32;
    size_t artwork_bytes = 32u * 1024u * 1024u;
    size_t worker_count = 3;
    bool prefetch = true;
};
CacheProfile desktop_profile();
CacheProfile mobile_profile();
CacheProfile low_memory_profile();
CacheProfile minimal_profile();

class StringTable {
public:
    Id intern(const std::string& value);
    Id find(const std::string& value) const;
    const std::string& get(Id id) const;
    size_t size() const { return values_.size(); }
    size_t bytes() const;
private:
    // Open-addressing table of ids into values_: no second copy of every string, no node allocation per entry.
    static uint32_t hash_of(const std::string& v);
    void grow();
    std::vector<std::string> values_{std::string()};
    std::vector<uint32_t> hashes_{0};   // by id
    std::vector<Id> slots_ = std::vector<Id>(64, kInvalidId);
};

struct SongInput {
    std::string id, title, artist_id, artist_name, album_id, album_name, album_artist_name;
    std::string format, codec, cover_art;
    uint32_t track = 0, disc = 0, year = 0, duration = 0, bitrate = 0, sample_rate = 0;
    uint16_t channels = 0, bit_depth = 0;
    uint64_t size = 0;
    bool liked = false;
};

struct PackedSong {
    Id id = kInvalidId, title = kInvalidId, artist_id = kInvalidId, artist_name = kInvalidId;
    Id album_id = kInvalidId, album_name = kInvalidId, album_artist_name = kInvalidId;
    Id format = kInvalidId, codec = kInvalidId, cover_art = kInvalidId;
    uint32_t track = 0, disc = 0, year = 0, duration = 0, bitrate = 0, sample_rate = 0;
    uint16_t channels = 0, bit_depth = 0;
    uint64_t size = 0;
    bool liked = false;
};

struct SongView {
    const PackedSong* packed = nullptr;
    const StringTable* strings = nullptr;
    bool valid() const { return packed != nullptr && strings != nullptr; }
    const std::string& title() const;
    const std::string& artist_name() const;
    const std::string& album_name() const;
    const std::string& format() const;
    const std::string& codec() const;
    const std::string& id() const;
    const std::string& album_artist_name() const;
    const std::string& artist_id() const;
    const std::string& album_id() const;
    const std::string& cover_art() const;
};

class MetadataSnapshot {
public:
    bool upsert(const SongInput& input);
    bool set_liked(const std::string& song_id, bool liked);
    bool set_playlist(const std::string& playlist_id, const std::vector<std::string>& song_ids);
    SongView song(const std::string& song_id) const;
    // Builds the sorted prefix index. Call once after a batch of upserts; until then search_songs()
    // answers correctly through a slower scan, so a reader never needs a lock or a mutation.
    void finalize();
    bool indexed() const { return !dirty_; }
    std::vector<std::string> search_songs(const std::string& query, size_t limit = 100) const;
    // Indexed queries that keep UI code from walking records itself.
    std::vector<std::string> liked_songs() const;
    std::vector<std::string> album_songs(const std::string& album_id) const;
    std::vector<SongInput> export_songs() const;
    // Sorted prefix index as bytes, so a restart can skip normalising and sorting every name.
    // import_index() rejects anything that does not match the current songs and leaves the index dirty.
    std::string export_index() const;
    bool import_index(const std::string& bytes);
    std::vector<std::pair<std::string, std::vector<std::string>>> export_playlists() const;
    bool liked(const std::string& song_id) const;
    bool playlist_contains(const std::string& playlist_id, const std::string& song_id) const;
    std::vector<std::string> playlist_songs(const std::string& playlist_id) const;
    size_t song_count() const { return songs_.size(); }
    size_t string_bytes() const { return strings_.bytes(); }
private:
    struct SearchEntry { std::string key; uint32_t song_index = 0; };
    std::vector<SearchEntry> build_entries() const;
    std::vector<std::string> search_in(const std::vector<SearchEntry>& entries, const std::string& key, size_t limit) const;
    static std::string normalize(const std::string& value);
    Id id_for(const std::string& value) const;
    StringTable strings_;
    std::vector<PackedSong> songs_;
    std::unordered_map<Id, uint32_t> song_index_;
    std::unordered_map<Id, std::vector<Id>> playlists_;
    void add_entries(uint32_t song_index, std::vector<SearchEntry>& into) const;
    std::vector<SearchEntry> search_, pending_;   // pending_: entries of songs added since the last finalize()
    bool dirty_ = false, rebuild_all_ = false;      // rebuild_all_: an upsert changed a searchable name
};
} // namespace xf::cache
