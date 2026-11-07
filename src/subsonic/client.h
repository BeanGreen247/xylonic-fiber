// Subsonic/Navidrome API client (REST, JSON, token auth). Blocking; call from a worker thread.
#pragma once
#include <atomic>
#include <mutex>
#include <string>
#include <vector>
#include "../net/stream.h"
#include "model.h"

class SubsonicClient {
public:
    SubsonicClient(std::string base_url, std::string user, std::string password, ConnectOptions opt = {});

    bool ping(std::string& err);
    bool artists(std::vector<Artist>& out, std::string& err);
    bool artist(const std::string& id, Artist& out, std::string& err);
    bool album(const std::string& id, Album& out, std::string& err);
    // type: recent, newest, frequent, random, alphabeticalByName, ...
    bool album_list(const std::string& type, int size, int offset, std::vector<Album>& out, std::string& err);
    // getSongCount of the original: sum of songCount over the first 500 albums (alphabeticalByName). 0 on failure.
    bool song_count(int& total, std::string& err);
    // Calls made to the server (metadata API calls in the Settings stats); reset after the precache.
    static uint64_t api_calls();
    static void reset_api_calls();
    bool search_songs(const std::string& query, int count, int offset, std::vector<Song>& out, std::string& err);
    struct SearchAll { std::vector<Artist> artists; std::vector<Album> albums; std::vector<Song> songs; };
    // search3 with the old app's limits (20 artists, 20 albums, 50 songs)
    bool search_all(const std::string& query, SearchAll& out, std::string& err);
    bool starred(std::vector<Song>& out, std::string& err);
    bool song(const std::string& id, Song& out, std::string& err);   // getSong: technical metadata for one track
    // Playlists
    bool playlists(std::vector<PlaylistInfo>& out, std::string& err);
    bool playlist(const std::string& id, PlaylistInfo& info, std::vector<Song>& songs, std::string& err);
    bool create_playlist(const std::string& name, const std::vector<std::string>& song_ids, std::string& new_id, std::string& err);
    bool replace_playlist(const std::string& id, const std::vector<std::string>& song_ids, std::string& err);  // createPlaylist with playlistId
    bool update_playlist(const std::string& id, const std::string& new_name, const std::vector<std::string>& add_ids,
                         const std::vector<int>& remove_indexes, std::string& err);
    bool delete_playlist(const std::string& id, std::string& err);
    bool set_star(const std::string& id, bool on, std::string& err);
    bool scrobble(const std::string& id, bool submission, std::string& err);

    // URLs embed auth (per Subsonic spec) so they must not be logged.
    // format: ask the server to transcode (e.g. "mp3") when the source codec is not decodable here.
    std::string stream_url(const std::string& id, int max_bitrate = 0, const std::string& format = "") const;
    // Original file (`download`) unless transcode_mp3: then `stream` with maxBitRate and format=mp3, because `download` ignores the bitrate
    // (the original app also downloads through stream.view). max_bitrate 0 with transcode_mp3 asks for the server's best MP3 (320).
    std::string download_url(const std::string& id, int max_bitrate = 0, bool transcode_mp3 = false) const;
    std::string cover_url(const std::string& cover_id, int size = 0) const;

    // Exposed for tests.
    static bool parse_envelope(const std::string& body, std::string& err);
    static bool parse_album(const std::string& body, Album& out, std::string& err);
    static bool parse_playlists(const std::string& body, std::vector<PlaylistInfo>& out, std::string& err);
    static bool parse_playlist(const std::string& body, PlaylistInfo& info, std::vector<Song>& songs, std::string& err);
    static bool parse_artists(const std::string& body, std::vector<Artist>& out, std::string& err);

    const ConnectOptions& connect_options() const { return opt_; }
    // True once the server rejected the credentials (HTTP 401 or Subsonic error 40/41/44). The app then asks for the
    // password again and calls set_password(), which keeps every holder of this client valid.
    bool auth_failed() const { return auth_failed_.load(); }
    void clear_auth_failed() { auth_failed_ = false; }
    void set_password(std::string password);

private:
    std::string auth_query() const;  // fresh salt each call
    std::string url_for(const std::string& endpoint, const std::string& extra) const;
    bool call(const std::string& endpoint, const std::string& extra, std::string& body, std::string& err);
    std::string base_, user_, pass_;
    mutable std::mutex cred_m_;
    std::atomic<bool> auth_failed_{false};
    ConnectOptions opt_;
};
