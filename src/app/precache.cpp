// Performance cache prestep, ported from the original CachePreloadDialog + App.tsx triggers:
//  - first launch for an account: the dialog runs on its own;
//  - every later launch (3 s after connect): server artist/song counts are compared with the last known ones and a
//    "Library Change Detected" prompt offers a rebuild; a cache over a year old gets "Cache Is Outdated";
//  - Settings > Rebuild Cache clears the caches and runs it again.
// Phases: artist images (300 px, batches of 12) while album metadata loads in the background, album covers while song
// metadata loads, then the search index (cached artists/albums/songs in the Store + persisted index).
#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#include "app.h"
#include "model_json.h"
#include "log.h"
#include "skin.h"

namespace {
Color alpha(Color c, float a) { c.a = static_cast<uint8_t>(std::lround(a * 255)); return c; }
constexpr size_t kBatch = 12;                  // SAFE_BATCH_SIZE
constexpr int kRestartDelayS = 15;             // RELOAD_DELAY_S
constexpr int64_t kCacheMaxAgeSec = 365LL * 24 * 3600;   // checkCacheAge: ONE_YEAR

using Clock = std::chrono::steady_clock;
double secs_since(Clock::time_point t) { return std::chrono::duration<double>(Clock::now() - t).count(); }

std::string fmt_time(double secs) {
    if (secs < 60) return std::to_string(static_cast<int>(std::lround(secs))) + "s";
    const int m = static_cast<int>(secs / 60), s = static_cast<int>(std::lround(std::fmod(secs, 60)));
    return s > 0 ? std::to_string(m) + "m " + std::to_string(s) + "s" : std::to_string(m) + "m";
}

void sleep_ms(const std::atomic<bool>& cancel, int ms) {
    for (int t = 0; t < ms && !cancel.load(); t += 50) std::this_thread::sleep_for(std::chrono::milliseconds(50));
}

// fetchWithRetry: attempts with 500 ms doubling (max 2 s) back-off.
template <class F> bool with_retry(const std::atomic<bool>& cancel, int attempts, F fn) {
    for (int a = 1; a <= attempts && !cancel.load(); ++a) {
        if (fn()) return true;
        if (a < attempts) sleep_ms(cancel, std::min(500 * (1 << (a - 1)), 2000));
    }
    return false;
}

// runResilient: adaptive batches (a quarter failing halves the batch, clean batches grow it back), up to two more
// passes over what failed at a smaller size after a pause. Returns the items that still fail.
template <class T, class F> std::vector<T> run_resilient(const std::vector<T>& items, const std::atomic<bool>& cancel, F worker) {
    std::vector<T> pending = items;
    size_t size = kBatch;
    for (int pass = 0; pass < 3 && !pending.empty() && !cancel.load(); ++pass) {
        if (pass > 0) { size = std::max<size_t>(1, kBatch >> pass); sleep_ms(cancel, 2000 * pass); }
        std::vector<T> failed;
        for (size_t i = 0; i < pending.size() && !cancel.load();) {
            const size_t n = std::min(size, pending.size() - i);
            std::vector<char> ok(n, 0);
            std::vector<std::thread> th;
            for (size_t k = 0; k < n; ++k) th.emplace_back([&, k] { ok[k] = worker(pending[i + k]) ? 1 : 0; });
            for (auto& t : th) t.join();
            size_t bad = 0;
            for (size_t k = 0; k < n; ++k) if (!ok[k]) { failed.push_back(pending[i + k]); ++bad; }
            i += n;
            if (bad * 4 >= n) size = std::max<size_t>(1, (size + 1) / 2);
            else if (bad == 0 && size < kBatch) ++size;
        }
        pending = std::move(failed);
    }
    return pending;
}

bool is_stored_flag(Store* st, const char* key, int64_t* ts) { std::string v; return st && st->get(key, v, ts); }
}  // namespace

// ---------------------------------------------------------------- state and triggers

void App::mark_precache_done() {
    if (!store_) return;
    store_->put("pre:done", "true");      // the kv timestamp is cachePreloadTimestamp
    store_->erase_prefix("pre:counts");   // next launch writes a fresh baseline without prompting
}

void App::check_first_run() {
    if (!store_ || offline_ || opt_.no_precache || opt_.selftest) return;
    if (!is_stored_flag(store_.get(), "pre:done", nullptr)) start_cache_preload(false);
}

