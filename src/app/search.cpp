// Search results page (SearchResults.tsx): Best Result, Songs (top 5), Artists, Albums, More Songs. Server search3
// with the old limits (20/20/50), 300 ms debounce, best result = first artist > first album > first song.
#include <algorithm>
#include <cmath>
#include "app.h"

namespace {
Color alpha(Color c, float a) { c.a = static_cast<uint8_t>(std::lround(a * 255)); return c; }
std::string mmss(int s) {
    if (s < 0) s = 0;
    char b[16];
    std::snprintf(b, sizeof b, "%d:%02d", s / 60, s % 60);
    return b;
}
}  // namespace

void App::run_search() {
    std::string q = search_q_;
    search_pending_ = false;
    size_t b = q.find_first_not_of(" \t"), e = q.find_last_not_of(" \t");
    q = b == std::string::npos ? std::string() : q.substr(b, e - b + 1);
    if (q.empty()) { search_res_.artists.clear(); search_res_.albums.clear(); search_res_.songs.clear(); search_done_q_.clear(); mark(); return; }
    if (!client_ || offline_) return;
    search_loading_ = true;
    worker_->post([this, c = client_, s = session_, q] {
        SubsonicClient::SearchAll r; std::string e;
        bool ok = c->search_all(q, r, e);
        ui_post([this, s, q, r = std::move(r), ok]() mutable {
            if (s != session_) return;
            search_loading_ = false;
            if (ok) { search_res_ = std::move(r); search_done_q_ = q; }
            mark();
        });
    });
}

