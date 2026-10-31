#include "app.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sys/stat.h>
#include "../net/url.h"
#include "secrets.h"

namespace {
const char* kOrg = "xylonic-fiber";
const char* kApp = "xylonic-fiber";

std::string hash8(const std::string& k) {
    uint64_t h = 1469598103934665603ULL;
    for (unsigned char c : k) { h ^= c; h *= 1099511628211ULL; }
    char b[16];
    std::snprintf(b, sizeof b, "%08x", static_cast<unsigned>(h ^ (h >> 32)));
    return b;
}

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
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_AUDIO) < 0) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return false;
    }
    win_ = SDL_CreateWindow("xylonic-fiber", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, opt_.width, opt_.height,
                            SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!win_) return false;
    SDL_SetWindowMinimumSize(win_, 640, 480);
    ren_ = SDL_CreateRenderer(win_, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren_) ren_ = SDL_CreateRenderer(win_, -1, SDL_RENDERER_SOFTWARE);
    if (!ren_) return false;
    SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_BLEND);
    int ww, wh, dw, dh;
    SDL_GetWindowSize(win_, &ww, &wh);
    SDL_GetRendererOutputSize(ren_, &dw, &dh);
    S_ = ww > 0 ? static_cast<float>(dw) / ww : 1.0f;

    char* p = SDL_GetPrefPath(kOrg, kApp);
    pref_ = p ? p : "./";
    SDL_free(p);
    cfg_path_ = pref_ + "config.json";
    perm_dir_ = pref_ + "downloads/";
    stream_dir_ = pref_ + "stream/";
    img_dir_ = pref_ + "covers/";
    mkdirs(perm_dir_); mkdirs(stream_dir_); mkdirs(img_dir_);
    cfg_.load(cfg_path_);
    vol_ = static_cast<float>(cfg_.volume_pct) / 100.0f;
    player_.set_volume(vol_);

    gfx_ = std::make_unique<Gfx>();
    if (!gfx_->init(win_, ren_)) return false;
    ui_ = std::make_unique<Ui>(*gfx_);
    apply_theme();
    char* base = SDL_GetBasePath();
    assets_dir_ = std::string(base ? base : "./") + "assets/fonts/";
    SDL_free(base);
    ui_->f_body = F(400, 15);
    ui_->f_small = F(400, 12.5f);
    ui_->f_title = F(700, 26);
    ui_->f_bold = F(500, 15);
    if (ui_->f_body < 0 || ui_->f_title < 0) {
        std::fprintf(stderr, "fonts not found in %s\n", assets_dir_.c_str());
        return false;
    }
    worker_ = std::make_unique<Worker>(3);
    images_ = std::make_unique<ImageCache>(ren_, *worker_, img_dir_, 96);
    images_->set_on_ready([this] { mark(); });
    ui_post([] {});  // registers the wake-up event type on the UI thread
    ui_drain();

    f_url_ = cfg_.server_url;
    f_user_ = cfg_.username;
    if (!opt_.server.empty()) { f_url_ = opt_.server; f_user_ = opt_.user; f_pass_ = opt_.pass; }
    if (opt_.server.empty() && !cfg_.server_url.empty() && !cfg_.username.empty()) {
        std::string saved;
        if (secret_lookup(cfg_.server_url + "|" + cfg_.username, saved)) { f_pass_ = saved; connect(false); }
    }
    if (!opt_.server.empty() && !opt_.pass.empty()) connect(false);
    if (opt_.offline) { enter_offline(); if (opt_.nav == "downloads") set_nav(Nav::Downloads); else if (opt_.nav == "library") set_nav(Nav::Library); }
    if (!opt_.shot_path.empty()) shot_deadline_ = SDL_GetTicks() + static_cast<uint32_t>(opt_.shot_after_ms);
    return true;
}

