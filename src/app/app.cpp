#include <cctype>
#include "app.h"
#include "../audio/decoder.h"
#include "../../third_party/yyjson.h"
#include <unordered_set>
#include <thread>
#include "android_bridge.h"
#if !defined(XF_MOBILE)
#include <SDL_image.h>   // phones decode artwork with stb_image instead
#endif
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sys/stat.h>
#include <unistd.h>
#include "../net/url.h"
#include "metered.h"
#include "log.h"
#include "secrets.h"

namespace {
const char* kOrg = "xylonic-fiber";
const char* kApp = "xylonic-fiber";

void mkdirs(const std::string& p) {
    for (size_t i = 1; i < p.size(); ++i)
        if (p[i] == '/') { std::string d = p.substr(0, i); ::mkdir(d.c_str(), 0700); }
    ::mkdir(p.c_str(), 0700);
}

std::string lower_ext(const std::string& s) {
    std::string o = s;
    for (auto& c : o) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
    return o;
}
}  // namespace

App::App(AppOptions o) : opt_(std::move(o)) {}

App::~App() {
    *alive_ = false;
    cancel_cache_preload();
    if (metered_timer_) SDL_RemoveTimer(metered_timer_);
    if (scan_cancel_) scan_cancel_->store(true);
    close_mini();
    save_queue_state();
    player_.stop();
    images_.reset();   // textures must die before the renderer
    gfx_.reset();
    worker_.reset();
    if (ren_) SDL_DestroyRenderer(ren_);
    if (win_) SDL_DestroyWindow(win_);
    SDL_Quit();
}

bool App::init() {
    SDL_SetHint(SDL_HINT_RENDER_BATCHING, "1");
#if defined(__ANDROID__)
    SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");
    // SDL's OpenSL ES backend asks for a low-latency fast track and fails to open on some phones (Oilsky G88: "notificationFrames=-2 not
    // permitted for non-fast track"); AAudio negotiates the stream itself and works there.
    SDL_SetHint(SDL_HINT_AUDIODRIVER, "aaudio");
    // Keep running (and playing) when the activity is paused: the foreground MediaService keeps the process alive, and the
    // media session commands, the gapless hand-over and the next-track logic all run on this loop.
    SDL_SetHint(SDL_HINT_ANDROID_BLOCK_ON_PAUSE, "0");
    SDL_SetHint(SDL_HINT_ANDROID_BLOCK_ON_PAUSE_PAUSEAUDIO, "0");
#endif
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_AUDIO) < 0) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return false;
    }
    {   // the stored settings are needed before the renderer exists (software rendering)
        char* p = SDL_GetPrefPath(kOrg, kApp);
        pref_ = p ? p : "./";
        SDL_free(p);
        cfg_path_ = pref_ + "config.json";
        cfg_.load(cfg_path_);
    }
    win_ = SDL_CreateWindow("Xylonic Fiber", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, opt_.width, opt_.height,
                            SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!win_) return false;
    SDL_SetWindowMinimumSize(win_, 320, 480);
    // CPU rasteriser (SDL's software renderer) when asked for (--software, XF_SOFTWARE=1, Settings > Advanced) and as the fallback
    // when no GPU renderer can be created, so a machine with a broken or missing GL driver still gets a window.
    const char* envsw = std::getenv("XF_SOFTWARE");
    const bool want_sw = opt_.software || cfg_.software_render || (envsw && *envsw && *envsw != '0');
    ren_ = nullptr;
    if (!want_sw) {
        ren_ = SDL_CreateRenderer(win_, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        if (!ren_) ren_ = SDL_CreateRenderer(win_, -1, SDL_RENDERER_ACCELERATED);   // some drivers refuse vsync
    }
    if (!ren_) ren_ = SDL_CreateRenderer(win_, -1, SDL_RENDERER_SOFTWARE);
    if (!ren_) return false;
    {
        SDL_RendererInfo ri{};
        if (SDL_GetRendererInfo(ren_, &ri) == 0) { software_ = (ri.flags & SDL_RENDERER_SOFTWARE) != 0; renderer_name_ = ri.name ? ri.name : ""; xf_log("renderer: %s", ri.name); }
    }
    SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_BLEND);
    int ww = 0, wh = 0, dw = 0, dh = 0;
    SDL_GetWindowSize(win_, &ww, &wh);
    SDL_GetRendererOutputSize(ren_, &dw, &dh);
    S_ = ww > 0 ? static_cast<float>(dw) / ww : 1.0f;
    M_ = S_;   // window coordinates -> render pixels for the mouse; S_ below may become a DPI-based UI scale instead (phones)
#if defined(__ANDROID__) || defined(__IPHONEOS__)
    {   // phones report pixel sizes with no density scaling: derive the UI scale from the display DPI (reference 160 dpi = 1x)
        float ddpi = 0;
        if (SDL_GetDisplayDPI(0, &ddpi, nullptr, nullptr) == 0 && ddpi > 0) S_ = std::min(4.0f, std::max(1.0f, ddpi / 160.0f * 1.0f));
#if defined(__ANDROID__)
        const float dens = android_bridge::density();   // what a WebView calls devicePixelRatio: follows the display-size override (the G88 runs at 320 dpi, reports 260)
        if (dens > 0.5f) S_ = std::min(4.0f, std::max(1.0f, dens));
#endif
    }
#endif

    img_dir_ = pref_ + "covers/";
    mkdirs(img_dir_);
    perm_dir_ = downloads_base() + "downloads/"; stream_dir_ = cache_base() + "stream/";
    mkdirs(perm_dir_); mkdirs(stream_dir_);
    xf_log_configure(cfg_.debug_log, pref_ + "logs");
    vol_ = static_cast<float>(cfg_.volume_pct) / 100.0f;
    player_.set_volume(vol_);
#if defined(__ANDROID__)
    player_.set_preferred_device_rate(static_cast<uint32_t>(android_bridge::output_rate()));
#endif

    gfx_ = std::make_unique<Gfx>();
    if (!gfx_->init(win_, ren_)) {   // the GPU renderer came up but cannot hold the glyph atlas: fall back to the CPU rasteriser
        if (software_) return false;
        SDL_DestroyRenderer(ren_);
        ren_ = SDL_CreateRenderer(win_, -1, SDL_RENDERER_SOFTWARE);
        if (!ren_) return false;
        software_ = true; renderer_name_ = "software";
        SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_BLEND);
        gfx_ = std::make_unique<Gfx>();
        if (!gfx_->init(win_, ren_)) return false;
    }
    ui_ = std::make_unique<Ui>(*gfx_);
    apply_theme();
    char* base = SDL_GetBasePath();
#if defined(__ANDROID__)
    assets_dir_ = "fonts/";      // APK assets are opened through SDL_RWFromFile relative to the asset root; there is no ../
    icons_dir_ = "icons/";
#else
    assets_dir_ = std::string(base ? base : "./") + "assets/fonts/";
    icons_dir_ = std::string(base ? base : "./") + "assets/icons/";
#endif
    SDL_free(base);
    apply_app_icon(false);
    ui_->f_body = F(400, 15);
    ui_->f_small = F(400, 12.5f);
    ui_->f_title = F(700, 26);
    ui_->f_bold = F(500, 15);
    ui_->draw_fa = [this](uint32_t cp, Rect r, Color c) {
        const float px = std::min(r.w, r.h) * 0.9f / std::max(1.0f, S_);
        fa(cp, r.x + r.w / 2, r.y + r.h / 2, px, c);
    };
    if (ui_->f_body < 0 || ui_->f_title < 0) {
        std::fprintf(stderr, "fonts not found in %s\n", assets_dir_.c_str());
        SDL_Log("fonts not found in %s", assets_dir_.c_str());
        return false;
    }
    worker_ = std::make_unique<Worker>(3);
    mpris_.start("xylonic-fiber", [this](MprisCmd c, double a, const std::string& sa) { mpris_command(c, a, sa); });
    images_ = std::make_unique<ImageCache>(ren_, *worker_, img_dir_, 96);
    apply_perf_mode();   // fetch concurrency, texture and disk budgets for the stored mode
    metered_timer_ = SDL_AddTimer(30000, [](Uint32 interval, void* self) -> Uint32 { ui_post([self] { static_cast<App*>(self)->check_metered(); }); return interval; }, this);
    images_->set_on_ready([this] { mark(); });
    ui_post([] {});  // registers the wake-up event type on the UI thread
    ui_drain();

    f_url_ = cfg_.server_url;
    f_user_ = cfg_.username;
    if (!opt_.server.empty()) { f_url_ = opt_.server; f_user_ = opt_.user; f_pass_ = opt_.pass; }
    // The LAN sweep (hundreds of connect attempts on 48 threads) only helps someone who has to pick a server. With a saved login it
    // would burn CPU through startup, so it waits until sign-in turns out to need it (no saved password, or the connect fails).
    const bool saved_login = !cfg_.server_url.empty() && !cfg_.username.empty();
    if (opt_.server.empty() && !saved_login) start_scan();
    if (opt_.server.empty() && !cfg_.server_url.empty() && !cfg_.username.empty()) {
        if (cfg_.offline_mode) enter_offline();   // like the original: offline mode survives a restart and nothing connects until it is switched off
        else {
            // The sign-in screen draws at once; the stored password arrives later and signs in by itself.
            lookup_secret_async(cfg_.server_url + "|" + cfg_.username, [this](bool ok, std::string saved) {
                if (!ok && scan_gen_ == 0) start_scan();
                if (ok && screen_ == Screen::Login && !connecting_ && !client_ && f_pass_.empty()) { f_pass_ = std::move(saved); connect(false); }
            });
        }
    }
    if (!opt_.server.empty() && !opt_.pass.empty()) connect(false);
    if (opt_.offline) { enter_offline(); if (opt_.nav == "downloads") set_nav(Nav::Downloads); else if (opt_.nav == "library") set_nav(Nav::Library); else if (opt_.nav == "settings") set_nav(Nav::Settings); }
    if (opt_.offline && opt_.modal == "appicon") modal_ = Modal::AppIcon;
    if (!opt_.shot_path.empty()) shot_deadline_ = SDL_GetTicks() + static_cast<uint32_t>(opt_.shot_after_ms);
    return true;
}

