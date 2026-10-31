#include <cstdio>
#include "../src/cache/metadata.h"
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)
using namespace xf::cache;
static SongInput song(const char* id, const char* title, bool liked = false) {
    SongInput s; s.id = id; s.title = title; s.artist_id = "artist-1"; s.artist_name = "  The Artist  "; s.album_id = "album-1"; s.album_name = "First Album"; s.album_artist_name = "The Artist"; s.format = "mp3"; s.codec = "mp3"; s.duration = 201; s.bitrate = 192; s.sample_rate = 44100; s.channels = 2; s.bit_depth = 16; s.liked = liked; return s;
}
int main() {
    StringTable strings; const Id a = strings.intern("same"); CHECK(a != kInvalidId && strings.intern("same") == a && strings.find("same") == a && strings.find("missing") == kInvalidId && strings.size() == 2); CHECK(strings.get(999).empty());
    MetadataSnapshot cache; CHECK(cache.upsert(song("s1", "  First   Song  ", true))); CHECK(cache.upsert(song("s2", "Second Song"))); CHECK(cache.song_count() == 2);
    SongView s1 = cache.song("s1"); CHECK(s1.valid() && s1.title() == "  First   Song  " && s1.format() == "mp3"); CHECK(cache.liked("s1") && !cache.liked("s2"));
    auto results = cache.search_songs("first song"); CHECK(results.size() == 1 && results[0] == "s1"); results = cache.search_songs("THE ARTIST"); CHECK(results.size() == 2);
    CHECK(cache.set_playlist("p1", {"s2", "s1", "missing"})); CHECK(cache.playlist_contains("p1", "s1") && !cache.playlist_contains("p1", "missing")); CHECK(cache.playlist_songs("p1").size() == 2);
    CHECK(cache.set_liked("s2", true) && cache.liked("s2")); CHECK(!cache.set_liked("missing", true));
    CHECK(desktop_profile().hot_song_limit > mobile_profile().hot_song_limit); CHECK(mobile_profile().hot_song_limit > low_memory_profile().hot_song_limit); CHECK(!low_memory_profile().prefetch && minimal_profile().worker_count == 1);
    std::printf(fails ? "FAILED\n" : "OK\n"); return fails ? 1 : 0;
}
