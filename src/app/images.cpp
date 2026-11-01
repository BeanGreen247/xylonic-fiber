#include "images.h"
#if defined(__ANDROID__)
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "../../third_party/stb_image.h"
#else
#include <SDL_image.h>
#endif
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <unistd.h>
#include <vector>
#include "../net/http.h"

namespace {
std::string hash_name(const std::string& k) {  // FNV-1a 64
    uint64_t h = 1469598103934665603ULL;
    for (unsigned char c : k) { h ^= c; h *= 1099511628211ULL; }
    char b[24];
    std::snprintf(b, sizeof b, "%016llx", static_cast<unsigned long long>(h));
    return b;
}
bool read_file(const std::string& p, std::string& out) {
    FILE* f = std::fopen(p.c_str(), "rb");
    if (!f) return false;
    char buf[16384];
    size_t n;
    out.clear();
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
    std::fclose(f);
    return !out.empty();
}
bool write_file(const std::string& p, const std::string& d) {
    std::string tmp = p + ".tmp";
    FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) return false;
    bool ok = std::fwrite(d.data(), 1, d.size(), f) == d.size();
    ok = std::fclose(f) == 0 && ok;
    return ok && std::rename(tmp.c_str(), p.c_str()) == 0;
}
}  // namespace

ImageCache::ImageCache(SDL_Renderer* r, Worker& w, std::string dir, size_t max) : r_(r), w_(w), dir_(std::move(dir)), max_(max) {
#if !defined(__ANDROID__)
    IMG_Init(IMG_INIT_JPG | IMG_INIT_PNG | IMG_INIT_WEBP);
#endif
}
ImageCache::~ImageCache() { clear_memory();
#if !defined(__ANDROID__)
    IMG_Quit();
#endif
}

void ImageCache::clear_memory() {
    for (auto& kv : tex_) SDL_DestroyTexture(kv.second.t);
    tex_.clear();
}

int ImageCache::clear_disk() {
    DIR* dir = ::opendir(dir_.c_str());
    if (!dir) return 0;
    int removed = 0;
    while (dirent* e = ::readdir(dir)) {
        if (e->d_name[0] == '.') continue;
        std::string name(e->d_name);
        if (name.size() < 4 || name.compare(name.size() - 4, 4, ".img") != 0) continue;
        if (::unlink((dir_ + name).c_str()) == 0) ++removed;
    }
    ::closedir(dir);
    avg_.clear(); failed_.clear();
    return removed;
}

void ImageCache::evict() {
    while (tex_.size() > max_) {
        auto oldest = tex_.begin();
        for (auto it = tex_.begin(); it != tex_.end(); ++it) if (it->second.tick < oldest->second.tick) oldest = it;
        SDL_DestroyTexture(oldest->second.t);
        tex_.erase(oldest);
    }
}

std::string ImageCache::disk_path(const std::string& key) const { return dir_ + hash_name(key) + ".img"; }

bool ImageCache::average(const std::string& key, uint32_t& rgb) const {
    auto it = avg_.find(key);
    if (it == avg_.end()) return false;
    rgb = it->second;
    return true;
}