// ---------------------------------------------------------------- session

void App::lookup_secret_async(const std::string& account, std::function<void(bool, std::string)> done) {
    std::thread([alive = alive_, account, done = std::move(done)] {
        std::string pw;
        const bool ok = secret_lookup(account, pw);
        ui_post([alive, ok, pw = std::move(pw), done = std::move(done)]() mutable { if (*alive) done(ok, std::move(pw)); });
    }).detach();
}

// Saves into the OS keyring (libsecret / Keystore / Keychain) off the UI thread: an unlock prompt or a slow D-Bus start must not freeze the window.
void App::store_secret_async(const std::string& account, const std::string& pw) {
    if (!secret_available()) { toast_ = "No system keyring found: the password was not saved"; toast_until_ = SDL_GetTicks() + 4000; mark(); return; }
    std::thread([this, alive = alive_, account, pw] {
        const bool ok = secret_store(account, pw);
        if (!ok) ui_post([this, alive] {
            if (!*alive) return;
            toast_ = "Keyring locked or unavailable: the password was not saved"; toast_until_ = SDL_GetTicks() + 4000; mark();
        });
    }).detach();
}

void App::connect(bool trust_pin) {
    if (connecting_) return;
    Url u;
    std::string url = f_url_;
    if (url.find("://") == std::string::npos) url = "http://" + url;
    if (!parse_url(url, u)) { login_msg_ = "Enter a full server address, e.g. http://host:4533"; mark(); return; }
    connecting_ = true;
    cert_prompt_ = false;
    login_msg_ = "Connecting...";
    copt_ = ConnectOptions{};
    copt_.timeout_ms = 10000;
    if (trust_pin) copt_.pin_sha256 = cert_fp_;
    else if (!login_pin_.empty()) copt_.pin_sha256 = login_pin_;
    else if (url == cfg_.server_url) copt_.pin_sha256 = cfg_.pin_sha256;
    auto client = std::make_shared<SubsonicClient>(url, f_user_, f_pass_, copt_);
    uint32_t sess = ++session_;
    ConnectOptions co = copt_;
    worker_->post([this, client, sess, u, url, co] {
        std::string err;
        bool ok = client->ping(err);
        std::string fp;
        if (!ok && u.scheme == "https" && co.pin_sha256.empty() && err.find("TLS handshake") != std::string::npos) {
            std::string e2;
            if (!fetch_cert_fingerprint(u.host, u.port, 5000, fp, e2)) fp.clear();
        }
        ui_post([this, client, sess, url, ok, err, fp] {
            if (sess != session_) return;
            connecting_ = false;
            xf_log("connect %s: %s", url.c_str(), ok ? "ok" : (err.empty() ? "failed" : err.c_str()));
            if (!ok && scan_gen_ == 0 && opt_.server.empty()) start_scan();   // sign-in failed: the server list may be needed after all
            if (ok) {
                client_ = client;
                cfg_.server_url = url;
                cfg_.username = f_user_;
                cfg_.pin_sha256 = copt_.pin_sha256;
                cfg_.remember_connection(url, f_user_);
                cfg_.save(cfg_path_);
                if (opt_.server.empty()) store_secret_async(url + "|" + f_user_, f_pass_);  // not for scripted test logins
                resolve_account([this] { on_connected(); });
            } else if (!fp.empty()) {
                cert_fp_ = fp;
                cert_prompt_ = true;
                login_msg_ = "This server's certificate is not trusted.";
            } else {
                login_msg_ = err;
            }
            mark();
        });
    });
    mark();
}

// Works out which account this address belongs to. An address never seen before is compared (by a sample of the library's artist ids)
// with the accounts already known for the same user: a match means the same server reached another way (another house, a VPN, a
// hostname), so its downloads, index, queue and playlists are reused instead of starting empty. Known addresses skip the network.
void App::resolve_account(std::function<void()> done) {
    const std::string url = cfg_.server_url, user = cfg_.username;
    Config::Account* known = cfg_.find_account(url, user);
    if (known && !known->fp.empty()) { done(); return; }
    connecting_ = true;
    auto c = client_;
    const uint32_t sess = session_;
    worker_->post([this, c, sess, url, user, done = std::move(done)]() mutable {
        std::vector<Artist> artists;
        std::string err;
        std::vector<std::string> fp;
        if (c && c->artists(artists, err)) {
            std::vector<std::string> ids;
            ids.reserve(artists.size());
            for (const Artist& ar : artists) ids.push_back(ar.id);
            fp = library_fingerprint(ids);
        }
        ui_post([this, sess, url, user, fp = std::move(fp), done = std::move(done)] {
            if (sess != session_) return;
            connecting_ = false;
            if (Config::Account* ex = cfg_.find_account(url, user)) {
                if (!fp.empty()) ex->fp = fp;
            } else if (Config::Account* m = fp.empty() ? nullptr : cfg_.match_account(user, fp)) {
                m->urls.push_back(url);
                m->fp = fp;
                toast_ = "Recognised this server from an earlier connection: your downloads and library are shared"; toast_until_ = SDL_GetTicks() + 6000;
                xf_log("account: %s is the same server as %s (shared data)", url.c_str(), m->urls.front().c_str());
            } else {
                Config::Account n;
                n.id = cfg_.account_id(url, user);   // the legacy id, so data written before accounts existed stays reachable
                n.user = user; n.urls.push_back(url); n.fp = fp;
                cfg_.accounts.push_back(std::move(n));
            }
            cfg_.save(cfg_path_);
            done();
        });
    });
    mark();
}

// The configured downloads/cache folder cannot be used right now (an SD card that is not mounted yet) and a stand-in is in use.
bool App::storage_unavailable() const {
    auto slash = [](std::string r) { if (!r.empty() && r.back() != '/') r += '/'; return r; };
    return (!cfg_.cache_root.empty() && cache_base() != slash(cfg_.cache_root)) || (!cfg_.downloads_root.empty() && downloads_base() != slash(cfg_.downloads_root));
}

void App::open_account_data() {
    acct_ = cfg_.account_id(cfg_.server_url, cfg_.username);   // follows the account, not the address
    const std::string base = cache_base();
    perm_dir_ = downloads_base() + "downloads/" + acct_ + "/";
    stream_dir_ = base + "stream/" + acct_ + "/";
    storage_lost_ = storage_unavailable();
    if (storage_lost_) { toast_ = "Chosen storage is unavailable (SD card not mounted yet?). Downloads return when it is"; toast_until_ = SDL_GetTicks() + 6000; }   // the setting is kept: it applies again when the volume returns
    mkdirs(perm_dir_); mkdirs(stream_dir_);
    downloads_.reset();   // its threads read the Store: they must be gone before the Store is replaced (use-after-free otherwise)
    store_ = std::make_unique<Store>();
    if (!store_->open(pref_ + "data_" + acct_ + ".json")) store_.reset();
    if (store_ && images_) images_->set_alias(store_->cover_alias());   // song covers resolve to their album art, offline starts included
    if (store_ && !storage_lost_) {   // the cache may have moved since these rows were written: re-root them under the current folder (never onto a stand-in folder: the rows must keep pointing at the real one)
        const std::string marker = "/downloads/" + acct_ + "/";
        for (auto& r : store_->dl_all()) {
            const std::string np = heal_path(r.path, marker, perm_dir_);
            if (np != r.path) store_->dl_set(r.id, r.state, r.bytes, r.total, np, r.content_hash);
        }
    }
    downloads_.reset();
    if (store_) {
        auto c = client_;
        downloads_ = std::make_unique<Downloads>(*store_, perm_dir_, [this, c](const std::string& id) {
            return c ? c->download_url(id, cfg_.download_quality) : std::string();   // read per request so the setting applies at once
        }, copt_, cfg_.concurrent_downloads);
        // Per-song quality (the picker's choice, or the setting when -1). The player decodes MP3 and FLAC only, so an original in any other
        // format, or any bitrate-limited download, is fetched as MP3 through stream.view; FLAC and MP3 originals come through download.view.
        downloads_->set_quality_url_fn([this, c](const std::string& id, int quality, const std::string& suffix) {
            if (!c) return std::string();
            const int kbps = quality < 0 ? cfg_.download_quality : quality;
            const bool native = !decoder_kind_for_ext(lower_ext(suffix)).empty();   // anything the player cannot decode is fetched as MP3
            return c->download_url(id, kbps, kbps > 0 || !native);
        });
        downloads_->set_default_quality_fn([this] { return cfg_.download_quality; });
        downloads_->set_exact_fn([this](int quality, const std::string& suffix) {
            const int kbps = quality < 0 ? cfg_.download_quality : quality;
            return kbps == 0 && !decoder_kind_for_ext(lower_ext(suffix)).empty();   // matches the download.view branch of the URL builder above
        });
        downloads_->set_quality_label(cfg_.download_quality ? std::to_string(cfg_.download_quality) : "original");
        downloads_->set_on_change([this] { ui_post([this] { dl_dirty_ = true; mark(); }); });
        downloads_->set_on_done([this, c](const Song& sg) {   // cover art of every downloaded song, fetched once per cover (offline browsing shows it)
            if (c && images_ && !sg.cover_art.empty()) images_->fetch_to_disk(sg.cover_art, [c, id = sg.cover_art](int size) { return c->cover_url(id, size); });
        });
        if (storage_lost_) downloads_->pause_all();   // nothing may be fetched or dropped while the real folder is away
        else downloads_->start_verify();   // drops rows whose file vanished, on worker threads (it used to block the first frame)
    }
    dl_dirty_ = true;
}

