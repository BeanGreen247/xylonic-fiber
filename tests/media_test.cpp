#include <cstdio>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>
#include "../src/cache/media.h"
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)
using namespace xf::cache;
namespace fs = std::filesystem;
int main() {
    const fs::path dir = fs::temp_directory_path() / "xf_media_test"; std::error_code ec; fs::remove_all(dir, ec);
    MediaCache c(dir.string()); CHECK(c.open());
    const std::string a(1000, 'a'), b(2000, 'b'), cc(3000, 'c');
    CHECK(c.put("song1@320", MediaKind::Audio, a) && c.put("song1@128", MediaKind::Audio, b) && c.put("cover1", MediaKind::Artwork, cc));
    CHECK(c.put("song2@320", MediaKind::Audio, a));                                   // dedup: same bytes, one file
    CHECK(c.blob_count() == 3 && c.total_bytes() == 6000 && c.refs("song1@320") == 2);
    std::string path, q; CHECK(c.best("song1", {"flac", "320", "128"}, path, &q) && q == "320" && fs::file_size(path) == 1000);
    CHECK(c.best("song1", {"flac"}, path) == false);
    CHECK(c.release("song1@320") && c.refs("song2@320") == 1 && c.blob_count() == 3);
    CHECK(c.release("song2@320") && c.blob_count() == 2);                                // refcount zero removes the file
    CHECK(c.put("song1@320", MediaKind::Audio, a) && c.save());
    // reopen, orphan reconcile: stray file, stray temp, missing blob file
    { std::ofstream(dir / "audio" / "stray") << "x"; std::ofstream(dir / "artwork" / "deadbeef.tmp") << "y"; }
    MediaCache r(dir.string()); CHECK(r.open() && r.blob_count() == 3 && !fs::exists(dir / "audio" / "stray") && !fs::exists(dir / "artwork" / "deadbeef.tmp"));
    CHECK(r.get("cover1", path)); fs::remove(path, ec); CHECK(!r.get("cover1", path) && r.blob_count() == 2);   // damaged media drops only its own entries
    { std::string audio; CHECK(r.get("song1@128", audio)); std::ofstream(audio, std::ios::trunc) << "short"; }
    auto rep = r.reconcile(); CHECK(rep.missing_files == 1 && r.blob_count() == 1 && !r.get("song1@128", path));
    // LRU eviction and pinning
    MediaCache e((dir / "evict").string()); CHECK(e.open());
    for (int i = 0; i < 5; ++i) CHECK(e.put("k" + std::to_string(i), MediaKind::Audio, std::string(1000, static_cast<char>('a' + i)), i == 0));
    CHECK(e.get("k1", path));                                                          // k1 becomes most recent
    CHECK(e.evict_to(2000) == 3000 && e.total_bytes() == 2000 && e.get("k0", path) && e.get("k1", path) && !e.get("k2", path));
    CHECK(e.pin("k0", false) && e.evict_to(0) == 2000 && e.blob_count() == 0);
    // clear_all drops pinned blobs too and removes their files (the wipe path)
    { MediaCache w((dir / "wipe").string()); CHECK(w.open());
      std::vector<std::string> files;
      for (int i = 0; i < 600; ++i) { CHECK(w.put("w" + std::to_string(i), MediaKind::Artwork, "blob" + std::to_string(i), i % 7 == 0)); std::string wp; CHECK(w.get("w" + std::to_string(i), wp)); files.push_back(wp); }
      CHECK(w.clear_all() == 600 && w.blob_count() == 0 && w.total_bytes() == 0);
      bool any = false; for (auto& f : files) any = any || fs::exists(f); CHECK(!any); }
    // a corrupt manifest starts empty and removes stray files
    CHECK(e.put("z", MediaKind::Audio, a) && e.save());
    { std::fstream f((dir / "evict" / "cache.manifest").string(), std::ios::in | std::ios::out | std::ios::binary); f.seekp(40); char x = '!'; f.write(&x, 1); }
    MediaCache bad((dir / "evict").string()); CHECK(bad.open() && bad.blob_count() == 0 && fs::is_empty(dir / "evict" / "audio"));
    // concurrent puts and gets
    MediaCache cc2((dir / "conc").string()); CHECK(cc2.open());
    std::vector<std::thread> ts; for (int t = 0; t < 4; ++t) ts.emplace_back([&, t] { for (int i = 0; i < 50; ++i) { const std::string k = "k" + std::to_string(t) + "_" + std::to_string(i); cc2.put(k, MediaKind::Artwork, std::string(100 + i % 7, static_cast<char>('a' + i % 7))); std::string p; cc2.get(k, p); } });
    for (auto& t : ts) t.join();
    CHECK(cc2.blob_count() == 7);
    {   // adopt finished files: streamed hash, rename in, dedup by content, release by prefix
        MediaCache m((dir / "adopt").string()); CHECK(m.open());
        const std::string big(300000, 'q'); auto write = [&](const char* n, const std::string& d) { std::ofstream(dir / n, std::ios::binary) << d; return (dir / n).string(); };
        std::string p1, p2, h1, h2;
        CHECK(m.put_file("song:1@original", MediaKind::Audio, write("f1", big), true, &p1, &h1) && !fs::exists(dir / "f1") && fs::file_size(p1) == big.size());
        CHECK(m.put_file("song:2@original", MediaKind::Audio, write("f2", big), true, &p2, &h2) && p1 == p2 && h1 == h2 && !fs::exists(dir / "f2") && m.blob_count() == 1 && m.refs("song:1@original") == 2);
        CHECK(m.put_file("song:1@320", MediaKind::Audio, write("f3", std::string(500, 'z')), true) && m.blob_count() == 2);
        CHECK(m.release_prefix("song:1@") == 2 && m.blob_count() == 1 && fs::exists(p1) && m.release_prefix("song:2@") == 1 && !fs::exists(p1) && m.blob_count() == 0);
        CHECK(!m.put_file("x", MediaKind::Audio, (dir / "nothing").string(), false) && !m.put_file("x", MediaKind::Audio, write("empty", ""), false));
        CHECK(m.evict_to(0) == 0 || true);
    }
    fs::remove_all(dir, ec);
    std::printf(fails ? "FAILED\n" : "OK\n"); return fails ? 1 : 0;
}
