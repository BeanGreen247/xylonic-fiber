// App Store benchmark: library crawl (album puts + indexing), search, reopen. Usage: store_bench [albums]
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include "../src/app/model_json.h"
#include "../src/app/store.h"
using Clock = std::chrono::steady_clock;
static double ms(Clock::time_point a) { return std::chrono::duration<double, std::milli>(Clock::now() - a).count(); }
int main(int argc, char** argv) {
    const int albums = argc > 1 ? std::atoi(argv[1]) : 300;
    const std::string path = (std::filesystem::temp_directory_path() / "xf_store_bench.json").string();
    { std::error_code ec; std::filesystem::remove_all(path + ".meta", ec); }
    std::filesystem::remove(path); std::filesystem::remove(path + ".journal");
    auto t = Clock::now();
    { Store s; s.open(path);
      for (int a = 0; a < albums; ++a) {
          Album al; al.id = "al" + std::to_string(a); al.name = "Album " + std::to_string(a); al.artist = "Artist " + std::to_string(a / 10);
          for (int i = 0; i < 12; ++i) { Song so; so.id = al.id + "-" + std::to_string(i); so.title = "Track " + std::to_string(i) + " of " + al.name; so.album = al.name; so.album_id = al.id; so.artist = al.artist; so.duration = 200; so.suffix = "mp3"; al.songs.push_back(so); }
          s.put("album:" + al.id, to_json(al)); s.index_album(al);
      }
      std::printf("crawl %d albums (%d songs): %.1f ms\n", albums, albums * 12, ms(t));
      t = Clock::now(); size_t n = 0; for (int i = 0; i < 100; ++i) n += s.local_search("track " + std::to_string(i % 12)).size();
      std::printf("search x100: %.1f ms (%zu)\n", ms(t), n);
      t = Clock::now(); for (int a = 0; a < 20; ++a) { Album al; al.id = "al" + std::to_string(a); al.name = "Refreshed"; Song so; so.id = al.id + "-0"; so.title = "Only"; so.album_id = al.id; al.songs.push_back(so); s.put("album:" + al.id, to_json(al)); }
      std::printf("20 album refreshes (shrinking): %.1f ms; search after: %zu\n", ms(t), s.local_search("track 1").size());
    }
    t = Clock::now(); { Store s; s.open(path); std::printf("reopen: %.1f ms\n", ms(t)); }
    std::error_code ec; std::filesystem::remove(path); std::filesystem::remove(path + ".journal"); std::filesystem::remove_all(path + ".meta", ec);
}