// ArtistList: the main artist of "A feat. B", "A & B", ... lower-cased with collapsed spaces
std::string App::norm_artist(const std::string& full) {
    static const char* seps[] = {", ", " \xE2\x80\xA2 ", " - ", " feat.", " feat ", " ft.", " ft ", " with ", " & "};
    std::string m = full;
    for (const char* sp : seps) { size_t i = full.find(sp); if (i != std::string::npos && i > 0) { m = full.substr(0, i); break; } }
    std::string o; bool sp = false;
    for (unsigned char c : m) { if (std::isspace(c)) { sp = true; continue; } if (sp && !o.empty()) o += ' '; sp = false; o += static_cast<char>(std::tolower(c)); }
    return o;
}

bool App::album_cached(const Album& al) const {
    auto it = cached_album_songs_.find(al.id);
    if (it == cached_album_songs_.end()) return false;
    return al.song_count ? it->second >= al.song_count : it->second > 0;
}

// AlbumList: "Cover art cached" (top-left, 24 px) once the cover sits in the on-disk image cache
void App::draw_cover_cached_badge(const Rect& cov, const std::string& cover_id) {
    if (cover_id.empty() || !images_ || !images_->on_disk(cover_id)) return;
    const float s = S_;
    Rect b{cov.x + 8 * s, cov.y + 8 * s, 24 * s, 24 * s};
    gfx_->fill_round(b, 12 * s, {0, 0, 0, 178});
    fa(0xf03e, b.x + 12 * s, b.y + 12 * s, 12, ui_->th.accent);
}

void App::draw_cached_badge(const Rect& cov) {
    const float s = S_;
    Rect b{cov.x + cov.w - 36 * s, cov.y + 8 * s, 28 * s, 28 * s};
    gfx_->fill_round(b, 14 * s, {0, 0, 0, 178});
    fa(skin::CircleCheck, b.x + 14 * s, b.y + 14 * s, 16, ui_->th.accent);
}

void App::refresh_dl_state() {
    dl_dirty_ = false;
    dl_state_.clear();
    cached_artists_.clear(); cached_album_songs_.clear();
    if (!downloads_) return;
    auto snap = downloads_->snapshot();
    for (auto& it : *snap) if (it.state == Downloads::Done) {   // library badges: "Has cached songs" (artist) and "Album cached"
        cached_artists_.insert(norm_artist(it.song.artist));
        ++cached_album_songs_[it.song.album_id];
    }
    for (auto& it : *snap) dl_state_[it.song.id] = {it.state, it.state == Downloads::Active ? downloads_->live_bytes(it.song.id, it.bytes) : it.bytes, it.total};
}

int App::songs_downloaded() const {
    if (!downloads_) return 0;
    auto snap = downloads_->snapshot();
    const uint64_t ver = store_ ? store_->dl_version() : 0;
    if (snap.get() == songs_dl_snap_ && ver == songs_dl_ver_) return songs_dl_n_;
    int n = 0;
    for (const auto& it : *snap) if (it.state == Downloads::Done) ++n;
    songs_dl_snap_ = snap.get(); songs_dl_n_ = n; songs_dl_ver_ = ver;
    return n;
}

// Every 30 s: with the setting on and songs downloaded, a metered connection switches to offline mode and an unmetered one switches back.
void App::check_metered() {
    check_connectivity();   // reachable -> unreachable offers offline mode
    if (!worker_ || (!cfg_.auto_offline_metered && !auto_offline_active_)) return;
    worker_->post([this] { const bool m = cfg_.auto_offline_metered && network_metered(); ui_post([this, m] { apply_metered(m); }); });
}

void App::apply_metered(bool metered) {
    if (metered && !offline_ && cfg_.auto_offline_metered && songs_downloaded() > 0 && !cellular_prompt_shown_) {
        auto_offline_active_ = true; cellular_prompt_shown_ = true; toggle_offline(); prompt_ = Prompt::Cellular; mark();   // "Mobile Data Detected"
    } else if (!metered && auto_offline_active_) {
        auto_offline_active_ = false; cellular_prompt_shown_ = false;
        if (offline_) toggle_offline();
    }
}

void App::enter_offline() {
    if (cfg_.server_url.empty() || cfg_.username.empty()) return;
    offline_ = true;
    if (!cfg_.offline_mode) { cfg_.offline_mode = true; cfg_.save(cfg_path_); }
    client_.reset();
    screen_ = Screen::Main;
    views_.clear();
    views_.push_back({ViewKind::Root, "", "Library", {}});
    nav_ = Nav::Library;   // Discover needs the server: offline starts in the Library
    open_account_data();
    queue_.load_songs(pref_ + "queue_" + acct_ + ".json");
    likes_load_pending();
    load_playstate();
    queue_saved_rev_ = queue_.revision();
    history_load();
    pl_load();
    remote_start();
    load_home();
    load_artists();
    mark();
}

void App::on_connected() {
    offline_ = false;
    screen_ = Screen::Main;
    images_->set_connect_options(copt_);
    images_->forget_offline_misses();   // covers that could not load offline get a real try now
    f_pass_.clear();
    views_.clear();
    views_.push_back({ViewKind::Root, "", "Home", {}});
    nav_ = Nav::Home;
    open_account_data();
    std::string acct = acct_;
    queue_.load_songs(pref_ + "queue_" + acct + ".json");
    likes_load_pending();
    load_playstate();
    queue_saved_rev_ = queue_.revision();
    history_load();
    pl_load();
    load_home();
    load_artists();
    like_sync_ms_ = SDL_GetTicks();
    likes_sync();   // unsent likes go out, then the server's starred list refreshes the queue, the index and the Liked page
    check_first_run();   // first launch for this account: build the performance cache
    if (store_) images_->set_alias(store_->cover_alias());   // song covers resolve to their album art
    if (downloads_ && client_) {   // downloaded songs keep their cover art on disk, so the library looks right offline (older downloads did not fetch it)
        worker_->post([this, c = client_, s = session_] {
            std::vector<std::string> ids;
            std::unordered_set<std::string> have, artist_ids;
            for (const Album& al : downloads_->done_albums()) for (const Song& sg : al.songs) {
                if (!sg.cover_art.empty() && have.insert(sg.cover_art).second) ids.push_back(sg.cover_art);
                if (!sg.artist_id.empty()) artist_ids.insert(sg.artist_id);
            }
            std::string aj; std::vector<Artist> all_artists;   // artist pictures of downloaded music too, so the offline library is not blank
            if (store_ && store_->get("artists", aj) && from_json(aj, all_artists))
                for (const Artist& ar : all_artists) if (!ar.cover_art.empty() && artist_ids.count(ar.id) && have.insert(ar.cover_art).second) ids.push_back(ar.cover_art);
            for (const std::string& id : ids) { if (s != session_) return; images_->fetch_to_disk(id, [c, id](int size) { return c->cover_url(id, size); }); }
        });
    }
    SDL_AddTimer(3000, [](Uint32, void* self) -> Uint32 { ui_post([self] { static_cast<App*>(self)->check_library_changes(); }); return 0; }, this);   // then compare server counts
    SDL_AddTimer(5000, [](Uint32, void* self) -> Uint32 { ui_post([self] { static_cast<App*>(self)->check_missing_songs(); }); return 0; }, this);   // and look for songs not downloaded yet
    if (opt_.nav == "library") set_nav(Nav::Library);
    else if (opt_.nav == "queue") set_nav(Nav::Queue);
    else if (opt_.nav == "settings") set_nav(Nav::Settings);
    else if (opt_.nav == "downloads") set_nav(Nav::Downloads);
    else if (opt_.nav == "search") set_nav(Nav::Search);
    else if (opt_.nav.rfind("artist:", 0) == 0) { set_nav(Nav::Library); push_view(ViewKind::Artist, opt_.nav.substr(7), "Artist 0"); load_artist(opt_.nav.substr(7)); }
    else if (opt_.nav.rfind("album:", 0) == 0) { push_view(ViewKind::Album, opt_.nav.substr(6), "Album"); load_album(opt_.nav.substr(6)); }
    if (opt_.nowplaying) np_open_ = true;
    if (opt_.pulse) cfg_.car_pulse = true;
    if (opt_.carmode || cfg_.car_start) set_car_mode(true);
    if (opt_.help) show_help_ = true;
    if (opt_.scroll > 0) { settings_scroll_.y = static_cast<float>(opt_.scroll) * S_; dl_scroll_.y = settings_scroll_.y; }
    if (opt_.dm.find("queue") != std::string::npos) dm_show_queue_ = true;
    if (opt_.dm.find("manage") != std::string::npos) dm_manage_ = true;
    if (!opt_.panel.empty()) toggle_panel(opt_.panel == "history" ? PanelTab::History : opt_.panel == "playlists" ? PanelTab::Playlists : PanelTab::Queue);
    if (opt_.modal == "dlpicker") { open_download_picker(false); } else if (opt_.modal == "banner") { missing_songs_ = 25; missing_banner_ = true; } else if (opt_.modal == "offlineprompt") { prompt_ = Prompt::Offline; } else if (opt_.modal == "cellprompt") { prompt_ = Prompt::Cellular; } else if (opt_.modal == "newcontent") { nc_outdated_ = false; nc_artists_ = 3; nc_songs_ = 25; newcontent_open_ = true; } else if (opt_.modal == "outdated") { nc_outdated_ = true; newcontent_open_ = true; } else if (opt_.modal == "rebuild") modal_ = Modal::ConfirmRebuild; else if (opt_.modal == "dlquality") modal_ = Modal::DownloadQuality; else if (opt_.modal == "reauth") modal_ = Modal::Reauth; else if (opt_.modal == "theme") modal_ = Modal::Theme; else if (opt_.modal == "themeedit") modal_ = Modal::ThemeEdit; else if (opt_.modal == "firewall") modal_ = Modal::Firewall; else if (opt_.modal == "appicon") modal_ = Modal::AppIcon;
    if (!opt_.search.empty()) { search_q_ = opt_.search; nav_ = Nav::Search; views_.clear(); views_.push_back({ViewKind::Root, "", "Search", {}}); run_search(); }
    if (!opt_.download_album.empty()) {
        worker_->post([this, c = client_, s = session_, id = opt_.download_album] {
            Album a; std::string e;
            if (c->album(id, a, e)) ui_post([this, s, a] { if (s == session_ && downloads_) { downloads_->enqueue(a.songs); prefetch_covers(a.songs); } });
        });
    }
    if (opt_.autoplay) {
        std::string sess_acct = acct;
        (void)sess_acct;
        worker_->post([this, c = client_, s = session_] {
            std::vector<Album> l; std::string e;
            Album a;
            if (c->album_list("newest", 1, 0, l, e) && !l.empty() && c->album(l[0].id, a, e))
                ui_post([this, s, a] { if (s == session_) play_songs(a.songs, 0, false); });
        });
    }
}

