#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct Song {
    std::string id, title, album, album_id, artist, artist_id, cover_art, suffix;
    int track = 0, year = 0, duration = 0, bit_rate = 0;
    int sampling_rate = 0, bit_depth = 0, channels = 0;   // OpenSubsonic samplingRate / bitDepth / channelCount (0 = unknown)
    uint64_t size = 0;
    bool starred = false;
};

struct Album {
    std::string id, name, artist, artist_id, cover_art;
    int song_count = 0, duration = 0, year = 0;
    std::vector<Song> songs;
};

struct Artist {
    std::string id, name, cover_art;
    int album_count = 0;
    std::vector<Album> albums;
};

struct PlaylistInfo {
    std::string id, name, owner;
    int song_count = 0;
};
