// Compact, platform-neutral metadata/index foundation.
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
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
    std::vector<std::string> values_{std::string()};
    std::unordered_map<std::string, Id> ids_;
};

struct SongInput {
    std::string id, title, artist_id, artist_name, album_id, album_name, album_artist_name;
    std::string format, codec;
    uint32_t track = 0, disc = 0, year = 0, duration = 0, bitrate = 0, sample_rate = 0;
    uint16_t channels = 0, bit_depth = 0;
    uint64_t size = 0;
    bool liked = false;
};

struct PackedSong {
    Id id = kInvalidId, title = kInvalidId, artist_id = kInvalidId, artist_name = kInvalidId;
    Id album_id = kInvalidId, album_name = kInvalidId, album_artist_name = kInvalidId;
    Id format = kInvalidId, codec = kInvalidId;
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
};

class MetadataSnapshot {
public:
    bool upsert(const SongInput& input);
    bool set_liked(const std::string& song_id, bool liked);
    bool set_playlist(const std::string& playlist_id, const std::vector<std::string>& song_ids);
    SongView song(const std::string& song_id) const;
    std::vector<std::string> search_songs(const std::string& query, size_t limit = 100) const;
    bool liked(const std::string& song_id) const;
    bool playlist_contains(const std::string& playlist_id, const std::string& song_id) const;
    std::vector<std::string> playlist_songs(const std::string& playlist_id) const;
    size_t song_count() const { return songs_.size(); }
    size_t string_bytes() const { return strings_.bytes(); }
private:
    struct SearchEntry { std::string key; uint32_t song_index = 0; };
    static std::string normalize(const std::string& value);
    Id id_for(const std::string& value) const;
    void rebuild_search_entry(uint32_t song_index);
    StringTable strings_;
    std::vector<PackedSong> songs_;
    std::unordered_map<Id, uint32_t> song_index_;
    std::unordered_map<Id, std::vector<Id>> playlists_;
    std::vector<SearchEntry> search_;
};
} // namespace xf::cache
