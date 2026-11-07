// Library-wide actions and the prompts around them, ported from the original:
//  ArtistList.tsx (Shuffle All "Confirm?" -> "Loading...", Download All / Download Missing), ShuffleAllButton.tsx,
//  DownloadQualityPicker.tsx, App.tsx (missing-songs banner, offline and mobile-data prompts, offline redirect).
#include <algorithm>
#include <cmath>
#include <ctime>
#include "app.h"
#include "model_json.h"
#include "log.h"
#include "secrets.h"
#include "skin.h"

namespace {
Color alpha(Color c, float a) { c.a = static_cast<uint8_t>(std::lround(a * 255)); return c; }
struct QOpt { int kbps = 0; const char* label; const char* desc; int bars = 0; };
const QOpt kQualityOptions[] = {   // DOWNLOAD_QUALITY_OPTIONS
    {0, "Original (Raw)", "No transcoding \xC2\xB7 Best quality", 6}, {320, "320 kbps", "Excellent \xC2\xB7 Recommended", 6}, {256, "256 kbps", "High quality \xC2\xB7 Good balance", 5},
    {128, "128 kbps", "Good quality \xC2\xB7 Smaller files", 3}, {64, "64 kbps", "Low \xC2\xB7 Minimum storage", 2}};
}

// ---------------------------------------------------------------- fetching the whole library

// 500 songs per page; the pages after the first run in parallel when the song count is known (otherwise they are walked in order).
void App::fetch_library_songs(std::function<void(std::vector<Song>&&, bool)> on_page, std::function<void(bool)> on_done) {
    if (!client_) { on_done(false); return; }
    constexpr int kPage = 500;
    worker_->post([this, c = client_, s = session_, total = total_songs_, on_page, on_done] {
        std::vector<Song> first; std::string e;
        if (!c->search_songs("", kPage, 0, first, e) || first.empty()) { ui_post([this, s, on_done] { if (s == session_) on_done(false); }); return; }
        const bool more = static_cast<int>(first.size()) == kPage;
        const int pages = total > kPage ? (total + kPage - 1) / kPage : 0;
        ui_post([this, s, first = std::move(first), on_page, on_done, more]() mutable { if (s != session_) return; on_page(std::move(first), true); if (!more) on_done(true); });
        if (!more) return;
        auto deliver = [this, s, on_page](std::vector<Song> page) { ui_post([this, s, on_page, page = std::move(page)]() mutable { if (s == session_) on_page(std::move(page), false); }); };
        if (pages > 1) {
            auto left = std::make_shared<std::atomic<int>>(pages - 1);
            for (int p = 1; p < pages; ++p)
                worker_->post([this, c, s, p, left, deliver, on_done] {
                    std::vector<Song> r; std::string err;
                    if (c->search_songs("", kPage, p * kPage, r, err) && !r.empty()) deliver(std::move(r));
                    if (--*left == 0) ui_post([this, s, on_done] { if (s == session_) on_done(true); });
                });
        } else {
            for (int off = kPage;; off += kPage) {
                std::vector<Song> r; std::string err;
                if (!c->search_songs("", kPage, off, r, err) || r.empty()) break;
                const bool last = static_cast<int>(r.size()) < kPage;
                deliver(std::move(r));
                if (last) break;
            }
            ui_post([this, s, on_done] { if (s == session_) on_done(true); });
        }
    });
}