SDL_Texture* ImageCache::get(const std::string& key, std::function<std::string()> url_fn) {
    auto it = tex_.find(key);
    if (it != tex_.end()) { it->second.tick = ++tick_; return it->second.t; }
    if (pending_.count(key)) return nullptr;
    auto f = failed_.find(key);
    if (f != failed_.end() && SDL_GetTicks() < f->second) return nullptr;
    pending_.insert(key);
    std::string path = dir_ + hash_name(key) + ".img";
    ConnectOptions opt = opt_;
    w_.post([this, key, path, opt, url_fn = std::move(url_fn)] {
        std::string bytes;
        bool have = read_file(path, bytes);
        if (!have) {
            HttpResult r = http_get_string(url_fn(), bytes, opt, 8u << 20);
            // Do not trust Content-Type: Navidrome and compatible servers
            // commonly return WebP as image/octet-stream or omit the header.
            have = r.error.empty() && r.status == 200 && !bytes.empty();
        }
        int w = 0, h = 0;
        SDL_Surface* decoded = nullptr;
#if !defined(__ANDROID__)
        if (have) {
            SDL_RWops* rw = SDL_RWFromConstMem(bytes.data(), static_cast<int>(bytes.size()));
            if (rw) decoded = IMG_Load_RW(rw, 1);
        }
#endif
        SDL_Surface* rgba = decoded ? SDL_ConvertSurfaceFormat(decoded, SDL_PIXELFORMAT_RGBA32, 0) : nullptr;
        if (decoded) SDL_FreeSurface(decoded);
        std::vector<uint8_t> data;
        uint32_t avg = 0;
#if defined(__ANDROID__)
        int n = 0;
        unsigned char* px = have ? stbi_load_from_memory(reinterpret_cast<const unsigned char*>(bytes.data()), static_cast<int>(bytes.size()), &w, &h, &n, 4) : nullptr;
        if (px) { data.assign(px, px + static_cast<size_t>(w) * h * 4); stbi_image_free(px); }
#else
        if (rgba) {
            w = rgba->w; h = rgba->h;
            data.resize(static_cast<size_t>(w) * h * 4);
            for (int y = 0; y < h; ++y)
                std::memcpy(data.data() + static_cast<size_t>(y) * w * 4,
                            static_cast<const uint8_t*>(rgba->pixels) + static_cast<size_t>(y) * rgba->pitch,
                            static_cast<size_t>(w) * 4);
            uint64_t r = 0, g = 0, b = 0, n = 0;  // sample a 16x16 grid
            for (int yy = 0; yy < 16; ++yy)
                for (int xx = 0; xx < 16; ++xx) {
                    const uint8_t* q = &data[(static_cast<size_t>(yy * h / 16) * w + static_cast<size_t>(xx * w / 16)) * 4];
                    r += q[0]; g += q[1]; b += q[2]; ++n;
                }
            avg = (static_cast<uint32_t>(r / n) << 16) | (static_cast<uint32_t>(g / n) << 8) | static_cast<uint32_t>(b / n);
            SDL_FreeSurface(rgba);
            if (!read_file(path, bytes)) write_file(path, bytes);
        }
#endif
        if (!data.empty()) {
            uint64_t r = 0, g = 0, b = 0, n = 0;
            for (int yy = 0; yy < 16; ++yy) for (int xx = 0; xx < 16; ++xx) {
                const uint8_t* q = &data[(static_cast<size_t>(yy * h / 16) * w + static_cast<size_t>(xx * w / 16)) * 4];
                r += q[0]; g += q[1]; b += q[2]; ++n;
            }
            avg = (static_cast<uint32_t>(r / n) << 16) | (static_cast<uint32_t>(g / n) << 8) | static_cast<uint32_t>(b / n);
#if defined(__ANDROID__)
            if (!read_file(path, bytes)) write_file(path, bytes);
#endif
        }
        ui_post([this, key, data = std::move(data), w, h, avg] {
            pending_.erase(key);
            if (data.empty()) { failed_[key] = SDL_GetTicks() + 30000; return; }
            SDL_Texture* t = SDL_CreateTexture(r_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, w, h);
            if (!t) return;
            SDL_UpdateTexture(t, nullptr, data.data(), w * 4);
            SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
            SDL_SetTextureScaleMode(t, SDL_ScaleModeLinear);
            tex_[key] = {t, ++tick_};
            avg_[key] = avg;
            evict();
            if (on_ready_) on_ready_();
        });
    });
    return nullptr;
}

void ImageCache::prefetch(const std::string& key, std::function<std::string()> url_fn) {
    std::string path = dir_ + hash_name(key) + ".img";
    ConnectOptions opt = opt_;
    w_.post([path, opt, url_fn = std::move(url_fn)] {
        std::string bytes;
        if (read_file(path, bytes)) return;
        HttpResult r = http_get_string(url_fn(), bytes, opt, 8u << 20);
        if (r.error.empty() && r.status == 200 && !bytes.empty()) {
#if !defined(__ANDROID__)
            SDL_RWops* rw = SDL_RWFromConstMem(bytes.data(), static_cast<int>(bytes.size()));
            SDL_Surface* decoded = rw ? IMG_Load_RW(rw, 1) : nullptr;
            if (decoded) { SDL_FreeSurface(decoded); write_file(path, bytes); }
#else
            int w = 0, h = 0, n = 0;
            unsigned char* px = stbi_load_from_memory(reinterpret_cast<const unsigned char*>(bytes.data()), static_cast<int>(bytes.size()), &w, &h, &n, 4);
            if (px) { stbi_image_free(px); write_file(path, bytes); }
#endif
        }
    });
}