// 3 s after connect: compare server counts with the last known ones; also the cache-age prompt.
void App::check_library_changes() {
    if (!store_ || !client_ || offline_ || pre_open_ || opt_.no_precache || opt_.selftest) return;
    int64_t ts = 0;
    if (!is_stored_flag(store_.get(), "pre:done", &ts)) return;   // first launch: the dialog handles it
    if (static_cast<int64_t>(std::time(nullptr)) - ts > kCacheMaxAgeSec) { nc_outdated_ = true; nc_artists_ = nc_songs_ = 0; newcontent_open_ = true; mark(); }
    worker_->post([this, c = client_, sess = session_] {
        std::vector<Artist> artists; std::string err; int songs = 0;
        if (!c->artists(artists, err)) return;
        if (!c->song_count(songs, err)) songs = 0;           // getSongCount returns 0 on failure
        ui_post([this, sess, n = static_cast<int>(artists.size()), songs] {
            if (sess != session_ || !store_) return;
            std::string v;
            int sa = -1, ss = -1;
            if (store_->get("pre:counts", v, nullptr)) { std::sscanf(v.c_str(), "%d,%d", &sa, &ss); }
            store_->put("pre:counts", std::to_string(n) + "," + std::to_string(songs));   // always refresh the baseline
            if (sa < 0) return;                                                           // no baseline yet
            if (n != sa || songs != ss) { nc_outdated_ = false; nc_artists_ = n - sa; nc_songs_ = songs - ss; newcontent_open_ = true; mark(); }
        });
    });
}

// ---------------------------------------------------------------- the build

