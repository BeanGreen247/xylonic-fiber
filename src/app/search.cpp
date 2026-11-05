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
    if (!client_ || offline_) {
        search_res_ = SubsonicClient::SearchAll{};
        if (store_) search_res_.songs = store_->local_search(q);
        search_done_q_ = q;
        mark();
        return;
    }
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
    const float x = mobile_ ? r.x + 28 * s : r.x + 48 * s, w = mobile_ ? r.w - 56 * s : r.w - 96 * s - 12 * s;  // 24 padding + 24 page inset (compact: 16), minus scrollbar gutter
    const auto& res = search_res_;
    const Artist* best_artist = res.artists.empty() ? nullptr : &res.artists[0];
    const Album* best_album = !best_artist && !res.albums.empty() ? &res.albums[0] : nullptr;
    const Song* best_song = !best_artist && !best_album && !res.songs.empty() ? &res.songs[0] : nullptr;
    size_t top_n = std::min<size_t>(5, res.songs.size());
    bool has_top = best_artist || best_album || best_song || top_n > 0;
    float top_h = has_top ? 33 + 16 + 360 : 0;  // logical px
    if (mobile_ && has_top) top_h = 33 + 16 + 92 + 32 + 33 + 16 + top_n * 72 - 40 + 32;   // stacked: best result row, then the songs
    const float card_h = mobile_ ? 190.0f : 216.0f;
    float more_h = res.songs.size() > 5 ? 40 + 33 + 16 + (res.songs.size() - 5) * 72 : 0;
    float art_h = res.artists.empty() ? 0 : 40 + 33 + 16 + card_h, alb_h = res.albums.empty() ? 0 : 40 + 33 + 16 + card_h;
    float total = (67 + top_h + art_h + alb_h + more_h + 60) * s;
    if (u.hover(r)) search_scroll_.y -= u.in.wheel * 60 * s;
    search_scroll_.clamp(total, r.h);
    float y = r.y + (mobile_ ? 12.0f : 24.0f) * s - search_scroll_.y;
    if (mobile_) {   // .sr-mobile-bar: in-page search input (the header has none on phones), 12 above and 16 below
        Rect wrap{x, y + 12 * s, w, 44 * s};
        const bool focus = u.focus == kSearch;
        gfx_->fill_round(wrap, 22 * s, {255, 255, 255, static_cast<uint8_t>(focus ? 38 : 31)});
        gfx_->stroke_round(wrap, 22 * s, 1.5f * s, focus ? th.accent : Color{255, 255, 255, 61});
        fa(skin::Search, wrap.x + 16 * s + 7 * s, wrap.y + wrap.h / 2, 14, {255, 255, 255, 153});
        Rect fld{wrap.x + 40 * s, wrap.y + 2 * s, wrap.w - 40 * s - 44 * s, wrap.h - 4 * s};
        std::string before = search_q_;
        const bool changed = u.text_field(kSearch, fld, search_q_, "Artists, albums, songs\xE2\x80\xA6", false, false, 4 * s, F(400, 15));
        if (changed || search_q_ != before) { search_pending_ = true; search_due_ = SDL_GetTicks() + 300; if (search_q_.empty()) run_search(); }
        if (search_loading_) fa(skin::Spinner, wrap.x + wrap.w - 16 * s - 7 * s, wrap.y + wrap.h / 2, 14, th.accent);
        else if (!search_q_.empty()) {
            Rect xb{wrap.x + wrap.w - 10 * s - 26 * s, wrap.y + 9 * s, 26 * s, 26 * s};
            gfx_->circle(xb.x + 13 * s, xb.y + 13 * s, 13 * s, {255, 255, 255, 61});
            fa(skin::Xmark, xb.x + 13 * s, xb.y + 13 * s, 10, th.sub);
            if (u.clicked(xb)) { search_q_.clear(); run_search(); }
        }
        y += 72 * s;
        if (search_q_.empty() && res.artists.empty() && res.albums.empty() && res.songs.empty()) {   // .sr-empty: icon 36, text 16/600
            const float cx = r.x + r.w / 2;
            fa(skin::Search, cx, y + 48 * s + 18 * s, 36, {255, 255, 255, 64});
            const std::string msg = "What do you want to listen to?";
            T(600, 16, cx - gfx_->measure(F(600, 16), msg) / 2, y + 48 * s + 36 * s + 20 * s, 24, msg, th.text);
            return;
        }
    }
    // desktop header: back + query label (hidden on phones: .sr-desktop-header)
    if (!mobile_) {
    Rect back{x, y + 20 * s, 111 * s, 23 * s};
    fa(skin::ArrowLeft, x + 8 * s, back.y + 11.5f * s, 13, u.hover(back) ? th.text : th.sub);
    T(400, 14, x + 24 * s, back.y, 23, "Back to Library", u.hover(back) ? th.text : th.sub);
    if (!mobile_ && u.clicked(back)) { search_q_.clear(); run_search(); set_nav(search_prev_nav_ == Nav::Search ? Nav::Library : search_prev_nav_); return; }
    std::string qlabel = "Results for \"";
    float lx = x + 131 * s;
    float w1 = T(400, 14, lx, back.y, 23, qlabel, th.sub);
    float w2 = T(600, 14, lx + w1, back.y, 23, search_done_q_.empty() ? search_q_ : search_done_q_, th.text, w - 300 * s);
    T(400, 14, lx + w1 + w2, back.y, 23, "\"", th.sub);
    if (search_loading_) fa(skin::Spinner, x + w - 12 * s, back.y + 11 * s, 14, th.accent);
    y += 67 * s;
    }
    gfx_->clip(&r);
    if (res.artists.empty() && res.albums.empty() && res.songs.empty()) {
        if (search_loading_ || search_done_q_.empty()) {
            T(400, 16, x, y + 24 * s, 24, search_loading_ ? "Searching..." : "Type to search your library", th.sub);
            return;
        }
        // .sr-empty: centred column, gap 12, icon 48 (36 compact) at 25 %, 18 px/600 message with the query in bold, muted hint
        const float cx = r.x + r.w / 2, top = y + (mobile_ ? 48.0f : 80.0f) * s;
        const float isz = mobile_ ? 36.0f : 48.0f, mfs = mobile_ ? 16.0f : 18.0f;
        fa(skin::Music, cx, top + isz / 2 * s, isz, {th.sub.r, th.sub.g, th.sub.b, 64});
        const std::string a = "No results found for ", q = "\"" + search_done_q_ + "\"";
        const float aw = gfx_->measure(F(600, mfs), a), qw = gfx_->measure(F(700, mfs), q), my = top + (isz + 8 + 12) * s;
        T(600, mfs, cx - (aw + qw) / 2, my, 28, a, th.text);
        T(700, mfs, cx - (aw + qw) / 2 + aw, my, 28, q, th.text, r.w - 48 * s);
        const std::string h = "Try different keywords or check the spelling.";
        T(400, 14, cx - gfx_->measure(F(400, 14), h) / 2, my + 40 * s, 21, h, th.sub);
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
    if (has_top && mobile_) {   // @media (max-width:680px): .sr-top stacks (gap 32); best card is a 64 px art row (padding 14), play button always shown
        const float tfs = 19;
        T(700, tfs, x, y, 33, "Best Result", th.text);
        Rect card{x, y + 49 * s, w, 92 * s};
        gfx_->fill_round(card, 16 * s, {255, 255, 255, 31});
        std::string cid, name, type;
        bool circle = false;
        if (best_artist) { cid = best_artist->cover_art.empty() ? best_artist->id : best_artist->cover_art; name = best_artist->name; type = "Artist"; circle = true; }
        else if (best_album) { cid = best_album->cover_art; name = best_album->name; type = "Album \xC2\xB7 " + best_album->artist; }
        else { cid = best_song->cover_art; name = best_song->title; type = "Song \xC2\xB7 " + best_song->artist; }
        Rect art{card.x + 14 * s, card.y + 14 * s, 64 * s, 64 * s};
        if (SDL_Texture* t = cover(cid, 64)) gfx_->image_round(t, art, circle ? 32 * s : 8 * s);
        else draw_art_fallback(art, cid, circle ? 32 * s : 8 * s);
        T(700, 16, art.x + 76 * s, card.y + 20 * s, 24, name, th.text, w - 14 * s - 76 * s - 64 * s - 14 * s);
        T(600, 12, art.x + 76 * s, card.y + 48 * s, 18, type, th.sub, w - 14 * s - 76 * s - 64 * s - 14 * s);
        Rect pb{card.x + card.w - 10 * s - 40 * s, card.y + card.h - 10 * s - 40 * s, 40 * s, 40 * s};
        gfx_->circle(pb.x + 20 * s, pb.y + 20 * s, 20 * s, th.accent);
        fa(skin::Play, pb.x + 21 * s, pb.y + 20 * s, 14, rgb(0x000000));
        if (u.clicked(card) && r.contains(u.in.mx, u.in.my)) {
            if (best_artist) { push_view(ViewKind::Artist, best_artist->id, best_artist->name); load_artist(best_artist->id); }
            else if (best_album) { push_view(ViewKind::Album, best_album->id, best_album->name); load_album(best_album->id); }
            else play_songs(res.songs, 0, false);
        }
        y += (49 + 92 + 32) * s;
        if (top_n > 0) {
            T(700, tfs, x, y, 33, "Songs", th.text);
            if (res.songs.size() > 1) {
                Rect pa{x + 76 * s, y + 9 * s, 73 * s, 22 * s};
                fa(skin::Play, pa.x + 14 * s, pa.y + 11 * s, 12, rgb(0xb5b5b5));
                T(500, 12, pa.x + 26 * s, pa.y, 22, "Play all", rgb(0xb5b5b5));
                if (u.clicked(pa)) play_songs(res.songs, 0, false);
            }
            for (size_t i = 0; i < top_n; ++i) song_row({x, y + 49 * s + i * 72 * s, w, 72 * s}, res.songs[i], i, res.songs);
            y += (49 + top_n * 72 + 32) * s;
        }
    } else if (has_top) {
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
        else draw_art_fallback(art, cid, circle ? 50 * s : 8 * s);
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
        T(700, mobile_ ? 19.0f : 22.0f, x, y, 33, title, th.text);
        float ry = y + 49 * s;
        size_t n = artists ? res.artists.size() : res.albums.size();
        for (size_t i = 0; i < n; ++i) {
            const float cw = (mobile_ ? (artists ? 130.0f : 140.0f) : 148.0f), asz = mobile_ ? (artists ? 110.0f : 124.0f) : 124.0f, inset = (cw - asz) / 2;
            float cx = x + i * (cw + (mobile_ ? 12 : 16)) * s;
            if (cx > x + w) break;
            Rect card{cx, ry, cw * s, (mobile_ ? card_h - 8 : 208.0f) * s};
            if (u.hover(card) && r.contains(u.in.mx, u.in.my)) gfx_->fill_round(card, 12 * s, th.hover);
            Rect art{cx + inset * s, ry + (mobile_ ? 8.0f : 12.0f) * s, asz * s, asz * s};
            std::string cid, nm, sub, id;
            if (artists) { const Artist& a = res.artists[i]; cid = a.cover_art.empty() ? a.id : a.cover_art; nm = a.name; id = a.id; sub = a.album_count ? std::to_string(a.album_count) + (a.album_count != 1 ? " albums" : " album") : "Artist"; }
            else { const Album& a = res.albums[i]; cid = a.cover_art; nm = a.name; id = a.id; sub = a.artist; }
            if (SDL_Texture* t = cover(cid, static_cast<int>(asz))) gfx_->image_round(t, art, artists ? asz / 2 * s : 8 * s);
            else draw_art_fallback(art, cid, artists ? asz / 2 * s : 8 * s);
            T(600, 13, art.x, ry + (asz + 20) * s, 20, nm, th.text, art.w);
            T(400, 11, art.x, ry + (asz + 50) * s, 17, sub, th.sub, art.w);
            if (u.clicked(card) && r.contains(u.in.mx, u.in.my)) {
                if (artists) { push_view(ViewKind::Artist, id, nm); load_artist(id); }
                else { push_view(ViewKind::Album, id, nm); load_album(id); }
            }
        }
        y += (33 + 16 + card_h + 40) * s;
    };
    if (!res.artists.empty()) card_row("Artists", true);
    if (!res.albums.empty()) card_row("Albums", false);
    if (res.songs.size() > 5) {
        T(700, 22, x, y, 33, "More Songs", th.text);
        for (size_t i = 5; i < res.songs.size(); ++i) song_row({x, y + 49 * s + (i - 5) * 72 * s, w, 72 * s}, res.songs[i], i, res.songs);
    }
}
