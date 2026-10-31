// Headless audio tests: ring, decode throughput/length, player over SDL dummy driver.
#include <SDL.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fstream>
#include <iterator>
#include <vector>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include "../src/audio/player.h"

static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

// Serves `data` once per connection: first 64 KiB, a pause, then the rest (proves play-before-complete).
static void serve_slow(int lfd, const std::vector<char>* data, int conns) {
    for (int i = 0; i < conns; ++i) {
        int c = accept(lfd, nullptr, nullptr);
        char b[1024]; recv(c, b, sizeof b, 0);
        std::string h = "HTTP/1.1 200 OK\r\nContent-Type: audio/mpeg\r\nContent-Length: " + std::to_string(data->size()) + "\r\n\r\n";
        send(c, h.data(), h.size(), MSG_NOSIGNAL);
        size_t first = data->size() < 65536 ? data->size() : 65536;
        send(c, data->data(), first, MSG_NOSIGNAL);
        usleep(600000);
        size_t off = first;
        while (off < data->size()) { ssize_t w = send(c, data->data() + off, data->size() - off, MSG_NOSIGNAL); if (w <= 0) break; off += static_cast<size_t>(w); }
        close(c);
    }
}

int main(int argc, char** argv) {
    if (argc < 2) { std::printf("usage: audio_test <file.mp3>\n"); return 2; }
    std::string path = argv[1];
    { // ring wraparound
        SampleRing r(8);
        int16_t a[6] = {1,2,3,4,5,6}, b[8] = {};
        CHECK(r.write(a, 6) == 6); CHECK(r.read(b, 4) == 4);
        CHECK(r.write(a, 6) == 6); CHECK(r.readable() == 8); CHECK(r.write(a, 1) == 0);
        CHECK(r.read(b, 8) == 8); CHECK(b[0] == 5 && b[1] == 6 && b[2] == 1);
    }
    { // full decode: length and speed
        auto d = open_decoder(path);
        CHECK(d != nullptr);
        if (!d) return 1;
        auto t0 = std::chrono::steady_clock::now();
        std::vector<int16_t> buf(4096 * d->channels());
        uint64_t frames = 0, n;
        while ((n = d->read(buf.data(), 4096)) > 0) frames += n;
        double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        double dur = double(frames) / d->sample_rate();
        std::printf("decoded %.1fs audio in %.3fs (%.0fx realtime)\n", dur, sec, dur / sec);
        CHECK(dur > 1.0);
        CHECK(d->total_frames() == 0 || d->total_frames() == frames);
        CHECK(d->seek(d->sample_rate())); CHECK(d->read(buf.data(), 1024) == 1024);
    }
    { // player over dummy audio device
        CHECK(SDL_Init(SDL_INIT_AUDIO) == 0);
        Player p;
        CHECK(p.load(path)); CHECK(!p.playing());
        CHECK(p.play()); CHECK(p.playing());
        std::this_thread::sleep_for(std::chrono::milliseconds(1500));
        double pos = p.position_seconds();
        std::printf("position after 1.5s: %.2f\n", pos);
        CHECK(pos > 0.5 && pos < 3.0);
        CHECK(p.seek_seconds(2.0)); CHECK(p.playing());
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        CHECK(p.position_seconds() >= 2.0 && p.position_seconds() < 3.0);
        p.pause(); CHECK(!p.playing());
        double hold = p.position_seconds();
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        CHECK(p.position_seconds() == hold);
        CHECK(p.play());
        p.stop();
        SDL_Quit();
    }
    { // speed, EQ, spectrum and gapless advance on short tracks
        std::string shortp = argc > 2 ? argv[2] : path;
        CHECK(SDL_Init(SDL_INIT_AUDIO) == 0);
        Player p;
        CHECK(p.load(path)); CHECK(p.play());
        p.set_speed(2.0);
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        double p0 = p.position_seconds();
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        double dp = p.position_seconds() - p0;
        std::printf("speed 2x: advanced %.2fs in 1s\n", dp);
        CHECK(dp > 1.6 && dp < 2.4);
        p.set_speed(1.0);
        p.set_eq(8, -6);  // must not disturb playback
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        float bands[24];
        p.spectrum(bands, 24);
        float mx = 0; for (float b : bands) mx = std::max(mx, b);
        std::printf("spectrum peak %.2f\n", mx);
        CHECK(mx > 0.5f);  // 440 Hz sine is audible in the tap
        p.stop();
        // gapless: a short track followed by a prepared next
        Player g;
        CHECK(g.load(shortp)); CHECK(g.play());
        auto nx = open_decoder(shortp);
        CHECK(nx != nullptr);
        g.set_next(std::move(nx), 3.0);
        CHECK(g.has_next());
        bool adv = false;
        for (int i = 0; i < 160 && !adv; ++i) { std::this_thread::sleep_for(std::chrono::milliseconds(50)); adv = g.take_advanced(); }
        CHECK(adv);
        double pos = g.position_seconds();
        std::printf("after gapless advance position %.2f\n", pos);
        CHECK(pos >= 0.0 && pos < 1.5 && g.playing());
        CHECK(!g.has_next());
        g.stop();
        SDL_Quit();
    }
    { // streaming from http into a cache file
        std::ifstream f(path, std::ios::binary);
        std::vector<char> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        int lfd = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in a{}; a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        bind(lfd, reinterpret_cast<sockaddr*>(&a), sizeof a); listen(lfd, 4);
        socklen_t l = sizeof a; getsockname(lfd, reinterpret_cast<sockaddr*>(&a), &l);
        std::string url = "http://127.0.0.1:" + std::to_string(ntohs(a.sin_port)) + "/stream";
        std::string cache = path + ".cached";
        ::unlink(cache.c_str());
        std::thread srv(serve_slow, lfd, &data, 1);
        CHECK(SDL_Init(SDL_INIT_AUDIO) == 0);
        {
            Player p; std::string err;
            CHECK(p.load_url(url, cache, "mp3", ConnectOptions{}, 30.0, err));
            CHECK(p.duration_seconds() == 30.0);
            CHECK(p.play());
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            struct stat st{};
            CHECK(stat(cache.c_str(), &st) != 0);  // not finished yet: only .part exists
            CHECK(p.position_seconds() > 0.0);     // playing from the partial download
            CHECK(p.seek_seconds(20.0));           // beyond the downloaded part: blocks until it arrives
            std::this_thread::sleep_for(std::chrono::milliseconds(1500));
            CHECK(p.position_seconds() > 20.0);
            while (!p.finished()) std::this_thread::sleep_for(std::chrono::milliseconds(50));
            p.stop();
        }
        struct stat st{};
        CHECK(stat(cache.c_str(), &st) == 0 && static_cast<size_t>(st.st_size) == data.size());
        srv.join(); close(lfd);
        { // second load: server gone, plays from the cache file
            Player p; std::string err;
            CHECK(p.load_url(url, cache, "mp3", ConnectOptions{}, 0, err));
            CHECK(p.play()); std::this_thread::sleep_for(std::chrono::milliseconds(300));
            CHECK(p.position_seconds() > 0.0);
        }
        { // failed download leaves no cache file
            Player p; std::string err;
            std::string bad = path + ".none";
            CHECK(!p.load_url("http://127.0.0.1:1/x", bad, "mp3", ConnectOptions{}, 0, err));
            CHECK(access(bad.c_str(), F_OK) != 0 && access((bad + ".part").c_str(), F_OK) != 0);
        }
        ::unlink(cache.c_str());
        SDL_Quit();
    }
    std::printf(fails ? "FAILED\n" : "OK\n");
    return fails ? 1 : 0;
}
