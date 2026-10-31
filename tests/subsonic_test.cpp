// Parser + URL tests with canned JSON; live check with --live <url> against any Subsonic server.
#include <cstdio>
#include <cstring>
#include "../src/subsonic/client.h"

static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

int main(int argc, char** argv) {
    std::string err;
    Album a;
    const char* ok = R"({"subsonic-response":{"status":"ok","album":{"id":"al1","name":"A","artist":"X","songCount":2,"year":2020,"song":[
      {"id":"s1","title":"One","track":1,"duration":60,"bitRate":192,"size":123456789012,"suffix":"mp3","starred":"2020-01-01T00:00:00Z"},{"id":"s2","title":"Two"}]}}})";
    CHECK(SubsonicClient::parse_album(ok, a, err));
    CHECK(a.id == "al1" && a.songs.size() == 2 && a.songs[0].size == 123456789012ULL && a.songs[0].starred && !a.songs[1].starred && a.songs[1].track == 0);
    CHECK(!SubsonicClient::parse_album(R"({"subsonic-response":{"status":"failed","error":{"code":40,"message":"Wrong username or password"}}})", a, err) && err == "Wrong username or password");
    CHECK(!SubsonicClient::parse_envelope("not json", err) && err == "invalid response");
    std::vector<Artist> ar;
    CHECK(SubsonicClient::parse_artists(R"({"subsonic-response":{"status":"ok","artists":{"index":[{"name":"A","artist":[{"id":"1","name":"Q","albumCount":3},{"id":"2","name":"R"}]},{"name":"B","artist":[{"id":"3","name":"S"}]}]}}})", ar, err));
    CHECK(ar.size() == 3 && ar[0].album_count == 3 && ar[2].name == "S");
    CHECK(SubsonicClient::parse_artists(R"({"subsonic-response":{"status":"ok","artists":{}}})", ar, err) && ar.empty());

    SubsonicClient c("http://h:1/", "us er", "pw");
    std::string u = c.stream_url("a b", 128);
    CHECK(u.compare(0, 31, "http://h:1/rest/stream.view?u=u") == 0);
    CHECK(u.find("u=us%20er&t=") != std::string::npos && u.find("&id=a%20b&maxBitRate=128") != std::string::npos);
    CHECK(c.stream_url("x") != c.stream_url("x"));  // fresh salt per URL

    if (argc >= 5 && !std::strcmp(argv[1], "--live")) {  // --live <url> <user> <pass>
        SubsonicClient l(argv[2], argv[3], argv[4]);
        CHECK(l.ping(err));
        std::vector<Artist> arts; CHECK(l.artists(arts, err) && !arts.empty());
        Artist art; CHECK(l.artist(arts[0].id, art, err) && !art.albums.empty());
        Album al; CHECK(l.album(art.albums[0].id, al, err) && !al.songs.empty());
        std::vector<Album> lst; CHECK(l.album_list("newest", 5, 0, lst, err) && lst.size() == 5);
        std::vector<Song> ss; CHECK(l.search_songs("", 30, 0, ss, err) && ss.size() == 30);
        std::printf("live: %zu artists, album %s has %zu songs (%s)\n", arts.size(), al.name.c_str(), al.songs.size(), err.c_str());
    }
    std::printf(fails ? "FAILED\n" : "OK\n");
    return fails ? 1 : 0;
}
