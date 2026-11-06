// HttpFileSource: sequential streaming, Range jumps ahead and back, and a server that ignores Range.
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include "../src/audio/source.h"

static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)
static constexpr size_t kTotal = 3000000;
static std::atomic<bool> g_run{true};
static std::atomic<bool> g_ranges{true};
static uint8_t byte_at(size_t i) { return static_cast<uint8_t>((i * 31) % 251); }

static void handle(int c) {
    char b[2048]; ssize_t n = recv(c, b, sizeof b - 1, 0); b[n > 0 ? n : 0] = 0;
    std::string req(b);
    size_t start = 0;
    bool ranged = false;
    size_t p = req.find("Range: bytes=");
    if (p == std::string::npos) p = req.find("range: bytes=");
    if (g_ranges && p != std::string::npos) { start = std::strtoull(req.c_str() + p + 13, nullptr, 10); ranged = true; }
    const size_t len = kTotal - start;
    std::string h = std::string(ranged ? "HTTP/1.1 206 Partial Content\r\n" : "HTTP/1.1 200 OK\r\n") + "Content-Length: " + std::to_string(len) + "\r\n";
    if (ranged) h += "Content-Range: bytes " + std::to_string(start) + "-" + std::to_string(kTotal - 1) + "/" + std::to_string(kTotal) + "\r\n";
    h += "\r\n";
    send(c, h.data(), h.size(), MSG_NOSIGNAL);
    std::string chunk(50000, 0);
    for (size_t off = 0; off < len; off += chunk.size()) {
        const size_t m = std::min(chunk.size(), len - off);
        for (size_t i = 0; i < m; ++i) chunk[i] = static_cast<char>(byte_at(start + off + i));
        if (send(c, chunk.data(), m, MSG_NOSIGNAL) <= 0) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(40));   // ~1.2 MB/s: a sequential trip to 2.5 MB takes 2 s
    }
    close(c);
}
static void serve(int lfd) {
    while (g_run) {
        pollfd pf{lfd, POLLIN, 0};
        if (poll(&pf, 1, 100) > 0) { int c = accept(lfd, nullptr, nullptr); if (c >= 0) std::thread(handle, c).detach(); }
    }
}
static bool check_at(HttpFileSource& s, int64_t at, size_t n) {
    s.seek(at, ByteSource::Set);
    std::string buf(n, 0);
    size_t got = 0;
    while (got < n) { long r = s.read(&buf[got], n - got); if (r <= 0) return false; got += static_cast<size_t>(r); }
    for (size_t i = 0; i < n; ++i) if (static_cast<uint8_t>(buf[i]) != byte_at(static_cast<size_t>(at) + i)) return false;
    return true;
}
static double secs(std::chrono::steady_clock::time_point t) { return std::chrono::duration<double>(std::chrono::steady_clock::now() - t).count(); }

int main() {
    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in ad{}; ad.sin_family = AF_INET; ad.sin_addr.s_addr = htonl(INADDR_LOOPBACK); ad.sin_port = 0;
    bind(lfd, reinterpret_cast<sockaddr*>(&ad), sizeof ad); listen(lfd, 16);
    socklen_t sl = sizeof ad; getsockname(lfd, reinterpret_cast<sockaddr*>(&ad), &sl);
    const std::string url = "http://127.0.0.1:" + std::to_string(ntohs(ad.sin_port)) + "/x";
    std::thread srv(serve, lfd);
    const std::string dir = "/tmp/xf_source_test_" + std::to_string(getpid());
    std::string cmd = "mkdir -p " + dir; if (std::system(cmd.c_str()) != 0) return 1;

    {   // Range server: jump to 2.5 MB at once, back to the start, then past the end of the window again
        HttpFileSource s(url, dir + "/a.bin", {});
        std::string err;
        CHECK(s.start(5000, err));
        CHECK(s.size() == static_cast<int64_t>(kTotal));
        CHECK(check_at(s, 0, 4000));
        auto t = std::chrono::steady_clock::now();
        CHECK(check_at(s, 2500000, 4000));
        CHECK(secs(t) < 1.0);
        t = std::chrono::steady_clock::now();
        CHECK(check_at(s, 100, 4000));   // behind the new window: another Range request
        CHECK(secs(t) < 1.0);
        CHECK(check_at(s, static_cast<int64_t>(kTotal) - 100, 100));
        CHECK(!s.complete());            // never held the whole file from byte 0
    }
    CHECK(access((dir + "/a.bin").c_str(), F_OK) != 0 && access((dir + "/a.bin.part").c_str(), F_OK) != 0);

    {   // plain download still completes and lands as the cache file
        HttpFileSource s(url, dir + "/b.bin", {});
        std::string err;
        CHECK(s.start(5000, err));
        CHECK(check_at(s, 0, 200000));
        std::string buf(65536, 0);
        long r; size_t total = 200000;
        while ((r = s.read(&buf[0], buf.size())) > 0) total += static_cast<size_t>(r);
        CHECK(total == kTotal && s.complete());
    }
    CHECK(access((dir + "/b.bin").c_str(), F_OK) == 0);

    g_ranges = false;   // server ignores Range: seeking ahead must still return correct bytes (by waiting)
    {
        HttpFileSource s(url, dir + "/c.bin", {});
        std::string err;
        CHECK(s.start(5000, err));
        CHECK(check_at(s, 0, 1000));
        CHECK(check_at(s, 1200000, 1000));
        CHECK(check_at(s, 500, 1000));
    }
    g_run = false;
    srv.join();
    cmd = "rm -rf " + dir; (void)!std::system(cmd.c_str());
    if (fails) { std::printf("%d failures\n", fails); return 1; }
    std::printf("source ok\n");
    return 0;
}