// handleShuffleAll: with a song playing the first press only asks "Confirm?" (3 s); then every song is loaded, shuffle is
// switched on and playback starts at a random one. Offline it shuffles the downloaded songs. Failure: inline error for 4 s.
void App::shuffle_all_pressed() {
    if (shuffling_all_) return;
    if (queue_.current() && !shuffle_confirm_) {
        shuffle_confirm_ = true;
        SDL_AddTimer(3000, [](Uint32, void* self) -> Uint32 { ui_post([self] { auto* a = static_cast<App*>(self); a->shuffle_confirm_ = false; a->mark(); }); return 0; }, this);
        mark(); return;
    }
    shuffle_confirm_ = false; shuffling_all_ = true; shuffle_error_.clear();
    auto fail = [this] {
        shuffling_all_ = false; shuffle_error_ = "Could not load songs for shuffle";
        SDL_AddTimer(4000, [](Uint32, void* self) -> Uint32 { ui_post([self] { auto* a = static_cast<App*>(self); a->shuffle_error_.clear(); a->mark(); }); return 0; }, this);
        mark();
    };
    if (offline_) {
        std::vector<Song> songs;
        if (downloads_) { auto snap = downloads_->snapshot(); for (auto& it : *snap) if (it.state == Downloads::Done) songs.push_back(it.song); }
        if (songs.empty()) { shuffle_error_ = "No cached songs available"; fail(); shuffle_error_ = "Could not load songs for shuffle"; return; }
        shuffling_all_ = false;
        play_songs(songs, static_cast<size_t>(SDL_GetTicks()) % songs.size(), true);
        return;
    }
    fetch_library_songs([this](std::vector<Song>&& page, bool first) {
        if (first) play_songs(page, static_cast<size_t>(SDL_GetTicks()) % page.size(), true);
        else queue_.append(std::move(page));
        mark();
    }, [this, fail](bool any) { if (any) { shuffling_all_ = false; mark(); } else fail(); });
}

int App::download_btn_state() const {
    if (offline_) return 0;
    const int cached = songs_downloaded();
    if (missing_songs_ == 0 && cached > 0) return 0;
    return cached > 0 ? 2 : 1;
}

void App::open_download_picker(bool missing_label) {
    dq_value_ = cfg_.download_quality;          // the picker starts from the default download quality
    dq_missing_label_ = missing_label; dq_songs_.clear(); dq_label_.clear();
    modal_ = Modal::DownloadQuality; mark();
}

void App::open_download_picker_songs(std::vector<Song> songs, std::string label) {
    if (!songs.empty()) dq_album_ids_.clear();
    dq_value_ = cfg_.download_quality;
    dq_songs_ = std::move(songs); dq_label_ = std::move(label); dq_missing_label_ = false;
    modal_ = Modal::DownloadQuality; mark();
}

// handleConfirmDownloadAll / handleDownloadMissing: every song that is not downloaded yet, grouped by the downloads queue
// per album, then the Download Manager (the Downloads page) opens.
void App::queue_downloads(int quality) {
    if (!downloads_ || bulk_downloading_) return;
    bulk_downloading_ = true;
    fetch_library_songs([this, quality](std::vector<Song>&& page, bool) {
        std::vector<Song> todo;
        for (Song& sg : page) if (downloads_->path_if_done(sg.id).empty()) todo.push_back(std::move(sg));
        if (!todo.empty()) { downloads_->enqueue(todo, quality); prefetch_covers(todo); }
        mark();
    }, [this](bool) { bulk_downloading_ = false; missing_banner_ = false; set_nav(Nav::Downloads); });
}

// ---------------------------------------------------------------- missing-songs banner (App.tsx)

void App::check_missing_songs() {
    if (!client_ || offline_ || missing_checked_ || !downloads_) return;
    missing_checked_ = true;
    worker_->post([this, c = client_, s = session_] {
        int server = 0; std::string e;
        if (!c->song_count(server, e)) return;
        ui_post([this, s, server] {
            if (s != session_) return;
            const int cached = songs_downloaded(), missing = server - cached;
            if (missing > 0 && cached > 0) { missing_songs_ = missing; missing_banner_ = true; }   // only when some songs are downloaded
            else if (missing == 0 && cached > 0) missing_songs_ = 0;                                // all downloaded
            mark();
        });
    });
}

