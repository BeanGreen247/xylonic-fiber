#include "model_json.h"
#include "../../third_party/yyjson.h"

namespace {
using Doc = yyjson_mut_doc;

yyjson_mut_val* song_val(Doc* d, const Song& s) {
    yyjson_mut_val* o = yyjson_mut_obj(d);
    yyjson_mut_obj_add_strcpy(d, o, "id", s.id.c_str());
    yyjson_mut_obj_add_strcpy(d, o, "title", s.title.c_str());
    yyjson_mut_obj_add_strcpy(d, o, "album", s.album.c_str());
    yyjson_mut_obj_add_strcpy(d, o, "albumId", s.album_id.c_str());
    yyjson_mut_obj_add_strcpy(d, o, "artist", s.artist.c_str());
    yyjson_mut_obj_add_strcpy(d, o, "artistId", s.artist_id.c_str());
    yyjson_mut_obj_add_strcpy(d, o, "coverArt", s.cover_art.c_str());
    yyjson_mut_obj_add_strcpy(d, o, "suffix", s.suffix.c_str());
    yyjson_mut_obj_add_int(d, o, "track", s.track);
    yyjson_mut_obj_add_int(d, o, "year", s.year);
    yyjson_mut_obj_add_int(d, o, "duration", s.duration);
    yyjson_mut_obj_add_int(d, o, "bitRate", s.bit_rate);
    yyjson_mut_obj_add_int(d, o, "samplingRate", s.sampling_rate); yyjson_mut_obj_add_int(d, o, "bitDepth", s.bit_depth); yyjson_mut_obj_add_int(d, o, "channelCount", s.channels);
    yyjson_mut_obj_add_uint(d, o, "size", s.size);
    yyjson_mut_obj_add_bool(d, o, "starred", s.starred);
    return o;
}
yyjson_mut_val* album_val(Doc* d, const Album& a, bool with_songs) {
    yyjson_mut_val* o = yyjson_mut_obj(d);
    yyjson_mut_obj_add_strcpy(d, o, "id", a.id.c_str());
    yyjson_mut_obj_add_strcpy(d, o, "name", a.name.c_str());
    yyjson_mut_obj_add_strcpy(d, o, "artist", a.artist.c_str());
    yyjson_mut_obj_add_strcpy(d, o, "artistId", a.artist_id.c_str());
    yyjson_mut_obj_add_strcpy(d, o, "coverArt", a.cover_art.c_str());
    yyjson_mut_obj_add_int(d, o, "songCount", a.song_count);
    yyjson_mut_obj_add_int(d, o, "duration", a.duration);
    yyjson_mut_obj_add_int(d, o, "year", a.year);
    if (with_songs) {
        yyjson_mut_val* arr = yyjson_mut_arr(d);
        for (const Song& s : a.songs) yyjson_mut_arr_add_val(arr, song_val(d, s));
        yyjson_mut_obj_add_val(d, o, "songs", arr);
    }
    return o;
}
yyjson_mut_val* artist_val(Doc* d, const Artist& a) {
    yyjson_mut_val* o = yyjson_mut_obj(d);
    yyjson_mut_obj_add_strcpy(d, o, "id", a.id.c_str());
    yyjson_mut_obj_add_strcpy(d, o, "name", a.name.c_str());
    yyjson_mut_obj_add_strcpy(d, o, "coverArt", a.cover_art.c_str());
    yyjson_mut_obj_add_int(d, o, "albumCount", a.album_count);
    yyjson_mut_val* arr = yyjson_mut_arr(d);
    for (const Album& al : a.albums) yyjson_mut_arr_add_val(arr, album_val(d, al, false));
    yyjson_mut_obj_add_val(d, o, "albums", arr);
    return o;
}
template <class F>
std::string dump(F build) {
    Doc* d = yyjson_mut_doc_new(nullptr);
    yyjson_mut_doc_set_root(d, build(d));
    char* p = yyjson_mut_write(d, 0, nullptr);
    std::string r = p ? p : "";
    free(p);
    yyjson_mut_doc_free(d);
    return r;
}

std::string str(yyjson_val* o, const char* k) { yyjson_val* v = yyjson_obj_get(o, k); return v && yyjson_is_str(v) ? std::string(yyjson_get_str(v), yyjson_get_len(v)) : std::string(); }
int num(yyjson_val* o, const char* k) { yyjson_val* v = yyjson_obj_get(o, k); return v && yyjson_is_num(v) ? static_cast<int>(yyjson_get_sint(v)) : 0; }

void read_song(yyjson_val* o, Song& s) {
    s.id = str(o, "id"); s.title = str(o, "title"); s.album = str(o, "album"); s.album_id = str(o, "albumId");
    s.artist = str(o, "artist"); s.artist_id = str(o, "artistId"); s.cover_art = str(o, "coverArt"); s.suffix = str(o, "suffix");
    s.track = num(o, "track"); s.year = num(o, "year"); s.duration = num(o, "duration"); s.bit_rate = num(o, "bitRate");
    s.sampling_rate = num(o, "samplingRate"); s.bit_depth = num(o, "bitDepth"); s.channels = num(o, "channelCount");
    yyjson_val* sz = yyjson_obj_get(o, "size"); s.size = sz && yyjson_is_num(sz) ? yyjson_get_uint(sz) : 0;
    yyjson_val* st = yyjson_obj_get(o, "starred"); s.starred = st && yyjson_get_bool(st);
}
void read_album(yyjson_val* o, Album& a) {
    a.id = str(o, "id"); a.name = str(o, "name"); a.artist = str(o, "artist"); a.artist_id = str(o, "artistId"); a.cover_art = str(o, "coverArt");
    a.song_count = num(o, "songCount"); a.duration = num(o, "duration"); a.year = num(o, "year");
    size_t i = 0, n = 0; yyjson_val* s = nullptr;
    yyjson_arr_foreach(yyjson_obj_get(o, "songs"), i, n, s) { a.songs.emplace_back(); read_song(s, a.songs.back()); }
}
void read_artist(yyjson_val* o, Artist& a) {
    a.id = str(o, "id"); a.name = str(o, "name"); a.cover_art = str(o, "coverArt"); a.album_count = num(o, "albumCount");
    size_t i = 0, n = 0; yyjson_val* s = nullptr;
    yyjson_arr_foreach(yyjson_obj_get(o, "albums"), i, n, s) { a.albums.emplace_back(); read_album(s, a.albums.back()); }
}
template <class F>
bool parse(const std::string& j, F f) {
    yyjson_doc* d = yyjson_read(j.data(), j.size(), 0);
    if (!d) return false;
    bool ok = f(yyjson_doc_get_root(d));
    yyjson_doc_free(d);
    return ok;
}
}  // namespace