// ---------------------------------------------------------------- session

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
            if (ok) {
                client_ = client;
                cfg_.server_url = url;
                cfg_.username = f_user_;
                cfg_.pin_sha256 = copt_.pin_sha256;
                cfg_.save(cfg_path_);
                if (opt_.server.empty()) secret_store(url + "|" + f_user_, f_pass_);  // not for scripted test logins
                on_connected();
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

void App::open_account_data() {
    acct_ = hash8(cfg_.server_url + "|" + cfg_.username);
    perm_dir_ = pref_ + "downloads/" + acct_ + "/";
    stream_dir_ = pref_ + "stream/" + acct_ + "/";
    mkdirs(perm_dir_); mkdirs(stream_dir_);
    store_ = std::make_unique<Store>();
    if (!store_->open(pref_ + "data_" + acct_ + ".db")) store_.reset();
    downloads_.reset();
    if (store_) {
        auto c = client_;
        downloads_ = std::make_unique<Downloads>(*store_, perm_dir_, [c](const std::string& id) { return c ? c->download_url(id) : std::string(); }, copt_, 2);
        downloads_->set_on_change([this] { ui_post([this] { dl_dirty_ = true; mark(); }); });
    }
    dl_dirty_ = true;
}

void App::refresh_dl_state() {
    dl_dirty_ = false;
    dl_state_.clear();
    if (!downloads_) return;
    for (auto& it : downloads_->snapshot()) dl_state_[it.song.id] = {it.state, it.bytes, it.total};
}

void App::enter_offline() {
    if (cfg_.server_url.empty() || cfg_.username.empty()) return;
    offline_ = true;
    client_.reset();
    screen_ = Screen::Main;
    views_.clear();
    views_.push_back({ViewKind::Root, "", "Home", {}});
    nav_ = Nav::Home;
    open_account_data();
    queue_.load_songs(pref_ + "queue_" + acct_ + ".json");
    queue_saved_rev_ = queue_.revision();
    history_load();
    load_home();
    load_artists();
    mark();
}

void App::on_connected() {
    offline_ = false;
    screen_ = Screen::Main;
    images_->set_connect_options(copt_);
    f_pass_.clear();
    views_.clear();
    views_.push_back({ViewKind::Root, "", "Home", {}});
    nav_ = Nav::Home;
    open_account_data();
    std::string acct = acct_;
    queue_.load_songs(pref_ + "queue_" + acct + ".json");
    queue_saved_rev_ = queue_.revision();
    history_load();
    load_home();
    load_artists();
    if (opt_.nav == "library") set_nav(Nav::Library);
    else if (opt_.nav == "queue") set_nav(Nav::Queue);
    else if (opt_.nav == "settings") set_nav(Nav::Settings);
    else if (opt_.nav == "downloads") set_nav(Nav::Downloads);
    else if (opt_.nav == "search") set_nav(Nav::Search);
    else if (opt_.nav.rfind("album:", 0) == 0) { push_view(ViewKind::Album, opt_.nav.substr(6), "Album"); load_album(opt_.nav.substr(6)); }
    if (opt_.nowplaying) np_open_ = true;
    if (!opt_.panel.empty()) toggle_panel(opt_.panel == "history" ? PanelTab::History : opt_.panel == "playlists" ? PanelTab::Playlists : PanelTab::Queue);
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

void App::logout() {
    secret_clear(cfg_.server_url + "|" + cfg_.username);
    player_.stop();
    ++session_;
    ++play_gen_;
    save_queue_state();
    client_.reset();
    downloads_.reset();
    store_.reset();
    offline_ = false;
    images_->clear_memory();
    for (auto& sec : sections_) { sec.albums.clear(); sec.loading = true; sec.error.clear(); sec.scroll_x = 0; }
    artists_.clear(); artist_cache_.clear(); album_cache_.clear();
    search_res_ = SubsonicClient::SearchAll{};
    queue_.clear();
    screen_ = Screen::Login;
    login_msg_.clear();
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
            if (ok) { if (store_) store_->put("artists", to_json(a)); artists_ = std::move(a); } else load_err_ = e;
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
            if (ok) { artist_cache_[id] = a; if (store_) store_->put("artist:" + id, to_json(a)); }
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
            if (ok) { album_cache_[id] = a; if (store_) store_->put("album:" + id, to_json(a)); }
            else if (album_cache_[id].id.empty()) album_cache_.erase(id);
            mark();
        });
    });
}

SDL_Texture* App::cover(const std::string& id, int px) {
    if (id.empty()) return nullptr;
    int dev = static_cast<int>(px * S_);
    auto c = client_;  // offline: empty URL, the image only resolves from the disk cache
    return images_->get(cover_key(id, dev), [c, id, dev] { return c ? c->cover_url(id, dev) : std::string(); });
}