// .missing-songs-banner: blue gradient strip under the header; the subtitle is hidden on phones.
void App::draw_missing_banner(float W, float y, float& height) {
    height = 0;
    if (!missing_banner_ || offline_) return;
    Ui& u = *ui_;
    const float s = S_;
    const bool compact = mobile_;
    const float pad_y = compact ? 8 : 10, pad_x = compact ? 12 : 20;
    height = (compact ? 8 + 30 + 8 : 10 + 39 + 10) * s + 1 * s;
    Rect r{0, y, W, height};
    gfx_->quad_gradient(r, rgb(0x1565c0), rgb(0x1a5cb4), rgb(0x0d47a1), rgb(0x1160b5));
    gfx_->fill({0, r.y + r.h - 1 * s, W, 1 * s}, {255, 255, 255, 102});
    const float cy = r.y + (r.h - 1 * s) / 2;
    fa(0xf0ed, pad_x * s + 9 * s, cy, 18, {255, 255, 255, 242});
    // dismiss (30 round) and Download Missing (pill with chevron)
    Rect x{W - pad_x * s - 30 * s, cy - 15 * s, 30 * s, 30 * s};
    const std::string label = bulk_downloading_ ? "Queuing\xE2\x80\xA6" : compact ? "Download" : "Download Missing";   // a phone banner needs the room for the count
    const float fpx = compact ? 12.0f : 13.0f;
    const float lw = gfx_->measure(F(600, fpx), label);
    const float bw = lw + (compact ? 20 : 28) * s + 14 * s + 18 * s + 8 * s;
    Rect b{x.x - 8 * s - bw, cy - 16 * s, bw, 32 * s};   // even height: the stroke corners meet exactly
    const std::string title = std::to_string(missing_songs_) + " song" + (missing_songs_ != 1 ? "s" : "") + " not downloaded";
    if (compact) {   // the text gets what the button leaves; a short form when even that is too tight
        const float avail = b.x - 8 * s - (pad_x + 30) * s;
        const std::string shortt = std::to_string(missing_songs_) + " missing";
        const std::string shown = gfx_->measure(F(700, 14), title) <= avail ? title : gfx_->measure(F(700, 14), shortt) <= avail ? shortt : std::to_string(missing_songs_);
        T(700, 14, (pad_x + 30) * s, cy - 10.5f * s, 21, shown, rgb(0xffffff), avail);
    }
    else {
        T(700, 14, (pad_x + 30) * s, r.y + pad_y * s, 21, title, rgb(0xffffff));
        T(400, 12, (pad_x + 30) * s, r.y + (pad_y + 21) * s, 18, "Your library has songs not yet saved for offline use", {255, 255, 255, 217});
    }
    gfx_->circle(x.x + 15 * s, x.y + 15 * s, 15 * s, {255, 255, 255, static_cast<uint8_t>(u.hover(x) ? 77 : 102)});
    fa(skin::Xmark, x.x + 15 * s, x.y + 15 * s, 13, rgb(0xffffff));
    gfx_->fill_round(b, 16 * s, {255, 255, 255, static_cast<uint8_t>(u.hover(b) ? 82 : 102)});
    gfx_->stroke_round(b, 16 * s, 1 * s, {255, 255, 255, 89});
    fa(skin::Download, b.x + (compact ? 10 : 14) * s + 6 * s, cy, 12, rgb(0xffffff));
    T(600, fpx, b.x + (compact ? 10 : 14) * s + 20 * s, b.y, 31, label, rgb(0xffffff));
    fa(0xf078, b.x + b.w - (compact ? 10 : 14) * s - 4 * s, cy, 10, {255, 255, 255, 153});
    if (u.clicked(x)) { missing_banner_ = false; mark(); }
    else if (u.clicked(b) && !bulk_downloading_) open_download_picker(true);
}

// ---------------------------------------------------------------- Download Quality picker (DownloadQualityPicker.tsx)

