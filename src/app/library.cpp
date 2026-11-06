// Library page: Artists / Albums / Songs / Liked. Exact port of ArtistList, AllAlbumsGrid, AllSongsGrid, LikedSongsView,
// LibraryViewToggle, ShuffleAllButton and Pagination (measured layout at 1024x768, see docs/ui-ref).
#include <algorithm>
#include <cmath>
#include "app.h"

namespace {
const int kPageSize = 50;
Color alpha(Color c, float a) { c.a = static_cast<uint8_t>(std::lround(a * 255)); return c; }
std::string mmss(int s) {
    if (s < 0) s = 0;
    char b[16];
    std::snprintf(b, sizeof b, "%d:%02d", s / 60, s % 60);
    return b;
}
}  // namespace

// ---------------------------------------------------------------- data

// Offline: the Albums and Songs views list what is downloaded (AllAlbumsGrid / AllSongsGrid), paged like the online lists.
void App::load_offline_page(int mode, int page) {
    std::vector<Album> albums;
    std::vector<Song> songs;
    if (downloads_) {
        albums = downloads_->done_albums();
        for (auto& a : albums) for (auto& s : a.songs) songs.push_back(s);
    }
    auto lower = [](std::string v) { for (auto& c : v) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32); return v; };
    const size_t off = static_cast<size_t>(page) * kPageSize;
    if (mode == 1) {
        std::sort(albums.begin(), albums.end(), [&](const Album& a, const Album& b) { return lower(a.name) < lower(b.name); });
        alb_more_ = albums.size() > off + kPageSize;
        alb_items_.clear();
        for (size_t i = off; i < albums.size() && i < off + kPageSize; ++i) alb_items_.push_back(albums[i]);
        offline_total_[0] = static_cast<int>(albums.size());
        alb_loaded_page_ = page;
    } else {
        std::sort(songs.begin(), songs.end(), [&](const Song& a, const Song& b) { return lower(a.title) < lower(b.title); });
        song_more_ = songs.size() > off + kPageSize;
        song_items_.clear();
        for (size_t i = off; i < songs.size() && i < off + kPageSize; ++i) song_items_.push_back(songs[i]);
        offline_total_[1] = static_cast<int>(songs.size());
        song_loaded_page_ = page;
    }
    mark();
}

void App::load_albums_page(int page) {
    if (offline_) { load_offline_page(1, page); return; }
    if (alb_loading_ || !client_) return;
    alb_loading_ = true;
    std::string key = "albums:p" + std::to_string(page);
    std::string j;
    if (store_ && store_->get(key, j)) { std::vector<Album> a; if (from_json(j, a)) { alb_items_ = a; alb_more_ = a.size() == static_cast<size_t>(kPageSize); alb_loaded_page_ = page; } }
    worker_->post([this, c = client_, s = session_, page, key] {
        std::vector<Album> a; std::string e;
        bool ok = c->album_list("alphabeticalByName", kPageSize, page * kPageSize, a, e);
        ui_post([this, s, page, key, a = std::move(a), ok]() mutable {
            if (s != session_) return;
            alb_loading_ = false;
            if (ok) { if (store_) store_->put(key, to_json(a)); alb_more_ = a.size() == static_cast<size_t>(kPageSize); alb_items_ = std::move(a); alb_loaded_page_ = page; }
            mark();
        });
    });
}

void App::load_songs_page(int page) {
    if (offline_) { load_offline_page(2, page); return; }
    if (song_loading_ || !client_) return;
    song_loading_ = true;
    worker_->post([this, c = client_, s = session_, page] {
        std::vector<Song> r; std::string e;
        bool ok = c->search_songs("", kPageSize, page * kPageSize, r, e);
        ui_post([this, s, page, r = std::move(r), ok]() mutable {
            if (s != session_) return;
            song_loading_ = false;
            if (ok) { song_more_ = r.size() == static_cast<size_t>(kPageSize); song_items_ = std::move(r); song_loaded_page_ = page; }
            mark();
        });
    });
}

