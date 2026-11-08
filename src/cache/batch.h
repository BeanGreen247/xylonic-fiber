// Subsonic responses to metadata update batches.
#pragma once
#include "generation.h"
#include "../subsonic/model.h"

namespace xf::cache {
inline SongInput to_input(const Song& s) {
    SongInput in; in.id = s.id; in.title = s.title; in.artist_id = s.artist_id; in.artist_name = s.artist; in.album_id = s.album_id; in.album_name = s.album;
    in.album_artist_name = s.artist; in.cover_art = s.cover_art; in.format = s.suffix; in.codec = s.suffix; in.track = static_cast<uint32_t>(s.track < 0 ? 0 : s.track);
    in.year = static_cast<uint32_t>(s.year < 0 ? 0 : s.year); in.duration = static_cast<uint32_t>(s.duration < 0 ? 0 : s.duration);
    in.bitrate = static_cast<uint32_t>(s.bit_rate < 0 ? 0 : s.bit_rate); in.sample_rate = static_cast<uint32_t>(s.sampling_rate < 0 ? 0 : s.sampling_rate);
    in.bit_depth = static_cast<uint16_t>(s.bit_depth < 0 ? 0 : s.bit_depth); in.channels = static_cast<uint16_t>(s.channels < 0 ? 0 : s.channels); in.size = s.size; in.liked = s.starred; return in;
}
inline void add_album(UpdateBatch& batch, const Album& album) { for (const Song& s : album.songs) if (!s.id.empty()) batch.songs.push_back(to_input(s)); }
inline void add_playlist(UpdateBatch& batch, const std::string& id, const std::vector<Song>& songs) {
    std::vector<std::string> ids; ids.reserve(songs.size());
    for (const Song& s : songs) if (!s.id.empty()) { batch.songs.push_back(to_input(s)); ids.push_back(s.id); }
    batch.playlists.emplace_back(id, std::move(ids));
}
} // namespace xf::cache
