#include "metadata.h"
#include <algorithm>
#include <cctype>

namespace xf::cache {
namespace { const std::string& empty_string() { static const std::string e; return e; } }

CacheProfile desktop_profile() { return {"desktop", 1024, 64, 64u * 1024u * 1024u, 4, true}; }
CacheProfile mobile_profile() { return {"mobile", 384, 24, 24u * 1024u * 1024u, 2, true}; }
CacheProfile low_memory_profile() { return {"low-memory", 128, 8, 8u * 1024u * 1024u, 1, false}; }
CacheProfile minimal_profile() { return {"minimal", 64, 2, 2u * 1024u * 1024u, 1, false}; }

Id StringTable::intern(const std::string& value) {
    if (value.empty()) return kInvalidId;
    auto found = ids_.find(value);
    if (found != ids_.end()) return found->second;
    const Id id = static_cast<Id>(values_.size());
    values_.push_back(value);
    ids_.emplace(values_.back(), id);
    return id;
}
Id StringTable::find(const std::string& value) const {
    auto found = ids_.find(value);
    return found == ids_.end() ? kInvalidId : found->second;
}
const std::string& StringTable::get(Id id) const { return id < values_.size() ? values_[id] : empty_string(); }
size_t StringTable::bytes() const { size_t total = 0; for (const auto& v : values_) total += v.size(); return total; }

const std::string& SongView::title() const { return valid() ? strings->get(packed->title) : empty_string(); }
const std::string& SongView::artist_name() const { return valid() ? strings->get(packed->artist_name) : empty_string(); }
const std::string& SongView::album_name() const { return valid() ? strings->get(packed->album_name) : empty_string(); }
const std::string& SongView::format() const { return valid() ? strings->get(packed->format) : empty_string(); }
const std::string& SongView::codec() const { return valid() ? strings->get(packed->codec) : empty_string(); }

std::string MetadataSnapshot::normalize(const std::string& value) {
    std::string out; out.reserve(value.size()); bool space = false;
    for (unsigned char c : value) {
        if (std::isspace(c)) { space = !out.empty(); continue; }
        if (space) { out.push_back(' '); space = false; }
        out.push_back(static_cast<char>(std::tolower(c)));
    }
    return out;
}
Id MetadataSnapshot::id_for(const std::string& value) const {
    return strings_.find(value);
}
void MetadataSnapshot::rebuild_search_entry(uint32_t index) {
    search_.erase(std::remove_if(search_.begin(), search_.end(), [index](const SearchEntry& e) { return e.song_index == index; }), search_.end());
    const PackedSong& s = songs_[index];
    search_.push_back({normalize(strings_.get(s.title)), index});
    search_.push_back({normalize(strings_.get(s.artist_name)), index});
    search_.push_back({normalize(strings_.get(s.album_name)), index});
    std::sort(search_.begin(), search_.end(), [](const SearchEntry& a, const SearchEntry& b) { return a.key == b.key ? a.song_index < b.song_index : a.key < b.key; });
}

bool MetadataSnapshot::upsert(const SongInput& input) {
    if (input.id.empty()) return false;
    PackedSong p;
    p.id = strings_.intern(input.id); p.title = strings_.intern(input.title);
    p.artist_id = strings_.intern(input.artist_id); p.artist_name = strings_.intern(input.artist_name);
    p.album_id = strings_.intern(input.album_id); p.album_name = strings_.intern(input.album_name);
    p.album_artist_name = strings_.intern(input.album_artist_name); p.format = strings_.intern(input.format); p.codec = strings_.intern(input.codec);
    p.track = input.track; p.disc = input.disc; p.year = input.year; p.duration = input.duration; p.bitrate = input.bitrate; p.sample_rate = input.sample_rate;
    p.channels = input.channels; p.bit_depth = input.bit_depth; p.size = input.size; p.liked = input.liked;
    auto found = song_index_.find(p.id); uint32_t index;
    if (found == song_index_.end()) { index = static_cast<uint32_t>(songs_.size()); song_index_[p.id] = index; songs_.push_back(p); }
    else { index = found->second; songs_[index] = p; }
    rebuild_search_entry(index); return true;
}
bool MetadataSnapshot::set_liked(const std::string& song_id, bool liked_value) {
    auto found = song_index_.find(id_for(song_id)); if (found == song_index_.end()) return false;
    songs_[found->second].liked = liked_value; return true;
}
bool MetadataSnapshot::set_playlist(const std::string& playlist_id, const std::vector<std::string>& song_ids) {
    const Id playlist = strings_.intern(playlist_id); if (playlist == kInvalidId) return false;
    std::vector<Id>& ids = playlists_[playlist]; ids.clear();
    for (const auto& song_id : song_ids) { const Id id = id_for(song_id); if (id != kInvalidId && song_index_.find(id) != song_index_.end()) ids.push_back(id); }
    return true;
}
SongView MetadataSnapshot::song(const std::string& song_id) const {
    auto found = song_index_.find(id_for(song_id)); return found == song_index_.end() ? SongView{} : SongView{&songs_[found->second], &strings_};
}
std::vector<std::string> MetadataSnapshot::search_songs(const std::string& query, size_t limit) const {
    const std::string key = normalize(query); std::vector<std::string> result; if (key.empty()) return result;
    for (const auto& entry : search_) {
        if (entry.key.compare(0, key.size(), key) != 0) continue;
        const std::string& id = strings_.get(songs_[entry.song_index].id);
        if (std::find(result.begin(), result.end(), id) == result.end()) { result.push_back(id); if (result.size() == limit) break; }
    }
    return result;
}
bool MetadataSnapshot::liked(const std::string& song_id) const { SongView v = song(song_id); return v.valid() && v.packed->liked; }
bool MetadataSnapshot::playlist_contains(const std::string& playlist_id, const std::string& song_id) const {
    auto found = playlists_.find(id_for(playlist_id)); const Id id = id_for(song_id);
    return found != playlists_.end() && std::find(found->second.begin(), found->second.end(), id) != found->second.end();
}
std::vector<std::string> MetadataSnapshot::playlist_songs(const std::string& playlist_id) const {
    std::vector<std::string> result; auto found = playlists_.find(id_for(playlist_id)); if (found == playlists_.end()) return result;
    result.reserve(found->second.size()); for (Id id : found->second) result.push_back(strings_.get(id)); return result;
}
} // namespace xf::cache
