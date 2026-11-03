#include "metadata.h"
#include <algorithm>
#include <cctype>

namespace xf::cache {
namespace { const std::string& empty_string() { static const std::string e; return e; } }

CacheProfile desktop_profile() { return {"desktop", 1024, 64, 64u * 1024u * 1024u, 4, true}; }
CacheProfile mobile_profile() { return {"mobile", 384, 24, 24u * 1024u * 1024u, 2, true}; }
CacheProfile low_memory_profile() { return {"low-memory", 128, 8, 8u * 1024u * 1024u, 1, false}; }
CacheProfile minimal_profile() { return {"minimal", 64, 2, 2u * 1024u * 1024u, 1, false}; }

uint32_t StringTable::hash_of(const std::string& v) {
    uint32_t h = 2166136261u;
    for (unsigned char ch : v) { h ^= ch; h *= 16777619u; }
    return h ^ (h >> 15);
}
void StringTable::grow() {
    std::vector<Id> next(slots_.size() * 2, kInvalidId); const size_t mask = next.size() - 1;
    for (Id id = 1; id < values_.size(); ++id) { size_t i = hashes_[id] & mask; while (next[i] != kInvalidId) i = (i + 1) & mask; next[i] = id; }
    slots_ = std::move(next);
}
Id StringTable::intern(const std::string& value) {
    if (value.empty()) return kInvalidId;
    const uint32_t h = hash_of(value); size_t mask = slots_.size() - 1, i = h & mask;
    for (; slots_[i] != kInvalidId; i = (i + 1) & mask) { const Id id = slots_[i]; if (hashes_[id] == h && values_[id] == value) return id; }
    const Id id = static_cast<Id>(values_.size());
    values_.push_back(value); hashes_.push_back(h); slots_[i] = id;
    if (values_.size() * 2 > slots_.size()) grow();
    return id;
}
Id StringTable::find(const std::string& value) const {
    if (value.empty()) return kInvalidId;
    const uint32_t h = hash_of(value); const size_t mask = slots_.size() - 1;
    for (size_t i = h & mask; slots_[i] != kInvalidId; i = (i + 1) & mask) { const Id id = slots_[i]; if (hashes_[id] == h && values_[id] == value) return id; }
    return kInvalidId;
}
const std::string& StringTable::get(Id id) const { return id < values_.size() ? values_[id] : empty_string(); }
size_t StringTable::bytes() const { size_t total = 0; for (const auto& v : values_) total += v.size(); return total; }

const std::string& SongView::title() const { return valid() ? strings->get(packed->title) : empty_string(); }
const std::string& SongView::artist_name() const { return valid() ? strings->get(packed->artist_name) : empty_string(); }
const std::string& SongView::album_name() const { return valid() ? strings->get(packed->album_name) : empty_string(); }
const std::string& SongView::format() const { return valid() ? strings->get(packed->format) : empty_string(); }
const std::string& SongView::codec() const { return valid() ? strings->get(packed->codec) : empty_string(); }
const std::string& SongView::id() const { return valid() ? strings->get(packed->id) : empty_string(); }
const std::string& SongView::artist_id() const { return valid() ? strings->get(packed->artist_id) : empty_string(); }
const std::string& SongView::album_id() const { return valid() ? strings->get(packed->album_id) : empty_string(); }
const std::string& SongView::cover_art() const { return valid() ? strings->get(packed->cover_art) : empty_string(); }
const std::string& SongView::album_artist_name() const { return valid() ? strings->get(packed->album_artist_name) : empty_string(); }

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
static bool entry_less(const std::string& ak, uint32_t ai, const std::string& bk, uint32_t bi) { return ak == bk ? ai < bi : ak < bk; }
void MetadataSnapshot::add_entries(uint32_t i, std::vector<SearchEntry>& into) const {
    const PackedSong& s = songs_[i];
    for (Id field : {s.title, s.artist_name, s.album_name}) { std::string key = normalize(strings_.get(field)); if (!key.empty()) into.push_back({std::move(key), i}); }
}
std::vector<MetadataSnapshot::SearchEntry> MetadataSnapshot::build_entries() const {
    std::vector<SearchEntry> entries; entries.reserve(songs_.size() * 3);
    for (uint32_t i = 0; i < songs_.size(); ++i) add_entries(i, entries);
    std::sort(entries.begin(), entries.end(), [](const SearchEntry& a, const SearchEntry& b) { return entry_less(a.key, a.song_index, b.key, b.song_index); });
    return entries;
}
void MetadataSnapshot::finalize() {
    if (!dirty_) return;
    if (rebuild_all_) { search_ = build_entries(); }
    else if (!pending_.empty()) {   // new songs only: sort the small tail and merge it, O(n) instead of a full sort
        auto less = [](const SearchEntry& a, const SearchEntry& b) { return entry_less(a.key, a.song_index, b.key, b.song_index); };
        std::sort(pending_.begin(), pending_.end(), less);
        const size_t mid = search_.size();
        search_.insert(search_.end(), std::make_move_iterator(pending_.begin()), std::make_move_iterator(pending_.end()));
        std::inplace_merge(search_.begin(), search_.begin() + static_cast<std::ptrdiff_t>(mid), search_.end(), less);
    }
    pending_.clear(); pending_.shrink_to_fit(); dirty_ = rebuild_all_ = false;
}

bool MetadataSnapshot::upsert(const SongInput& input) {
    if (input.id.empty()) return false;
    PackedSong p;
    p.id = strings_.intern(input.id); p.title = strings_.intern(input.title);
    p.artist_id = strings_.intern(input.artist_id); p.artist_name = strings_.intern(input.artist_name);
    p.album_id = strings_.intern(input.album_id); p.album_name = strings_.intern(input.album_name);
    p.album_artist_name = strings_.intern(input.album_artist_name); p.format = strings_.intern(input.format); p.codec = strings_.intern(input.codec); p.cover_art = strings_.intern(input.cover_art);
    p.track = input.track; p.disc = input.disc; p.year = input.year; p.duration = input.duration; p.bitrate = input.bitrate; p.sample_rate = input.sample_rate;
    p.channels = input.channels; p.bit_depth = input.bit_depth; p.size = input.size; p.liked = input.liked;
    auto found = song_index_.find(p.id);
    if (found == song_index_.end()) {
        const uint32_t index = static_cast<uint32_t>(songs_.size()); song_index_[p.id] = index; songs_.push_back(p);
        if (!rebuild_all_) add_entries(index, pending_);
        dirty_ = true;
    } else {
        PackedSong& old = songs_[found->second];
        if (old.title != p.title || old.artist_name != p.artist_name || old.album_name != p.album_name) { rebuild_all_ = true; dirty_ = true; }
        old = p;
    }
    return true;
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
std::vector<std::string> MetadataSnapshot::search_in(const std::vector<SearchEntry>& entries, const std::string& key, size_t limit) const {
    std::vector<std::string> result; std::vector<uint32_t> seen;
    auto it = std::lower_bound(entries.begin(), entries.end(), key, [](const SearchEntry& e, const std::string& k) { return e.key < k; });
    for (; it != entries.end() && it->key.compare(0, key.size(), key) == 0; ++it) {
        if (std::find(seen.begin(), seen.end(), it->song_index) != seen.end()) continue;
        seen.push_back(it->song_index); result.push_back(strings_.get(songs_[it->song_index].id));
        if (result.size() == limit) break;
    }
    return result;
}
std::vector<std::string> MetadataSnapshot::search_songs(const std::string& query, size_t limit) const {
    const std::string key = normalize(query); if (key.empty() || limit == 0) return {};
    return dirty_ ? search_in(build_entries(), key, limit) : search_in(search_, key, limit);
}
std::vector<std::string> MetadataSnapshot::liked_songs() const {
    std::vector<std::string> out; for (const PackedSong& s : songs_) if (s.liked) out.push_back(strings_.get(s.id)); return out;
}
std::vector<std::string> MetadataSnapshot::album_songs(const std::string& album_id) const {
    std::vector<std::string> out; const Id album = id_for(album_id); if (album == kInvalidId) return out;
    std::vector<const PackedSong*> match; for (const PackedSong& s : songs_) if (s.album_id == album) match.push_back(&s);
    std::stable_sort(match.begin(), match.end(), [](const PackedSong* a, const PackedSong* b) { return a->disc == b->disc ? a->track < b->track : a->disc < b->disc; });
    for (const PackedSong* s : match) out.push_back(strings_.get(s->id));
    return out;
}
std::vector<SongInput> MetadataSnapshot::export_songs() const {
    std::vector<SongInput> out; out.reserve(songs_.size());
    for (const PackedSong& p : songs_) {
        SongInput s; s.id = strings_.get(p.id); s.title = strings_.get(p.title); s.artist_id = strings_.get(p.artist_id); s.artist_name = strings_.get(p.artist_name);
        s.album_id = strings_.get(p.album_id); s.album_name = strings_.get(p.album_name); s.album_artist_name = strings_.get(p.album_artist_name);
        s.format = strings_.get(p.format); s.codec = strings_.get(p.codec); s.cover_art = strings_.get(p.cover_art); s.track = p.track; s.disc = p.disc; s.year = p.year; s.duration = p.duration;
        s.bitrate = p.bitrate; s.sample_rate = p.sample_rate; s.channels = p.channels; s.bit_depth = p.bit_depth; s.size = p.size; s.liked = p.liked;
        out.push_back(std::move(s));
    }
    return out;
}
std::vector<std::pair<std::string, std::vector<std::string>>> MetadataSnapshot::export_playlists() const {
    std::vector<std::pair<std::string, std::vector<std::string>>> out;
    for (const auto& item : playlists_) { std::vector<std::string> ids; ids.reserve(item.second.size()); for (Id id : item.second) ids.push_back(strings_.get(id)); out.emplace_back(strings_.get(item.first), std::move(ids)); }
    std::sort(out.begin(), out.end()); return out;
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
std::string MetadataSnapshot::export_index() const {
    std::string out; if (dirty_) return out;
    auto put32 = [&out](uint32_t v) { for (int i = 0; i < 4; ++i) out.push_back(static_cast<char>(v >> (i * 8))); };
    put32(static_cast<uint32_t>(songs_.size())); put32(static_cast<uint32_t>(search_.size()));
    for (const SearchEntry& e : search_) { put32(e.song_index); put32(static_cast<uint32_t>(e.key.size())); out += e.key; }
    return out;
}
bool MetadataSnapshot::import_index(const std::string& bytes) {
    size_t p = 0;
    auto get32 = [&](uint32_t& v) { if (bytes.size() - p < 4) return false; v = 0; for (int i = 0; i < 4; ++i) v |= static_cast<uint32_t>(static_cast<unsigned char>(bytes[p++])) << (i * 8); return true; };
    uint32_t songs = 0, count = 0;
    if (!get32(songs) || !get32(count) || songs != songs_.size() || count > bytes.size() / 8) return false;
    std::vector<SearchEntry> entries; entries.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t index = 0, len = 0;
        if (!get32(index) || !get32(len) || index >= songs_.size() || len > bytes.size() - p) return false;
        if (!entries.empty() && entries.back().key.compare(0, std::string::npos, bytes, p, len) > 0) return false;   // must be sorted
        entries.push_back({bytes.substr(p, len), index}); p += len;
    }
    if (p != bytes.size()) return false;
    search_ = std::move(entries); pending_.clear(); dirty_ = rebuild_all_ = false; return true;
}
} // namespace xf::cache
