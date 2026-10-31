#include "client.h"
#include <mbedtls/md5.h>
#include <psa/crypto.h>
#include "../net/http.h"
#include "../net/url.h"
#include "../../third_party/yyjson.h"

namespace {
const char* kApiVersion = "1.16.1";
const char* kClient = "xylonic-fiber";

std::string hex(const unsigned char* p, size_t n) {
    static const char* d = "0123456789abcdef";
    std::string s;
    for (size_t i = 0; i < n; ++i) { s += d[p[i] >> 4]; s += d[p[i] & 15]; }
    return s;
}

std::string str(yyjson_val* o, const char* k) {
    yyjson_val* v = yyjson_obj_get(o, k);
    return v && yyjson_is_str(v) ? std::string(yyjson_get_str(v), yyjson_get_len(v)) : std::string();
}
int num(yyjson_val* o, const char* k) {
    yyjson_val* v = yyjson_obj_get(o, k);
    return v && yyjson_is_num(v) ? static_cast<int>(yyjson_get_sint(v)) : 0;
}
uint64_t unum(yyjson_val* o, const char* k) {
    yyjson_val* v = yyjson_obj_get(o, k);
    return v && yyjson_is_num(v) ? yyjson_get_uint(v) : 0;
}

void to_song(yyjson_val* o, Song& s) {
    s.id = str(o, "id"); s.title = str(o, "title"); s.album = str(o, "album"); s.album_id = str(o, "albumId");
    s.artist = str(o, "artist"); s.artist_id = str(o, "artistId"); s.cover_art = str(o, "coverArt"); s.suffix = str(o, "suffix");
    s.track = num(o, "track"); s.year = num(o, "year"); s.duration = num(o, "duration"); s.bit_rate = num(o, "bitRate");
    s.size = unum(o, "size"); s.starred = yyjson_obj_get(o, "starred") != nullptr;
}
void to_album(yyjson_val* o, Album& a) {
    a.id = str(o, "id"); a.name = str(o, "name"); if (a.name.empty()) a.name = str(o, "title");
    a.artist = str(o, "artist"); a.artist_id = str(o, "artistId"); a.cover_art = str(o, "coverArt");
    a.song_count = num(o, "songCount"); a.duration = num(o, "duration"); a.year = num(o, "year");
}

// Returns the "subsonic-response" object or null (err set). doc must be freed by the caller.
yyjson_val* root(yyjson_doc* doc, std::string& err) {
    yyjson_val* r = doc ? yyjson_obj_get(yyjson_doc_get_root(doc), "subsonic-response") : nullptr;
    if (!r) { err = "invalid response"; return nullptr; }
    if (str(r, "status") != "ok") {
        yyjson_val* e = yyjson_obj_get(r, "error");
        err = e ? str(e, "message") : "server error";
        if (err.empty()) err = "server error";
        return nullptr;
    }
    return r;
}

template <class F>
bool with_doc(const std::string& body, std::string& err, F f) {
    yyjson_doc* doc = yyjson_read(body.data(), body.size(), 0);
    yyjson_val* r = root(doc, err);
    bool ok = r != nullptr;
    if (ok) f(r);
    yyjson_doc_free(doc);
    return ok;
}

}  // namespace

SubsonicClient::SubsonicClient(std::string base, std::string user, std::string pass, ConnectOptions opt)
    : base_(std::move(base)), user_(std::move(user)), pass_(std::move(pass)), opt_(std::move(opt)) {
    while (!base_.empty() && base_.back() == '/') base_.pop_back();
}

std::string SubsonicClient::auth_query() const {
    unsigned char r[8], dig[16];
    psa_crypto_init();
    psa_generate_random(r, sizeof r);
    std::string salt = hex(r, sizeof r);
    std::string in = pass_ + salt;
    mbedtls_md5(reinterpret_cast<const unsigned char*>(in.data()), in.size(), dig);
    return "u=" + url_encode(user_) + "&t=" + hex(dig, 16) + "&s=" + salt + "&v=" + kApiVersion + "&c=" + kClient + "&f=json";
}

std::string SubsonicClient::url_for(const std::string& ep, const std::string& extra) const {
    return base_ + "/rest/" + ep + ".view?" + auth_query() + (extra.empty() ? "" : "&" + extra);
}

bool SubsonicClient::call(const std::string& ep, const std::string& extra, std::string& body, std::string& err) {
    HttpResult r = http_get_string(url_for(ep, extra), body, opt_);
    if (!r.error.empty()) { err = r.error; return false; }
    if (r.status != 200) { err = "HTTP " + std::to_string(r.status); return false; }
    return true;
}

bool SubsonicClient::parse_envelope(const std::string& body, std::string& err) {
    return with_doc(body, err, [](yyjson_val*) {});
}

bool SubsonicClient::parse_album(const std::string& body, Album& out, std::string& err) {
    return with_doc(body, err, [&](yyjson_val* r) {
        yyjson_val* a = yyjson_obj_get(r, "album");
        out = Album{};
        if (!a) return;
        to_album(a, out);
        size_t i, n; yyjson_val* s;
        yyjson_arr_foreach(yyjson_obj_get(a, "song"), i, n, s) { out.songs.emplace_back(); to_song(s, out.songs.back()); }
    });
}

bool SubsonicClient::parse_artists(const std::string& body, std::vector<Artist>& out, std::string& err) {
    return with_doc(body, err, [&](yyjson_val* r) {
        out.clear();
        size_t i, n, j, m; yyjson_val *idx, *ar;
        yyjson_arr_foreach(yyjson_obj_get(yyjson_obj_get(r, "artists"), "index"), i, n, idx)
            yyjson_arr_foreach(yyjson_obj_get(idx, "artist"), j, m, ar) {
                Artist a;
                a.id = str(ar, "id"); a.name = str(ar, "name"); a.cover_art = str(ar, "coverArt"); a.album_count = num(ar, "albumCount");
                out.push_back(std::move(a));
            }
    });
}