void App::draw_download_picker(float W, float H) {
    Ui& u = *ui_;
    const float s = S_;
    const Theme& th = u.th;
    const bool sheet = mobile_;
    const float item_h = sheet ? 74.0f : 62.0f;
    const float mw = sheet ? W / s : std::min(420.0f, 0.92f * W / s);
    const int sub_lines = wrap_count(400, 11, "Choose quality for offline storage. Higher quality uses more space.", (mw - 36) * s);   // the list starts below however many lines the hint needs
    const float head_h = 48 + 4 + 16.5f * sub_lines + 10;
    const float footer_h = 8 + 1 + 12 + 42 + 16;
    const float mh = std::min(head_h + 5 * item_h + footer_h, H / s * (sheet ? 0.9f : 0.8f));
    Rect m = sheet ? Rect{0, H - mh * s, W, mh * s} : Rect{(W - mw * s) / 2, (H - mh * s) / 2, mw * s, mh * s};
    if (sheet) gfx_->fill_round({m.x, m.y, m.w, m.h + 20 * s}, 16 * s, th.bg_light);
    else { gfx_->fill({m.x - 1, m.y - 1, m.w + 2, m.h + 2}, {255, 255, 255, 61}); gfx_->fill(m, th.bg_light); }
    // header
    fa(skin::Download, m.x + 18 * s + 7 * s, m.y + 16 * s + 16 * s, 14, th.accent);
    T(700, 15, m.x + 18 * s + 22 * s, m.y + 16 * s, 32, "Download Quality", th.text);
    Rect cl{m.x + m.w - 18 * s - 32 * s + 9 * s, m.y + 16 * s, 32 * s, 32 * s};
    fa(skin::Xmark, cl.x + 16 * s, cl.y + 16 * s, 14, u.hover(cl) ? th.text : th.sub);
    if (u.clicked(cl) || u.in.escape) { modal_ = Modal::None; mark(); return; }
    TW(400, 11, m.x + 18 * s, m.y + (16 + 32 + 4) * s, 16.5f, "Choose quality for offline storage. Higher quality uses more space.", {255, 255, 255, 153}, m.w - 36 * s);
    // the list scrolls between the header and the pinned buttons when the screen is too short for all five options (phones, the G88)
    Rect list{m.x, m.y + head_h * s, m.w, (mh - head_h - footer_h) * s};
    dq_scroll_.clamp(5 * item_h * s, list.h);
    if (u.hover(list)) dq_scroll_.y -= u.in.wheel * 60 * s;
    dq_scroll_.clamp(5 * item_h * s, list.h);
    gfx_->clip(&list);
    float y = list.y - dq_scroll_.y;
    for (int i = 0; i < 5; ++i) {
        const QOpt& o = kQualityOptions[i];
        Rect ir{m.x, y, m.w, item_h * s};
        const bool active = o.kbps == dq_value_;
        if (i) gfx_->fill({m.x, y, m.w, 1 * s}, {255, 255, 255, 15});
        if (active) gfx_->fill(ir, alpha(th.accent, 0.1f)); else if (u.hover(ir)) gfx_->fill(ir, {255, 255, 255, 31});
        const float bar_w = sheet ? 5.0f : 4.0f, bar_box = sheet ? 32.0f : 26.0f, bx = m.x + (sheet ? 20 : 18) * s;
        for (int b = 1; b <= 6; ++b) {
            const float bh = (b * 4 + 2) * s;
            gfx_->fill_round({bx + (b - 1) * (bar_w + 2) * s, y + (item_h * s - bar_box * s) / 2 + bar_box * s - bh, bar_w * s, bh}, 2 * s, b <= o.bars ? th.accent : Color{255, 255, 255, 102});
        }
        const float tx = bx + 6 * (bar_w + 2) * s + 14 * s;
        const float name_px = sheet ? 17.0f : 15.0f, desc_px = sheet ? 13.0f : 12.0f;
        T(600, name_px, tx, y + (item_h - (name_px * 1.3f + 2 + desc_px * 1.4f)) / 2 * s, name_px * 1.3f, o.label, active ? th.accent : th.text);
        T(400, desc_px, tx, y + (item_h - (name_px * 1.3f + 2 + desc_px * 1.4f)) / 2 * s + (name_px * 1.3f + 2) * s, desc_px * 1.4f, o.desc, th.sub);
        if (active) fa(skin::Check, m.x + m.w - (sheet ? 20 : 18) * s - 8 * s, y + item_h * s / 2, 16, th.accent);
        if (u.clicked(ir) && list.contains(u.in.mx, u.in.my)) dq_value_ = o.kbps;
        y += item_h * s;
    }
    gfx_->clip(nullptr);
    y = m.y + (mh - footer_h) * s + 8 * s;
    gfx_->fill({m.x, y, m.w, 1 * s}, {255, 255, 255, 31});
    y += (1 + 12) * s;
    const float cw = m.w - 36 * s - 10 * s, c1 = cw / 3.0f * 1.0f * 0.96f;
    Rect cancel{m.x + 18 * s, y, std::max(c1, 100 * s), 42 * s}, go{cancel.x + cancel.w + 10 * s, y, m.w - 36 * s - cancel.w - 10 * s, 42 * s};
    gfx_->fill_round(cancel, 8 * s, th.bg_medium);
    gfx_->stroke_round(cancel, 8 * s, 1 * s, u.hover(cancel) ? Color{255, 255, 255, 153} : Color{255, 255, 255, 102}, th.bg_medium);
    T(500, 14, cancel.x + (cancel.w - gfx_->measure(F(500, 14), "Cancel")) / 2, cancel.y, 42, "Cancel", th.text);
    const std::string label = bulk_downloading_ ? "Queuing\xE2\x80\xA6" : !dq_label_.empty() ? dq_label_ : dq_missing_label_ ? "Download Missing" : "Download All";
    gfx_->fill_round(go, 8 * s, u.hover(go) ? alpha(th.accent, 0.88f) : th.accent);
    const float lw = gfx_->measure(F(700, 14), label);
    fa(skin::Download, go.x + (go.w - lw - 22 * s) / 2 + 7 * s, go.y + 21 * s, 14, th.bg);
    T(700, 14, go.x + (go.w - lw - 22 * s) / 2 + 22 * s, go.y, 42, label, th.bg);
    if (u.clicked(cancel)) { modal_ = Modal::None; mark(); }
    if (u.clicked(go) && !bulk_downloading_) {
        modal_ = Modal::None;
        if (!dq_album_ids_.empty() && downloads_ && client_) {   // artist page: every album's songs, fetched now
            worker_->post([this, c = client_, s = session_, ids = dq_album_ids_, q = dq_value_] {
                for (const std::string& id : ids) { Album al; std::string e; if (c->album(id, al, e) && !al.songs.empty()) ui_post([this, s, al, q] { if (s == session_ && downloads_) { downloads_->enqueue(al.songs, q); prefetch_covers(al.songs); } }); }
            });
            dq_album_ids_.clear(); dq_label_.clear(); mark();
        } else if (!dq_songs_.empty() && downloads_) { downloads_->enqueue(dq_songs_, dq_value_); prefetch_covers(dq_songs_); dq_songs_.clear(); dq_label_.clear(); mark(); }
        else queue_downloads(dq_value_);
    }
}

