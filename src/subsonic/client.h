// Subsonic/Navidrome API client (REST, JSON, token auth). Blocking; call from a worker thread.
#pragma once
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
    bool search_songs(const std::string& query, int count, int offset, std::vector<Song>& out, std::string& err);
    bool starred(std::vector<Song>& out, std::string& err);
    bool set_star(const std::string& id, bool on, std::string& err);
    bool scrobble(const std::string& id, bool submission, std::string& err);

    // URLs embed auth (per Subsonic spec) so they must not be logged.
    std::string stream_url(const std::string& id, int max_bitrate = 0) const;
    std::string download_url(const std::string& id) const;
    std::string cover_url(const std::string& cover_id, int size = 0) const;

    // Exposed for tests.
    static bool parse_envelope(const std::string& body, std::string& err);
    static bool parse_album(const std::string& body, Album& out, std::string& err);
    static bool parse_artists(const std::string& body, std::vector<Artist>& out, std::string& err);

    const ConnectOptions& connect_options() const { return opt_; }

private:
    std::string auth_query() const;  // fresh salt each call
    std::string url_for(const std::string& endpoint, const std::string& extra) const;
    bool call(const std::string& endpoint, const std::string& extra, std::string& body, std::string& err);
    std::string base_, user_, pass_;
    ConnectOptions opt_;
};