void App::logout(bool forget_password) {
    cancel_cache_preload(); pre_open_ = false; newcontent_open_ = false;
    if (forget_password) secret_clear(cfg_.server_url + "|" + cfg_.username);
    player_.stop();
    ++session_;
    ++play_gen_;
    save_queue_state();
    remote_stop();
    client_.reset();
    downloads_.reset();
    store_.reset();
    offline_ = false;
    if (cfg_.offline_mode) { cfg_.offline_mode = false; cfg_.save(cfg_path_); }
    images_->clear_memory();
    for (auto& sec : sections_) { sec.albums.clear(); sec.loading = true; sec.error.clear(); sec.scroll_x = 0; }
    artists_.clear(); artist_cache_.clear(); album_cache_.clear();
    search_res_ = SubsonicClient::SearchAll{};
    queue_.clear();
    screen_ = Screen::Login;
    login_msg_.clear();
    start_scan();   // the sign-in screen is where the server list is needed
    mark();
}

void App::add_server() {
    logout(false);
    f_url_.clear(); f_user_.clear(); f_pass_.clear();   // a blank form: the previous account and its saved password stay for Switch Server
    test_ok_ = false;
    mark();
}

void App::load_home() {
    for (int i = 0; i < 4; ++i) load_section(i);
}

void App::load_artists() {
    if (loading_artists_) return;
    std::string j;
    if (store_ && store_->get("artists", j)) from_json(j, artists_);
    if (offline_ || !client_) {
        artists_.clear();
        if (downloads_) for (auto& al : downloads_->done_albums()) {
            Artist* ar = nullptr;
            for (auto& x : artists_) if (x.id == al.artist_id) { ar = &x; break; }
            if (!ar) { artists_.emplace_back(); ar = &artists_.back(); ar->id = al.artist_id; ar->name = al.artist; ar->cover_art = al.cover_art; }
            ar->albums.push_back(al);
            ar->album_count = static_cast<int>(ar->albums.size());
        }
        load_err_ = artists_.empty() ? "No cached songs available" : std::string();   // ArtistList: offline with nothing downloaded
        artists_loaded_ = true;
        mark();
        return;
    }
    loading_artists_ = true;
    worker_->post([this, c = client_, s = session_] {
        std::vector<Artist> a; std::string e;
        bool ok = c->artists(a, e);
        ui_post([this, s, a = std::move(a), ok, e]() mutable {
            if (s != session_) return;
            loading_artists_ = false;
            artists_loaded_ = true;
            if (ok) { if (store_) store_->put("artists", to_json(a)); artists_ = std::move(a); load_err_.clear(); } else load_err_ = e.empty() ? "Failed to load artists" : e;
            mark();
        });
    });
}

void App::load_artist(const std::string& id) {
    if (artist_cache_.count(id)) return;
    artist_cache_[id] = Artist{};  // placeholder marks "loading"
    std::string j;
    if (store_ && store_->get("artist:" + id, j)) { Artist a; if (from_json(j, a)) artist_cache_[id] = a; }
    if (offline_ || !client_) {
        for (auto& x : artists_) if (x.id == id) artist_cache_[id] = x;
        mark();
        return;
    }
    worker_->post([this, c = client_, s = session_, id] {
        Artist a; std::string e;
        bool ok = c->artist(id, a, e);
        ui_post([this, s, id, a = std::move(a), ok] {
            if (s != session_) return;
            if (ok) { artist_cache_[id] = a; if (store_) { store_->put("artist:" + id, to_json(a)); for (const Album& al : a.albums) store_->index_album(al); } }
            else if (artist_cache_[id].id.empty()) artist_cache_.erase(id);
            mark();
        });
    });
}

void App::load_album(const std::string& id) {
    if (album_cache_.count(id)) return;
    album_cache_[id] = Album{};
    std::string j;
    if (store_ && store_->get("album:" + id, j)) { Album a; if (from_json(j, a)) album_cache_[id] = a; }
    if (offline_ || !client_) {
        if (downloads_) for (auto& al : downloads_->done_albums()) if (al.id == id) album_cache_[id] = al;
        mark();
        return;
    }
    worker_->post([this, c = client_, s = session_, id] {
        Album a; std::string e;
        bool ok = c->album(id, a, e);
        ui_post([this, s, id, a = std::move(a), ok] {
            if (s != session_) return;
            if (ok) { album_cache_[id] = a; if (store_) { store_->put("album:" + id, to_json(a)); store_->index_album(a); } }
            else if (album_cache_[id].id.empty()) album_cache_.erase(id);
            mark();
        });
    });
}

SDL_Texture* App::cover(const std::string& id, int px, const std::string& artist, const std::string& album, bool hi) {
    // Performance modes request smaller textures: Gaming 3/4, Eco 1/2 of the device size (the disk copy is always the 300 px original).
    const float mode = cfg_.perf_mode == 2 ? 0.5f : cfg_.perf_mode == 1 ? 0.75f : 1.0f;
    const int dev = std::max(32, static_cast<int>(px * S_ * mode));
    auto c = offline_ ? nullptr : client_;  // offline: empty URL, the image only resolves from the disk cache (and no request leaves the device)
    ImageCache::UrlFn url = [c, id](int size) { return c ? c->cover_url(id, size) : std::string(); };
    if (id.empty()) {
        if (offline_ || (artist.empty() && album.empty())) return nullptr;
        return images_->get("net:" + artist + "::" + album + "@" + std::to_string(dev), url, artist, album);
    }
    return images_->get((hi ? "hi:" : "") + cover_key(id, dev), std::move(url));
}

void App::draw_art_fallback(Rect r, const std::string& id, float radius) {
    const Theme& th = ui_->th;
    gfx_->fill_round(r, radius, th.panel2);                 // album-art-loading
    if (id.empty() ? false : !images_->fallback_id(id)) return;
    fa(id.compare(0, 3, "ar-") == 0 ? 0xf2bdu : static_cast<uint32_t>(skin::Music), r.x + r.w / 2, r.y + r.h / 2, std::max(12.0f, r.w / S_ / 3), Color{th.text.r, th.text.g, th.text.b, 90});
}

// Downloads the covers (300 px, disk only) of these songs in the background, as the original's cover prefetch did.
void App::prefetch_covers(const std::vector<Song>& songs) {
    if (!client_) return;
    std::vector<std::string> ids;
    for (const Song& sg : songs) if (!sg.cover_art.empty() && std::find(ids.begin(), ids.end(), sg.cover_art) == ids.end()) ids.push_back(sg.cover_art);
    if (ids.empty()) return;
    worker_->post([this, c = client_, ids = std::move(ids)] {
        for (const std::string& id : ids) images_->fetch_to_disk(id, [c, id](int size) { return c->cover_url(id, size); });
    });
}

ThemeDef App::theme_def(int idx) const {
    if (idx >= kPresetCount) {
        const Config::Custom& c = cfg_.custom[(idx - kPresetCount) % kCustomSlots];
        return make_custom(c.primary, c.name);
    }
    return preset(idx);
}

void App::apply_theme() {
    bool light = cfg_.theme_mode == 1;  // 0 = follow the system: dark until platform detection lands (docs/port-map.md)
    ui_->th = make_theme(theme_def(cfg_.theme), light);
    mark();
}

