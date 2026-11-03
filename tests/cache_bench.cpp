// Cache core benchmark: bulk load, lookup, prefix search, journal CRC. Usage: cache_bench [songs]
#include <chrono>
#include <filesystem>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include "../src/cache/generation.h"
#include "../src/cache/journal.h"
#include "../src/cache/metadata.h"
using namespace xf::cache;
using Clock = std::chrono::steady_clock;
static double ms(Clock::time_point a) { return std::chrono::duration<double, std::milli>(Clock::now() - a).count(); }
static long rss_kb() { FILE* f = std::fopen("/proc/self/statm", "r"); long a = 0, b = 0; if (f) { if (std::fscanf(f, "%ld %ld", &a, &b) != 2) b = 0; std::fclose(f); } return b * 4; }
int main(int argc, char** argv) {
    const size_t n = argc > 1 ? std::strtoul(argv[1], nullptr, 10) : 20000;
    const long rss0 = rss_kb();
    MetadataSnapshot m; auto t = Clock::now();
    for (size_t i = 0; i < n; ++i) {
        SongInput s; s.id = "song-" + std::to_string(i); s.title = "Title " + std::to_string(i * 7919 % n);
        s.artist_id = "ar-" + std::to_string(i / 100); s.artist_name = "Artist " + std::to_string(i / 100);
        s.album_id = "al-" + std::to_string(i / 10); s.album_name = "Album " + std::to_string(i / 10); s.format = "mp3"; s.codec = "mp3";
        s.duration = 200; s.bitrate = 192; m.upsert(s);
    }
    std::printf("load %zu songs: %.1f ms\n", n, ms(t));
    t = Clock::now(); m.finalize(); std::printf("finalize index: %.1f ms\n", ms(t));
    t = Clock::now(); size_t hit = 0; for (size_t i = 0; i < n; ++i) hit += m.song("song-" + std::to_string(i)).valid();
    std::printf("lookup x%zu: %.1f ms (%zu hits)\n", n, ms(t), hit);
    t = Clock::now(); size_t found = 0; for (int i = 0; i < 200; ++i) found += m.search_songs("artist " + std::to_string(i % 50), 100).size();
    std::printf("search x200: %.1f ms (%zu results)\n", ms(t), found);
    std::vector<std::string> ids; for (size_t i = 0; i < n; ++i) ids.push_back("song-" + std::to_string(i));
    t = Clock::now(); m.set_playlist("p", ids); size_t c = 0; for (size_t i = 0; i < 1000; ++i) c += m.playlist_contains("p", ids[n - 1 - i]);
    std::printf("playlist set+1000 contains: %.1f ms (%zu)\n", ms(t), c);
    std::string blob(8u << 20, 'x'); t = Clock::now(); volatile uint32_t crc = cache_crc32(blob.data(), blob.size()); (void)crc;
    std::printf("crc32 8MiB: %.1f ms\n", ms(t));
    {   // persistence: batch apply (journal), compact, cold open with and without the persisted index
        namespace fs = std::filesystem; const fs::path dir = fs::current_path() / "xf_cache_bench"; std::error_code ec; fs::remove_all(dir, ec); fs::create_directories(dir, ec);
        UpdateBatch b; b.songs = m.export_songs();
        MetadataStore store(dir.string()); store.open(); t = Clock::now(); store.apply(b); std::printf("apply %zu-song batch (journal + fsync + publish): %.1f ms\n", n, ms(t));
        t = Clock::now(); store.compact(); std::printf("compact (snapshot + index): %.1f ms\n", ms(t));
        t = Clock::now(); { MetadataStore again(dir.string()); again.open(); } std::printf("cold open with persisted index: %.1f ms\n", ms(t));
        fs::remove(dir / "metadata.index"); t = Clock::now(); { MetadataStore again(dir.string()); again.open(); } std::printf("cold open, index rebuilt: %.1f ms\n", ms(t));
        UpdateBatch one; one.likes.emplace_back("song-1", true);
        store.durable = true; t = Clock::now(); for (int i = 0; i < 50; ++i) store.apply(one); std::printf("small batch, fsync each: %.2f ms/batch\n", ms(t) / 50);
        store.durable = false; t = Clock::now(); for (int i = 0; i < 50; ++i) store.apply(one); std::printf("small batch, no fsync: %.3f ms/batch\n", ms(t) / 50);
        fs::remove_all(dir, ec);
    }
    std::printf("rss delta: %ld KB, string bytes %zu\n", rss_kb() - rss0, m.string_bytes());
}