void App::load_liked() {
    if (liked_loading_ || !client_) return;
    liked_loading_ = true;
    worker_->post([this, c = client_, s = session_] {
        std::vector<Song> r; std::string e;
        bool ok = c->starred(r, e);
        ui_post([this, s, r = std::move(r), ok]() mutable {
            if (s != session_) return;
            liked_loading_ = false;
            if (ok) { if (store_) store_->index_songs(r); liked_ = std::move(r); liked_loaded_ = true; }
            else if (store_) { liked_ = store_->liked_songs(); liked_loaded_ = !liked_.empty(); }   // offline: serve the indexed likes
            mark();
        });
    });
}

void App::load_song_count() {
    if (total_songs_ >= 0 || !client_) return;
    total_songs_ = 0;
    worker_->post([this, c = client_, s = session_] {  // sum of songCount over the first 500 albums, as getSongCount did
        std::vector<Album> a; std::string e;
        int total = 0;
        if (c->album_list("alphabeticalByName", 500, 0, a, e)) for (auto& al : a) total += al.song_count;
        ui_post([this, s, total] { if (s != session_) return; total_songs_ = total; mark(); });
    });
}

// ---------------------------------------------------------------- drawing

void App::draw_library(Rect r) {
    Ui& u = *ui_;
    const float s = S_;
    const Theme& th = u.th;
    const bool narrow = mobile_;   // compact (<=767 px): main-content padding 12, icon-only toggle, full-width stats and actions, 3-column grids
    const float pad = (narrow ? 12.0f : static_cast<float>(skin::kContentPad)) * s;
    float x = r.x + pad, w = r.w - 2 * pad - 12 * s;   // 12 px scrollbar gutter on every layout
    if (lib_mode_ == 1 && alb_loaded_page_ != lib_page_[1] && !alb_loading_) load_albums_page(lib_page_[1]);
    if (lib_mode_ == 2 && song_loaded_page_ != lib_page_[2] && !song_loading_) load_songs_page(lib_page_[2]);
    if (lib_mode_ == 3 && !liked_loaded_) load_liked();
    load_song_count();

    // content metrics first so the wheel can clamp
    const std::vector<Artist>& arts = artists_;
    if (lib_mode_ == 0 && arts.empty() && !loading_artists_ && artists_loaded_) {
        const bool err = !load_err_.empty();
        const bool online_switch = err && offline_ && load_err_.find("No cached songs") != std::string::npos;
        const std::string title = err ? "Error Loading Artists" : offline_ ? "No Cached Artists" : "No Artists Found";
        const std::string msg = err ? load_err_ : offline_ ? "Download some songs to listen offline." : "Your music library appears to be empty.";
        if (draw_library_state(x, r.y + pad, w, err ? 0xf06au : static_cast<uint32_t>(skin::Music), title, msg, err,
                               err ? (online_switch ? "Switch to Online Mode" : "Retry") : "", online_switch ? static_cast<uint32_t>(skin::Cloud) : static_cast<uint32_t>(skin::Sync))) {
            if (online_switch) toggle_offline(); else load_artists();
        }
        return;
    }
    size_t n_items = lib_mode_ == 0 ? std::min<size_t>(kPageSize, arts.size() > static_cast<size_t>(lib_page_[0] * kPageSize) ? arts.size() - lib_page_[0] * kPageSize : 0)
                   : lib_mode_ == 1 ? alb_items_.size() : lib_mode_ == 2 ? song_items_.size() : liked_.size();
    int cols_a = std::max(1, static_cast<int>((w / s + 32) / (201 + 32)));
    int cols_b = std::max(1, static_cast<int>((w / s + 24) / (207 + 24)));
    // compact header: toggle 52, gap 12, then one 40 px row: the count on the left, round Shuffle and Download buttons on the right
    // Artists: the stats and actions wrap under the toggle, but share its row when the page is wide enough
    const float art_dx = offline_ ? 47 * s : 0;
    const bool stats_inline = !narrow && lib_mode_ == 0 && w >= 402 * s + 28 * s + 707 * s + art_dx;
    float header_h = narrow ? (52 + 12 + 40) * s : (lib_mode_ == 0 && !stats_inline ? 99 : 42) * s;
    if (narrow) { cols_a = 3; cols_b = 3; }
    const float base_header_h = header_h;
    const float err_h = shuffle_error_.empty() ? 0.0f : 45.0f * s;   // .library-inline-error: 8 + 8 + line + 8
    header_h += err_h;
    float body_h = narrow && lib_mode_ <= 1 ? std::ceil(static_cast<float>(n_items) / 3) * (133 + 6) * s
                 : lib_mode_ == 0 ? std::ceil(static_cast<float>(n_items) / cols_a) * (261 + 31) * s
                 : lib_mode_ == 1 ? std::ceil(static_cast<float>(n_items) / cols_b) * (266 + 24) * s
                 : n_items * 72 * s;
    float total = pad + header_h + 32 * s + body_h + 100 * s;
    if (u.hover(r)) lib_scroll_.y -= u.in.wheel * 60 * s;
    lib_scroll_.clamp(total + pad, r.h);
    float y0 = r.y + pad - lib_scroll_.y;

    // view toggle (402x41 pill)
    Color edge = alpha(th.primary_light, 0.464f);
    Rect tg{x, y0, narrow ? w : 402 * s, (narrow ? 52.0f : 41.0f) * s};
    gfx_->stroke_round(tg, narrow ? 26.0f * s : 20.5f * s, 1 * s, edge, th.bg);
    static const char* names[4] = {"Artists", "Albums", "Songs", "Liked"};
    static const uint32_t icons[4] = {0xf0c0, 0xf8d9, skin::Music, skin::Heart};
    static const float bx[4] = {5, 103, 209, 308}, bw[4] = {94, 102, 95, 89};
    for (int i = 0; i < 4; ++i) {
        const float nbw = (w - 10 * s) / 4;
        Rect b = narrow ? Rect{x + 5 * s + i * nbw, y0 + 4 * s, nbw, 44 * s} : Rect{x + bx[i] * s, y0 + 5 * s, bw[i] * s, 31 * s};
        bool on = lib_mode_ == i;
        const float br = narrow ? 22.0f * s : 15.5f * s;
        if (on) gfx_->fill_round(b, br, th.accent);
        else if (u.hover(b)) gfx_->fill_round(b, br, th.hover);
        Color c = on ? rgb(0xffffff) : th.sub;
        if (narrow) fa(icons[i], b.x + b.w / 2, b.y + b.h / 2, 14.5f, c);   // .view-toggle-btn span hidden: icon only
        else {
            fa(icons[i], b.x + 24 * s, b.y + 15.5f * s, 13, c);
            T(500, 13, b.x + 40 * s, b.y, 31, names[i], c);
        }
        if (u.clicked(b) && lib_mode_ != i) { lib_mode_ = i; lib_scroll_.y = 0; mark(); }
    }
    // right side / second row: stats pills, Shuffle All, Download All
    auto pill = [&](float px, float py, float pw, uint32_t ic, const std::string& label) {
        Rect b{px, py, pw, 42 * s};
        gfx_->stroke_round(b, 21 * s, 1 * s, {255, 255, 255, 54}, th.bg);
        fa(ic, b.x + 24 * s, b.y + 21 * s, 13, th.accent);
        T(400, 13, b.x + 41 * s, b.y, 42, label, th.sub);
    };
    // .shuffle-all-button: accent tint; .confirming orange (#ff9500); :disabled grey. On phones it spans the row at 48 px.
    auto action = [&](float px, float py, float pw, uint32_t ic, const std::string& label, Color tint, bool disabled) {
        const float bh = narrow ? 48.0f * s : 38.0f * s;
        Rect b{px, py, pw, bh};
        if (disabled) tint = {108, 117, 125, 255};
        const float fa_ = disabled ? 0.15f : (u.hover(b) ? 0.25f : 0.15f);  // tint over the page background
        auto mixc = [&](uint8_t f, uint8_t bg_) { return static_cast<uint8_t>(std::lround(f * fa_ + bg_ * (1 - fa_))); };
        Color fill{mixc(tint.r, th.bg.r), mixc(tint.g, th.bg.g), mixc(tint.b, th.bg.b), 255};
        gfx_->fill_round(b, narrow ? 24 * s : 20 * s, fill);
        gfx_->stroke_round(b, narrow ? 24 * s : 20 * s, 1 * s, alpha(tint, 0.3f), fill);
        const float lw = gfx_->measure(F(500, narrow ? 15.0f : 14.0f), label);
        const float cx0 = narrow ? b.x + (b.w - lw - 22 * s) / 2 : b.x + 28 * s - 8 * s;
        fa(ic, cx0 + 8 * s, b.y + bh / 2, 14, tint);
        T(500, narrow ? 15.0f : 14.0f, cx0 + 22 * s, b.y, bh / s, label, tint);
        return !disabled && u.clicked(b);
    };
    const Color orange{0xff, 0x95, 0x00, 255};
    auto shuffle_btn = [&](float px, float py, float pw) {
        const bool conf = shuffle_confirm_, busy = shuffling_all_;
        if (action(px, py, pw, busy ? 0xf110u : conf ? 0xf071u : static_cast<uint32_t>(skin::Shuffle), busy ? "Loading..." : conf ? "Confirm?" : "Shuffle All", conf ? orange : th.accent, busy)) shuffle_all_pressed();
    };
    auto download_btn = [&](float px, float py, float pw) {
        const int st = download_btn_state();
        if (st == 0) return;
        const std::string label = bulk_downloading_ ? "Queuing\xE2\x80\xA6" : st == 2 ? "Download Missing" : "Download All";
        if (action(px, py, pw, bulk_downloading_ ? 0xf110u : static_cast<uint32_t>(skin::Download), label, th.accent, bulk_downloading_)) open_download_picker(st == 2);
    };
    int n_art = static_cast<int>(arts.size()), n_alb = 0;
    for (auto& a : arts) n_alb += a.album_count;
    if (narrow) {
        const float ry = y0 + (52 + 12) * s;
        std::string label; uint32_t ic;
        if (lib_mode_ == 0) { ic = 0xf0c0; label = std::to_string(n_art) + (offline_ ? " cached artists" : " artists"); }
        else if (lib_mode_ == 3) { ic = skin::Heart; label = std::to_string(liked_.size()) + " liked"; }
        else {
            int off = lib_page_[lib_mode_] * kPageSize;
            size_t cnt = lib_mode_ == 1 ? alb_items_.size() : song_items_.size();
            bool more = lib_mode_ == 1 ? alb_more_ : song_more_;
            label = cnt ? std::to_string(off + 1) + "\xE2\x80\x93" + std::to_string(off + cnt) + (lib_mode_ == 1 && more ? " of " + std::to_string(off + cnt + 1) + "+" : "") : "0";
            if (offline_) label = std::to_string(offline_total_[lib_mode_ == 1 ? 0 : 1]) + (lib_mode_ == 1 ? " cached albums" : " cached songs");   // AllAlbumsGrid / AllSongsGrid
            ic = lib_mode_ == 1 ? 0xf8d9u : static_cast<uint32_t>(skin::Music);
        }
        Color lc = th.sub; bool warn = false;
        if (lib_mode_ != 3 && shuffle_confirm_) { label = "Tap again to confirm"; lc = orange; ic = skin::Warning; warn = true; }
        else if (lib_mode_ != 3 && shuffling_all_) label = "Loading...";
        const bool has_dl = lib_mode_ != 3 && download_btn_state() != 0;
        const float btn = 40 * s, tw = w - (lib_mode_ == 3 ? 0 : (has_dl ? 2 : 1) * (btn + 8 * s)) - 26 * s;
        fa(ic, x + 8 * s, ry + btn / 2, 14, warn ? lc : th.accent);
        T(500, 14, x + 26 * s, ry, 40, label, lc, tw);
        auto circ = [&](float px, uint32_t gi, Color tint, bool disabled) {
            Rect b{px, ry, btn, btn};
            if (disabled) tint = {108, 117, 125, 255};
            gfx_->fill_round(b, btn / 2, alpha(tint, disabled ? 0.15f : u.hover(b) ? 0.30f : 0.18f));
            fa(gi, b.x + btn / 2, b.y + btn / 2, 16, tint);
            return !disabled && u.clicked(b);
        };
        if (lib_mode_ != 3) {
            float bx2 = x + w - btn;
            if (circ(bx2, shuffling_all_ ? 0xf110u : static_cast<uint32_t>(skin::Shuffle), shuffle_confirm_ ? orange : th.accent, shuffling_all_)) shuffle_all_pressed();
            if (has_dl) {
                bx2 -= btn + 8 * s;
                if (circ(bx2, bulk_downloading_ ? 0xf110u : static_cast<uint32_t>(skin::Download), th.accent, bulk_downloading_)) open_download_picker(download_btn_state() == 2);
            }
        }
    } else {
        float ry = lib_mode_ == 0 && !stats_inline ? y0 + 57 * s : y0 - 1 * s;
        float rx = lib_mode_ == 0 ? (stats_inline ? x + w - (707 * s + art_dx) : x) : x + w - (lib_mode_ == 3 ? 104 : 399) * s;
        if (lib_mode_ == 0) {
            const float dx = art_dx;   // "cached artists" is wider
            pill(rx, ry, 113 * s + dx, 0xf0c0, std::to_string(n_art) + (offline_ ? " cached artists" : " artists"));
            pill(rx + 129 * s + dx, ry, 126 * s, 0xf8d9, std::to_string(n_alb) + " albums");
            pill(rx + 271 * s + dx, ry, 126 * s, skin::Music, std::to_string(std::max(0, total_songs_)) + " songs");
            shuffle_btn(rx + 413 * s + dx, ry + 2 * s, 130 * s);
            download_btn(rx + 559 * s + dx, ry + 2 * s, 148 * s);
        } else if (lib_mode_ == 3) {
            pill(rx, ry, 104 * s, skin::Heart, std::to_string(liked_.size()));
        } else {
            int off = lib_page_[lib_mode_] * kPageSize;
            size_t cnt = lib_mode_ == 1 ? alb_items_.size() : song_items_.size();
            bool more = lib_mode_ == 1 ? alb_more_ : song_more_;
            std::string range = cnt ? std::to_string(off + 1) + "\xE2\x80\x93" + std::to_string(off + cnt) + (lib_mode_ == 1 && more ? " of " + std::to_string(off + cnt + 1) + "+" : "") : "0";
            float pw = lib_mode_ == 1 ? 110 * s : 89 * s;
            pill(rx - (lib_mode_ == 1 ? 21 * s : 0), ry, pw, lib_mode_ == 1 ? 0xf8d9u : static_cast<uint32_t>(skin::Music), range);
            float ax = rx - (lib_mode_ == 1 ? 21 * s : 0) + pw + 16 * s;
            shuffle_btn(ax, ry + 2 * s, 130 * s);
            download_btn(ax + 146 * s, ry + 2 * s, 148 * s);
        }


    }

    if (err_h > 0) {
        Rect er{x, y0 + base_header_h, w, 37 * s};
        gfx_->fill_round(er, 4 * s, {255, 59, 48, 38});
        fa(0xf06a, er.x + 16 * s + 7 * s, er.y + er.h / 2, 14, rgb(0xff3b30));
        T(400, 14, er.x + 16 * s + 22 * s, er.y, 37, shuffle_error_, rgb(0xff3b30), er.w - 60 * s);
    }
    float cy = y0 + header_h + (narrow ? 16.0f : 32.0f) * s;
    Rect clipr{r.x, r.y, r.w, r.h};
    gfx_->clip(&clipr);
    const Song* playing = queue_.current();
    if (lib_mode_ == 0) {
        float cw = 201 * s, chh = 261 * s;
        size_t first = static_cast<size_t>(lib_page_[0]) * kPageSize;
        for (size_t i = 0; i < n_items; ++i) {
            const Artist& a = arts[first + i];
            if (narrow) {   // .artists-grid (compact): 3 columns, 6 px gaps, 5 px padding, 100 px cover cropped, 11 px name, album count hidden
                const float cwn = (w - 12 * s) / 3, chn = 133 * s;
                const int cn = static_cast<int>(i) % 3, rn = static_cast<int>(i) / 3;
                const float pxn = x + cn * (cwn + 6 * s), pyn = cy + rn * (chn + 6 * s);
                if (pyn + chn < r.y || pyn > r.y + r.h) continue;
                Rect card{pxn, pyn, cwn, chn};
                if (u.hover(card) && r.contains(u.in.mx, u.in.my)) gfx_->fill_round(card, 12 * s, th.hover);
                const float d = cwn - 10 * s;   // round artist photo, name and album count centred under it
                Rect av{pxn + 5 * s, pyn + 4 * s, d, d};
                std::string cid = a.cover_art.empty() ? a.id : a.cover_art;
                if (SDL_Texture* t = cover(cid, 110, a.name, "")) gfx_->image_round(t, av, d / 2); else draw_art_fallback(av, cid, d / 2);
                const float nw = std::min(gfx_->measure(F(600, 12), a.name), cwn - 8 * s);
                T(600, 12, pxn + (cwn - nw) / 2, av.y + d + 6 * s, 17, a.name, th.text, cwn - 8 * s);
                const std::string cnt = std::to_string(a.album_count) + (a.album_count == 1 ? " album" : " albums");
                const float cw2 = std::min(gfx_->measure(F(400, 11), cnt), cwn - 8 * s);
                T(400, 11, pxn + (cwn - cw2) / 2, av.y + d + 23 * s, 16, cnt, th.sub, cwn - 8 * s);
                if (u.clicked(card) && r.contains(u.in.mx, u.in.my)) { push_view(ViewKind::Artist, a.id, a.name); load_artist(a.id); }
                continue;
            }
            int col = static_cast<int>(i) % cols_a, row = static_cast<int>(i) / cols_a;
            float px = x + std::max(0.0f, (w - (cols_a * cw + (cols_a - 1) * 32 * s)) / 2) + col * (cw + 32 * s), py = cy + row * (chh + 31 * s);
            if (py + chh < r.y || py > r.y + r.h) continue;
            Rect card{px, py, cw, chh};
            gfx_->fill_round(card, 16 * s, th.panel);
            if (u.hover(card) && r.contains(u.in.mx, u.in.my)) gfx_->fill_round(card, 16 * s, th.hover);
            Rect cov{px + 16 * s, py + 16 * s, 169 * s, 169 * s};
            std::string cid = a.cover_art.empty() ? a.id : a.cover_art;
            if (SDL_Texture* t = cover(cid, 169, a.name, "")) gfx_->image_round(t, cov, 12 * s);
            else draw_art_fallback(cov, cid, 12 * s);
            if (cached_artists_.count(norm_artist(a.name))) draw_cached_badge(cov);
            int fn = F(600, 16);
            float nw = std::min(gfx_->measure(fn, a.name), cov.w);
            T(600, 16, cov.x + (cov.w - nw) / 2, py + 197 * s, 24, a.name, th.text, cov.w);
            std::string cnt = std::to_string(a.album_count) + " albums";
            float cwid = gfx_->measure(F(400, 13), cnt);
            T(400, 13, cov.x + (cov.w - cwid) / 2, py + 225 * s, 20, cnt, th.sub, cov.w);
            if (u.clicked(card) && r.contains(u.in.mx, u.in.my)) { push_view(ViewKind::Artist, a.id, a.name); load_artist(a.id); }
        }
        if (arts.empty()) T(400, 16, x, cy, 24, loading_artists_ ? "Loading..." : "No artists found", th.sub);
    } else if (lib_mode_ == 1) {
        float cw = 207 * s, chh = 266 * s;
        for (size_t i = 0; i < alb_items_.size(); ++i) {
            const Album& a = alb_items_[i];
            if (narrow) {   // .albums-grid (compact), same card as the artists grid; the artist line is hidden
                const float cwn = (w - 12 * s) / 3, chn = 133 * s;
                const int cn = static_cast<int>(i) % 3, rn = static_cast<int>(i) / 3;
                const float pxn = x + cn * (cwn + 6 * s), pyn = cy + rn * (chn + 6 * s);
                if (pyn + chn < r.y || pyn > r.y + r.h) continue;
                Rect card{pxn, pyn, cwn, chn};
                if (u.hover(card) && r.contains(u.in.mx, u.in.my)) gfx_->fill_round(card, 12 * s, th.hover);
                Rect sq{pxn + 2 * s, pyn, cwn - 4 * s, cwn - 4 * s};   // flat card: square art, then title and artist left aligned
                if (SDL_Texture* t = cover(a.cover_art, 110, a.artist, a.name)) gfx_->image_round(t, sq, 8 * s); else draw_art_fallback(sq, a.cover_art, 8 * s);
                T(600, 12, sq.x, sq.y + sq.h + 4 * s, 17, a.name, th.text, sq.w);
                T(400, 11, sq.x, sq.y + sq.h + 21 * s, 16, a.artist, th.sub, sq.w);
                if (u.clicked(card) && r.contains(u.in.mx, u.in.my)) { push_view(ViewKind::Album, a.id, a.name); load_album(a.id); }
                continue;
            }
            int col = static_cast<int>(i) % cols_b, row = static_cast<int>(i) / cols_b;
            float px = x + std::max(0.0f, (w - (cols_b * cw + (cols_b - 1) * 24 * s)) / 2) + col * (cw + 24 * s), py = cy + row * (chh + 24 * s);
            if (py + chh < r.y || py > r.y + r.h) continue;
            Rect card{px, py, cw, chh};
            gfx_->fill_round(card, 16 * s, th.panel);
            if (u.hover(card) && r.contains(u.in.mx, u.in.my)) gfx_->fill_round(card, 16 * s, th.hover);
            Rect cov{px + 16 * s, py + 16 * s, 175 * s, 175 * s};
            if (SDL_Texture* t = cover(a.cover_art, 175, a.artist, a.name)) gfx_->image_round(t, cov, 4 * s);
            else draw_art_fallback(cov, a.cover_art, 4 * s);
            if (album_cached(a)) draw_cached_badge(cov);
            draw_cover_cached_badge(cov, a.cover_art);
            float nw = std::min(gfx_->measure(F(600, 15.2f), a.name), cov.w);
            T(600, 15.2f, cov.x + (cov.w - nw) / 2, py + 203 * s, 23, a.name, th.text, cov.w);
            float aw = std::min(gfx_->measure(F(600, 13), a.artist), cov.w);
            T(600, 13, cov.x + (cov.w - aw) / 2, py + 229 * s, 20, a.artist, th.sub, cov.w);
            if (u.clicked(card) && r.contains(u.in.mx, u.in.my)) { push_view(ViewKind::Album, a.id, a.name); load_album(a.id); }
        }
        if (alb_items_.empty()) {
            if (alb_loading_) T(400, 16, x, cy, 24, "Loading...", th.sub);
            else draw_library_state(x, cy, w, 0xf51fu, offline_ ? "No Cached Albums" : "No Albums Found", "", false, "", 0);
        }
    } else {
        const std::vector<Song>& list = lib_mode_ == 2 ? song_items_ : liked_;
        for (size_t i = 0; i < list.size(); ++i) {
            const Song& sg = list[i];
            Rect row{x, cy + i * 72 * s, w, 72 * s};
            if (row.y + row.h < r.y || row.y > r.y + r.h) continue;
            bool isplay = playing && playing->id == sg.id;
            bool hov = u.hover(row) && r.contains(u.in.mx, u.in.my);
            if (isplay) gfx_->fill_round(row, 8 * s, alpha(th.accent, 0.12f));
            else if (hov) gfx_->fill_round(row, 8 * s, th.hover);
            Rect art{row.x + 16 * s, row.y + 12 * s, 48 * s, 48 * s};
            if (SDL_Texture* t = cover(sg.cover_art, 48)) gfx_->image_round(t, art, 8 * s);
            else gfx_->fill_round(art, 8 * s, th.panel2);
            if (isplay || hov) {
                gfx_->fill_round(art, 8 * s, {0, 0, 0, 140});
                fa(isplay && player_.playing() ? static_cast<uint32_t>(skin::Pause) : static_cast<uint32_t>(skin::Play), art.x + 24 * s, art.y + 24 * s, 16, rgb(0xffffff));
            }
            float tw = row.w - 80 * s - 100 * s;
            T(600, 15.2f, row.x + 80 * s, row.y + 12 * s, 23, sg.title, isplay ? th.accent : th.text, tw);
            float aw = T(400, 13.6f, row.x + 80 * s, row.y + 39 * s, 20, sg.artist, th.sub, tw * 0.5f);
            T(400, 13.6f, row.x + 80 * s + aw, row.y + 39 * s, 20, sg.album.empty() ? "" : " \xC2\xB7 " + sg.album, th.muted, tw - aw);
            std::string d = mmss(sg.duration);
            float dw = gfx_->measure(F(400, 13), d);
            T(400, 13, row.x + row.w - 16 * s - dw, row.y + 26 * s, 20, d, th.sub);
            ctx_hit(row, sg);
            if (u.clicked(row) && r.contains(u.in.mx, u.in.my)) play_songs(list, i, false);
        }
        if (list.empty()) {
            bool busy = lib_mode_ == 2 ? song_loading_ : liked_loading_;
            float ey = cy + 40 * s;
            if (busy) T(400, 16, x, cy, 24, "Loading...", th.sub);
            else if (lib_mode_ == 3) draw_library_state(x, cy, w, skin::Heart, "No Liked Songs", "Star songs on your server to see them here.", false, "", 0);
            else draw_library_state(x, cy, w, skin::Music, offline_ ? "No Cached Songs" : "No Songs Found", offline_ ? "Download some songs to listen offline." : "", false, "", 0);
        }
    }

    // pagination (online songs/albums: Previous | current | Next; artists: page numbers when > 50)
    bool paged = lib_mode_ == 1 || lib_mode_ == 2 || (lib_mode_ == 0 && arts.size() > static_cast<size_t>(kPageSize));
    if (paged) {
        int page = lib_page_[lib_mode_];
        int total_pages = lib_mode_ == 0 ? static_cast<int>((arts.size() + kPageSize - 1) / kPageSize) : 0;
        bool more = lib_mode_ == 1 ? alb_more_ : lib_mode_ == 2 ? song_more_ : page + 1 < total_pages;
        float py = cy + body_h + 21 * s;
        auto nav = [&](float px, float wd, const std::string& label, bool left, bool enabled) {
            Rect b{px, py, wd, 38 * s};
            gfx_->stroke_round(b, 12 * s, 1 * s, rgb(0x2a2a2a), th.panel);
            Color c = enabled ? th.text : th.muted;
            if (enabled && u.hover(b)) gfx_->fill_round(b, 12 * s, th.hover);
            fa(left ? static_cast<uint32_t>(skin::ChevLeft) : static_cast<uint32_t>(skin::ChevRight), left ? b.x + 21 * s : b.x + wd - 21 * s, b.y + 19 * s, 12, c);
            float lw = gfx_->measure(F(500, 14), label);
            T(500, 14, left ? b.x + 38 * s : b.x + wd - 38 * s - lw, b.y, 38, label, c);
            return enabled && u.clicked(b);
        };
        float cx = x + w / 2;
        if (nav(cx - 119 * s - 12 * s - 20 * s + 0 * s, 119 * s, "Previous", true, page > 0)) { --lib_page_[lib_mode_]; lib_scroll_.y = 0; mark(); }
        Rect pg{cx - 20 * s, py - 1 * s, 40 * s, 40 * s};
        gfx_->fill_round(pg, 12 * s, th.accent);
        T(500, 14, pg.x + (pg.w - gfx_->measure(F(500, 14), std::to_string(page + 1))) / 2, pg.y, 40, std::to_string(page + 1), rgb(0xffffff));
        if (nav(cx + 20 * s + 12 * s, 94 * s, "Next", false, more)) { ++lib_page_[lib_mode_]; lib_scroll_.y = 0; mark(); }
    }
    gfx_->clip(nullptr);
}

