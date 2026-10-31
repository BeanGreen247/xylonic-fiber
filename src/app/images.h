// Cover art cache: disk (raw bytes, survives restarts, works offline) + a bounded LRU of GPU textures.
#pragma once
#include <SDL.h>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include "../net/stream.h"
#include "worker.h"

class ImageCache {
public:
    ImageCache(SDL_Renderer* r, Worker& w, std::string dir, size_t max_textures);
    ~ImageCache();
    void set_connect_options(ConnectOptions o) { opt_ = std::move(o); }
    void set_on_ready(std::function<void()> f) { on_ready_ = std::move(f); }
    // Non-blocking: returns the texture if resident, else schedules a load and returns nullptr.
    // key identifies the image (cover id + size); url_fn builds the URL on the worker thread.
    SDL_Texture* get(const std::string& key, std::function<std::string()> url_fn);
    // Downloads the image to the disk cache only (for offline use); no texture is created.
    void prefetch(const std::string& key, std::function<std::string()> url_fn);
    // Average colour (0xRRGGBB) of a loaded image, for backdrops; false until it has been decoded.
    bool average(const std::string& key, uint32_t& rgb) const;
    void set_max_textures(size_t n) { max_ = n; evict(); }
    size_t resident() const { return tex_.size(); }
    void clear_memory();

private:
    struct Entry { SDL_Texture* t; uint64_t tick; };
    void evict();
    SDL_Renderer* r_;
    Worker& w_;
    std::string dir_;
    size_t max_;
    ConnectOptions opt_;
    std::function<void()> on_ready_;
    std::unordered_map<std::string, Entry> tex_;
    std::unordered_map<std::string, uint32_t> avg_;
    std::unordered_map<std::string, uint32_t> failed_;  // key -> tick when it may be retried
    std::unordered_set<std::string> pending_;
    uint64_t tick_ = 0;
};