void App::push_view(ViewKind k, const std::string& id, const std::string& title) { views_.push_back({k, id, title, {}}); mark(); }
// Back: close the topmost thing (keyboard, dialog, overlay, panel), then walk up (artist/album page, Home), and at the top level
// leave the app running in the background while music plays or close it.
void App::handle_back() {
    if (ui_->focus) { ui_->focus = 0; mark(); return; }
    const bool overlay = modal_ != Modal::None || add_song_ || show_help_ || ctx_song_ || hamburger_open_ || pre_open_ || newcontent_open_ || prompt_ != Prompt::None || car_open_ || mini_open_ || np_open_ || panel_open_ || bottom_menu_open_;
    if (overlay) {
        ++ui_->in.escape;   // dialogs and Car Mode close on Escape
        np_open_ = false; panel_open_ = false; bottom_menu_open_ = false;
        mark();
        return;
    }
    if (screen_ == Screen::Main && views_.size() > 1) { pop_view(); return; }
    if (screen_ == Screen::Main && nav_ != Nav::Home) { set_nav(Nav::Home); return; }
#if defined(__ANDROID__)
    android_bridge::back(player_.playing() || track_loading_);
#endif
}

void App::pop_view() { if (views_.size() > 1) { views_.pop_back(); mark(); } }

void App::set_nav(Nav n) {
    if (n == Nav::Downloads && nav_ != Nav::Downloads) { dl_prev_nav_ = nav_; dl_has_prev_ = true; }
    nav_ = n;
    perf_stats_.loaded = false;   // Settings re-reads the cache stats when opened
    views_.clear();
    static const char* names[] = {"Home", "Library", "Search", "Queue", "Downloads", "Settings"};
    views_.push_back({ViewKind::Root, "", names[static_cast<int>(n)], {}});
    if (n == Nav::Search) ui_->focus = kSearch;
    if (n == Nav::Home && client_ && !offline_) {   // DiscoverView remount: refetch sections older than 5 minutes; "random" is never cached
        const uint64_t now = SDL_GetTicks64();
        for (int i = 0; i < 4; ++i) if (!sections_[i].loading && (i == 3 || now - sections_[i].fetched_ms > 5ull * 60 * 1000)) load_section(i);
    }
    mark();
}

// ---------------------------------------------------------------- playback

std::string App::track_kind(const Song& s) const {
    const std::string e = lower_ext(s.suffix), k = decoder_kind_for_ext(e);
    // the original file when the player can decode it and no bitrate cap applies (FLAC ignores the cap); otherwise the server sends MP3
    return !k.empty() && (cfg_.stream_quality == 0 || e == "flac") ? k : std::string("mp3");
}

std::string App::perm_path(const Song& s) const { return downloads_ ? downloads_->path_if_done(s.id) : std::string(); }

std::string App::stream_path(const Song& s) const {
    std::string e = lower_ext(s.suffix);
    const bool original = !decoder_kind_for_ext(e).empty() && (cfg_.stream_quality == 0 || e == "flac");
    std::string q = cfg_.stream_quality > 0 && e != "flac" ? "_q" + std::to_string(cfg_.stream_quality) : "";
    return stream_dir_ + s.id + q + "." + (original ? e : std::string("mp3"));
}

void App::play_songs(const std::vector<Song>& songs, size_t start, bool shuffle) {
    if (songs.empty()) return;
    queue_.set_shuffle(false);
    queue_.set(songs, start);
    queue_.set_shuffle(shuffle);
    start_current();
}

// A transcoded stream (maxBitRate) without an explicit format comes back in whatever the server's default transcoding is (Navidrome:
// often Opus or AAC): ask for MP3. Formats the player has no decoder for (WMA, APE, DSD...) are also asked for as MP3.
static std::string stream_format(const std::string& ext, int kbps) {
    if (kbps > 0) return "mp3";
    return !decoder_kind_for_ext(ext).empty() ? std::string() : std::string("mp3");
}

void App::start_current() {
    const Song* s = queue_.current();
    if (!s) return;
    Song song = *s;
    uint32_t gen = ++play_gen_;
    player_.clear_next();
    if (queue_.size() > 1) prefetch_covers(queue_.neighbours(2));   // what skip back/forward reaches next
    eq_bass_db_ = eq_treble_db_ = 0;
    player_.set_eq(0, 0);
    preloaded_id_.clear();
    preloading_ = false;
    track_loading_ = true;
    play_err_.clear();
    player_.stop();
    std::string e = lower_ext(song.suffix);
    int kbps = cfg_.stream_quality;
    std::string url = client_ ? client_->stream_url(song.id, e == "flac" ? 0 : kbps, stream_format(e, e == "flac" ? 0 : kbps)) : std::string();
    worker_->post([this, gen, song, url, c = copt_, perm = perm_path(song), strm = stream_path(song), kind = track_kind(song)] {
        std::string err;
        auto dec = std::make_shared<std::unique_ptr<Decoder>>(open_track(url, perm, strm, kind, c, err));
        ui_post([this, gen, dec, err, song] {
            if (gen != play_gen_) return;  // superseded by a newer track
            track_loading_ = false;
            if (!*dec) { play_err_ = err.empty() ? "Cannot play this track" : err; mark(); return; }
            player_.set_volume(vol_);
            const bool attached = player_.attach(std::move(*dec), static_cast<double>(song.duration));
            const bool started = attached && player_.play();
            if (started && resume_id_ == song.id && resume_pos_ > 1.0 && resume_pos_ < static_cast<double>(song.duration) - 2.0) player_.seek_seconds(resume_pos_);   // pick up where the last session stopped
            resume_id_.clear(); resume_pos_ = 0;
            xf_log("play %s: attached=%d started=%d", song.id.c_str(), attached, started);
            SDL_Log("play %s: attached=%d started=%d: %s", song.id.c_str(), attached, started, started ? "" : SDL_GetError());
            scrobbled_id_.clear();
            history_add(song);
            if (client_) worker_->post([c = client_, id = song.id] { std::string e; c->scrobble(id, false, e); });
            save_playstate();
            mark();
        });
    });
    mark();
}

void App::toggle_play() {
    if (remote_mode_) { remote_send("togglePlay"); return; }
    if (track_loading_) return;
    if (!queue_.current()) return;
    if (player_.playing()) player_.pause();
    else if (player_.duration_seconds() > 0 || player_.position_seconds() > 0) player_.play();
    else start_current();
    save_playstate();
    mark();
}

void App::next_track() { if (remote_mode_) { remote_send("next"); return; } if (queue_.next()) start_current(); }

void App::prev_track() {
    if (remote_mode_) { remote_send("previous"); return; }
    if (player_.position_seconds() > 3.0) { player_.seek_seconds(0); mark(); return; }
    if (queue_.prev()) start_current();
}

void App::mpris_command(MprisCmd c, double arg, const std::string& sarg) {
    ui_post([this, c, arg, sarg] {
        switch (c) {
        case MprisCmd::Play: if (!player_.playing()) toggle_play(); break;
        case MprisCmd::Pause: case MprisCmd::Stop: if (player_.playing()) player_.pause(); break;
        case MprisCmd::PlayPause: toggle_play(); break;
        case MprisCmd::Next: next_track(); break;
        case MprisCmd::Previous: prev_track(); break;
        case MprisCmd::SeekRelative: player_.seek_seconds(player_.position_seconds() + arg); break;
        case MprisCmd::SeekAbsolute: player_.seek_seconds(arg); break;
        case MprisCmd::SetVolume: set_volume(static_cast<float>(arg)); break;
        case MprisCmd::SetShuffle: queue_.set_shuffle(arg != 0); break;
        case MprisCmd::SetLoop: queue_.set_repeat(sarg == "Track" ? Repeat::One : sarg == "Playlist" ? Repeat::All : Repeat::Off); break;
        case MprisCmd::Raise: if (win_) SDL_RaiseWindow(win_); break;
        }
        mark();
    });
}

void App::mpris_push() {
    { const uint32_t t = SDL_GetTicks(); if (t - mpris_push_ms_ < 250) return; mpris_push_ms_ = t; }   // 4 Hz is plenty: this built a signature string and hit the cover cache on every frame
    const Song* cur = queue_.current();
    MprisState st;
    st.has_song = cur != nullptr;
    st.playing = player_.playing();
    st.shuffle = queue_.shuffle();
    st.loop = queue_.repeat() == Repeat::One ? "Track" : queue_.repeat() == Repeat::All ? "Playlist" : "None";
    st.volume = vol_;
    st.downloads = downloads_ ? downloads_->pending() : 0;
    st.position_s = player_.position_seconds();
    st.length_s = player_.duration_seconds();
    if (cur) {
        st.title = cur->title; st.artist = cur->artist; st.album = cur->album; st.track_id = cur->id;
        if (!cur->cover_art.empty()) {
            if (client_ && mpris_art_id_ != cur->cover_art) {
                mpris_art_id_ = cur->cover_art;
                worker_->post([this, c = client_, id = cur->cover_art] { images_->fetch_to_disk(id, [c, id](int size) { return c->cover_url(id, size); }); });
            }
            std::string path = images_->disk_path(cur->cover_art);
            if (!path.empty() && ::access(path.c_str(), R_OK) == 0) st.art_url = "file://" + path;
        }
    }
    uint32_t now = SDL_GetTicks();
    std::string sig = st.title + "|" + st.track_id + "|" + std::to_string(st.playing) + "|" + std::to_string(st.shuffle) + st.loop + std::to_string(static_cast<int>(st.volume * 100)) + st.art_url + "|" + std::to_string(st.downloads);
    if (sig != mpris_sig_ || now - mpris_last_ms_ > 1000) { mpris_sig_ = sig; mpris_last_ms_ = now; mpris_.update(st); }
}