// .library-state (+ .is-error): centred icon 48, h3, message and an optional full-width pill button; padding 40 20.
bool App::draw_library_state(float x, float y, float w, uint32_t icon, const std::string& title, const std::string& msg, bool err, const std::string& button, uint32_t button_icon) {
    Ui& u = *ui_; const float s = S_; const Theme& th = u.th;
    const Color col = err ? rgb(0xff3b30) : th.sub;
    const float cx = x + w / 2;
    float sy = y + 40 * s;
    fa(icon, cx, sy + 24 * s, 48, col);
    sy += 64 * s;
    T(700, 18.7f, cx - gfx_->measure(F(700, 18.7f), title) / 2, sy, 28, title, col);
    sy += 36 * s;
    if (!msg.empty()) T(400, 16, cx - gfx_->measure(F(400, 16), msg) / 2, sy, 24, msg, col, w);
    if (button.empty()) return false;
    sy += 44 * s;
    Rect b{x, sy, w, 48 * s};
    gfx_->fill_round(b, 24 * s, u.hover(b) ? th.panel2 : th.bg_light);
    gfx_->stroke_round(b, 24 * s, 1 * s, {255, 255, 255, 61});
    const float bw = gfx_->measure(F(600, 15), button);
    fa(button_icon, b.x + (b.w - bw - 22 * s) / 2 + 7 * s, b.y + 24 * s, 14, th.text);
    T(600, 15, b.x + (b.w - bw - 22 * s) / 2 + 22 * s, b.y, 48, button, th.text);
    return u.clicked(b);
}