void App::draw_search(Rect r) {
    Ui& u = *ui_;
    const float s = S_;
    const Theme& th = u.th;
    const float x = r.x + 48 * s, w = r.w - 96 * s - 12 * s;  // 24 padding + 24 page inset, minus scrollbar gutter
    const auto& res = search_res_;
    const Artist* best_artist = res.artists.empty() ? nullptr : &res.artists[0];
    const Album* best_album = !best_artist && !res.albums.empty() ? &res.albums[0] : nullptr;
    const Song* best_song = !best_artist && !best_album && !res.songs.empty() ? &res.songs[0] : nullptr;
    size_t top_n = std::min<size_t>(5, res.songs.size());
    bool has_top = best_artist || best_album || best_song || top_n > 0;
    float top_h = has_top ? 33 + 16 + 360 : 0;  // logical px
    float more_h = res.songs.size() > 5 ? 40 + 33 + 16 + (res.songs.size() - 5) * 72 : 0;
    float art_h = res.artists.empty() ? 0 : 40 + 33 + 16 + 216, alb_h = res.albums.empty() ? 0 : 40 + 33 + 16 + 216;
    float total = (67 + top_h + art_h + alb_h + more_h + 60) * s;
    if (u.hover(r)) search_scroll_.y -= u.in.wheel * 60 * s;
    search_scroll_.clamp(total, r.h);
    float y = r.y + 24 * s - search_scroll_.y;
    // desktop header: back + query label
    Rect back{x, y + 20 * s, 111 * s, 23 * s};
    fa(skin::ArrowLeft, x + 8 * s, back.y + 11.5f * s, 13, u.hover(back) ? th.text : th.sub);
    T(400, 14, x + 24 * s, back.y, 23, "Back to Library", u.hover(back) ? th.text : th.sub);
    if (u.clicked(back)) { search_q_.clear(); run_search(); set_nav(search_prev_nav_ == Nav::Search ? Nav::Library : search_prev_nav_); return; }
    std::string qlabel = "Results for \"";
    float lx = x + 131 * s;
    float w1 = T(400, 14, lx, back.y, 23, qlabel, th.sub);
    float w2 = T(600, 14, lx + w1, back.y, 23, search_done_q_.empty() ? search_q_ : search_done_q_, th.text, w - 300 * s);
    T(400, 14, lx + w1 + w2, back.y, 23, "\"", th.sub);
    if (search_loading_) fa(skin::Spinner, x + w - 12 * s, back.y + 11 * s, 14, th.accent);
    y += 67 * s;
    gfx_->clip(&r);
    if (res.artists.empty() && res.albums.empty() && res.songs.empty()) {
        std::string msg = search_loading_ ? "Searching..." : search_done_q_.empty() ? "Type to search your library" : "No results for \"" + search_done_q_ + "\"";
        T(400, 16, x, y + 24 * s, 24, msg, th.sub);
        return;
    }
    const Song* playing = queue_.current();
    auto song_row = [&](Rect row, const Song& sg, size_t idx, const std::vector<Song>& list) {
        if (row.y + row.h < r.y || row.y > r.y + r.h) return;
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
        T(600, 15.2f, row.x + 80 * s, row.y + 12 * s, 23, sg.title, isplay ? th.accent : th.text, row.w - 160 * s);
        T(400, 13.6f, row.x + 80 * s, row.y + 39 * s, 20, sg.artist, th.sub, row.w - 160 * s);
        std::string d = mmss(sg.duration);
        float dw = gfx_->measure(F(400, 13), d);
        T(400, 13, row.x + row.w - 16 * s - dw, row.y + 26 * s, 20, d, th.sub);
        ctx_hit(row, sg);
        if (u.clicked(row) && r.contains(u.in.mx, u.in.my)) play_songs(list, idx, false);
    };
    if (has_top) {
        float bx = x, bw = 340 * s;
        T(700, 22, bx, y, 33, "Best Result", th.text);
        Rect card{bx, y + 49 * s, bw, 409 * s};
        gfx_->fill_round({card.x - 6 * s, card.y + 6 * s, card.w + 12 * s, card.h + 4 * s}, 20 * s, {0, 0, 0, 40});  // soft shadow
        gfx_->fill_round(card, 16 * s, {255, 255, 255, 31});
        bool hov = u.hover(card) && r.contains(u.in.mx, u.in.my);
        std::string cid, name, type;
        bool circle = false;
        if (best_artist) { cid = best_artist->cover_art.empty() ? best_artist->id : best_artist->cover_art; name = best_artist->name; type = "Artist"; circle = true; }
        else if (best_album) { cid = best_album->cover_art; name = best_album->name; type = "Album \xC2\xB7 " + best_album->artist; }
        else { cid = best_song->cover_art; name = best_song->title; type = "Song \xC2\xB7 " + best_song->artist; }
        Rect art{card.x + 20 * s, card.y + 20 * s, 100 * s, 100 * s};
        if (!circle) gfx_->fill_round({art.x, art.y + 8 * s, art.w, art.h}, 10 * s, {0, 0, 0, 60});
        if (SDL_Texture* t = cover(cid, 100)) gfx_->image_round(t, art, circle ? 50 * s : 8 * s);
        else gfx_->fill_round(art, circle ? 50 * s : 8 * s, th.panel2);
        T(700, 28, card.x + 20 * s, card.y + 136 * s, 31, name, th.text, 300 * s);
        T(600, 13, card.x + 20 * s, card.y + 173 * s, 20, type, th.sub, 300 * s);
        if (hov) {
            Rect pb{card.x + 272 * s, card.y + 349 * s, 48 * s, 48 * s};
            gfx_->circle(pb.x + 24 * s, pb.y + 24 * s, 24 * s, th.accent);
            fa(skin::Play, pb.x + 25 * s, pb.y + 24 * s, 16, rgb(0x000000));
        }
        if (u.clicked(card) && r.contains(u.in.mx, u.in.my)) {
            if (best_artist) { push_view(ViewKind::Artist, best_artist->id, best_artist->name); load_artist(best_artist->id); }
            else if (best_album) { push_view(ViewKind::Album, best_album->id, best_album->name); load_album(best_album->id); }
            else play_songs(res.songs, 0, false);
        }
        // top songs
        float sx = x + 364 * s, sw = 488 * s;
        T(700, 22, sx, y, 33, "Songs", th.text);
        if (res.songs.size() > 1) {
            Rect pa{sx + 76 * s, y + 9 * s, 73 * s, 22 * s};
            fa(skin::Play, pa.x + 14 * s, pa.y + 11 * s, 12, u.hover(pa) ? th.text : rgb(0xb5b5b5));
            T(500, 12, pa.x + 26 * s, pa.y, 22, "Play all", u.hover(pa) ? th.text : rgb(0xb5b5b5));
            if (u.clicked(pa)) play_songs(res.songs, 0, false);
        }
        for (size_t i = 0; i < top_n; ++i) song_row({sx, y + 49 * s + i * 72 * s, sw, 72 * s}, res.songs[i], i, res.songs);
        y += (top_h + 40) * s;
    }
    auto card_row = [&](const char* title, bool artists) {
        T(700, 22, x, y, 33, title, th.text);
        float ry = y + 49 * s;
        size_t n = artists ? res.artists.size() : res.albums.size();
        for (size_t i = 0; i < n; ++i) {
            float cx = x + i * (148 + 16) * s;
            if (cx > x + w) break;
            Rect card{cx, ry, 148 * s, 208 * s};
            if (u.hover(card) && r.contains(u.in.mx, u.in.my)) gfx_->fill_round(card, 12 * s, th.hover);
            Rect art{cx + 12 * s, ry + 12 * s, 124 * s, 124 * s};
            std::string cid, nm, sub, id;
            if (artists) { const Artist& a = res.artists[i]; cid = a.cover_art.empty() ? a.id : a.cover_art; nm = a.name; id = a.id; sub = a.album_count ? std::to_string(a.album_count) + (a.album_count != 1 ? " albums" : " album") : "Artist"; }
            else { const Album& a = res.albums[i]; cid = a.cover_art; nm = a.name; id = a.id; sub = a.artist; }
            if (SDL_Texture* t = cover(cid, 124)) gfx_->image_round(t, art, artists ? 62 * s : 8 * s);
            else gfx_->fill_round(art, artists ? 62 * s : 8 * s, th.panel2);
            T(600, 13, art.x, ry + 146 * s, 20, nm, th.text, art.w);
            T(400, 11, art.x, ry + 176 * s, 17, sub, th.sub, art.w);
            if (u.clicked(card) && r.contains(u.in.mx, u.in.my)) {
                if (artists) { push_view(ViewKind::Artist, id, nm); load_artist(id); }
                else { push_view(ViewKind::Album, id, nm); load_album(id); }
            }
        }
        y += (33 + 16 + 216 + 40) * s;
    };
    if (!res.artists.empty()) card_row("Artists", true);
    if (!res.albums.empty()) card_row("Albums", false);
    if (res.songs.size() > 5) {
        T(700, 22, x, y, 33, "More Songs", th.text);
        for (size_t i = 5; i < res.songs.size(); ++i) song_row({x, y + 49 * s + (i - 5) * 72 * s, w, 72 * s}, res.songs[i], i, res.songs);
    }
}