// ---------------------------------------------------------------- offline mode and the prompts (App.tsx)

// Leaving Home when offline mode starts: Discover needs the server.
void App::toggle_offline() {
    offline_ = !offline_;
    cfg_.offline_mode = offline_; cfg_.save(cfg_path_);
    if (!offline_ && !client_ && !connecting_) {   // going online after an offline start: sign in again with the stored password
        // keyring lookup off the UI thread (it can block for seconds); without a stored password go back to the sign-in screen
        lookup_secret_async(cfg_.server_url + "|" + cfg_.username, [this](bool ok, std::string saved) {
            if (offline_ || client_ || connecting_) return;   // switched back meanwhile
            if (ok) { f_url_ = cfg_.server_url; f_user_ = cfg_.username; f_pass_ = std::move(saved); connect(false); }
            else { toast_ = "Sign in again to go online"; toast_until_ = SDL_GetTicks() + 3500; logout(); }
        });
        mark();
        return;
    }
    if (!offline_ && images_) images_->forget_offline_misses();
    alb_loaded_page_ = song_loaded_page_ = -1;   // the Albums and Songs lists switch between the server and the downloads
    if (offline_ && nav_ == Nav::Home) set_nav(Nav::Library);
    load_home(); load_artists();
    for (int i = 0; i < 4; ++i) load_section(i);
    mark();
}

// Asks the server; going from reachable to unreachable (and not on mobile data) offers offline mode.
void App::check_connectivity() {
    if (!client_ || offline_) return;
    worker_->post([this, c = client_, s = session_] {
        std::string e; const bool up = c->ping(e);
        ui_post([this, s, up] {
            if (s != session_) return;
            const bool was = online_; online_ = up;
            if (was && !up && !offline_ && prompt_ == Prompt::None) { prompt_ = Prompt::Offline; mark(); }
        });
    });
}

// App.tsx "No Internet Connection" and "Mobile Data Detected" prompts.
void App::draw_connectivity_prompt(float W, float H) {
    if (prompt_ == Prompt::Offline) {
        const int r = draw_prompt(W, H, 0xf071, "No Internet Connection", "You appear to be offline. Would you like to enable Offline Mode to use cached songs?", {}, "", "Continue Online Mode", "Enable Offline Mode", 0xf072, false);
        if (r == 1) { prompt_ = Prompt::None; mark(); }
        else if (r == 2) { prompt_ = Prompt::None; if (!offline_) toggle_offline(); }
    } else if (prompt_ == Prompt::Cellular) {
        const int r = draw_prompt(W, H, 0xf012, "Mobile Data Detected", "Offline mode has been enabled to protect your data. Your cached library is available without using any data.", {}, "", "Stay Offline", "Go Online", 0xf1eb, false);
        if (r == 1) { prompt_ = Prompt::None; mark(); }
        else if (r == 2) { prompt_ = Prompt::None; cellular_prompt_shown_ = true; if (offline_) toggle_offline(); }
    }
}