void App::start_cache_preload(bool library_change) {
    if (pre_running_ || !client_ || !store_ || offline_) return;
    cancel_cache_preload();                          // joins an earlier attempt
    pre_cancel_ = std::make_shared<std::atomic<bool>>(false);
    auto cancel = pre_cancel_;
    const uint32_t sess = session_;
    pre_open_ = true; pre_running_ = true; pre_library_change_ = library_change;
    pre_change_artists_ = nc_artists_; pre_change_songs_ = nc_songs_;
    pre_phase_ = PrePhase::Artists;
    pre_art_done_ = pre_art_total_ = pre_alb_done_ = pre_alb_total_ = pre_search_pct_ = 0;
    for (float& f : pre_disp_) f = 0;
    pre_search_msg_ = "Waiting..."; pre_art_elapsed_.clear(); pre_alb_elapsed_.clear(); pre_search_elapsed_.clear(); pre_failure_.clear();
    newcontent_open_ = false;
    // Every attempt starts from empty caches, and no cover is fetched on its own while the bulk build runs.
    images_->clear_memory(); images_->clear_disk(); images_->set_blackout(true);
    store_->clear_library();
    Store* store = store_.get();
    ImageCache* images = images_.get();
    pre_thread_ = std::thread([this, c = client_, store, images, cancel, sess] {
        auto post = [&](std::function<void()> fn) { ui_post([this, sess, cancel, fn = std::move(fn)] { if (sess != session_ || pre_cancel_ != cancel) return; fn(); mark(); }); };
        std::string failure;
        int artist_meta_failed = 0, album_meta_failed = 0;
        std::vector<Artist> artists;            // from getArtists
        std::string err;
        const bool listed = with_retry(*cancel, 3, [&] { artists.clear(); return c->artists(artists, err); });
        if (cancel->load()) return;
        if (!listed) { failure = "The server could not be reached."; }
        std::vector<Artist> full_artists; std::vector<Album> albums; std::map<std::string, Album> full_albums;
        std::mutex m;
        if (failure.empty()) {
            post([n = static_cast<int>(artists.size())] { (void)n; });
            post([this, n = static_cast<int>(artists.size())] { pre_art_total_ = n; });
            // Album metadata runs in the background while artist images download.
            std::thread album_meta([&] {
                std::vector<Artist> todo = artists;
                auto failed = run_resilient(todo, *cancel, [&](const Artist& a) {
                    Artist fa;
                    const bool ok = with_retry(*cancel, 2, [&] { std::string e; return c->artist(a.id, fa, e); });
                    if (ok) { std::lock_guard<std::mutex> lk(m); full_artists.push_back(fa); }
                    return ok;
                });
                artist_meta_failed = static_cast<int>(failed.size());
            });
            const auto t_art = Clock::now();
            for (size_t i = 0; i < artists.size() && !cancel->load(); i += kBatch) {
                const size_t n = std::min(kBatch, artists.size() - i);
                std::vector<std::thread> th;
                for (size_t k = 0; k < n; ++k) th.emplace_back([&, k] {
                    const Artist& a = artists[i + k];
                    if (!a.cover_art.empty()) images->fetch_to_disk(a.cover_art, [c, id = a.cover_art](int s) { return c->cover_url(id, s); });
                });
                for (auto& t : th) t.join();
                const int done = static_cast<int>(i + n);
                const bool last = i + kBatch >= artists.size();
                post([this, done, last, el = fmt_time(secs_since(t_art))] { pre_art_done_ = done; if (last) pre_art_elapsed_ = el; });
            }
            if (cancel->load()) { album_meta.join(); return; }
            post([this] { pre_phase_ = PrePhase::Albums; });
            const auto t_alb = Clock::now();
            album_meta.join();
            {   // unique albums over every artist
                std::set<std::string> seen;
                for (const Artist& a : full_artists) for (const Album& al : a.albums) if (!al.id.empty() && seen.insert(al.id).second) albums.push_back(al);
            }
            std::vector<std::string> cover_ids; { std::set<std::string> seen; for (const Album& al : albums) if (!al.cover_art.empty() && seen.insert(al.cover_art).second) cover_ids.push_back(al.cover_art); }
            post([this, n = static_cast<int>(cover_ids.size())] { pre_alb_total_ = n; });
            std::thread song_meta([&] {
                auto failed = run_resilient(albums, *cancel, [&](const Album& al) {
                    Album fa;
                    const bool ok = with_retry(*cancel, 2, [&] { std::string e; return c->album(al.id, fa, e); });
                    if (ok) { std::lock_guard<std::mutex> lk(m); full_albums[al.id] = fa; }
                    return ok;
                });
                album_meta_failed = static_cast<int>(failed.size());
            });
            for (size_t i = 0; i < cover_ids.size() && !cancel->load(); i += kBatch) {
                const size_t n = std::min(kBatch, cover_ids.size() - i);
                std::vector<std::thread> th;
                for (size_t k = 0; k < n; ++k) th.emplace_back([&, k] { const std::string& id = cover_ids[i + k]; images->fetch_to_disk(id, [c, id](int s) { return c->cover_url(id, s); }); });
                for (auto& t : th) t.join();
                const int done = static_cast<int>(i + n);
                const bool last = i + kBatch >= cover_ids.size();
                post([this, done, last, el = fmt_time(secs_since(t_alb))] { pre_alb_done_ = done; if (last) pre_alb_elapsed_ = el; });
            }
            if (cancel->load()) { song_meta.join(); return; }
            post([this] { pre_phase_ = PrePhase::Search; pre_search_msg_ = "Finalizing search index..."; });
            const auto t_search = Clock::now();
            song_meta.join();
            const int tol_a = static_cast<int>(artists.size() * 0.02), tol_b = static_cast<int>(albums.size() * 0.02);
            if (artist_meta_failed > tol_a || album_meta_failed > tol_b) {
                // More than 2% still failing after every retry pass would leave search silently incomplete.
                failure = "The library could not be fully loaded (" + std::to_string(artist_meta_failed) + " artists and " + std::to_string(album_meta_failed) + " albums could not be loaded).";
            } else {
                post([this] { pre_search_msg_ = "Saving search index\xE2\x80\xA6"; pre_search_pct_ = 50; });
                store->put("artists", to_json(artists));
                for (const Artist& a : full_artists) store->put("artist:" + a.id, to_json(a));
                for (const auto& kv : full_albums) { store->put("album:" + kv.first, to_json(kv.second)); store->index_album(kv.second); }
                store->compact_metadata();                   // snapshot + persisted search index
                std::unordered_map<std::string, std::string> alias;   // song cover id -> album cover id
                for (const auto& kv : full_albums) for (const Song& s : kv.second.songs) if (!s.cover_art.empty() && s.cover_art != kv.second.cover_art) alias[s.cover_art] = kv.second.cover_art;
                post([this, el = fmt_time(secs_since(t_search)), alias = std::move(alias), art = artists]() mutable {
                    images_->set_alias(std::move(alias));
                    artists_ = std::move(art);
                    pre_search_msg_ = "Search index ready!"; pre_search_pct_ = 100; pre_search_elapsed_ = el;
                });
            }
        }
        if (cancel->load()) return;
        post([this, failure] {
            pre_running_ = false;
            images_->set_blackout(false);
            if (!failure.empty()) { pre_failure_ = failure; xf_log("cache build failed: %s", failure.c_str()); return; }     // a failed build never counts as done
            SubsonicClient::reset_api_calls(); images_->reset_stats();    // Settings stats reflect browsing only
            pre_phase_ = PrePhase::Complete; pre_complete_tick_ = SDL_GetTicks();
            mark_precache_done();
        });
    });
    mark();
}

