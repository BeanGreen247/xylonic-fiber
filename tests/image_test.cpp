// The "no image" artist star is recognised by id prefix + size + SHA-256 (as in the original imageCacheService),
// using the 546-byte file a real Navidrome returns for an artist without a photo.
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include "../src/app/images.h"
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)
int main(int, char** argv) {
    std::string dir = argv[0]; dir = dir.substr(0, dir.find_last_of('/') + 1);
    std::ifstream f(dir + "../tests/fixtures/navidrome_artist_placeholder.webp", std::ios::binary);
    if (!f) f.open("tests/fixtures/navidrome_artist_placeholder.webp", std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    CHECK(bytes.size() == 546);
    CHECK(ImageCache::is_placeholder("ar-0kRW9yashrcPrH9wKQwSND_0", bytes));
    CHECK(!ImageCache::is_placeholder("al-0kRW9yashrcPrH9wKQwSND_0", bytes));   // only artist ids are checked
    CHECK(!ImageCache::is_placeholder("hi:ar-1", bytes));                       // hi-res copies are never placeholders
    std::string other = bytes; other[100] ^= 0x01;
    CHECK(!ImageCache::is_placeholder("ar-1", other));                          // same size, different bytes: a real image
    CHECK(!ImageCache::is_placeholder("ar-1", bytes.substr(0, 545)));
    std::printf(fails ? "FAILED\n" : "OK\n"); return fails ? 1 : 0;
}
