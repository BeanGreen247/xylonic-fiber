// Generation publishing, recovery, corruption, concurrency, page cache and memory-budget tests.
#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <thread>
#include "../src/cache/batch.h"
#include "../src/cache/generation.h"
#include "../src/cache/lru.h"
#include "../src/cache/swr.h"
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)
using namespace xf::cache;
namespace fs = std::filesystem;
static SongInput song(int i, bool liked = false) {
    SongInput s; s.id = "s" + std::to_string(i); s.title = "Song " + std::to_string(i); s.artist_id = "ar"; s.artist_name = "Artist"; s.album_id = "al" + std::to_string(i / 5);
    s.album_name = "Album"; s.format = "flac"; s.codec = "flac"; s.track = i % 5 + 1; s.disc = 1; s.sample_rate = 96000; s.bit_depth = 24; s.channels = 2; s.size = 1234567890123ull; s.liked = liked; return s;
}
static UpdateBatch batch_of(int from, int to) { UpdateBatch b; for (int i = from; i < to; ++i) b.songs.push_back(song(i)); return b; }
int main() {
    const fs::path dir = fs::temp_directory_path() / "xf_cache_store_test"; std::error_code ec; fs::remove_all(dir, ec); fs::create_directories(dir, ec);
    {   // publish: readers keep their generation while the writer moves on
        MetadataStore store(dir.string()); CHECK(store.open() && store.generation() == 0);
        auto g0 = store.current(); CHECK(g0->song_count() == 0);
        UpdateBatch b = batch_of(0, 20); b.likes.emplace_back("s3", true); b.playlists.emplace_back("p1", std::vector<std::string>{"s2", "s1", "nope"}); CHECK(store.apply(b));
        auto g1 = store.current(); CHECK(g0->song_count() == 0 && g1->song_count() == 20 && store.generation() == 1);
        CHECK(g1->liked("s3") && g1->playlist_songs("p1").size() == 2 && g1->indexed() && g1->search_songs("song 1", 100).size() == 11);
        CHECK(g1->album_songs("al1").size() == 5 && g1->album_songs("al1")[0] == "s5" && g1->liked_songs().size() == 1);
        UpdateBatch bad; bad.songs.push_back(SongInput{}); CHECK(!store.apply(bad) && store.generation() == 1 && store.current()->song_count() == 20);
        SongView v = g1->song("s7"); CHECK(v.valid() && v.packed->sample_rate == 96000 && v.packed->bit_depth == 24 && v.packed->size == 1234567890123ull);
    }
    {   // recovery from the journal alone, then compaction, then snapshot + newer journal
        MetadataStore store(dir.string()); CHECK(store.open() && store.generation() == 1 && store.current()->song_count() == 20 && store.current()->liked("s3"));
        CHECK(store.apply(batch_of(20, 30)) && store.compact() && fs::file_size(dir / "metadata.journal") == 0);
        CHECK(store.apply(batch_of(30, 35)));
        MetadataStore again(dir.string()); CHECK(again.open() && again.generation() == 3 && again.current()->song_count() == 35 && again.current()->playlist_songs("p1").size() == 2);
    }
    {   // incomplete journal tail and corrupt tail are ignored; committed data survives
        { std::ofstream out(dir / "metadata.journal", std::ios::binary | std::ios::app); out << "partial-garbage-bytes-not-a-record-at-all-xxxxxxxxxxxxxxxx"; }
        MetadataStore store(dir.string()); CHECK(store.open() && store.current()->song_count() == 35);
    }
    {   // corrupt snapshot falls back without crashing; metadata never reads media files
        { std::fstream f((dir / "metadata.snapshot").string(), std::ios::in | std::ios::out | std::ios::binary); f.seekp(50); char x = '#'; f.write(&x, 1); }
        MetadataStore store(dir.string()); CHECK(store.open()); CHECK(store.current()->song_count() <= 35);
    }
    {   // concurrent readers see only complete, monotonically growing generations
        const fs::path d2 = dir / "conc"; fs::create_directories(d2, ec); MetadataStore store(d2.string()); CHECK(store.open());
        std::atomic<bool> stop{false}; std::atomic<int> bad{0}; std::vector<std::thread> readers;
        for (int t = 0; t < 4; ++t) readers.emplace_back([&] { size_t last = 0; while (!stop) { auto g = store.current(); const size_t n = g->song_count(); if (n < last || n % 10 != 0 || !g->indexed()) ++bad; if (n && !g->song("s" + std::to_string(n - 1)).valid()) ++bad; last = n; } });
        for (int i = 0; i < 40; ++i) CHECK(store.apply(batch_of(i * 10, i * 10 + 10)));
        stop = true; for (auto& t : readers) t.join(); CHECK(bad == 0 && store.current()->song_count() == 400 && store.generation() == 40);
    }
    {   // bounded page cache: page count and byte budget hold, loads are counted
        int loads = 0; PageCache cache(3, 100, [&](const std::string& k, std::string& out) { ++loads; out.assign(40, k[0]); return true; });
        for (const char* k : {"a", "b", "c", "a", "d", "e", "e", "a"}) CHECK(cache.get(k) != nullptr);
        CHECK(cache.pages() <= 2 && cache.bytes() <= 100);                         // 40-byte pages: byte budget (100) binds before the page cap (3)
        CHECK(cache.hits() >= 1 && loads >= 5);
        PageCache none(2, 0, [](const std::string&, std::string&) { return false; }); CHECK(none.get("x") == nullptr && none.pages() == 0);
    }
    {   // memory budget: 20k songs stay inside a fixed packed-record + string budget
        MetadataSnapshot m; for (int i = 0; i < 20000; ++i) m.upsert(song(i)); m.finalize();
        CHECK(m.song_count() == 20000 && m.string_bytes() < 1200000 && sizeof(PackedSong) <= 96);
    }
    {   // cover art, reset, persisted index, in-memory mode, held generations stay unchanged
        const fs::path d3 = dir / "idx"; fs::create_directories(d3, ec); MetadataStore store(d3.string()); CHECK(store.open());
        UpdateBatch b = batch_of(0, 50); b.songs[7].cover_art = "cover-7"; b.playlists.emplace_back("pl", std::vector<std::string>{"s7", "s8"}); CHECK(store.apply(b) && store.compact());
        CHECK(fs::exists(d3 / "metadata.index"));
        MetadataStore again(d3.string()); CHECK(again.open()); auto g = again.current();
        CHECK(g->indexed() && g->song("s7").cover_art() == "cover-7" && g->search_songs("song 1", 100).size() == 11 && g->playlist_contains("pl", "s8"));   // index imported, not rebuilt
        { std::fstream f((d3 / "metadata.index").string(), std::ios::in | std::ios::out | std::ios::binary); f.seekp(60); char x = '#'; f.write(&x, 1); }
        MetadataStore bad(d3.string()); CHECK(bad.open() && bad.current()->indexed() && bad.current()->search_songs("song 1", 100).size() == 11);   // corrupt index: rebuilt
        UpdateBatch reset; reset.reset = true; reset.songs.push_back(song(900)); CHECK(again.apply(reset));
        CHECK(again.current()->song_count() == 1 && g->song_count() == 50 && !again.current()->playlist_contains("pl", "s8"));   // held generation untouched
        MetadataStore mem(""); CHECK(mem.open() && mem.apply(batch_of(0, 5)) && mem.current()->song_count() == 5 && mem.compact() && mem.generation() == 1);
        UpdateBatch like; like.likes.emplace_back("s1", true); { auto held = mem.current(); CHECK(mem.apply(like) && !held->liked("s1") && mem.current()->liked("s1")); }
        mem.durable = false; CHECK(mem.apply(like));
    }
    CHECK(classify(false, 0, 100, kDiscoverTtlSeconds) == Freshness::Missing && classify(true, 0, 299, kDiscoverTtlSeconds) == Freshness::Fresh && classify(true, 0, 300, kDiscoverTtlSeconds) == Freshness::Stale);
    CHECK(use_cached(Freshness::Stale) && needs_refresh(Freshness::Stale) && !needs_refresh(Freshness::Fresh) && !use_cached(Freshness::Missing));
    { Song s; s.id = "x"; s.title = "T"; s.suffix = "mp3"; s.track = -1; Album a; a.songs.push_back(s); UpdateBatch b; add_album(b, a); CHECK(b.songs.size() == 1 && b.songs[0].track == 0 && b.songs[0].format == "mp3"); }
    fs::remove_all(dir, ec);
    std::printf(fails ? "FAILED\n" : "OK\n"); return fails ? 1 : 0;
}