// .quality-picker-modal shell shared by the download picker, Switch Server and Enter Password: centred 420 px box (bottom sheet on
// phones) with title, close button and hint. Returns the content area of the given logical height.
Rect App::qp_frame(float W, float H, float content_h, const char* title, uint32_t icon, const char* hint) {
    Ui& u = *ui_;
    const float s = S_;
    const Theme& th = u.th;
    const bool sheet = mobile_;
    const float head_h = 16 + 32 + (hint && *hint ? 4 + 16.5f + 10 : 8);
    const float mh = std::min(head_h + content_h + 8, H / s * 0.8f), mw = sheet ? W / s : std::min(420.0f, 0.92f * W / s);
    Rect m = sheet ? Rect{0, H - mh * s, W, mh * s} : Rect{(W - mw * s) / 2, (H - mh * s) / 2, mw * s, mh * s};
    if (sheet) gfx_->fill_round({m.x, m.y, m.w, m.h + 20 * s}, 16 * s, th.bg_light);
    else { gfx_->fill({m.x - 1, m.y - 1, m.w + 2, m.h + 2}, {255, 255, 255, 61}); gfx_->fill(m, th.bg_light); }
    fa(icon, m.x + 18 * s + 7 * s, m.y + 32 * s, 14, th.accent);
    T(700, 15, m.x + 18 * s + 22 * s, m.y + 16 * s, 32, title, th.text);
    Rect cl{m.x + m.w - 18 * s - 32 * s + 9 * s, m.y + 16 * s, 32 * s, 32 * s};
    fa(skin::Xmark, cl.x + 16 * s, cl.y + 16 * s, 14, u.hover(cl) ? th.text : th.sub);
    if (u.clicked(cl) || u.in.escape) { modal_ = Modal::None; mark(); }
    if (hint && *hint) T(400, 11, m.x + 18 * s, m.y + (16 + 32 + 4) * s, 16.5f, hint, {255, 255, 255, 153}, m.w - 36 * s);
    return {m.x, m.y + head_h * s, m.w, content_h * s};
}

// SwitchServerSection: with a saved password the connection is tried at once; otherwise (or when it fails) the password is asked for.
void App::switch_to(int idx, const std::string& password, bool from_keyring) {
    if (idx < 0 || idx >= static_cast<int>(cfg_.conns.size()) || switching_) return;
    const Config::Conn conn = cfg_.conns[static_cast<size_t>(idx)];
    std::string pass = password;
    if (pass.empty() && !switch_lookup_done_) {   // keyring read off the UI thread, then come back with whatever it found
        switch_lookup_done_ = true;
        lookup_secret_async(conn.url + "|" + conn.user, [this, idx](bool ok, std::string saved) {
            if (ok) switch_to(idx, saved, true);
            else { switch_lookup_done_ = true; switch_to(idx, ""); }
        });
        return;
    }
    switch_lookup_done_ = false;
    if (pass.empty()) { switch_idx_ = idx; switch_pass_.clear(); switch_err_.clear(); modal_ = Modal::SwitchPass; mark(); return; }
    switching_ = true;
    auto probe = std::make_shared<SubsonicClient>(conn.url, conn.user, pass, copt_);
    worker_->post([this, probe, idx, pass, from_prompt = !password.empty() && !from_keyring, s = session_] {
        std::string e; const bool ok = probe->ping(e);
        ui_post([this, ok, idx, pass, from_prompt, e, s] {
            if (s != session_) return;
            switching_ = false;
            if (ok) {
                modal_ = Modal::None;
                const Config::Conn& c = cfg_.conns[static_cast<size_t>(idx)];
                f_url_ = c.url; f_user_ = c.user; f_pass_ = pass; xf_log("switching server to %s", c.url.c_str());
                connect(false);
            } else if (from_prompt) { switch_err_ = "Wrong password or connection failed."; }
            else { switch_idx_ = idx; switch_pass_.clear(); switch_err_.clear(); modal_ = Modal::SwitchPass; }
            mark();
        });
    });
}
