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

void App::load_albums_page(int page) {
    if (alb_loading_ || !client_ || offline_) return;
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
    if (song_loading_ || !client_ || offline_) return;
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
            if (ok) { liked_ = std::move(r); liked_loaded_ = true; }
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

void App::fetch_all_songs(bool shuffle_play) {
    if (busy_all_ || !client_) return;
    busy_all_ = true;
    worker_->post([this, c = client_, s = session_, shuffle_play] {
        std::vector<Song> all;
        for (int off = 0;; off += 500) {
            std::vector<Song> r; std::string e;
            if (!c->search_songs("", 500, off, r, e) || r.empty()) break;
            all.insert(all.end(), r.begin(), r.end());
            if (r.size() < 500) break;
        }
        ui_post([this, s, shuffle_play, all = std::move(all)] {
            if (s != session_) return;
            busy_all_ = false;
            if (all.empty()) return;
            if (shuffle_play) play_songs(all, static_cast<size_t>(SDL_GetTicks()) % all.size(), true);
            else if (downloads_) { downloads_->enqueue(all); prefetch_covers(all); }
            mark();
        });
    });
}

// ---------------------------------------------------------------- drawing

void App::draw_library(Rect r) {
    Ui& u = *ui_;
    const float s = S_;
    const Theme& th = u.th;
    const float pad = skin::kContentPad * s;
    float x = r.x + pad, w = r.w - 2 * pad - 12 * s;
    if (lib_mode_ == 1 && alb_loaded_page_ != lib_page_[1] && !alb_loading_) load_albums_page(lib_page_[1]);
    if (lib_mode_ == 2 && song_loaded_page_ != lib_page_[2] && !song_loading_) load_songs_page(lib_page_[2]);
    if (lib_mode_ == 3 && !liked_loaded_) load_liked();
    load_song_count();

    // content metrics first so the wheel can clamp
    const std::vector<Artist>& arts = artists_;
    size_t n_items = lib_mode_ == 0 ? std::min<size_t>(kPageSize, arts.size() > static_cast<size_t>(lib_page_[0] * kPageSize) ? arts.size() - lib_page_[0] * kPageSize : 0)
                   : lib_mode_ == 1 ? alb_items_.size() : lib_mode_ == 2 ? song_items_.size() : liked_.size();
    int cols_a = std::max(1, static_cast<int>((w / s + 32) / (201 + 32)));
    int cols_b = std::max(1, static_cast<int>((w / s + 24) / (207 + 24)));
    float header_h = (lib_mode_ == 0 ? 99 : 42) * s;
    float body_h = lib_mode_ == 0 ? std::ceil(static_cast<float>(n_items) / cols_a) * (261 + 31) * s
                 : lib_mode_ == 1 ? std::ceil(static_cast<float>(n_items) / cols_b) * (266 + 24) * s
                 : n_items * 72 * s;
    float total = pad + header_h + 32 * s + body_h + 100 * s;
    if (u.hover(r)) lib_scroll_.y -= u.in.wheel * 60 * s;
    lib_scroll_.clamp(total + pad, r.h);
    float y0 = r.y + pad - lib_scroll_.y;

    // view toggle (402x41 pill)
    Color edge = alpha(th.primary_light, 0.464f);
    Rect tg{x, y0, 402 * s, 41 * s};
    gfx_->stroke_round(tg, 20.5f * s, 1 * s, edge, th.bg);
    static const char* names[4] = {"Artists", "Albums", "Songs", "Liked"};
    static const uint32_t icons[4] = {0xf0c0, 0xf8d9, skin::Music, skin::Heart};
    static const float bx[4] = {5, 103, 209, 308}, bw[4] = {94, 102, 95, 89};
    for (int i = 0; i < 4; ++i) {
        Rect b{x + bx[i] * s, y0 + 5 * s, bw[i] * s, 31 * s};
        bool on = lib_mode_ == i;
        if (on) gfx_->fill_round(b, 15.5f * s, th.accent);
        else if (u.hover(b)) gfx_->fill_round(b, 15.5f * s, th.hover);
        Color c = on ? rgb(0xffffff) : th.sub;
        fa(icons[i], b.x + 24 * s, b.y + 15.5f * s, 13, c);
        T(500, 13, b.x + 40 * s, b.y, 31, names[i], c);
        if (u.clicked(b) && lib_mode_ != i) { lib_mode_ = i; lib_scroll_.y = 0; mark(); }
    }
    // right side / second row: stats pills, Shuffle All, Download All
    auto pill = [&](float px, float py, float pw, uint32_t ic, const std::string& label) {
        Rect b{px, py, pw, 42 * s};
        gfx_->stroke_round(b, 21 * s, 1 * s, {255, 255, 255, 54}, th.bg);
        fa(ic, b.x + 24 * s, b.y + 21 * s, 13, th.accent);
        T(400, 13, b.x + 41 * s, b.y, 42, label, th.sub);
    };
    auto action = [&](float px, float py, float pw, uint32_t ic, const std::string& label) {
        Rect b{px, py, pw, 38 * s};
        float fa_ = u.hover(b) ? 0.25f : 0.15f;  // accent tint over the page background
        auto mixc = [&](uint8_t f, uint8_t bg_) { return static_cast<uint8_t>(std::lround(f * fa_ + bg_ * (1 - fa_))); };
        Color fill{mixc(th.accent.r, th.bg.r), mixc(th.accent.g, th.bg.g), mixc(th.accent.b, th.bg.b), 255};
        gfx_->stroke_round(b, 20 * s, 1 * s, alpha(th.accent, 0.3f), fill);
        fa(ic, b.x + 28 * s, b.y + 19 * s, 14, th.accent);
        T(500, 14, b.x + 47 * s, b.y, 38, label, th.accent);
        return u.clicked(b);
    };
    int n_art = static_cast<int>(arts.size()), n_alb = 0;
    for (auto& a : arts) n_alb += a.album_count;
    float ry = lib_mode_ == 0 ? y0 + 57 * s : y0 - 1 * s;
    float rx = lib_mode_ == 0 ? x : x + w - (lib_mode_ == 3 ? 104 : 399) * s;
    if (lib_mode_ == 0) {
        pill(rx, ry, 113 * s, 0xf0c0, std::to_string(n_art) + " artists");
        pill(rx + 129 * s, ry, 126 * s, 0xf8d9, std::to_string(n_alb) + " albums");
        pill(rx + 271 * s, ry, 126 * s, skin::Music, std::to_string(std::max(0, total_songs_)) + " songs");
        if (action(rx + 413 * s, ry + 2 * s, 130 * s, skin::Shuffle, "Shuffle All")) fetch_all_songs(true);
        if (action(rx + 559 * s, ry + 2 * s, 148 * s, skin::Download, "Download All")) fetch_all_songs(false);
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
        if (action(ax, ry + 2 * s, 130 * s, skin::Shuffle, "Shuffle All")) fetch_all_songs(true);
        if (action(ax + 146 * s, ry + 2 * s, 148 * s, skin::Download, "Download All")) fetch_all_songs(false);
    }

    float cy = y0 + header_h + 32 * s;
    Rect clipr{r.x, r.y, r.w, r.h};
    gfx_->clip(&clipr);
    const Song* playing = queue_.current();
    if (lib_mode_ == 0) {
        float cw = 201 * s, chh = 261 * s;
        size_t first = static_cast<size_t>(lib_page_[0]) * kPageSize;
        for (size_t i = 0; i < n_items; ++i) {
            const Artist& a = arts[first + i];
            int col = static_cast<int>(i) % cols_a, row = static_cast<int>(i) / cols_a;
            float px = x + col * (cw + 32 * s), py = cy + row * (chh + 31 * s);
            if (py + chh < r.y || py > r.y + r.h) continue;
            Rect card{px, py, cw, chh};
            gfx_->fill_round(card, 16 * s, th.panel);
            if (u.hover(card) && r.contains(u.in.mx, u.in.my)) gfx_->fill_round(card, 16 * s, th.hover);
            Rect cov{px + 16 * s, py + 16 * s, 169 * s, 169 * s};
            std::string cid = a.cover_art.empty() ? a.id : a.cover_art;
            if (SDL_Texture* t = cover(cid, 169)) gfx_->image(t, cov);
            else gfx_->fill_round(cov, 12 * s, th.panel2);
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
            int col = static_cast<int>(i) % cols_b, row = static_cast<int>(i) / cols_b;
            float px = x + col * (cw + 24 * s), py = cy + row * (chh + 24 * s);
            if (py + chh < r.y || py > r.y + r.h) continue;
            Rect card{px, py, cw, chh};
            gfx_->fill_round(card, 16 * s, th.panel);
            if (u.hover(card) && r.contains(u.in.mx, u.in.my)) gfx_->fill_round(card, 16 * s, th.hover);
            Rect cov{px + 16 * s, py + 16 * s, 175 * s, 175 * s};
            if (SDL_Texture* t = cover(a.cover_art, 175)) gfx_->image(t, cov);
            else gfx_->fill_round(cov, 4 * s, th.panel2);
            float nw = std::min(gfx_->measure(F(600, 15.2f), a.name), cov.w);
            T(600, 15.2f, cov.x + (cov.w - nw) / 2, py + 203 * s, 23, a.name, th.text, cov.w);
            float aw = std::min(gfx_->measure(F(600, 13), a.artist), cov.w);
            T(600, 13, cov.x + (cov.w - aw) / 2, py + 229 * s, 20, a.artist, th.sub, cov.w);
            if (u.clicked(card) && r.contains(u.in.mx, u.in.my)) { push_view(ViewKind::Album, a.id, a.name); load_album(a.id); }
        }
        if (alb_items_.empty()) T(400, 16, x, cy, 24, alb_loading_ ? "Loading..." : "No albums found", th.sub);
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
            if (SDL_Texture* t = cover(sg.cover_art, 48)) gfx_->image(t, art);
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
            if (u.clicked(row) && r.contains(u.in.mx, u.in.my)) play_songs(list, i, false);
        }
        if (list.empty()) {
            bool busy = lib_mode_ == 2 ? song_loading_ : liked_loading_;
            float ey = cy + 40 * s;
            if (lib_mode_ == 3 && !busy) {
                fa(skin::Heart, x + w / 2, ey + 24 * s, 48, th.sub);
                int f = F(700, 18.72f);
                std::string t = "No Liked Songs";
                T(700, 18.72f, x + (w - gfx_->measure(f, t)) / 2, ey + 76 * s, 28, t, th.sub);
                std::string t2 = "Star songs on your server to see them here.";
                T(400, 16, x + (w - gfx_->measure(F(400, 16), t2)) / 2, ey + 112 * s, 24, t2, th.sub);
            } else T(400, 16, x, cy, 24, busy ? "Loading..." : "No songs found", th.sub);
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
