#include "storage.h"
#include <cstdlib>
#include <SDL.h>
#if defined(__ANDROID__)
#include "android_bridge.h"
#endif
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
    const auto flat = android_bridge::volumes();   // every mounted volume: internal, SD card, USB OTG
    for (size_t i = 0; i + 2 < flat.size(); i += 3) {
        StorageVolume sv; sv.label = flat[i]; sv.path = flat[i + 1]; if (sv.path.empty()) continue;
        if (sv.path.back() != '/') sv.path += '/';
        sv.removable = flat[i + 2] == "1";
        disk_space(sv.path, sv.free_bytes, sv.total_bytes);
        v.push_back(sv);
    }
    if (v.empty()) {   // bridge unavailable: SDL's two paths
        const char* in = SDL_AndroidGetInternalStoragePath();
        if (in) { StorageVolume sv; sv.label = "Internal storage"; sv.path = std::string(in) + "/"; disk_space(sv.path, sv.free_bytes, sv.total_bytes); v.push_back(sv); }
        if (SDL_AndroidGetExternalStorageState() & SDL_ANDROID_EXTERNAL_STORAGE_WRITE) {
            const char* ex = SDL_AndroidGetExternalStoragePath();
            if (ex && (!in || std::string(ex) != in)) { StorageVolume sv; sv.label = "SD card / external"; sv.path = std::string(ex) + "/"; sv.removable = true; disk_space(sv.path, sv.free_bytes, sv.total_bytes); v.push_back(sv); }
        }
    }
#endif
    return v;
}

static std::string quote(const std::string& s) {
    std::string r = "'";
    for (char c : s) { if (c == '\'') r += "'\\''"; else r += c; }
    return r + "'";
}

// True when `name` is an executable on PATH (zenity/kdialog live in /usr/local/bin, Nix profiles, Flatpak runtimes, not only /usr/bin).
static bool on_path(const char* name) {
    const char* path = std::getenv("PATH");
    std::string p = path ? path : "/usr/bin:/bin:/usr/local/bin";
    size_t pos = 0;
    while (pos <= p.size()) {
        size_t e = p.find(':', pos);
        if (e == std::string::npos) e = p.size();
        if (e > pos && ::access((p.substr(pos, e - pos) + "/" + name).c_str(), X_OK) == 0) return true;
        pos = e + 1;
    }
    return false;
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
    if (on_path("zenity")) cmd = "zenity --file-selection --directory --title='Cache location' --filename=" + quote(start) + " 2>/dev/null";
    else if (on_path("kdialog")) cmd = "kdialog --getexistingdirectory " + quote(start) + " 2>/dev/null";
    else return {};
#endif
    FILE* f = popen(cmd.c_str(), "r");
    if (!f) return {};
    char buf[4096] = {}; std::string out;
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

static bool same_device(const std::string& a, const std::string& b) {
    struct stat x{}, y{};
    return ::stat(a.c_str(), &x) == 0 && ::stat(b.c_str(), &y) == 0 && x.st_dev == y.st_dev;
}

bool move_tree(const std::string& src, const std::string& dst, std::atomic<uint64_t>& progress, std::string& error, const std::atomic<bool>* cancel) {
    std::error_code ec;
    if (!fs::exists(src, ec)) return true;
    fs::create_directories(dst, ec);
    if (ec) { error = "Cannot create " + dst + ": " + ec.message(); return false; }
    if (fs::equivalent(src, dst, ec)) return true;
    const bool same_fs = same_device(src, dst);
    std::vector<fs::path> files, dirs;
    for (fs::recursive_directory_iterator it(src, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (it->is_directory(e2)) dirs.push_back(it->path());
        else if (it->is_regular_file(e2)) files.push_back(it->path());
    }
    if (ec) { error = "Read failed: " + ec.message(); return false; }
    for (auto& d : dirs) { std::error_code e2; fs::create_directories(fs::path(dst) / fs::relative(d, src, e2), e2); }
    for (auto& f : files) {
        if (cancel && cancel->load()) { error = "Cancelled. Files already moved stay in the new place."; return false; }
        std::error_code e2;
        const fs::path to = fs::path(dst) / fs::relative(f, src, e2);
        const auto sz = fs::file_size(f, e2);
        if (same_fs) {
            fs::rename(f, to, e2);
            if (!e2) { progress += sz; continue; }
        }
        const fs::path part = to.string() + ".part";
        fs::copy_file(f, part, fs::copy_options::overwrite_existing, e2);
        if (e2) { fs::remove(part, e2); error = "Copy failed: " + e2.message() + (e2 == std::errc::no_space_on_device ? " (not enough space)" : ""); return false; }
        std::error_code e3;
        if (fs::file_size(part, e3) != sz || e3) { fs::remove(part, e3); error = "Copy of " + f.filename().string() + " did not finish (storage removed?)"; return false; }
        fs::rename(part, to, e2);
        if (e2) { fs::remove(part, e2); error = "Could not place " + to.filename().string() + ": " + e2.message(); return false; }
        fs::remove(f, e2);
        progress += sz;
    }
    std::error_code e4;
    fs::remove_all(src, e4);
    return true;
}

bool pick_folder_async_supported() {
#if defined(__ANDROID__)
    return true;
#else
    return false;
#endif
}
void pick_folder_async_start() {
#if defined(__ANDROID__)
    android_bridge::pick_start();
#endif
}
bool pick_folder_async_poll(std::string& out) {
#if defined(__ANDROID__)
    const std::string r = android_bridge::pick_poll();
    if (r.empty()) return false;
    out = r == "!" ? std::string() : r;
    return true;
#else
    out.clear();
    return true;
#endif
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
