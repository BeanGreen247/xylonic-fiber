// move_tree, heal_path, resolve_cache_root, tree_bytes.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include "../src/app/storage.h"

static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)
namespace fs = std::filesystem;

int main() {
    const fs::path root = fs::temp_directory_path() / "xf_storage_test";
    fs::remove_all(root);
    fs::create_directories(root / "a/downloads/acct/media");
    { std::ofstream(root / "a/downloads/acct/s1.mp3") << std::string(1000, 'x'); }
    { std::ofstream(root / "a/downloads/acct/media/m.bin") << std::string(500, 'y'); }
    CHECK(tree_bytes((root / "a/downloads").string()) == 1500);
    std::atomic<uint64_t> prog{0}; std::string err;
    CHECK(move_tree((root / "a/downloads").string(), (root / "b/downloads").string(), prog, err));
    CHECK(prog == 1500);
    CHECK(fs::exists(root / "b/downloads/acct/s1.mp3") && fs::exists(root / "b/downloads/acct/media/m.bin"));
    CHECK(!fs::exists(root / "a/downloads"));
    CHECK(move_tree((root / "missing").string(), (root / "c").string(), prog, err));   // nothing to move is fine
    const std::string m = "/downloads/acct/", d = (root / "b/downloads/acct/").string();
    CHECK(heal_path("/old/place/downloads/acct/s1.mp3", m, d) == d + "s1.mp3");
    CHECK(heal_path(d + "s1.mp3", m, d) == d + "s1.mp3");
    CHECK(heal_path("/unrelated/s1.mp3", m, d) == "/unrelated/s1.mp3");
    CHECK(resolve_cache_root("", "/fb/") == "/fb/");
    CHECK(resolve_cache_root((root / "new").string(), "/fb/") == (root / "new").string() + "/");
    CHECK(resolve_cache_root("/proc/nope/x", "/fb/") == "/fb/");
    fs::remove_all(root);
    std::printf(fails ? "FAILED\n" : "ok\n");
    return fails;
}