bool SubsonicClient::ping(std::string& err) {
    std::string b;
    return call("ping", "", b, err) && parse_envelope(b, err);
}

bool SubsonicClient::artists(std::vector<Artist>& out, std::string& err) {
    std::string b;
    return call("getArtists", "", b, err) && parse_artists(b, out, err);
}

bool SubsonicClient::artist(const std::string& id, Artist& out, std::string& err) {
    std::string b;
    if (!call("getArtist", "id=" + url_encode(id), b, err)) return false;
    return with_doc(b, err, [&](yyjson_val* r) {
        out = Artist{};
        yyjson_val* a = yyjson_obj_get(r, "artist");
        if (!a) return;
        out.id = str(a, "id"); out.name = str(a, "name"); out.cover_art = str(a, "coverArt"); out.album_count = num(a, "albumCount");
        size_t i, n; yyjson_val* al;
        yyjson_arr_foreach(yyjson_obj_get(a, "album"), i, n, al) { out.albums.emplace_back(); to_album(al, out.albums.back()); }
    });
}

bool SubsonicClient::album(const std::string& id, Album& out, std::string& err) {
    std::string b;
    return call("getAlbum", "id=" + url_encode(id), b, err) && parse_album(b, out, err);
}

bool SubsonicClient::album_list(const std::string& type, int size, int offset, std::vector<Album>& out, std::string& err) {
    std::string b;
    if (!call("getAlbumList2", "type=" + url_encode(type) + "&size=" + std::to_string(size) + "&offset=" + std::to_string(offset), b, err)) return false;
    return with_doc(b, err, [&](yyjson_val* r) {
        out.clear();
        size_t i, n; yyjson_val* al;
        yyjson_arr_foreach(yyjson_obj_get(yyjson_obj_get(r, "albumList2"), "album"), i, n, al) { out.emplace_back(); to_album(al, out.back()); }
    });
}

bool SubsonicClient::search_songs(const std::string& q, int count, int offset, std::vector<Song>& out, std::string& err) {
    std::string b;
    if (!call("search3", "query=" + url_encode(q) + "&artistCount=0&albumCount=0&songCount=" + std::to_string(count) + "&songOffset=" + std::to_string(offset), b, err)) return false;
    return with_doc(b, err, [&](yyjson_val* r) {
        out.clear();
        size_t i, n; yyjson_val* s;
        yyjson_arr_foreach(yyjson_obj_get(yyjson_obj_get(r, "searchResult3"), "song"), i, n, s) { out.emplace_back(); to_song(s, out.back()); }
    });
}

bool SubsonicClient::search_all(const std::string& q, SearchAll& out, std::string& err) {
    std::string b;
    if (!call("search3", "query=" + url_encode(q) + "&artistCount=20&albumCount=20&songCount=50", b, err)) return false;
    return with_doc(b, err, [&](yyjson_val* r) {
        out = SearchAll{};
        yyjson_val* sr = yyjson_obj_get(r, "searchResult3");
        size_t i, n; yyjson_val* v;
        yyjson_arr_foreach(yyjson_obj_get(sr, "artist"), i, n, v) {
            Artist a;
            a.id = str(v, "id"); a.name = str(v, "name"); a.cover_art = str(v, "coverArt"); a.album_count = num(v, "albumCount");
            out.artists.push_back(std::move(a));
        }
        yyjson_arr_foreach(yyjson_obj_get(sr, "album"), i, n, v) { out.albums.emplace_back(); to_album(v, out.albums.back()); }
        yyjson_arr_foreach(yyjson_obj_get(sr, "song"), i, n, v) { out.songs.emplace_back(); to_song(v, out.songs.back()); }
    });
}

bool SubsonicClient::starred(std::vector<Song>& out, std::string& err) {
    std::string b;
    if (!call("getStarred2", "", b, err)) return false;
    return with_doc(b, err, [&](yyjson_val* r) {
        out.clear();
        size_t i, n; yyjson_val* s;
        yyjson_arr_foreach(yyjson_obj_get(yyjson_obj_get(r, "starred2"), "song"), i, n, s) { out.emplace_back(); to_song(s, out.back()); }
    });
}

bool SubsonicClient::set_star(const std::string& id, bool on, std::string& err) {
    std::string b;
    return call(on ? "star" : "unstar", "id=" + url_encode(id), b, err) && parse_envelope(b, err);
}

bool SubsonicClient::scrobble(const std::string& id, bool submission, std::string& err) {
    std::string b;
    return call("scrobble", "id=" + url_encode(id) + "&submission=" + (submission ? "true" : "false"), b, err) && parse_envelope(b, err);
}

std::string SubsonicClient::stream_url(const std::string& id, int max_bitrate, const std::string& format) const {
    return url_for("stream", "id=" + url_encode(id) + (max_bitrate > 0 ? "&maxBitRate=" + std::to_string(max_bitrate) : "") + (format.empty() ? "" : "&format=" + url_encode(format)));
}
std::string SubsonicClient::download_url(const std::string& id) const { return url_for("download", "id=" + url_encode(id)); }
std::string SubsonicClient::cover_url(const std::string& c, int size) const {
    return url_for("getCoverArt", "id=" + url_encode(c) + (size > 0 ? "&size=" + std::to_string(size) : ""));
}