std::string to_json(const Song& s) { return dump([&](Doc* d) { return song_val(d, s); }); }
std::string to_json(const Album& a) { return dump([&](Doc* d) { return album_val(d, a, true); }); }
std::string to_json(const Artist& a) { return dump([&](Doc* d) { return artist_val(d, a); }); }
std::string to_json(const std::vector<Album>& v) {
    return dump([&](Doc* d) { yyjson_mut_val* arr = yyjson_mut_arr(d); for (auto& a : v) yyjson_mut_arr_add_val(arr, album_val(d, a, false)); return arr; });
}
std::string to_json(const std::vector<Artist>& v) {
    return dump([&](Doc* d) { yyjson_mut_val* arr = yyjson_mut_arr(d); for (auto& a : v) yyjson_mut_arr_add_val(arr, artist_val(d, a)); return arr; });
}
bool from_json(const std::string& j, Song& out) { return parse(j, [&](yyjson_val* r) { if (!yyjson_is_obj(r)) return false; out = Song{}; read_song(r, out); return true; }); }
bool from_json(const std::string& j, Album& out) { return parse(j, [&](yyjson_val* r) { if (!yyjson_is_obj(r)) return false; out = Album{}; read_album(r, out); return true; }); }
bool from_json(const std::string& j, Artist& out) { return parse(j, [&](yyjson_val* r) { if (!yyjson_is_obj(r)) return false; out = Artist{}; read_artist(r, out); return true; }); }
bool from_json(const std::string& j, std::vector<Album>& out) {
    return parse(j, [&](yyjson_val* r) { if (!yyjson_is_arr(r)) return false; out.clear(); size_t i = 0, n = 0; yyjson_val* v = nullptr; yyjson_arr_foreach(r, i, n, v) { out.emplace_back(); read_album(v, out.back()); } return true; });
}
bool from_json(const std::string& j, std::vector<Artist>& out) {
    return parse(j, [&](yyjson_val* r) { if (!yyjson_is_arr(r)) return false; out.clear(); size_t i = 0, n = 0; yyjson_val* v = nullptr; yyjson_arr_foreach(r, i, n, v) { out.emplace_back(); read_artist(v, out.back()); } return true; });
}
