// Headless audio tests: ring, decode throughput/length, player over SDL dummy driver.
#include <SDL.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include "../src/audio/player.h"

static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

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
    std::printf(fails ? "FAILED\n" : "OK\n");
    return fails ? 1 : 0;
}
