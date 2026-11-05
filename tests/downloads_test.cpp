// Store + Downloads against an in-process HTTP server (fast files, a slow one, a 404).
#include <filesystem>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <poll.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>
#include "../src/app/downloads.h"
#include "../src/app/model_json.h"
#include "../src/cache/journal.h"

static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)
static void sleep_ms(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

static std::atomic<bool> g_run{true};
static void handle(int c) {
    char b[1024]; ssize_t n = recv(c, b, sizeof b - 1, 0); b[n > 0 ? n : 0] = 0;
    std::string req(b);
    auto has = [&](const char* s) { return req.find(s) != std::string::npos; };
    if (has("id=missing")) { std::string r = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n"; send(c, r.data(), r.size(), MSG_NOSIGNAL); }
    else {
        size_t total = has("id=slow") ? 400000 : 50000;
        std::string h = "HTTP/1.1 200 OK\r\nContent-Length: " + std::to_string(total) + "\r\n\r\n";
        send(c, h.data(), h.size(), MSG_NOSIGNAL);
        std::string chunk(10000, has("id=3") ? '3' : 'x');
        for (size_t off = 0; off < total; off += chunk.size()) {
            if (send(c, chunk.data(), chunk.size(), MSG_NOSIGNAL) <= 0) break;
            if (has("id=slow")) sleep_ms(60);
        }
    }
    close(c);
}
static void serve(int lfd) {
    while (g_run) {
        pollfd pf{lfd, POLLIN, 0};
        if (poll(&pf, 1, 100) > 0) { int c = accept(lfd, nullptr, nullptr); if (c >= 0) std::thread(handle, c).detach(); }
    }
}
static Song mk(const std::string& id, const std::string& album, uint64_t size = 50000) {
    Song s; s.id = id; s.title = "T" + id; s.album_id = album; s.album = "Album " + album; s.artist = "A"; s.suffix = "mp3"; s.size = size; s.duration = 100; return s;
}
static bool wait_for(const std::function<bool()>& f, int ms = 8000) { for (int t = 0; t < ms; t += 20) { if (f()) return true; sleep_ms(20); } return f(); }

int main() {
    // JSON round trips
    Album a; a.id = "al"; a.name = "N\"q"; a.songs = {mk("1", "al"), mk("2", "al")};
    Album b; CHECK(from_json(to_json(a), b) && b.songs.size() == 2 && b.name == "N\"q");
    std::vector<Artist> arts(1); arts[0].id = "ar"; arts[0].albums = {a};
    std::vector<Artist> arts2; CHECK(from_json(to_json(arts), arts2) && arts2.size() == 1 && arts2[0].albums.size() == 1);

    // metadata cache
    { Store s; CHECK(s.open(":memory:")); std::string v; int64_t ts = 0;
      CHECK(s.put("home:newest", "[1]") && s.get("home:newest", v, &ts) && v == "[1]" && ts > 0);
      s.put("album:1", "x"); s.put("album:2", "y"); s.erase_prefix("album:"); CHECK(!s.get("album:1", v) && s.get("home:newest", v)); }

    { Store s; CHECK(s.open(":memory:"));
      Album indexed; indexed.id = "indexed"; indexed.name = "Cached Album"; indexed.artist = "Cached Artist";
      indexed.songs = {mk("needle", "indexed")}; indexed.songs[0].title = "Needle Song";
      CHECK(s.put("album:indexed", to_json(indexed)));
      auto hits = s.local_search("needle");
      CHECK(hits.size() == 1 && hits[0].id == "needle");
    }

    { char tmpl[] = "journal-store-XXXXXX"; int fd = mkstemp(tmpl); close(fd); unlink(tmpl);
      std::string path = tmpl;
      { Store s; CHECK(s.open(path)); }
      { xf::cache::Journal j(path + ".journal"); std::string p(1, 'P'); uint32_t n = 13;
        for (int i = 0; i < 4; ++i) p.push_back(static_cast<char>(n >> (i * 8)));
        p += "album:journalpersisted"; CHECK(j.append_transaction(1, 1, {p})); }
      // The JSON snapshot is removed to simulate a crash after the journal commit.
      unlink(path.c_str());
      { Store s; CHECK(s.open(path)); std::string v; CHECK(s.get("album:journal", v) && v == "persisted"); }
      unlink(path.c_str()); unlink((path + ".journal").c_str());
      std::error_code ec0; std::filesystem::remove_all(path + ".meta", ec0);
    }

    {   // query API: likes, playlists and technical fields come back from the indexed metadata, across a restart
      char tmpl[] = "meta-store-XXXXXX"; int fd = mkstemp(tmpl); close(fd); unlink(tmpl); std::string path = tmpl;
      Album al; al.id = "alb"; al.name = "A"; al.artist = "Art"; al.songs = {mk("q1", "alb"), mk("q2", "alb")}; al.songs[0].title = "Quartz Song"; al.songs[0].cover_art = "cov1"; al.songs[0].bit_rate = 320; al.songs[0].suffix = "flac";
      { Store s; CHECK(s.open(path)); CHECK(s.put("album:alb", to_json(al))); s.set_liked("q2", true);
        s.set_playlist("pl1", {al.songs[1], al.songs[0]});
        Song got; CHECK(s.song("q1", got) && got.cover_art == "cov1" && got.bit_rate == 320 && got.suffix == "flac");
        CHECK(s.liked("q2")); CHECK(!s.liked("q1")); CHECK(s.liked_songs().size() == 1); CHECK(s.playlist_contains("pl1", "q1")); CHECK(!s.playlist_contains("pl1", "zz")); }
      { Store s; CHECK(s.open(path)); CHECK(s.liked("q2")); CHECK(s.playlist_contains("pl1", "q2")); CHECK(s.local_search("quartz").size() == 1); CHECK(s.local_search("quartz")[0].cover_art == "cov1"); // snapshot + persisted index
        Album shrunk = al; shrunk.songs.resize(1); CHECK(s.put("album:alb", to_json(shrunk))); CHECK(s.local_search("quartz").size() == 1 && s.local_search("q").size() <= 2); }
      std::error_code ec; std::filesystem::remove_all(path + ".meta", ec); unlink(path.c_str()); unlink((path + ".journal").c_str());
    }

    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in ad{}; ad.sin_family = AF_INET; ad.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bind(lfd, reinterpret_cast<sockaddr*>(&ad), sizeof ad); listen(lfd, 16);
    socklen_t l = sizeof ad; getsockname(lfd, reinterpret_cast<sockaddr*>(&ad), &l);
    std::string base = "http://127.0.0.1:" + std::to_string(ntohs(ad.sin_port));
    std::thread srv(serve, lfd);
    char tmpl[] = "dltestXXXXXX"; std::string dir = mkdtemp(tmpl);
    std::string dbp = dir + "/t.json";
    auto urlf = [&](const std::string& id) { return base + "/download?id=" + id; };
    {
        Store st; CHECK(st.open(dbp));
        Downloads d(st, dir + "/files", urlf, ConnectOptions{});
        mkdir((dir + "/files").c_str(), 0700);
        d.enqueue({mk("1", "alA"), mk("2", "alA"), mk("3", "alB"), mk("missing", "alB")});
        CHECK(wait_for([&] { return d.pending() == 0; }));
        auto snap_p = d.snapshot(); const auto& snap = *snap_p;
        int done = 0, failed = 0;
        for (auto& it : snap) { if (it.state == Downloads::Done) ++done; if (it.state == Downloads::Failed) ++failed; }
        CHECK(done == 3 && failed == 1);
        std::string p = d.path_if_done("1");
        struct stat sb{}; CHECK(!p.empty() && stat(p.c_str(), &sb) == 0 && sb.st_size == 50000);
        CHECK(d.path_if_done("2") == p);  // identical bytes share one canonical file
        CHECK(d.path_if_done("missing").empty());
        auto albums = d.done_albums();
        CHECK(albums.size() == 2 && (albums[0].songs.size() + albums[1].songs.size()) == 3);
        d.retry_failed(); CHECK(wait_for([&] { return d.pending() == 0; }));  // still 404: fails again
        // pause / resume on a slow file
        d.enqueue({mk("slow", "alC", 400000)});
        CHECK(wait_for([&] { auto sp = d.snapshot(); for (auto& it : *sp) if (it.song.id == "slow" && it.state == Downloads::Active && d.live_bytes(it.song.id, it.bytes) > 0) return true; return false; }));
        d.pause("slow");
        CHECK(wait_for([&] { auto sp = d.snapshot(); for (auto& it : *sp) if (it.song.id == "slow") return it.state == Downloads::Paused; return false; }));
        d.resume("slow");
        CHECK(wait_for([&] { return !d.path_if_done("slow").empty(); }));
        // remove
        std::string survivor = d.path_if_done("1");
        d.remove("2"); CHECK(d.path_if_done("2").empty() && access(survivor.c_str(), F_OK) == 0);
        // verify drops rows whose file vanished
        std::string missing = d.path_if_done("3"); unlink(missing.c_str()); CHECK(d.verify() == 1 && d.path_if_done("3").empty());
        FILE* orphan = std::fopen((dir + "/files/orphan.mp3").c_str(), "wb"); if (orphan) { std::fputs("orphan", orphan); std::fclose(orphan); }
        CHECK(d.reconcile() == 1 && access((dir + "/files/orphan.mp3").c_str(), F_OK) != 0);
    }
    { // a library-sized queue stays cheap: batch enqueue, repeated snapshots and pending(), batch clear (paused so nothing downloads)
        Store st; CHECK(st.open(dir + "/big.json"));
        Downloads d(st, dir + "/bigfiles", urlf, ConnectOptions{});
        d.pause_all();
        std::vector<Song> many;
        for (int i = 0; i < 4000; ++i) many.push_back(mk("big" + std::to_string(i), "alBig" + std::to_string(i / 10)));
        auto t0 = std::chrono::steady_clock::now();
        d.enqueue(many);
        auto ms = [&] { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count(); };
        CHECK(ms() < 3000);
        t0 = std::chrono::steady_clock::now();
        int n = 0;
        for (int i = 0; i < 200; ++i) { n += d.pending(); auto sp = d.snapshot(); n += static_cast<int>(sp->size()); }
        CHECK(n == 200 * 8000 && ms() < 1500);   // cached snapshot + non-copying pending()
        t0 = std::chrono::steady_clock::now();
        d.clear_queue();
        CHECK(d.pending() == 0 && d.snapshot()->empty() && ms() < 5000);
    }
    { // persistence across restart
        Store st; CHECK(st.open(dbp));
        Downloads d(st, dir + "/files", urlf, ConnectOptions{});
        CHECK(!d.path_if_done("1").empty());
    }
    g_run = false; srv.join(); close(lfd);
    std::string cmd = "rm -rf " + dir; if (system(cmd.c_str()) != 0) {}
    std::printf(fails ? "FAILED\n" : "OK\n");
    return fails ? 1 : 0;
}