void App::preload_next() {
    if (preloading_ || queue_.empty() || (!client_ && !downloads_)) return;
    // the track that follows the current one (repeat-one replays the same song)
    size_t n = queue_.size();
    const Song* nx = nullptr;
    if (queue_.repeat() == Repeat::One) nx = queue_.current();
    else {
        // peek without moving: work on a copy of the order via a scratch queue
        Queue scratch = queue_;
        nx = scratch.next();
        if (!nx) return;
        static Song held;
        held = *nx;
        nx = &held;
    }
    (void)n;
    if (!nx || preloaded_id_ == nx->id) return;
    Song song = *nx;
    preloading_ = true;
    preloaded_id_ = song.id;
    uint32_t gen = play_gen_;
    std::string e = lower_ext(song.suffix);
    std::string url = client_ ? client_->stream_url(song.id, e == "flac" ? 0 : cfg_.stream_quality, stream_format(e, e == "flac" ? 0 : cfg_.stream_quality)) : std::string();
    worker_->post([this, gen, song, url, c = copt_, perm = perm_path(song), strm = stream_path(song), kind = track_kind(song)] {
        std::string err;
        auto dec = std::make_shared<std::unique_ptr<Decoder>>(open_track(url, perm, strm, kind, c, err));
        ui_post([this, gen, dec, song] {
            preloading_ = false;
            if (gen != play_gen_ || !*dec) return;   // a failed preload keeps preloaded_id_ set so the same song is not retried every tick
            player_.set_next(std::move(*dec), static_cast<double>(song.duration));
        });
    });
}

void App::tick_player() {
    if (track_loading_) return;
    if (player_.take_advanced()) {  // gapless switch happened: move the queue on without reloading
        if (queue_.on_track_end()) {
            const Song* c = queue_.current();
            if (c) { history_add(*c); scrobbled_id_.clear(); if (client_) worker_->post([cl = client_, id = c->id] { std::string e; cl->scrobble(id, false, e); }); }
        }
        preloaded_id_.clear();
        eq_bass_db_ = eq_treble_db_ = 0;
        player_.set_eq(0, 0);
        mark();
    }
    if (player_.playing()) {
        double d = player_.duration_seconds(), p = player_.position_seconds();
        if (d > 0 && d - p <= 15.0 && !player_.has_next() && !preloading_) preload_next();  // gapless preload safety-net
    }
    if (player_.playing() && player_.finished()) {
        if (queue_.on_track_end()) start_current();
        return;
    }
    const Song* cur = queue_.current();
    if (cur && player_.playing() && scrobbled_id_ != cur->id && cur->duration > 0 && player_.position_seconds() > std::min(240.0, cur->duration * 0.5) && client_) {
        scrobbled_id_ = cur->id;
        worker_->post([c = client_, id = cur->id] { std::string e; c->scrobble(id, true, e); });
    }
    int sec = static_cast<int>(player_.position_seconds());
    if (sec != shown_sec_) { shown_sec_ = sec; mark(); }
    mpris_push();
    remote_push_state();
}

void App::set_volume(float v) {
    vol_ = std::min(1.0f, std::max(0.0f, v));
    player_.set_volume(vol_);
    cfg_.volume_pct = static_cast<int>(vol_ * 100 + 0.5f);
}

// Where playback stopped: current song, position, shuffle and repeat. Survives a restart or a reboot (the queue file alone only holds the songs).
void App::save_playstate() {
    if (screen_ != Screen::Main || queue_.empty() || acct_.empty()) return;
    const Song* cur = queue_.current();
    if (!cur) return;
    yyjson_mut_doc* d = yyjson_mut_doc_new(nullptr);
    yyjson_mut_val* o = yyjson_mut_obj(d);
    yyjson_mut_doc_set_root(d, o);
    yyjson_mut_obj_add_strcpy(d, o, "id", cur->id.c_str());
    yyjson_mut_obj_add_uint(d, o, "index", queue_.index());
    const double pos = track_loading_ ? 0.0 : player_.position_seconds();
    yyjson_mut_obj_add_real(d, o, "pos", pos > 0 ? pos : (resume_id_ == cur->id ? resume_pos_ : 0.0));   // still loading after a restart: keep the saved spot
    yyjson_mut_obj_add_bool(d, o, "shuffle", queue_.shuffle());
    yyjson_mut_obj_add_int(d, o, "repeat", static_cast<int>(queue_.repeat()));
    const std::string path = pref_ + "playstate_" + acct_ + ".json", tmp = path + ".tmp";
    if (yyjson_mut_write_file(tmp.c_str(), d, 0, nullptr, nullptr)) std::rename(tmp.c_str(), path.c_str());
    yyjson_mut_doc_free(d);
    playstate_saved_ms_ = SDL_GetTicks();
}

void App::load_playstate() {
    yyjson_doc* d = yyjson_read_file((pref_ + "playstate_" + acct_ + ".json").c_str(), 0, nullptr, nullptr);
    if (!d) return;
    yyjson_val* r = yyjson_doc_get_root(d);
    yyjson_val* id = yyjson_obj_get(r, "id"); yyjson_val* ix = yyjson_obj_get(r, "index"); yyjson_val* ps = yyjson_obj_get(r, "pos");
    yyjson_val* sh = yyjson_obj_get(r, "shuffle"); yyjson_val* rp = yyjson_obj_get(r, "repeat");
    if (id && yyjson_is_str(id) && ix && yyjson_is_num(ix)) {
        size_t idx = static_cast<size_t>(yyjson_get_uint(ix));
        const auto& songs = queue_.songs();
        if (idx >= songs.size() || songs[idx].id != yyjson_get_str(id)) {   // the queue changed since: find the song by id
            idx = songs.size();
            for (size_t i = 0; i < songs.size(); ++i) if (songs[i].id == yyjson_get_str(id)) { idx = i; break; }
        }
        if (idx < songs.size()) {
            queue_.jump(idx);
            resume_id_ = yyjson_get_str(id);
            resume_pos_ = ps && yyjson_is_num(ps) ? yyjson_get_num(ps) : 0.0;
            if (rp && yyjson_is_num(rp)) { const int v = static_cast<int>(yyjson_get_sint(rp)); if (v >= 0 && v <= 2) queue_.set_repeat(static_cast<Repeat>(v)); }
            if (sh && yyjson_get_bool(sh)) queue_.set_shuffle(true);   // after jump: the shuffle keeps the current song
        }
    }
    yyjson_doc_free(d);
}

void App::save_queue_state() {
    if (screen_ != Screen::Main) return;
    save_playstate();
    if (queue_.revision() != queue_saved_rev_) {
        queue_.save_songs(pref_ + "queue_" + acct_ + ".json");
        queue_saved_rev_ = queue_.revision();
    }
    cfg_.save(cfg_path_);
}

// ---------------------------------------------------------------- events / loop

