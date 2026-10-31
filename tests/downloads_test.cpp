// Store + Downloads against an in-process HTTP server (fast files, a slow one, a 404).
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
        std::string chunk(10000, 'x');
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

    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in ad{}; ad.sin_family = AF_INET; ad.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bind(lfd, reinterpret_cast<sockaddr*>(&ad), sizeof ad); listen(lfd, 16);
    socklen_t l = sizeof ad; getsockname(lfd, reinterpret_cast<sockaddr*>(&ad), &l);
    std::string base = "http://127.0.0.1:" + std::to_string(ntohs(ad.sin_port));
    std::thread srv(serve, lfd);
    char tmpl[] = "dltestXXXXXX"; std::string dir = mkdtemp(tmpl);
    std::string dbp = dir + "/t.db";
    auto urlf = [&](const std::string& id) { return base + "/download?id=" + id; };
    {
        Store st; CHECK(st.open(dbp));
        Downloads d(st, dir + "/files", urlf, ConnectOptions{});
        mkdir((dir + "/files").c_str(), 0700);
        d.enqueue({mk("1", "alA"), mk("2", "alA"), mk("3", "alB"), mk("missing", "alB")});
        CHECK(wait_for([&] { return d.pending() == 0; }));
        auto snap = d.snapshot();
        int done = 0, failed = 0;
        for (auto& it : snap) { if (it.state == Downloads::Done) ++done; if (it.state == Downloads::Failed) ++failed; }
        CHECK(done == 3 && failed == 1);
        std::string p = d.path_if_done("1");
        struct stat sb{}; CHECK(!p.empty() && stat(p.c_str(), &sb) == 0 && sb.st_size == 50000);
        CHECK(d.path_if_done("missing").empty());
        auto albums = d.done_albums();
        CHECK(albums.size() == 2 && (albums[0].songs.size() + albums[1].songs.size()) == 3);
        d.retry_failed(); CHECK(wait_for([&] { return d.pending() == 0; }));  // still 404: fails again
        // pause / resume on a slow file
        d.enqueue({mk("slow", "alC", 400000)});
        CHECK(wait_for([&] { for (auto& it : d.snapshot()) if (it.song.id == "slow" && it.state == Downloads::Active && it.bytes > 0) return true; return false; }));
        d.pause("slow");
        CHECK(wait_for([&] { for (auto& it : d.snapshot()) if (it.song.id == "slow") return it.state == Downloads::Paused; return false; }));
        d.resume("slow");
        CHECK(wait_for([&] { return !d.path_if_done("slow").empty(); }));
        // remove
        d.remove("2"); CHECK(d.path_if_done("2").empty() && access((dir + "/files/2.mp3").c_str(), F_OK) != 0);
        // verify drops rows whose file vanished
        unlink((dir + "/files/3.mp3").c_str()); CHECK(d.verify() == 1 && d.path_if_done("3").empty());
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