void App::prefetch_covers(const std::vector<Song>& songs) {
    if (!client_) return;
    auto c = client_;
    for (const Song& sg : songs) {
        if (sg.cover_art.empty()) continue;
        for (int px : {44, 64, 132, 164}) {
            int dev = static_cast<int>(px * S_);
            std::string id = sg.cover_art;
            images_->prefetch(cover_key(id, dev), [c, id, dev] { return c->cover_url(id, dev); });
        }
    }
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
void App::pop_view() { if (views_.size() > 1) { views_.pop_back(); mark(); } }

void App::set_nav(Nav n) {
    nav_ = n;
    views_.clear();
    static const char* names[] = {"Home", "Library", "Search", "Queue", "Downloads", "Settings"};
    views_.push_back({ViewKind::Root, "", names[static_cast<int>(n)], {}});
    if (n == Nav::Search) ui_->focus = kSearch;
    mark();
}

// ---------------------------------------------------------------- playback

std::string App::track_kind(const Song& s) const {
    std::string e = lower_ext(s.suffix);
    return e == "flac" ? "flac" : "mp3";  // anything else is transcoded to mp3 by the server
}

std::string App::perm_path(const Song& s) const { return downloads_ ? downloads_->path_if_done(s.id) : std::string(); }

std::string App::stream_path(const Song& s) const {
    std::string e = lower_ext(s.suffix);
    bool native = e == "mp3" || e == "flac";
    std::string q = cfg_.stream_quality > 0 && e != "flac" ? "_q" + std::to_string(cfg_.stream_quality) : "";
    return stream_dir_ + s.id + q + "." + (native ? e : std::string("mp3"));
}

void App::play_songs(const std::vector<Song>& songs, size_t start, bool shuffle) {
    if (songs.empty()) return;
    queue_.set_shuffle(false);
    queue_.set(songs, start);
    queue_.set_shuffle(shuffle);
    start_current();
}

void App::start_current() {
    const Song* s = queue_.current();
    if (!s) return;
    Song song = *s;
    uint32_t gen = ++play_gen_;
    track_loading_ = true;
    play_err_.clear();
    player_.stop();
    std::string e = lower_ext(song.suffix);
    bool native = e == "mp3" || e == "flac";
    int kbps = cfg_.stream_quality;
    std::string url = client_ ? client_->stream_url(song.id, e == "flac" ? 0 : kbps, native ? "" : "mp3") : std::string();
    worker_->post([this, gen, song, url, c = copt_, perm = perm_path(song), strm = stream_path(song), kind = track_kind(song)] {
        std::string err;
        auto dec = std::make_shared<std::unique_ptr<Decoder>>(open_track(url, perm, strm, kind, c, err));
        ui_post([this, gen, dec, err, song] {
            if (gen != play_gen_) return;  // superseded by a newer track
            track_loading_ = false;
            if (!*dec) { play_err_ = err.empty() ? "Cannot play this track" : err; mark(); return; }
            player_.set_volume(vol_);
            if (player_.attach(std::move(*dec), static_cast<double>(song.duration))) player_.play();
            scrobbled_id_.clear();
            history_add(song);
            if (client_) worker_->post([c = client_, id = song.id] { std::string e; c->scrobble(id, false, e); });
            mark();
        });
    });
    mark();
}

void App::toggle_play() {
    if (track_loading_) return;
    if (!queue_.current()) return;
    if (player_.playing()) player_.pause();
    else if (player_.duration_seconds() > 0 || player_.position_seconds() > 0) player_.play();
    else start_current();
    mark();
}

void App::next_track() { if (queue_.next()) start_current(); }

void App::prev_track() {
    if (player_.position_seconds() > 3.0) { player_.seek_seconds(0); mark(); return; }
    if (queue_.prev()) start_current();
}

void App::tick_player() {
    if (track_loading_) return;
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
}

void App::set_volume(float v) {
    vol_ = std::min(1.0f, std::max(0.0f, v));
    player_.set_volume(vol_);
    cfg_.volume_pct = static_cast<int>(vol_ * 100 + 0.5f);
}

void App::save_queue_state() {
    if (screen_ != Screen::Main) return;
    if (queue_.revision() != queue_saved_rev_) {
        queue_.save_songs(pref_ + "queue_" + acct_ + ".json");
        queue_saved_rev_ = queue_.revision();
    }
    cfg_.save(cfg_path_);
}

// ---------------------------------------------------------------- events / loop

void App::handle(const SDL_Event& e) {
    Input& in = ui_->in;
    switch (e.type) {
    case SDL_QUIT: running_ = false; break;
    case SDL_WINDOWEVENT:
        if (e.window.event == SDL_WINDOWEVENT_RESIZED || e.window.event == SDL_WINDOWEVENT_EXPOSED || e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED || e.window.event == SDL_WINDOWEVENT_RESTORED) mark();
        break;
    case SDL_MOUSEMOTION: in.mx = e.motion.x * S_; in.my = e.motion.y * S_; mark(); break;
    case SDL_MOUSEBUTTONDOWN:
        if (e.button.button == SDL_BUTTON_LEFT) { in.mx = e.button.x * S_; in.my = e.button.y * S_; in.down = in.pressed = true; in.press_x = in.mx; in.press_y = in.my; mark(); }
        break;
    case SDL_MOUSEBUTTONUP:
        if (e.button.button == SDL_BUTTON_LEFT) { in.mx = e.button.x * S_; in.my = e.button.y * S_; in.down = false; in.released = true; mark(); }
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
        if (k == SDLK_BACKSPACE) ++in.backspace;
        else if (k == SDLK_DELETE) ++in.del;
        else if (k == SDLK_LEFT && typing) ++in.left;
        else if (k == SDLK_RIGHT && typing) ++in.right;
        else if (k == SDLK_HOME) ++in.home;
        else if (k == SDLK_END) ++in.end;
        else if (k == SDLK_RETURN || k == SDLK_KP_ENTER) ++in.enter;
        else if (k == SDLK_TAB) ++in.tab;
        else if (k == SDLK_ESCAPE) { ++in.escape; if (typing) ui_->focus = 0; else if (np_open_) np_open_ = false; else if (panel_open_) panel_open_ = false; }
        else if (in.ctrl && k == SDLK_k && screen_ == Screen::Main) set_nav(Nav::Search);
        else if (!typing && screen_ == Screen::Main) {
            if (k == SDLK_SPACE) toggle_play();
            else if (k == SDLK_RIGHT) { if (shift) next_track(); else player_.seek_seconds(player_.position_seconds() + 5); }
            else if (k == SDLK_LEFT) { if (shift) prev_track(); else player_.seek_seconds(player_.position_seconds() - 5); }
            else if (k == SDLK_UP && shift) set_volume(vol_ + 0.1f);
            else if (k == SDLK_DOWN && shift) set_volume(vol_ - 0.1f);
            else if (k == SDLK_s) queue_.set_shuffle(!queue_.shuffle());
            else if (k == SDLK_r) queue_.set_repeat(queue_.repeat() == Repeat::Off ? Repeat::All : queue_.repeat() == Repeat::All ? Repeat::One : Repeat::Off);
            else if (k == SDLK_m) set_volume(vol_ > 0 ? 0 : 0.8f);
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

int App::run() {
    uint32_t last_draw = 0;
    while (running_) {
        bool animating = player_.playing() || track_loading_ || ui_->wants_frames || connecting_ || search_pending_;
        SDL_Event e;
        bool got;
        if (dirty_) got = SDL_PollEvent(&e) != 0;
        else if (shot_deadline_) got = SDL_WaitEventTimeout(&e, 50) != 0;
        else if (animating) got = SDL_WaitEventTimeout(&e, 250) != 0;
        else got = SDL_WaitEvent(&e) != 0;
        while (got) {
            handle(e);
            got = SDL_PollEvent(&e) != 0;
        }
        if (screen_ == Screen::Main) tick_player();
        if (search_pending_ && SDL_GetTicks() >= search_due_) run_search();
        uint32_t now = SDL_GetTicks();
        if (ui_->wants_frames && now - last_draw > 400) mark();
        if (dirty_) {
            dirty_ = false;
            ui_->wants_frames = false;
            frame();
            last_draw = SDL_GetTicks();
            ui_->in.new_frame();
        }
        if (shot_deadline_ && SDL_GetTicks() >= shot_deadline_ && !track_loading_ && !loading_home_) {
            int w, h;
            SDL_GetRendererOutputSize(ren_, &w, &h);
            SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
            SDL_RenderReadPixels(ren_, nullptr, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch);
            SDL_SaveBMP(s, opt_.shot_path.c_str());
            SDL_FreeSurface(s);
            running_ = false;
        }
    }
    return 0;
}

void App::frame() {
    int dw, dh;
    SDL_GetRendererOutputSize(ren_, &dw, &dh);
    float W = static_cast<float>(dw), H = static_cast<float>(dh);
    if (dl_dirty_) refresh_dl_state();
    gfx_->begin_frame(ui_->th.bg);
    if (screen_ == Screen::Login) draw_login(W, H);
    else draw_main(W, H);
    gfx_->end_frame();
    bool want = ui_->focus != 0;
    static bool text_on = false;
    if (want != text_on) { text_on = want; if (want) SDL_StartTextInput(); else SDL_StopTextInput(); }
    ui_->active = ui_->in.down ? ui_->active : 0;
}
