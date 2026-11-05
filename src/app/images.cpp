#include "images.h"
#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif
#if defined(__ANDROID__) || (defined(__APPLE__) && TARGET_OS_IPHONE)
#define XF_STB_IMAGES 1   // no SDL_image on phones: stb decodes JPEG and PNG
#endif
#if defined(XF_STB_IMAGES)
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "../../third_party/stb_image.h"
#else
#include <SDL_image.h>
#endif
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>
#include <mbedtls/sha256.h>
#include "../../third_party/yyjson.h"
#include "../net/http.h"

namespace {
// Navidrome answers getCoverArt for an artist with no photo by serving one fixed 546-byte webp (Last.fm's white
// "no image" star) at every size; identified by size + SHA-256 so a real image is never mistaken for it.
constexpr size_t kPlaceholderSize = 546;
const char* kPlaceholderSha256 = "521259c652af6d43cb124876106258d93b6b6f7fbe7ad2c73e8345875afe4dcf";
constexpr int64_t kArtistRevalidateSec = 7 * 24 * 3600;   // artist photos change server side; albums keep their 1-year life

bool is_artist_id(const std::string& id) { return id.compare(0, 3, "ar-") == 0; }

bool is_placeholder_bytes(const std::string& id, const std::string& bytes) {
    if (!is_artist_id(id) || bytes.size() != kPlaceholderSize) return false;
    unsigned char digest[32];
    mbedtls_sha256(reinterpret_cast<const unsigned char*>(bytes.data()), bytes.size(), digest, 0);
    char hex[65]; for (int i = 0; i < 32; ++i) std::snprintf(hex + i * 2, 3, "%02x", digest[i]);
    return std::strcmp(hex, kPlaceholderSha256) == 0;
}

std::string hash_name(const std::string& k) {  // FNV-1a 64, for legacy .img names
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

// Decodes any supported image to tightly packed RGBA. False when the bytes are not an image.
bool decode_rgba(const std::string& bytes, std::vector<uint8_t>& data, int& w, int& h) {
    if (bytes.empty()) return false;
#if defined(XF_STB_IMAGES)
    int n = 0;
    unsigned char* px = stbi_load_from_memory(reinterpret_cast<const unsigned char*>(bytes.data()), static_cast<int>(bytes.size()), &w, &h, &n, 4);
    if (!px) return false;
    data.assign(px, px + static_cast<size_t>(w) * h * 4); stbi_image_free(px); return true;
#else
    SDL_RWops* rw = SDL_RWFromConstMem(bytes.data(), static_cast<int>(bytes.size()));
    SDL_Surface* decoded = rw ? IMG_Load_RW(rw, 1) : nullptr;
    SDL_Surface* rgba = decoded ? SDL_ConvertSurfaceFormat(decoded, SDL_PIXELFORMAT_RGBA32, 0) : nullptr;
    if (decoded) SDL_FreeSurface(decoded);
    if (!rgba) return false;
    w = rgba->w; h = rgba->h; data.resize(static_cast<size_t>(w) * h * 4);
    for (int y = 0; y < h; ++y)
        std::memcpy(data.data() + static_cast<size_t>(y) * w * 4, static_cast<const uint8_t*>(rgba->pixels) + static_cast<size_t>(y) * rgba->pitch, static_cast<size_t>(w) * 4);
    SDL_FreeSurface(rgba); return true;
#endif
}

// Box-filter downscale so a 300 px (or 2000 px) source costs only the texture memory of the size it is drawn at.
void downscale_rgba(std::vector<uint8_t>& data, int& w, int& h, int max_edge) {
    const int edge = std::max(w, h);
    if (max_edge <= 0 || edge <= max_edge + max_edge / 4) return;
    const float f = static_cast<float>(max_edge) / edge;
    const int nw = std::max(1, static_cast<int>(w * f)), nh = std::max(1, static_cast<int>(h * f));
    std::vector<uint8_t> out(static_cast<size_t>(nw) * nh * 4);
    for (int y = 0; y < nh; ++y) {
        const int y0 = y * h / nh, y1 = std::max(y0 + 1, (y + 1) * h / nh);
        for (int x = 0; x < nw; ++x) {
            const int x0 = x * w / nw, x1 = std::max(x0 + 1, (x + 1) * w / nw);
            uint32_t acc[4] = {0, 0, 0, 0}; int n = 0;
            for (int yy = y0; yy < y1; ++yy) for (int xx = x0; xx < x1; ++xx) { const uint8_t* q = &data[(static_cast<size_t>(yy) * w + xx) * 4]; acc[0] += q[0]; acc[1] += q[1]; acc[2] += q[2]; acc[3] += q[3]; ++n; }
            uint8_t* o = &out[(static_cast<size_t>(y) * nw + x) * 4];
            for (int k = 0; k < 4; ++k) o[k] = static_cast<uint8_t>(acc[k] / n);
        }
    }
    data.swap(out); w = nw; h = nh;
}

uint32_t average_rgb(const std::vector<uint8_t>& data, int w, int h) {  // 16x16 sample grid
    uint64_t r = 0, g = 0, b = 0, n = 0;
    for (int yy = 0; yy < 16; ++yy) for (int xx = 0; xx < 16; ++xx) {
        const uint8_t* q = &data[(static_cast<size_t>(yy * h / 16) * w + static_cast<size_t>(xx * w / 16)) * 4];
        r += q[0]; g += q[1]; b += q[2]; ++n;
    }
    return (static_cast<uint32_t>(r / n) << 16) | (static_cast<uint32_t>(g / n) << 8) | static_cast<uint32_t>(b / n);
}

std::string url_encode_term(const std::string& s) {
    static const char* hex = "0123456789ABCDEF"; std::string o;
    for (unsigned char ch : s) { if (std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~') o += static_cast<char>(ch); else { o += '%'; o += hex[ch >> 4]; o += hex[ch & 15]; } }
    return o;
}

// internetArtworkService.fetchArtFromInternet: iTunes Search, term "artist album", first of up to 5 album results,
// artworkUrl100 upscaled to 600x600bb. Empty on any failure.
std::string itunes_artwork_url(const std::string& artist, const std::string& album, const ConnectOptions& opt) {
    std::string term = artist + " " + album;
    while (!term.empty() && term.back() == ' ') term.pop_back();
    while (!term.empty() && term.front() == ' ') term.erase(term.begin());
    if (term.empty()) return "";
    std::string body;
    HttpResult r = http_get_string("https://itunes.apple.com/search?term=" + url_encode_term(term) + "&entity=album&media=music&limit=5", body, opt, 1u << 20);
    if (!r.error.empty() || r.status != 200) return "";
    yyjson_doc* d = yyjson_read(body.data(), body.size(), 0);
    yyjson_val* res = d ? yyjson_obj_get(yyjson_doc_get_root(d), "results") : nullptr;
    yyjson_val* first = res ? yyjson_arr_get_first(res) : nullptr;
    yyjson_val* art = first ? yyjson_obj_get(first, "artworkUrl100") : nullptr;
    std::string url = art && yyjson_is_str(art) ? yyjson_get_str(art) : "";
    yyjson_doc_free(d);
    const size_t at = url.find("100x100bb");
    if (at != std::string::npos) url.replace(at, 9, "600x600bb");
    return url;
}

uint32_t now_ms() {
    return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}
}  // namespace

bool ImageCache::is_placeholder(const std::string& cover_id, const std::string& bytes) { return is_placeholder_bytes(cover_id, bytes); }

bool ImageCache::fallback_id(const std::string& cover_id) const {
    if (fallback_ids_.count(cover_id)) return true;
    std::lock_guard<std::mutex> lk(const_cast<std::mutex&>(pm_));
    return placeholder_ids_.count(resolve_alias(cover_id)) != 0;
}

ImageCache::ImageCache(SDL_Renderer* r, Worker& w, std::string dir, size_t max) : r_(r), w_(w), dir_(std::move(dir)), max_(max) {
#if !defined(XF_STB_IMAGES)
    IMG_Init(IMG_INIT_JPG | IMG_INIT_PNG | IMG_INIT_WEBP);
#endif
    media_ = std::make_shared<xf::cache::MediaCache>(dir_ + "media");
    media_->open();
    for (int i = 0; i < 4; ++i) threads_.emplace_back([this] { worker_loop(); });
}

ImageCache::~ImageCache() {
    { std::lock_guard<std::mutex> lk(qm_); stop_ = true; queue_.clear(); }
    qcv_.notify_all();
    for (auto& t : threads_) t.join();
    clear_memory(); media_->save();
#if !defined(XF_STB_IMAGES)
    IMG_Quit();
#endif
}

void ImageCache::worker_loop() {
    for (;;) {
        std::function<void()> job;
        {
            std::unique_lock<std::mutex> lk(qm_);
            qcv_.wait(lk, [this] { return stop_ || (!queue_.empty() && active_ < max_active_); });
            if (stop_) return;
            job = std::move(queue_.front()); queue_.pop_front(); ++active_;
        }
        job();
        { std::lock_guard<std::mutex> lk(qm_); --active_; }
        qcv_.notify_all();
    }
}

void ImageCache::clear_memory() {
    for (auto& kv : tex_) SDL_DestroyTexture(kv.second.t);
    tex_.clear();
}

int ImageCache::clear_disk() {
    int removed = static_cast<int>(media_->blob_count());
    media_->evict_to(0); media_->save();
    DIR* dir = ::opendir(dir_.c_str());
    if (dir) {   // pre-manifest files
        while (dirent* e = ::readdir(dir)) {
            std::string name(e->d_name);
            if (name.size() > 4 && name.compare(name.size() - 4, 4, ".img") == 0 && ::unlink((dir_ + name).c_str()) == 0) ++removed;
        }
        ::closedir(dir);
    }
    { std::lock_guard<std::mutex> lk(pm_); placeholder_ids_.clear(); itunes_urls_.clear(); }
    avg_.clear(); fallback_.clear(); fallback_ids_.clear(); retry_.clear(); { std::lock_guard<std::mutex> lk(pm_); revalidated_try_.clear(); }
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

ImageCache::Key ImageCache::parse(const std::string& key) {
    Key k; std::string s = key;
    const size_t at = s.rfind('@');
    if (at != std::string::npos) { k.dev = std::atoi(s.c_str() + at + 1); s.resize(at); }
    if (s.compare(0, 3, "hi:") == 0) { k.hi = true; k.id = s.substr(3); }
    else if (s.compare(0, 4, "net:") == 0) {
        k.net = true; const std::string t = s.substr(4); const size_t sep = t.find("::");
        k.term_artist = t.substr(0, sep); k.term_album = sep == std::string::npos ? "" : t.substr(sep + 2);
    } else k.id = s;
    return k;
}

std::string ImageCache::resolve_alias(const std::string& id) const {
    std::lock_guard<std::mutex> lk(am_);
    auto it = alias_.find(id);
    return it == alias_.end() ? id : it->second;
}

std::string ImageCache::disk_id(const Key& k) const {
    if (k.net) return "";
    return k.hi ? "hi:" + k.id : resolve_alias(k.id);
}

std::string ImageCache::disk_path(const std::string& cover_id) const {
    std::string p;
    return media_->get(resolve_alias(cover_id), p) ? p : std::string();
}

bool ImageCache::on_disk(const std::string& cover_id) const { std::string p; return media_->get(resolve_alias(cover_id), p); }

bool ImageCache::average(const std::string& key, uint32_t& rgb) const {
    auto it = avg_.find(key);
    if (it == avg_.end()) return false;
    rgb = it->second;
    return true;
}

bool ImageCache::fallback(const std::string& key) const {
    if (fallback_.count(key)) return true;
    const Key k = parse(key);
    if (k.net) return false;
    std::lock_guard<std::mutex> lk(const_cast<std::mutex&>(pm_));
    return placeholder_ids_.count(k.hi ? k.id : resolve_alias(k.id)) != 0;
}

void ImageCache::store(const std::string& did, const std::string& bytes) {
    if (did.empty() || !media_->put(did, xf::cache::MediaKind::Artwork, bytes)) return;
    media_->evict_to(budget_);
    if (did.compare(0, 3, "hi:") == 0) media_->prune_prefix("hi:", kHiMaxDisk);
    if (++puts_ % 32 == 0) media_->save();
}

// One GET, honouring the shared 429 back-off. Placeholder stars are recognised here and never stored.
ImageCache::Fetch ImageCache::fetch_bytes(const std::string& did, const std::string& url, bool placeholder_check, std::string& bytes) {
    for (;;) { const uint32_t until = backoff_until_.load(), now = now_ms(); if (until == 0 || static_cast<int32_t>(until - now) <= 0) break; std::this_thread::sleep_for(std::chrono::milliseconds(std::min<uint32_t>(until - now, 200))); }
    if (url.empty()) return Fetch::Failed;
    const size_t cap = did.compare(0, 3, "hi:") == 0 ? (32u << 20) : (8u << 20);
    HttpResult r = http_get_string(url, bytes, opt_, cap);
    if (r.error.empty() && r.status == 429) {
        const int n = ++rate_hits_;
        backoff_until_ = now_ms() + std::min(2000u << std::min(n - 1, 4), 30000u);   // 2 s doubling to 30 s
        return Fetch::RateLimited;
    }
    if (!r.error.empty() || r.status != 200 || bytes.empty()) return Fetch::Failed;
    if (placeholder_check && is_placeholder_bytes(did, bytes)) {
        std::lock_guard<std::mutex> lk(pm_); placeholder_ids_.insert(did);
        return Fetch::Placeholder;
    }
    rate_hits_ = 0; backoff_until_ = 0;
    store(did, bytes);
    return Fetch::Ok;
}

ImageCache::Fetch ImageCache::fetch_to_disk(const std::string& cover_id, const UrlFn& url_fn, bool hi) {
    const std::string did = hi ? "hi:" + cover_id : resolve_alias(cover_id);
    if (on_disk(did)) return Fetch::Ok;
    { std::lock_guard<std::mutex> lk(pm_); if (placeholder_ids_.count(did)) return Fetch::Placeholder; }
    std::string bytes;
    Fetch f = Fetch::Failed;
    for (int attempt = 0; attempt < 3; ++attempt) {   // the original retries a failed cover fetch before giving up
        f = fetch_bytes(did, url_fn(hi ? kHiSize : kStdSize), !hi, bytes);
        if (f == Fetch::Ok || f == Fetch::Placeholder) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(std::min(500 << attempt, 2000)));
    }
    return f;
}

SDL_Texture* ImageCache::get(const std::string& key, UrlFn url_fn, const std::string& artist, const std::string& album) {
    auto it = tex_.find(key);
    if (it != tex_.end()) {
        it->second.tick = ++tick_;
        const uint32_t now = SDL_GetTicks();
        if (now - it->second.stat_ms > 2000) { ++mem_hits_; it->second.stat_ms = now; }   // counted when a tile (re)appears, not per frame
        return it->second.t;
    }
    if (blackout_.load() && key.compare(0, 3, "hi:") != 0) return nullptr;
    if (fallback_.count(key) || pending_.count(key)) return nullptr;
    auto rt = retry_.find(key);
    if (rt != retry_.end() && static_cast<int32_t>(SDL_GetTicks() - rt->second.second) < 0) return nullptr;
    Key k = parse(key);
    if (k.net && (!itunes_ || (artist.empty() && album.empty()))) { fallback_.insert(key); return nullptr; }
    if (!k.net && fallback(key)) return nullptr;
    pending_.insert(key);
    {
        std::lock_guard<std::mutex> lk(qm_);
        queue_.push_back([this, key, k, url_fn = std::move(url_fn), artist, album]() mutable { run_job(key, std::move(k), std::move(url_fn), artist, album); });
    }
    qcv_.notify_one();
    return nullptr;
}

void ImageCache::run_job(const std::string& key, Key k, UrlFn url_fn, std::string artist, std::string album) {
    std::string bytes;
    Fetch st = Fetch::Failed;
    bool have = false;
    if (k.net) {
        ++internet_fetches_;
        const std::string tk = artist + "::" + album;
        std::string url; bool known = false;
        { std::lock_guard<std::mutex> lk(pm_); auto f = itunes_urls_.find(tk); if (f != itunes_urls_.end()) { url = f->second; known = true; } }
        if (!known) { url = itunes_artwork_url(artist, album, opt_); std::lock_guard<std::mutex> lk(pm_); itunes_urls_[tk] = url; }
        if (!url.empty()) { HttpResult r = http_get_string(url, bytes, opt_, 8u << 20); if (r.error.empty() && r.status == 200 && !bytes.empty()) { st = Fetch::Ok; have = true; } }
    } else {
        const std::string did = disk_id(k);
        std::string path;
        bool cached = media_->get(did, path) && read_file(path, bytes);
        bool refreshed = false;
        if (cached && !k.hi && is_artist_id(did)) {
            struct stat sb{};
            const bool stale = ::stat(path.c_str(), &sb) == 0 && (std::time(nullptr) - sb.st_mtime) > kArtistRevalidateSec;
            bool first_try = false;
            if (stale) { std::lock_guard<std::mutex> lk(pm_); first_try = revalidated_try_.insert(did).second; }
            if (first_try) {   // refresh once per session; any failure keeps the cached copy
                std::string fresh; Fetch f = fetch_bytes(did, url_fn(kStdSize), true, fresh);
                if (f == Fetch::Ok) { bytes = std::move(fresh); refreshed = true; ++server_fetches_; }
                else if (f == Fetch::Placeholder) { media_->release(did); cached = false; }
            }
        }
        if (cached) { if (!refreshed) ++disk_hits_; have = true; st = Fetch::Ok; }
        else {
            ++server_fetches_;
            st = fetch_bytes(did, url_fn(k.hi ? kHiSize : kStdSize), !k.hi, bytes);
            have = st == Fetch::Ok;
        }
    }
    std::vector<uint8_t> data; int w = 0, h = 0; uint32_t avg = 0;
    if (have && decode_rgba(bytes, data, w, h)) { downscale_rgba(data, w, h, k.dev); avg = average_rgb(data, w, h); }
    else { data.clear(); if (st == Fetch::Ok) st = Fetch::Failed; }
    ui_post([this, key, data = std::move(data), w, h, avg, st, hi = k.hi, net = k.net]() mutable {
        pending_.erase(key);
        if (st == Fetch::Placeholder) { fallback_.insert(key); fallback_ids_.insert(parse(key).id); if (on_ready_) on_ready_(); return; }
        if (st != Fetch::Ok || data.empty()) {
            auto& rt = retry_[key]; ++rt.first;
            if (net || rt.first > 3) { fallback_.insert(key); fallback_ids_.insert(parse(key).id); if (on_ready_) on_ready_(); return; }   // original: 3 retries, then the icon
            rt.second = SDL_GetTicks() + (st == Fetch::RateLimited ? 2000u : 5000u);
            SDL_AddTimer(st == Fetch::RateLimited ? 2100 : 5100, [](Uint32, void* self) -> Uint32 { ui_post([self] { auto* c = static_cast<ImageCache*>(self); if (c->on_ready_) c->on_ready_(); }); return 0; }, this);
            return;
        }
        SDL_Texture* t = SDL_CreateTexture(r_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, w, h);
        if (!t) return;
        SDL_UpdateTexture(t, nullptr, data.data(), w * 4);
        SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(t, SDL_ScaleModeLinear);
        retry_.erase(key);
        if (hi) {   // at most kHiMaxTextures hi-res textures resident
            std::vector<std::pair<uint64_t, std::string>> his;
            for (auto& kv : tex_) if (kv.first.compare(0, 3, "hi:") == 0) his.emplace_back(kv.second.tick, kv.first);
            std::sort(his.begin(), his.end());
            while (his.size() >= kHiMaxTextures) { auto e = tex_.find(his.front().second); if (e != tex_.end()) { SDL_DestroyTexture(e->second.t); tex_.erase(e); } his.erase(his.begin()); }
        }
        tex_[key] = {t, ++tick_, SDL_GetTicks()};
        avg_[key] = avg;
        evict();
        if (on_ready_) on_ready_();
    });
}