void App::cancel_cache_preload() {
    if (pre_cancel_) pre_cancel_->store(true);
    if (pre_thread_.joinable()) pre_thread_.join();
    if (pre_running_) { pre_running_ = false; if (images_) images_->set_blackout(false); }
}

void App::skip_cache_preload() {
    cancel_cache_preload();
    mark_precache_done();
    finish_precache();
}

// The original reloads the window; natively the same effect is dropping in-memory state and reloading the visible data.
void App::finish_precache() {
    cancel_cache_preload();
    pre_open_ = false; newcontent_open_ = false;
    images_->clear_memory();
    album_cache_.clear(); artist_cache_.clear();
    views_.clear(); views_.push_back({ViewKind::Root, "", "Home", {}});
    nav_ = Nav::Home;
    load_home(); load_artists();
    mark();
}

// ---------------------------------------------------------------- drawing (CachePreloadDialog.css)

void App::draw_precache(float W, float H) {
    Ui& u = *ui_;
    const float s = S_;
    const Theme& th = u.th;
    const bool short_win = H / s <= 600.0f;                                // @media (max-height:600px)
    const Color green{0x1d, 0xb9, 0x54, 255};
    gfx_->fill({0, 0, W, H}, {0, 0, 0, 209});                              // rgba(0,0,0,.82); the blur is not reproduced
    const float dw = std::min(400.0f, 0.92f * W / s);
    const float pad_t = short_win ? 20 : 32, pad_x = short_win ? 16 : 28, pad_b = short_win ? 16 : 24;
    const float ring = short_win ? 58.0f : 72.0f;

    // content height first, so the dialog can be centred
    const bool complete = pre_phase_ == PrePhase::Complete, failed = !pre_failure_.empty();
    const float title_h = short_win ? 22.5f : 25.5f, subtitle_h = 16.8f, gauge_h = ring + 6 + 13 + 6 + 15;
    float body_h = 0;
    const float fail_w = std::min(420.0f, dw - 2 * pad_x);
    int fail_lines = 1;
    if (failed) { const std::string m = pre_failure_ + " Check the connection to the server, then try again."; fail_lines = std::max(1, static_cast<int>(std::ceil(gfx_->measure(F(400, 13), m) / (fail_w * s)))); }
    if (failed) body_h = 8 + 42 + 12 + 19.5f * fail_lines + 12 + 35;
    else if (complete) body_h = 182.5f;                                    // 1 + 16 + 39.5 + 10 + 56 + 10 + 33 + 16 + 1
    else body_h = 16 + 20 + 35;                                            // status, its margin, Skip for Now
    std::string subtitle = "Building image cache and search index for instant performance";
    if (pre_library_change_) {
        std::string parts;
        auto add = [&](int n, const char* noun) { if (!n) return; if (!parts.empty()) parts += ", "; parts += (n > 0 ? "+" : "") + std::to_string(n) + " " + noun + (std::abs(n) != 1 ? "s" : ""); };
        add(pre_change_artists_, "artist"); add(pre_change_songs_, "song");
        subtitle = parts.empty() ? "Library changed \xE2\x80\x94 rebuilding cache" : parts + " detected \xE2\x80\x94 rebuilding cache";
    }
    const int sub_lines = wrap_count(400, 12, subtitle, (dw - 2 * pad_x) * s);   // narrow screens wrap the subtitle onto two lines
    const float dh = 1 + pad_t + title_h + 6 + subtitle_h * sub_lines + (short_win ? 16 : 28) + gauge_h + 18 + body_h + pad_b + 1;
    Rect d{(W - dw * s) / 2, std::max(12 * s, (H - dh * s) / 2), dw * s, dh * s};
    gfx_->fill_round(d, 18 * s, {255, 255, 255, 31});                                   // 1px border-subtle (rgba(255,255,255,.12))
    gfx_->fill_round({d.x + 1 * s, d.y + 1 * s, d.w - 2 * s, d.h - 2 * s}, 17 * s, rgb(0x1a1a1a));
    float y = d.y + (pad_t + 1) * s;
    const float cx = d.x + d.w / 2;
    auto centred = [&](int weight, float px, float line_h, const std::string& t, Color col, float yy) {
        const float tw = gfx_->measure(F(weight, px), t);
        T(weight, px, cx - tw / 2, yy, line_h, t, col, d.w - 2 * pad_x * s);
    };
    gfx_->set_letter_spacing(-0.2f * s);
    centred(700, short_win ? 15.0f : 17.0f, title_h, pre_library_change_ ? "Updating Library Cache" : "Building Performance Cache", rgb(0xffffff), y);
    gfx_->set_letter_spacing(0);
    y += (title_h + 6) * s;
    TWC(400, 12, cx, y, subtitle_h, subtitle, rgb(0x666666), (dw - 2 * pad_x) * s);
    y += (subtitle_h * sub_lines + (short_win ? 16 : 28)) * s;

    // gauges
    struct G { const char* label; float pct; int state; const std::string* elapsed; bool loading; };   // state 0 waiting, 1 active, 2 done
    const float ap = pre_art_total_ > 0 ? std::round(100.0f * pre_art_done_ / pre_art_total_) : 0, bp = pre_alb_total_ > 0 ? std::round(100.0f * pre_alb_done_ / pre_alb_total_) : 0;
    const float overall = complete ? 100 : pre_phase_ == PrePhase::Artists ? std::floor(ap * 0.33f) : pre_phase_ == PrePhase::Albums ? std::floor(33 + bp * 0.33f) : std::floor(66 + pre_search_pct_ * 0.34f);
    const bool a_done = pre_phase_ != PrePhase::Artists, b_done = pre_phase_ == PrePhase::Search || complete;
    const std::string none;
    const G gs[4] = {
        {"Artist Images", ap, a_done ? 2 : 1, &pre_art_elapsed_, pre_art_total_ == 0 && pre_phase_ == PrePhase::Artists},
        {"Album Covers", bp, b_done ? 2 : pre_phase_ == PrePhase::Albums ? 1 : 0, &pre_alb_elapsed_, pre_alb_total_ == 0 && pre_phase_ == PrePhase::Albums},
        {"Search Index", static_cast<float>(pre_search_pct_), complete ? 2 : pre_phase_ == PrePhase::Search ? 1 : 0, &pre_search_elapsed_, pre_search_pct_ == 0 && pre_phase_ == PrePhase::Search},
        {"Overall", overall, complete ? 2 : 1, &none, false}};
    const float gw = (d.w - 2 * pad_x * s - 3 * 8 * s) / 4;
    bool animating = false;
    for (int i = 0; i < 4; ++i) {
        const float gx = d.x + pad_x * s + i * (gw + 8 * s), gcx = gx + gw / 2, gcy = y + ring * s / 2, r = ring / 72.0f * 28 * s, stroke = ring / 72.0f * 5 * s;
        const G& g = gs[i];
        const float target = g.state == 2 ? 100.0f : g.state == 0 ? 0.0f : g.pct;
        float& disp = pre_disp_[i];
        if (std::fabs(disp - target) > 0.2f) { disp += (target - disp) * 0.25f; animating = true; } else disp = target;
        gfx_->ring(gcx, gcy, r + stroke / 2, stroke, {255, 255, 255, 15});                      // track: hover-overlay (r is the stroke centre line)
        if (g.state != 0 && disp > 0.2f) {
            const float sweep = 6.2831853f * std::min(100.0f, disp) / 100.0f;
            if (g.state == 1) gfx_->arc(gcx, gcy, r, stroke + 5 * s, -1.5707963f, sweep, {0x1d, 0xb9, 0x54, 40}, true);   // drop-shadow glow
            if (disp >= 99.9f) gfx_->ring(gcx, gcy, r + stroke / 2, stroke, green);
            else gfx_->arc(gcx, gcy, r, stroke, -1.5707963f, sweep, green, true);
        }
        if (g.state == 2) fa(skin::Check, gcx, gcy, short_win ? 13 : 16, green);
        else if (g.state == 0 || g.loading) { const float tw = gfx_->measure(F(400, short_win ? 11.0f : 13.0f), "\xE2\x80\x94"); T(400, short_win ? 11.0f : 13.0f, gcx - tw / 2, gcy - 9 * s, 18, "\xE2\x80\x94", {255, 255, 255, static_cast<uint8_t>(g.state == 0 ? 41 : 100)}); }
        else { const std::string t = std::to_string(static_cast<int>(g.pct)) + "%"; const float tw = gfx_->measure(F(700, short_win ? 11.0f : 13.0f), t); T(700, short_win ? 11.0f : 13.0f, gcx - tw / 2, gcy - 9 * s, 18, t, rgb(0xffffff)); }
        const float ly = y + (ring + 6) * s;
        const Color lc = g.state == 1 ? rgb(0xbbbbbb) : rgb(0x555555);
        const float lw = gfx_->measure(F(500, 10), g.label);
        T(500, 10, gcx - lw / 2, ly, 13, g.label, lc);
        if (!g.elapsed->empty()) { const float ew = gfx_->measure(F(400, 10), *g.elapsed); T(400, 10, gcx - ew / 2, ly + 19 * s, 15, *g.elapsed, rgb(0x444444)); }
    }
    if (animating) u.wants_frames = true;
    y += (gauge_h + 18) * s;

    if (failed) {
        y += 8 * s;
        fa(0xf071, cx, y + 21 * s, 28, th.accent);
        y += (42 + 12) * s;
        const std::string msg = pre_failure_ + " Check the connection to the server, then try again.";
        const float mw = fail_w * s;
        T(400, 13, d.x + (d.w - mw) / 2, y, 19.5f, msg, th.sub, mw);
        y += (19.5f * fail_lines + 12) * s;
        const float bw1 = 104 * s, bw2 = 118 * s, bh = 33 * s, gap = 6 * s;
        Rect again{cx - (bw1 + gap + bw2) / 2, y, bw1, bh}, skip{again.x + bw1 + gap, y - 1 * s, bw2, 35 * s};
        gfx_->fill_round(again, bh / 2, u.hover(again) ? rgb(0x22d062) : rgb(0x1ec95b));
        T(700, 13, again.x + (again.w - gfx_->measure(F(700, 13), "Try Again")) / 2, again.y, bh / s, "Try Again", rgb(0x000000));
        gfx_->stroke_round(skip, 8 * s, 1 * s, u.hover(skip) ? Color{255, 255, 255, 64} : Color{255, 255, 255, 61}, rgb(0x1a1a1a));
        T(500, 13, skip.x + (skip.w - gfx_->measure(F(500, 13), "Skip for Now")) / 2, skip.y, skip.h / s, "Skip for Now", u.hover(skip) ? rgb(0xaaaaaa) : rgb(0x666666));
        if (u.clicked(again)) start_cache_preload(pre_library_change_);
        if (u.clicked(skip)) skip_cache_preload();
        return;
    }
    if (complete) {
        Rect ban{d.x + pad_x * s, y, d.w - 2 * pad_x * s, 182.5f * s};
        gfx_->fill_round(ban, 12 * s, {0x1d, 0xb9, 0x54, 20});
        gfx_->stroke_round(ban, 12 * s, 1 * s, {0x1d, 0xb9, 0x54, 64}, rgb(0x1a1a1a));
        float by = ban.y + 17 * s;
        fa(0xf058, ban.x + 17 * s + 13.75f * s, by + 19.75f * s, 22, green);
        T(700, 14, ban.x + 17 * s + 39.5f * s, by, 21, pre_library_change_ ? "Cache Updated!" : "Library Ready!", rgb(0xffffff));
        T(400, 11, ban.x + 17 * s + 39.5f * s, by + 23 * s, 16.5f, pre_library_change_ ? "Cache refreshed with the latest library changes" : "App will restart automatically", rgb(0x666666), ban.w - 70 * s);
        by += (39.5f + 10) * s;
        const float elapsed = static_cast<float>(SDL_GetTicks() - pre_complete_tick_) / 1000.0f;
        const int left = std::max(0, static_cast<int>(std::ceil(kRestartDelayS - elapsed)));
        const float rcx = ban.x + ban.w / 2, rcy = by + 28 * s, rr = 42 * 0.56f * s, rs = 5 * 0.56f * s;
        gfx_->ring(rcx, rcy, rr + rs / 2, rs, {255, 255, 255, 15});
        const float frac = std::max(0.0f, 1.0f - elapsed / kRestartDelayS);
        if (frac > 0.002f) gfx_->arc(rcx, rcy, rr, rs, -1.5707963f, 6.2831853f * frac, green, true);
        const std::string num = std::to_string(left);
        T(700, 17, rcx - gfx_->measure(F(700, 17), num) / 2, rcy - 9 * s, 18, num, rgb(0xffffff));
        by += (56 + 10) * s;
        Rect btn{rcx - 66 * s, by, 132 * s, 33 * s};
        gfx_->fill_round({btn.x, btn.y + 3 * s, btn.w, btn.h}, btn.h / 2, {0x1d, 0xb9, 0x54, 50});   // 0 3px 12px glow
        gfx_->fill_round(btn, btn.h / 2, u.hover(btn) ? rgb(0x22d062) : rgb(0x1ec95b));
        T(700, 13, btn.x + (btn.w - gfx_->measure(F(700, 13), "Restart Now")) / 2, btn.y, btn.h / s, "Restart Now", rgb(0x000000));
        u.wants_frames = true;
        if (u.clicked(btn) || left == 0) finish_precache();
        return;
    }
    // in progress: status line and Skip for Now
    std::string status;
    if (pre_phase_ == PrePhase::Artists) status = pre_art_total_ > 0 ? std::to_string(pre_art_done_) + " / " + std::to_string(pre_art_total_) + " artist images" : "Fetching artist list\xE2\x80\xA6";
    else if (pre_phase_ == PrePhase::Albums) status = pre_alb_total_ > 0 ? std::to_string(pre_alb_done_) + " / " + std::to_string(pre_alb_total_) + " album covers" : "Fetching album list\xE2\x80\xA6";
    else status = pre_search_msg_;
    centred(400, 12, 16, status, rgb(0x555555), y);
    y += (16 + 20) * s;
    const float sw = 124 * s, sh = 35 * s;
    Rect skip{cx - sw / 2, y, sw, sh};
    gfx_->stroke_round(skip, 8 * s, 1 * s, u.hover(skip) ? Color{255, 255, 255, 64} : Color{255, 255, 255, 61}, rgb(0x1a1a1a));
    T(500, 13, skip.x + (skip.w - gfx_->measure(F(500, 13), "Skip for Now")) / 2, skip.y, sh / s, "Skip for Now", u.hover(skip) ? rgb(0xaaaaaa) : rgb(0x666666));
    if (u.clicked(skip)) skip_cache_preload();
}