void App::handle(const SDL_Event& e) {
    if (mini_win_) {
        uint32_t wid = e.type == SDL_WINDOWEVENT ? e.window.windowID : e.type == SDL_MOUSEMOTION ? e.motion.windowID : (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP) ? e.button.windowID : (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) ? e.key.windowID : 0;
        if (wid == mini_id_) { handle_mini(e); return; }
    }
    Input& in = ui_->in;
    switch (e.type) {
    case SDL_QUIT: running_ = false; break;
    case SDL_APP_WILLENTERBACKGROUND: save_playstate(); break;
    case SDL_APP_DIDENTERBACKGROUND: background_ = true; save_playstate(); break;
    case SDL_APP_WILLENTERFOREGROUND: case SDL_APP_DIDENTERFOREGROUND: background_ = false; mark(); break;
    case SDL_WINDOWEVENT:
        if (e.window.event == SDL_WINDOWEVENT_RESIZED || e.window.event == SDL_WINDOWEVENT_EXPOSED || e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED || e.window.event == SDL_WINDOWEVENT_RESTORED) mark();
        break;
    case SDL_MOUSEMOTION: in.mx = e.motion.x * M_; in.my = e.motion.y * M_; mark(); break;
    case SDL_MOUSEBUTTONDOWN:
        if (e.button.button == SDL_BUTTON_LEFT) { in.mx = e.button.x * M_; in.my = e.button.y * M_; in.down = in.pressed = true; in.press_x = in.mx; in.press_y = in.my; press_ms_ = SDL_GetTicks(); long_fired_ = false; press_touch_ = e.button.which == SDL_TOUCH_MOUSEID; mark(); }
        break;
    case SDL_MOUSEBUTTONUP:
        if (e.button.button == SDL_BUTTON_RIGHT) { in.rclick = true; in.rx = e.button.x * M_; in.ry = e.button.y * M_; mark(); }
        if (e.button.button == SDL_BUTTON_LEFT) { in.mx = e.button.x * M_; in.my = e.button.y * M_; in.down = false; const bool slid = press_touch_ && std::hypot(in.mx - in.press_x, in.my - in.press_y) > 12 * S_;   // a touch that travelled is a scroll or drag, never a tap, even when no finger-motion event marked it
            in.released = !touch_moved_ && !slid && !long_fired_; touch_moved_ = false; long_fired_ = false; press_touch_ = false; mark(); }
        break;
    case SDL_FINGERDOWN: touch_moved_ = false; touch_accum_ = 0; touch_dead_dy_ = touch_dead_dx_ = 0; fling_vel_ = 0; touch_scrolled_ = false; touch_last_ms_ = e.tfinger.timestamp; break;   // a new touch stops a fling
    case SDL_FINGERMOTION: {  // touch drag scrolls (the synthesized mouse events only click/drag widgets)
        const float sh = static_cast<float>(ui_->in.screen_h);
        float dy = e.tfinger.dy * sh, dx = e.tfinger.dx * sh;
        touch_accum_ += std::fabs(dy) + std::fabs(dx);
        if (!touch_moved_) { touch_dead_dy_ += dy; touch_dead_dx_ += dx; }
        if (touch_accum_ > 10 * S_ && ui_->active == 0) {
            // the movement inside the tap slop is applied too, so the content follows the finger from where it went down
            if (!touch_moved_) { dy = touch_dead_dy_; dx = touch_dead_dx_; }
            touch_moved_ = touch_scrolled_ = true;
            in.wheel += dy / (60 * S_); in.hwheel += -dx / (60 * S_) * -1;
            const uint32_t dt = std::max<uint32_t>(1, e.tfinger.timestamp - touch_last_ms_);
            touch_last_ms_ = e.tfinger.timestamp; touch_wall_ms_ = SDL_GetTicks();
            const float v = std::max(-60.0f, std::min(60.0f, (dy / (60 * S_)) * 1000.0f / static_cast<float>(dt)));   // wheel notches per second, smoothed
            fling_vel_ = fling_vel_ * 0.6f + v * 0.4f;
            mark();
        }
        break;
    }
    case SDL_FINGERUP:   // lifted while still moving: keep scrolling with momentum
        if (touch_scrolled_ && std::fabs(fling_vel_) >= 3.0f && SDL_GetTicks() - touch_wall_ms_ <= 100u) fling_last_ = SDL_GetTicks();
        else fling_vel_ = 0;
        break;
    case SDL_MOUSEWHEEL:
        if (SDL_GetModState() & KMOD_SHIFT) in.hwheel += e.wheel.preciseY; else in.wheel += e.wheel.preciseY;
        in.hwheel += e.wheel.preciseX;
        mark();
        break;
    case SDL_TEXTINPUT: in.text += e.text.text; mark(); break;
    case SDL_KEYDOWN: {
        SDL_Keycode k = e.key.keysym.sym;
        bool shift = e.key.keysym.mod & KMOD_SHIFT;
        in.ctrl = e.key.keysym.mod & KMOD_CTRL;
        bool typing = ui_->focus != 0;
        if (k == SDLK_DELETE && in.ctrl && shift && !typing && screen_ == Screen::Main) { modal_ = Modal::ConfirmClearCaches; mark(); }   // Ctrl+Shift+Delete: clear caches
        else if (k == SDLK_BACKSPACE) ++in.backspace;
        else if (k == SDLK_DELETE) ++in.del;
        else if (k == SDLK_LEFT && typing) ++in.left;
        else if (k == SDLK_RIGHT && typing) ++in.right;
        else if (k == SDLK_HOME) ++in.home;
        else if (k == SDLK_END) ++in.end;
        else if (k == SDLK_RETURN || k == SDLK_KP_ENTER) ++in.enter;
        else if (k == SDLK_TAB) ++in.tab;
        else if (k == SDLK_AC_BACK) handle_back();   // Android Back button / gesture (SDL_HINT_ANDROID_TRAP_BACK_BUTTON)
        else if (k == SDLK_ESCAPE) { ++in.escape; if (typing) ui_->focus = 0; else if (np_open_) np_open_ = false; else if (panel_open_) panel_open_ = false; }
        else if (in.ctrl && k == SDLK_k && screen_ == Screen::Main) set_nav(Nav::Search);
        else if (in.ctrl && k == SDLK_m && screen_ == Screen::Main) toggle_mini();
        else if (!typing && screen_ == Screen::Main) {
            if (k == SDLK_SPACE) toggle_play();
            else if (k == SDLK_RIGHT) { if (shift) next_track(); else player_.seek_seconds(player_.position_seconds() + 5); }
            else if (k == SDLK_LEFT) { if (shift) prev_track(); else player_.seek_seconds(player_.position_seconds() - 5); }
            else if (k == SDLK_UP && shift) set_volume(vol_ + 0.1f);
            else if (k == SDLK_DOWN && shift) set_volume(vol_ - 0.1f);
            else if (k == SDLK_s) queue_.set_shuffle(!queue_.shuffle());
            else if (k == SDLK_r) queue_.set_repeat(queue_.repeat() == Repeat::Off ? Repeat::All : queue_.repeat() == Repeat::All ? Repeat::One : Repeat::Off);
            else if (k == SDLK_m) set_volume(vol_ > 0 ? 0 : 0.7f);
            else if (k == SDLK_q) toggle_panel(PanelTab::Queue);
            else if (k == SDLK_h) toggle_panel(PanelTab::History);
            else if (k == SDLK_p) toggle_panel(PanelTab::Playlists);
        }
        mark();
        break;
    }
    default:
        if (e.type >= SDL_USEREVENT) ui_drain();
        break;
    }
}

// SDL 2.32 only blocks natively on X11, Wayland, Windows and Cocoa. Everywhere else SDL_WaitEvent(Timeout) is a loop of SDL_Delay(1): 1000 wakeups a
// second while the app is idle, which kept an idle Android phone at ~5% of a core. An event watch (called for every event, from any thread: touch
// from the Java thread, ui_post from workers, lifecycle) signals a semaphore instead, so the thread sleeps in the kernel until something happens.
#if defined(XF_MOBILE)
namespace {
SDL_sem* g_wake = nullptr;
int SDLCALL wake_watch(void*, SDL_Event* ev) { if (ev->type == 0x7f00 /* SDL_POLLSENTINEL: SDL pushes it on every poll */) return 1; if (g_wake && SDL_SemValue(g_wake) == 0) SDL_SemPost(g_wake); return 1; }
}  // namespace
bool App::wait_event(SDL_Event& e, int timeout_ms) {
    if (!g_wake) { g_wake = SDL_CreateSemaphore(0); SDL_AddEventWatch(wake_watch, nullptr); }
    if (SDL_PollEvent(&e)) return true;
    if (timeout_ms == 0) return false;
    if (timeout_ms < 0) SDL_SemWait(g_wake); else SDL_SemWaitTimeout(g_wake, static_cast<Uint32>(timeout_ms));
    return SDL_PollEvent(&e) != 0;
}
#else
bool App::wait_event(SDL_Event& e, int timeout_ms) { return (timeout_ms < 0 ? SDL_WaitEvent(&e) : SDL_WaitEventTimeout(&e, timeout_ms)) != 0; }
#endif

