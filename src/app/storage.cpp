#include "storage.h"
#include <SDL.h>
#include <cstdio>
#include <filesystem>
#include <system_error>
#include <sys/statvfs.h>
#include <sys/stat.h>
#include <unistd.h>

namespace fs = std::filesystem;

bool disk_space(const std::string& path, uint64_t& free_bytes, uint64_t& total_bytes) {
    struct statvfs sv{};
    if (::statvfs(path.c_str(), &sv) != 0) return false;
    free_bytes = static_cast<uint64_t>(sv.f_bavail) * sv.f_frsize;
    total_bytes = static_cast<uint64_t>(sv.f_blocks) * sv.f_frsize;
    return true;
}

std::vector<StorageVolume> storage_volumes() {
    std::vector<StorageVolume> v;
#if defined(__ANDROID__)
    const char* in = SDL_AndroidGetInternalStoragePath();
    if (in) { StorageVolume s; s.label = "Internal storage"; s.path = std::string(in) + "/"; disk_space(s.path, s.free_bytes, s.total_bytes); v.push_back(s); }
    if (SDL_AndroidGetExternalStorageState() & SDL_ANDROID_EXTERNAL_STORAGE_WRITE) {
        const char* ex = SDL_AndroidGetExternalStoragePath();
        if (ex && (!in || std::string(ex) != in)) { StorageVolume s; s.label = "SD card / external"; s.path = std::string(ex) + "/"; s.removable = true; disk_space(s.path, s.free_bytes, s.total_bytes); v.push_back(s); }
    }
#endif
    return v;
}

static std::string quote(const std::string& s) {
    std::string r = "'";
    for (char c : s) { if (c == '\'') r += "'\\''"; else r += c; }
    return r + "'";
}

std::string pick_folder(const std::string& start) {
#if defined(__ANDROID__) || defined(__IPHONEOS__) || defined(_WIN32)
    (void)start;
    return {};
#else
    std::string cmd;
#if defined(__APPLE__)
    cmd = "osascript -e 'POSIX path of (choose folder)' 2>/dev/null";
    (void)start;
#else
    if (::access("/usr/bin/zenity", X_OK) == 0) cmd = "zenity --file-selection --directory --title='Cache location' --filename=" + quote(start) + " 2>/dev/null";
    else if (::access("/usr/bin/kdialog", X_OK) == 0) cmd = "kdialog --getexistingdirectory " + quote(start) + " 2>/dev/null";
    else return {};
#endif
    FILE* f = popen(cmd.c_str(), "r");
    if (!f) return {};
    char buf[4096]; std::string out;
    while (size_t n = fread(buf, 1, sizeof buf, f)) out.append(buf, n);
    pclose(f);
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
    if (!out.empty() && out.back() != '/') out += '/';
    return out;
#endif
}

uint64_t tree_bytes(const std::string& dir) {
    std::error_code ec;
    uint64_t t = 0;
    for (fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (it->is_regular_file(e2)) { const auto n = it->file_size(e2); if (!e2) t += n; }
    }
    return t;
}

bool move_tree(const std::string& src, const std::string& dst, std::atomic<uint64_t>& progress, std::string& error) {
    std::error_code ec;
    if (!fs::exists(src, ec)) return true;
    if (fs::equivalent(src, dst, ec)) return true;
    fs::create_directories(dst, ec);
    if (ec) { error = "Cannot create " + dst + ": " + ec.message(); return false; }
    std::vector<fs::path> copied;
    for (fs::recursive_directory_iterator it(src, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        const fs::path rel = fs::relative(it->path(), src, e2);
        const fs::path to = fs::path(dst) / rel;
        if (it->is_directory(e2)) { fs::create_directories(to, e2); continue; }
        if (!it->is_regular_file(e2)) continue;
        const auto sz = it->file_size(e2);
        fs::copy_file(it->path(), to, fs::copy_options::overwrite_existing, e2);
        if (e2) { error = "Copy failed: " + e2.message(); return false; }
        progress += sz;
        copied.push_back(it->path());
    }
    if (ec) { error = "Read failed: " + ec.message(); return false; }
    for (auto& p : copied) fs::remove(p, ec);
    fs::remove_all(src, ec);
    return true;
}

std::string heal_path(const std::string& p, const std::string& marker, const std::string& dir) {
    if (dir.empty() || p.compare(0, dir.size(), dir) == 0) return p;
    const size_t k = p.rfind(marker);
    if (k == std::string::npos) return p;
    return dir + p.substr(k + marker.size());
}

std::string resolve_cache_root(const std::string& root, const std::string& fallback) {
    if (root.empty()) return fallback;
    std::error_code ec;
    if (!fs::is_directory(root, ec)) { fs::create_directories(root, ec); if (ec || !fs::is_directory(root, ec)) return fallback; }
    return root.back() == '/' ? root : root + "/";
}