// .offline-prompt overlay shared by the "Library Change Detected", "No Internet Connection" and "Mobile Data Detected" prompts.
// Returns 1 when the left button was pressed, 2 for the right one, 0 otherwise.
int App::draw_prompt(float W, float H, uint32_t icon, const std::string& title, const std::string& body,
                     const std::vector<std::pair<std::string, std::string>>& list, const std::string& note,
                     const std::string& left, const std::string& right, uint32_t right_icon, bool accent_icon) {
    Ui& u = *ui_;
    const float s = S_;
    const Theme& th = u.th;
    gfx_->fill({0, 0, W, H}, {0, 0, 0, 217});                              // rgba(0,0,0,.85)
    const float pw = std::min(500.0f, 0.9f * W / s) * s, padd = 40 * s;
    const float text_w = pw - 2 * padd;
    std::vector<std::string> lines;
    {
        std::string line, word; const int f = F(400, 16);
        for (size_t i = 0; i <= body.size(); ++i) {
            if (i == body.size() || body[i] == ' ') { std::string t = line.empty() ? word : line + " " + word; if (!line.empty() && gfx_->measure(f, t) > text_w) { lines.push_back(line); line = word; } else line = t; word.clear(); } else word += body[i];
        }
        if (!line.empty()) lines.push_back(line);
    }
    const float list_h = list.empty() ? 0 : 21.0f * list.size() + 15;
    const float note_h = note.empty() ? 0 : 19.5f + 24;
    // .offline-prompt: padding 40; icon 80 + 20; h3 28px (line 42) + 12; p lines of 24 + 24; list; footnote; buttons 50.75
    const float ph = (40 + 80 + 20 + 42 + 12 + 24.0f * lines.size() + 24 + list_h + note_h + 50.75f + 40) * s;
    Rect p{(W - pw) / 2, (H - ph) / 2, pw, ph};
    gfx_->fill_round({p.x - 1, p.y - 1, p.w + 2, p.h + 2}, 21 * s, {255, 255, 255, 20});
    gfx_->fill_round(p, 20 * s, rgb(0x1e1e1e));
    float y = p.y + 40 * s;
    const float cx = p.x + p.w / 2;
    gfx_->circle(cx, y + 40 * s, 40 * s, {255, 59, 48, 38});                // icon background rgba(255,59,48,.15)
    fa(icon, cx, y + 40 * s, 36, accent_icon ? th.accent : rgb(0xff3b30));
    y += 100 * s;
    T(600, 28, cx - gfx_->measure(F(600, 28), title) / 2, y, 42, title, rgb(0xffffff), text_w);
    y += (42 + 12) * s;
    for (const auto& l : lines) { T(400, 16, cx - gfx_->measure(F(400, 16), l) / 2, y, 24, l, {255, 255, 255, 179}); y += 24 * s; }
    y += 24 * s;
    for (const auto& li : list) {
        T(700, 14, p.x + padd, y, 21, li.first, rgb(0xffffff));
        T(400, 14, p.x + padd + gfx_->measure(F(700, 14), li.first), y, 21, li.second, {255, 255, 255, 179});
        y += 21 * s;
    }
    if (!list.empty()) y += 15 * s;
    if (!note.empty()) { T(400, 13, cx - gfx_->measure(F(400, 13), note) / 2, y, 19.5f, note, rgb(0x888888), text_w); y += (19.5f + 24) * s; }
    const float bh = 50.75f * s, gap = 12 * s;
    const float lw = gfx_->measure(F(600, 16), left) + 60 * s, rw = gfx_->measure(F(600, 16), right) + 60 * s + (right_icon ? 28 * s : 0);
    Rect lb{cx - (lw + gap + rw) / 2, y, lw, bh}, rb{lb.x + lw + gap, y, rw, bh};
    gfx_->fill_round(lb, bh / 2, rgb(u.hover(lb) ? 0x333333 : 0x2a2a2a));
    gfx_->stroke_round(lb, bh / 2, 2 * s, u.hover(lb) ? th.accent : Color{255, 255, 255, 102}, rgb(0x2a2a2a));
    T(600, 16, lb.x + 30 * s, lb.y, bh / s, left, rgb(0xffffff));
    gfx_->fill_round(rb, bh / 2, u.hover(rb) ? alpha(th.accent, 0.88f) : th.accent);
    if (right_icon) fa(right_icon, rb.x + 30 * s + 8 * s, rb.y + bh / 2, 16, rgb(0xffffff));
    T(600, 16, rb.x + 30 * s + (right_icon ? 28 * s : 0), rb.y, bh / s, right, rgb(0xffffff));
    if (u.clicked(lb) || u.in.escape) return 1;
    if (u.clicked(rb)) return 2;
    return 0;
}

// App.tsx showNewContentPrompt.
void App::draw_new_content_prompt(float W, float H) {
    const bool outdated = nc_outdated_ || (nc_artists_ == 0 && nc_songs_ == 0);
    std::vector<std::pair<std::string, std::string>> list;
    if (!outdated) {
        if (nc_artists_ != 0) list.emplace_back((nc_artists_ > 0 ? "+" : "") + std::to_string(nc_artists_), " artists");
        if (nc_songs_ != 0) list.emplace_back((nc_songs_ > 0 ? "+" : "") + std::to_string(nc_songs_), " songs");
    }
    const int r = draw_prompt(W, H, (nc_artists_ < 0 || nc_songs_ < 0) ? 0xf068u : 0xf2f1u, outdated ? "Cache Is Outdated" : "Library Change Detected!",
                              outdated ? "Your local cache is over a year old. Rebuilding it will keep search results and images up to date." : "Your server library has changed since the cache was built:",
                              list, "Rebuild the cache to keep search results accurate.", "Later", "Rebuild Cache Now", 0xf2f1, true);
    if (r == 1) { newcontent_open_ = false; mark(); }
    else if (r == 2) { newcontent_open_ = false; start_cache_preload(true); }
}