int App::run() {
    uint32_t last_draw = 0, frame_began = 0;   // frame_began: when the last frame STARTED, so the visualizer ticks at its interval and not at interval + frame cost
    while (running_) {
        bool pulse_live = car_open_ && cfg_.car_pulse && !pulse_off_ && player_.playing();
        const bool amp_live = (mini_open_ || mini_win_) && player_.playing();
        const bool rail_anim = (car_open_ && rail_vis_ > 0.0f && rail_vis_ < 1.0f) || (np_open_ && (np_anim_ || (np_gon_ && np_gdir_ == 2)));   // the Riverbero rail sliding: draw every few ms, not at the visualizer or idle rate
        bool animating = rail_anim || amp_live || storage_lost_ || fling_vel_ != 0 || pulse_live || sleep_end_ms_ != 0 || player_.playing() || track_loading_ || ui_->wants_frames || connecting_ || search_pending_ || tip_pending_ || (press_touch_ && !long_fired_ && ui_->in.down);
        SDL_Event e {};
        bool got = false;
        if (dirty_ || opt_.bench > 0) got = SDL_PollEvent(&e) != 0;
        else if (shot_deadline_) got = wait_event(e, 50);
        else if (animating) got = wait_event(e, rail_anim ? 4 : pulse_live ? static_cast<int>(std::max<int64_t>(1, static_cast<int64_t>(pulse_interval_ms()) - static_cast<int64_t>(SDL_GetTicks() - frame_began))) : amp_live ? 66 : (tip_pending_ || press_touch_) ? 80 : 250);
        else got = wait_event(e, -1);
        while (got) {
            handle(e);
            got = SDL_PollEvent(&e) != 0;
        }
        if (fling_vel_ != 0) {   // momentum after a touch scroll: decays exponentially, stops below a notch per second
            const uint32_t t = SDL_GetTicks();
            const float dt = std::min(0.05f, static_cast<float>(t - fling_last_) / 1000.0f);
            fling_last_ = t;
            if (dt > 0) {
                ui_->in.wheel += fling_vel_ * dt;
                fling_vel_ *= std::exp(-dt * 3.2f);
                if (std::fabs(fling_vel_) < 1.0f) fling_vel_ = 0;
                mark();
            }
        }
        if (screen_ == Screen::Main) tick_player();
        if (opt_.mini && !mini_win_ && !mini_open_ && !mini_opened_once_ && queue_.current() && !track_loading_) { mini_opened_once_ = true; toggle_mini(); }
        if (search_pending_ && SDL_GetTicks() >= search_due_) run_search();
        tick_sleep();
        uint32_t now = SDL_GetTicks();
        if (screen_ == Screen::Main && client_ && !offline_ && now - like_sync_ms_ > 30000) { like_sync_ms_ = now; likes_sync(); }   // likes made on another device show up, failed ones are retried
        if (screen_ == Screen::Main && !acct_.empty() && queue_.revision() != queue_saved_rev_ && now - queue_saved_ms_ > 1500) {   // the queue survives a kill or a power cut: written within 1.5 s of every change
            queue_saved_ms_ = now;
            queue_.save_songs(pref_ + "queue_" + acct_ + ".json");
            queue_saved_rev_ = queue_.revision();
            save_playstate();
        }
        if (player_.playing() && now - playstate_saved_ms_ > 10000) save_playstate();   // a killed app (Android, reboot) keeps the spot to within 10 s
        if (storage_lost_ && screen_ == Screen::Main && now - storage_check_ms_ > 3000) {   // the SD card came up after the app did: reopen the real folders
            storage_check_ms_ = now;
            if (!storage_unavailable()) {
                storage_lost_ = false;
                open_account_data();
                toast_ = "Storage is back: your downloads are available"; toast_until_ = SDL_GetTicks() + 4000;
                dl_dirty_ = true; mark();
            }
        }
        if (ui_->wants_frames && now - last_draw > 400) mark();
        if (pulse_live && now - frame_began >= pulse_interval_ms()) mark();
        if (rail_anim) mark();
        if (amp_live && now - last_draw >= 66) mark();
        if (tip_pending_ && now - last_draw >= 80) mark();
        if (press_touch_ && !long_fired_ && !touch_moved_ && ui_->in.down && ui_->active == 0 && now - press_ms_ >= 500 && std::hypot(ui_->in.mx - ui_->in.press_x, ui_->in.my - ui_->in.press_y) <= 8 * S_) {   // the original: Chromium turns a long press into "contextmenu"
            Input& in = ui_->in;
            in.rclick = true; in.rx = in.mx; in.ry = in.my; long_fired_ = true; mark();
        }
        if (opt_.bench > 0 && screen_ == Screen::Main && !track_loading_ && !loading_home_ && !pre_open_) {
            // benchmark: after the page has settled for 2.5 s, wheel the content up and down, one frame per iteration, no event waits
            if (!bench_start_) bench_start_ = SDL_GetTicks() + 2500;
            if (SDL_GetTicks() >= bench_start_) {
                const int k = static_cast<int>(bench_ms_.size());
                if (k >= opt_.bench) {
                    std::vector<double> v = bench_ms_;
                    std::sort(v.begin(), v.end());
                    double sum = 0; for (double d : v) sum += d;
                    auto pc = [&](double q) { return v[std::min(v.size() - 1, static_cast<size_t>(q * static_cast<double>(v.size())))]; };
                    std::printf("bench %zu frames (%s): avg %.2f ms  p50 %.2f  p95 %.2f  p99 %.2f  max %.2f  over16.7ms %zu  (present avg %.2f ms)\n", v.size(), renderer_name_.c_str(), sum / static_cast<double>(v.size()),
                                pc(0.5), pc(0.95), pc(0.99), v.back(), static_cast<size_t>(std::count_if(v.begin(), v.end(), [](double d) { return d > 16.7; })), bench_present_ / static_cast<double>(v.size()));
                    running_ = false;
                } else {
                    ui_->in.mx = W_last_ / 2; ui_->in.my = H_last_ / 2;
                    ui_->in.wheel = (k / 20) % 2 == 0 ? -1.0f : 1.0f;   // 20 notches down, 20 up
                    dirty_ = true;
                }
            }
        }
        if (dirty_) {
            dirty_ = false;
            ui_->wants_frames = false;
            frame_began = SDL_GetTicks();
            if (!background_) frame();   // nothing is visible while the app is in the background; playback logic still runs above
            ui_theme_cache_ = ui_->th;
            if (mini_win_) draw_mini();
            last_draw = SDL_GetTicks();
            const bool had_click = ui_->in.released;
            ui_->in.new_frame();
            if (had_click) mark();   // a click changes state while the frame is already drawn (a dialog closes, a page opens): draw once more so a touch screen shows it without a further event
        }
        if (shot_deadline_ && SDL_GetTicks() >= shot_deadline_ && !track_loading_ && !loading_home_) {
            // The back buffer is undefined after a present; draw the settled scene twice so the read-back is the current one.
            if (!mini_ren_) { frame(); gfx_->capture_next(opt_.shot_path); frame(); }
            int w = 0, h = 0;
            SDL_Renderer* shot_ren = mini_ren_ ? mini_ren_ : ren_;
            SDL_GetRendererOutputSize(shot_ren, &w, &h);
            SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
            if (mini_ren_) { SDL_RenderReadPixels(shot_ren, nullptr, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch); SDL_SaveBMP(s, opt_.shot_path.c_str()); }
            SDL_FreeSurface(s);
            running_ = false;
        }
    }
    return 0;
}

void App::frame() {
    const uint64_t frame_start = SDL_GetPerformanceCounter();
    int dw = 0, dh = 0;
    SDL_GetRendererOutputSize(ren_, &dw, &dh);
    float W = static_cast<float>(dw), H = static_cast<float>(dh);
    ui_->in.screen_h = dh;
    if (dl_dirty_) refresh_dl_state();
    gfx_->begin_frame(ui_->th.bg);
    const Input in0 = ui_->in;
    if (screen_ == Screen::Login) { draw_login(W, H); if (connecting_) draw_signing_in(W, H); }
    else {
        draw_main(W, H);
        if (pre_open_ || newcontent_open_ || prompt_ != Prompt::None) { ui_->in = in0; if (pre_open_) draw_precache(W, H); else if (prompt_ != Prompt::None) draw_connectivity_prompt(W, H); else draw_new_content_prompt(W, H); }
    }
    draw_splash(static_cast<float>(dw), static_cast<float>(dh));
    draw_render_hud(static_cast<float>(dw), static_cast<float>(dh));
    gfx_->end_frame();
    const uint64_t now = SDL_GetPerformanceCounter();
    const double freq = static_cast<double>(SDL_GetPerformanceFrequency());
    render_ms_ = freq > 0 ? static_cast<double>(now - frame_start) * 1000.0 / freq : 0.0;
    W_last_ = W; H_last_ = H;
    if (opt_.bench > 0 && bench_start_ && SDL_GetTicks() >= bench_start_ && ui_->in.wheel != 0) { bench_ms_.push_back(render_ms_); bench_present_ += gfx_->present_ms(); }
    ++render_frames_;
    if (!render_window_start_) render_window_start_ = now;
    const double window_s = freq > 0 ? static_cast<double>(now - render_window_start_) / freq : 0.0;
    if (window_s >= 0.5) {
        render_fps_ = static_cast<double>(render_frames_) / window_s;
        render_frames_ = 0;
        render_window_start_ = now;
    }
    bool want = ui_->focus != 0;
    static bool text_on = false;
    if (want != text_on) { text_on = want; if (want) SDL_StartTextInput(); else SDL_StopTextInput(); }
    ui_->active = ui_->in.down ? ui_->active : 0;
}

void App::draw_render_hud(float W, float H) {
    if (!cfg_.render_timer) return;
    const float s = S_;
    const Theme& th = ui_->th;
    if (mobile_) {   // one small pill inside the header (hidden with the header while the keyboard is up)
        if (ui_->focus != 0 && H / s < 420) return;
        char t[48] = {};
        std::snprintf(t, sizeof t, "%s \xC2\xB7 %.1f ms \xC2\xB7 %.0f FPS", software_ ? "CPU" : "GPU", render_ms_, render_fps_);
        const int f = F(500, 10);
        const float pw = gfx_->measure(f, t) + 16 * s;
        Rect p{W - 56 * s - pw, 17 * s, pw, 22 * s};
        gfx_->fill_round(p, 11 * s, {0, 0, 0, 150});
        T(500, 10, p.x + 8 * s, p.y, 22, t, th.sub);
        return;
    }
    const float w = 178 * s, h = 64 * s;
    Rect b{W - w - 12 * s, 68 * s, w, h};   // RenderTimerHUD.css: top 68, right 12 (below the header)
    gfx_->fill_round({b.x - 1, b.y - 1, b.w + 2, b.h + 2}, 8 * s, th.border);
    gfx_->fill_round(b, 7 * s, {0, 0, 0, 205});
    char line[96] = {};
    std::snprintf(line, sizeof line, "Render  %5.2f ms   %4.1f FPS", render_ms_, render_fps_);
    T(500, 11, b.x + 10 * s, b.y + 5 * s, 18, line, th.text);
    const char* mode = cfg_.perf_mode == 1 ? "Gaming" : cfg_.perf_mode == 2 ? "Eco" : "Balanced";
    std::snprintf(line, sizeof line, "Mode: %s", mode);
    T(400, 10, b.x + 10 * s, b.y + 25 * s, 16, line, th.sub);
    std::snprintf(line, sizeof line, "Renderer: %s (%s)", renderer_name_.c_str(), software_ ? "CPU" : "GPU");
    T(400, 10, b.x + 10 * s, b.y + 41 * s, 16, line, software_ ? th.accent : th.sub);
}

void App::apply_app_icon(bool launcher) {
    const int i = cfg_.app_icon >= 0 && cfg_.app_icon < kAppIconCount ? cfg_.app_icon : 0;
#if !defined(__ANDROID__) && !(defined(__APPLE__) && TARGET_OS_IPHONE)
    if (win_) {   // taskbar / window-switcher icon (macOS takes it from the app bundle instead)
        if (SDL_Surface* ic = IMG_Load((icons_dir_ + "styles/" + kAppIcons[i].name + std::string("-256.png")).c_str())) { SDL_SetWindowIcon(win_, ic); SDL_FreeSurface(ic); }
    }
#endif
#if defined(__ANDROID__)
    if (launcher) android_bridge::set_app_icon(kAppIcons[i].name);
#endif
    (void)launcher;
    if (splash_tex_) { SDL_DestroyTexture(splash_tex_); splash_tex_ = nullptr; }
}
