// Artist page (AlbumList) and album page (SongList hero). Layout measured from the old app at 1024x768.
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

void App::draw_artist(Rect r) {
    if (mobile_) { draw_artist_compact(r); return; }
    Ui& u = *ui_;
    const float s = S_;
    const Theme& th = u.th;
    View& v = views_.back();
    const float pad = skin::kContentPad * s;
    float x = r.x + pad, w = r.w - 2 * pad - 12 * s;
    auto it = artist_cache_.find(v.id);
    bool ready = it != artist_cache_.end() && !it->second.id.empty();
    const Artist* a = ready ? &it->second : nullptr;
    int cols = std::max(1, static_cast<int>((w / s + 24) / (207 + 24)));
    size_t n = a ? a->albums.size() : 0;
    float total = 80 * s + std::ceil(static_cast<float>(n) / cols) * (266 + 24) * s + pad;
    if (u.hover(r)) v.scroll.y -= u.in.wheel * 60 * s;
    v.scroll.clamp(total + pad, r.h);
    float y0 = r.y + pad - v.scroll.y;
    // header row: back, title (icon + name), stats, Download Artist
    Rect back{x, y0 + 13 * s, 110 * s, 23 * s};
    if (u.hover(back)) T(400, 14, x + 24 * s, back.y, 23, "Back to Artists", th.text);
    else T(400, 14, x + 24 * s, back.y, 23, "Back to Artists", th.sub);
    fa(skin::ArrowLeft, x + 8 * s, back.y + 11.5f * s, 13, u.hover(back) ? th.text : th.sub);
    if (u.clicked(back)) { pop_view(); return; }
    // right group (from the right edge): Download Artist 165, stats 111, stats 112, gaps 16
    float rx = x + w;
    auto pill = [&](float px, float pw, uint32_t ic, const std::string& label) {
        Rect b{px, y0 + 3 * s, pw, 42 * s};
        gfx_->stroke_round(b, 21 * s, 1 * s, {255, 255, 255, 54}, th.bg);
        fa(ic, b.x + 24 * s, b.y + 21 * s, 13, th.accent);
        T(400, 13, b.x + 41 * s, b.y, 42, label, th.sub);
    };
    Rect dl{rx - 165 * s, y0 + 5 * s, 165 * s, 38 * s};
    {
        float fa_ = u.hover(dl) ? 0.25f : 0.15f;
        auto mixc = [&](uint8_t f, uint8_t b) { return static_cast<uint8_t>(std::lround(f * fa_ + b * (1 - fa_))); };
        Color fill{mixc(th.accent.r, th.bg.r), mixc(th.accent.g, th.bg.g), mixc(th.accent.b, th.bg.b), 255};
        gfx_->fill_round(dl, 20 * s, fill);
        gfx_->stroke_round(dl, 20 * s, 1 * s, alpha(th.accent, 0.3f), fill);
        fa(skin::Download, dl.x + 28 * s, dl.y + 19 * s, 14, th.accent);
        T(500, 14, dl.x + 47 * s, dl.y, 38, "Download Artist", th.accent);
        if (a && downloads_ && !offline_ && u.clicked(dl)) { dq_album_ids_.clear(); for (const Album& al : a->albums) dq_album_ids_.push_back(al.id); open_download_picker_songs({}, "Download Artist"); }
    }
    int songs = 0;
    if (a) for (auto& al : a->albums) songs += al.song_count;
    pill(dl.x - 16 * s - 111 * s, 111 * s, skin::Music, std::to_string(songs) + " songs");
    pill(dl.x - 16 * s - 111 * s - 16 * s - 112 * s, 112 * s, 0xf51f, std::to_string(n) + " albums");
    // title centred in the free space between back and the pills
    {
        float tx0 = x + 216 * s;
        gfx_->circle(tx0 + 16 * s, y0 + 24 * s, 17 * s, th.accent);
        fa(0xf51f, tx0 + 16 * s, y0 + 24 * s, 20, th.accent_text);
        gfx_->set_letter_spacing(-0.32f * s);
        T(700, 32, tx0 + 52 * s, y0, 48, v.title, th.text, 260 * s);
        gfx_->set_letter_spacing(0);
    }
    if (!ready) { T(400, 16, x, y0 + 80 * s, 24, "Loading...", th.sub); return; }
    float cy = y0 + 80 * s;
    const float cw = 207 * s, chh = 266 * s;
    gfx_->clip(&r);
    for (size_t i = 0; i < n; ++i) {
        const Album& al = a->albums[i];
        int col = static_cast<int>(i) % cols, row = static_cast<int>(i) / cols;
        float px = x + col * (cw + 24 * s), py = cy + row * (chh + 24 * s);
        if (py + chh < r.y || py > r.y + r.h) continue;
        Rect card{px, py, cw, chh};
        gfx_->fill_round(card, 16 * s, th.panel);
        if (u.hover(card) && r.contains(u.in.mx, u.in.my)) gfx_->fill_round(card, 16 * s, th.hover);
        Rect cov{px + 16 * s, py + 16 * s, 175 * s, 175 * s};
        if (SDL_Texture* t = cover(al.cover_art, 175, al.artist, al.name)) gfx_->image_round(t, cov, 4 * s);
        else draw_art_fallback(cov, al.cover_art, 4 * s);
        float nw = std::min(gfx_->measure(F(600, 15.2f), al.name), cov.w);
        T(600, 15.2f, cov.x + (cov.w - nw) / 2, py + 203 * s, 23, al.name, th.text, cov.w);
        std::string sub = (al.year ? std::to_string(al.year) + " \xE2\x80\xA2 " : std::string()) + std::to_string(al.song_count) + " songs";
        T(400, 13.6f, cov.x, py + 230 * s, 20, sub, th.sub, cov.w);
        if (u.clicked(card) && r.contains(u.in.mx, u.in.my)) { push_view(ViewKind::Album, al.id, al.name); load_album(al.id); }
    }
}

void App::draw_album(Rect r) {
    if (mobile_) { draw_album_compact(r); return; }
    Ui& u = *ui_;
    const float s = S_;
    const Theme& th = u.th;
    View& v = views_.back();
    const float pad = skin::kContentPad * s;
    float x = r.x + pad, w = r.w - 2 * pad - 12 * s;
    auto it = album_cache_.find(v.id);
    bool ready = it != album_cache_.end() && !it->second.id.empty();
    const Album* a = ready ? &it->second : nullptr;
    size_t n = a ? a->songs.size() : 0;
    float total = 465 * s + n * 71 * s + pad;
    if (u.hover(r)) v.scroll.y -= u.in.wheel * 60 * s;
    v.scroll.clamp(total + pad, r.h);
    float y0 = r.y + pad - v.scroll.y;
    Rect back{x, y0 + 1 * s, 114 * s, 23 * s};
    T(400, 14, x + 24 * s, back.y, 23, "Back to Albums", u.hover(back) ? th.text : th.sub);
    fa(skin::ArrowLeft, x + 8 * s, back.y + 11.5f * s, 13, u.hover(back) ? th.text : th.sub);
    if (u.clicked(back)) { pop_view(); return; }
    if (!ready) { T(400, 16, x, y0 + 50 * s, 24, "Loading...", th.sub); return; }
    // hero card
    float hy = y0 + 44 * s;
    Rect hero{x, hy, w, 268 * s};
    gfx_->fill_round({hero.x, hero.y, hero.w, hero.h + 40 * s}, 24 * s, th.panel);  // rounded top; bottom hidden by the bar
    Rect art{x + 24 * s, hy + 24 * s, 220 * s, 220 * s};
    if (SDL_Texture* t = cover(a->cover_art, 220, a->name, "")) gfx_->image_round(t, art, 12 * s);
    else draw_art_fallback(art, a->cover_art, 12 * s);
    float ix = x + 272 * s;
    gfx_->set_letter_spacing(0.8f * s);
    T(700, 11, ix, hy + 154 * s, 17, "ALBUM", th.sub);
    gfx_->set_letter_spacing(0);
    T(700, 35.84f, ix, hy + 177 * s, 38, a->name, th.text, w - 296 * s);
    int hrs = a->duration / 3600, mins = (a->duration % 3600) / 60;
    std::string dur = hrs ? std::to_string(hrs) + " hr " + std::to_string(mins) + " min" : std::to_string(mins) + " min";
    float mx = ix;
    mx += T(600, 14, mx, hy + 225 * s, 20, a->artist, th.text);
    std::string rest;
    if (a->year) rest += "  \xE2\x80\xA2  " + std::to_string(a->year);
    rest += "  \xE2\x80\xA2  " + std::to_string(a->songs.size()) + " songs  \xE2\x80\xA2  " + dur;
    T(400, 14, mx, hy + 225 * s, 20, rest, th.sub);
    // action bar
    Rect bar{x, hy + 268 * s, w, 97 * s};
    gfx_->fill({bar.x, bar.y, bar.w, bar.h - 24 * s}, th.panel);
    gfx_->fill_round({bar.x, bar.y + bar.h - 48 * s, bar.w, 48 * s}, 24 * s, th.panel);
    gfx_->fill({bar.x, bar.y, bar.w, 1 * s}, {255, 255, 255, 20});
    Rect pc{x + 24 * s, bar.y + 21 * s, 56 * s, 56 * s};
    gfx_->circle(pc.x + 28 * s, pc.y + 28 * s, 28 * s, u.hover(pc) ? th.primary_light : th.accent);
    fa(skin::Play, pc.x + 30 * s, pc.y + 28 * s, 20, rgb(0xffffff));
    if (u.clicked(pc)) play_songs(a->songs, 0, false);
    auto ring_btn = [&](float bx, uint32_t ic) {
        Rect b{bx, bar.y + 28 * s, 48 * s, 42 * s};
        gfx_->stroke_round(b, 21 * s, 1 * s, {255, 255, 255, 54}, th.panel);
        if (u.hover(b)) gfx_->fill_round(b, 21 * s, th.hover);
        fa(ic, b.x + 24 * s, b.y + 21 * s, 24, th.sub);
        return u.clicked(b);
    };
    if (ring_btn(x + 96 * s, skin::Shuffle)) play_songs(a->songs, 0, true);
    if (downloads_ && !offline_ && ring_btn(x + 160 * s, skin::Download)) open_download_picker_songs(a->songs, "Download " + std::to_string(a->songs.size()) + " Song" + (a->songs.size() != 1 ? "s" : ""));
    // songs
    float sy = bar.y + 97 * s + 56 * s;
    const Song* playing = queue_.current();
    gfx_->clip(&r);
    for (size_t i = 0; i < n; ++i) {
        const Song& sg = a->songs[i];
        Rect row{x, sy + i * 71 * s, w, 71 * s};
        if (row.y + row.h < r.y || row.y > r.y + r.h) continue;
        bool isplay = playing && playing->id == sg.id;
        bool hov = u.hover(row) && r.contains(u.in.mx, u.in.my);
        if (isplay) gfx_->fill_round(row, 8 * s, alpha(th.accent, 0.12f));
        else if (hov) gfx_->fill_round(row, 8 * s, th.hover);
        T(600, 15.2f, row.x + 16 * s, row.y + 12 * s, 23, sg.title, isplay ? th.accent : th.text, row.w - 120 * s);
        T(400, 13.6f, row.x + 16 * s, row.y + 39 * s, 20, sg.artist, th.sub, row.w - 120 * s);
        std::string d = mmss(sg.duration);
        float dw = gfx_->measure(F(400, 13), d);
        T(400, 13, row.x + row.w - 16 * s - dw, row.y + 25 * s, 20, d, th.sub);
        auto ds = dl_state_.find(sg.id);
        if (ds != dl_state_.end() && ds->second.state == Downloads::Done) fa(skin::Download, row.x + row.w - 16 * s - dw - 24 * s, row.y + 36 * s, 11, th.accent);
        ctx_hit(row, sg);
        if (u.clicked(row) && r.contains(u.in.mx, u.in.my)) play_songs(a->songs, i, false);
    }
    if (n == 0) draw_album_empty(x, sy, w);
}

// ---------------------------------------------------------------- compact pages (<=767 px), from the original compact CSS

namespace {
Color alpha_c(Color c, float a) { c.a = static_cast<uint8_t>(std::lround(a * 255)); return c; }
}

// AlbumList (artist page): back (44), title 24 px, one full-width stat, "Download Artist" at 48, then the 3-column album grid.
void App::draw_artist_compact(Rect r) {
    Ui& u = *ui_;
    const float s = S_;
    const Theme& th = u.th;
    View& v = views_.back();
    auto it = artist_cache_.find(v.id);
    const bool ready = it != artist_cache_.end() && !it->second.id.empty();
    const Artist* a = ready ? &it->second : nullptr;
    const size_t n = a ? a->albums.size() : 0;
    const float total_h = (44 + 4 + 36 + 12 + 46 + 8 + 48 + 16 + std::ceil(n / 3.0f) * 139 + 16) * s;
    const bool overflow = total_h > r.h;
    const float x = r.x + 12 * s, w = r.w - 24 * s - (overflow ? 12 * s : 0);
    if (u.hover(r)) v.scroll.y -= u.in.wheel * 60 * s;
    v.scroll.clamp(total_h + 12 * s, r.h);
    float y = r.y + 12 * s - v.scroll.y;
    Rect back{x, y, w, 44 * s};
    fa(skin::ArrowLeft, x + 8 * s, y + 22 * s, 14, u.hover(back) ? th.text : th.sub);
    T(400, 14, x + 26 * s, y, 44, "Back to Artists", u.hover(back) ? th.text : th.sub);
    if (u.clicked(back)) { pop_view(); return; }
    y += 48 * s;
    fa(0xf51f, x + 12 * s, y + 18 * s, 22, th.accent);                               // record icon in front of the name
    T(700, 24, x + 37 * s, y, 36, v.title, th.text, w - 40 * s);
    y += 42 * s;
    if (!ready) { T(400, 16, x, y, 24, "Loading...", th.sub); return; }
    Rect st{x, y, w, 46 * s};
    gfx_->stroke_round(st, 23 * s, 1 * s, {255, 255, 255, 54}, th.bg);
    const std::string label = std::to_string(n) + " albums";
    const float lw = gfx_->measure(F(400, 14), label);
    fa(0xf51f, st.x + (st.w - lw - 22 * s) / 2 + 7 * s, st.y + st.h / 2, 14, th.accent);
    T(400, 14, st.x + (st.w - lw - 22 * s) / 2 + 22 * s, st.y, 46, label, th.sub);
    y += 54 * s;
    if (downloads_ && !offline_) {
        Rect dl{x, y, w, 48 * s};
        {   // the inner colour must be opaque: mix the accent over the page background
            const float fa_ = u.hover(dl) ? 0.25f : 0.15f;
            auto mixc = [&](uint8_t f, uint8_t bk) { return static_cast<uint8_t>(std::lround(f * fa_ + bk * (1 - fa_))); };
            const Color fill{mixc(th.accent.r, th.bg.r), mixc(th.accent.g, th.bg.g), mixc(th.accent.b, th.bg.b), 255};
            gfx_->fill_round(dl, 24 * s, fill);
            gfx_->stroke_round(dl, 24 * s, 1 * s, alpha_c(th.accent, 0.3f), fill);
        }
        const float dw = gfx_->measure(F(500, 15), "Download Artist");
        fa(skin::Download, dl.x + (dl.w - dw - 22 * s) / 2 + 7 * s, dl.y + 24 * s, 14, th.accent);
        T(500, 15, dl.x + (dl.w - dw - 22 * s) / 2 + 22 * s, dl.y, 48, "Download Artist", th.accent);
        if (u.clicked(dl)) { dq_album_ids_.clear(); for (const Album& al : a->albums) dq_album_ids_.push_back(al.id); open_download_picker_songs({}, "Download Artist"); }
        y += 64 * s;
    } else y += 16 * s;
    gfx_->clip(&r);
    const float cwn = (w - 12 * s) / 3, chn = 133 * s;
    for (size_t i = 0; i < n; ++i) {
        const Album& al = a->albums[i];
        const float px = x + (static_cast<int>(i) % 3) * (cwn + 6 * s), py = y + (static_cast<int>(i) / 3) * (chn + 6 * s);
        if (py + chn < r.y || py > r.y + r.h) continue;
        Rect card{px, py, cwn, chn};
        gfx_->fill_round(card, 12 * s, th.panel);
        if (u.hover(card) && r.contains(u.in.mx, u.in.my)) gfx_->fill_round(card, 12 * s, th.hover);
        Rect cov{px + 5 * s, py + 5 * s, cwn - 10 * s, 100 * s};
        gfx_->clip(&cov);
        Rect sq{cov.x, cov.y + (cov.h - cov.w) / 2, cov.w, cov.w};
        if (SDL_Texture* t = cover(al.cover_art, 110, al.artist, al.name)) gfx_->image_round(t, sq, 4 * s); else draw_art_fallback(sq, al.cover_art, 4 * s);
        gfx_->clip(&r);
        const float nw = std::min(gfx_->measure(F(600, 11), al.name), cov.w);
        T(600, 11, cov.x + (cov.w - nw) / 2, py + 109 * s, 17, al.name, th.text, cov.w);
        if (u.clicked(card) && r.contains(u.in.mx, u.in.my)) { push_view(ViewKind::Album, al.id, al.name); load_album(al.id); }
    }
}

// SongList (album page): hero card with centred art, ALBUM, title, meta and the action bar; 75 px song rows.
void App::draw_album_compact(Rect r) {
    Ui& u = *ui_;
    const float s = S_;
    const Theme& th = u.th;
    View& v = views_.back();
    const float x = r.x + 12 * s, w = r.w - 24 * s - 12 * s;
    auto it = album_cache_.find(v.id);
    const bool ready = it != album_cache_.end() && !it->second.id.empty();
    const Album* a = ready ? &it->second : nullptr;
    const size_t n = a ? a->songs.size() : 0;
    const float total_h = (44 + 388 + 55 + n * 75 + 16) * s;
    if (u.hover(r)) v.scroll.y -= u.in.wheel * 60 * s;
    v.scroll.clamp(total_h + 12 * s, r.h);
    const float y0 = r.y + 12 * s - v.scroll.y;
    Rect back{x, y0, 140 * s, 24 * s};
    fa(skin::ArrowLeft, x + 8 * s, y0 + 12 * s, 13, u.hover(back) ? th.text : th.sub);
    T(400, 14, x + 24 * s, y0, 24, "Back to Albums", u.hover(back) ? th.text : th.sub);
    if (u.clicked(back)) { pop_view(); return; }
    if (!ready) { T(400, 16, x, y0 + 50 * s, 24, "Loading...", th.sub); return; }
    // hero: column, centred, padding 16, gap 16
    const float hy = y0 + 44 * s;
    Rect hero{x, hy, w, 388 * s};
    gfx_->fill_round(hero, 16 * s, th.panel);
    const float cx = hero.x + hero.w / 2;
    Rect art{cx - 90 * s, hy + 16 * s, 180 * s, 180 * s};
    if (SDL_Texture* t = cover(a->cover_art, 180, a->artist, a->name)) gfx_->image_round(t, art, 12 * s); else draw_art_fallback(art, a->cover_art, 12 * s);
    float ty = hy + 16 * s + 180 * s + 16 * s;
    gfx_->set_letter_spacing(1.0f * s);
    T(600, 11, cx - gfx_->measure(F(600, 11), "ALBUM") / 2 - 1 * s, ty, 17, "ALBUM", th.sub);
    gfx_->set_letter_spacing(0);
    ty += 17 * s;
    T(700, 22, cx - std::min(gfx_->measure(F(700, 22), a->name), w - 32 * s) / 2, ty, 33, a->name, th.text, w - 32 * s);   // long titles truncate inside the card, centred
    ty += 38 * s;
    const int hrs = a->duration / 3600, mins = (a->duration % 3600) / 60;
    const std::string dur = hrs ? std::to_string(hrs) + " hr " + std::to_string(mins) + " min" : std::to_string(mins) + " min";
    std::string rest;
    if (a->year) rest += "  \xE2\x80\xA2  " + std::to_string(a->year);
    rest += "  \xE2\x80\xA2  " + std::to_string(a->songs.size()) + " songs  \xE2\x80\xA2  " + dur;
    const float aw = gfx_->measure(F(600, 15), a->artist), rw = gfx_->measure(F(400, 14), rest);
    float mx = cx - (aw + rw) / 2;
    T(600, 15, mx, ty, 22, a->artist, th.text);
    T(400, 14, mx + aw, ty, 22, rest, th.sub);
    // action bar: border-top, padding 16, play + shuffle + download right aligned with 20 px gaps
    const float by = hero.y + hero.h - 83 * s;
    gfx_->fill({hero.x, by, hero.w, 1 * s}, {255, 255, 255, 20});
    float rx = hero.x + hero.w - 16 * s;
    const bool can_dl = downloads_ && !offline_;
    auto ring_btn = [&](float right, uint32_t ic) {
        Rect b{right - 48 * s, by + 16 * s + 2 * s, 48 * s, 48 * s};
        gfx_->stroke_round(b, 24 * s, 1 * s, {255, 255, 255, 54}, th.panel);
        if (u.hover(b)) gfx_->fill_round(b, 24 * s, th.hover);
        fa(ic, b.x + 24 * s, b.y + 24 * s, 20, th.sub);
        return u.clicked(b);
    };
    if (can_dl) { if (ring_btn(rx, skin::Download)) open_download_picker_songs(a->songs, "Download " + std::to_string(a->songs.size()) + " Song" + (a->songs.size() != 1 ? "s" : "")); rx -= 68 * s; }
    if (ring_btn(rx, skin::Shuffle)) play_songs(a->songs, 0, true);
    rx -= 68 * s;
    Rect pc{rx - 52 * s, by + 16 * s, 52 * s, 52 * s};
    gfx_->circle(pc.x + 26 * s, pc.y + 26 * s, 26 * s, u.hover(pc) ? th.primary_light : th.accent);
    fa(skin::Play, pc.x + 28 * s, pc.y + 26 * s, 18, rgb(0xffffff));
    if (u.clicked(pc)) play_songs(a->songs, 0, false);
    // songs: min-height 64 + padding 14 -> 75 px rows
    float sy = hero.y + hero.h + 55 * s;
    const Song* playing = queue_.current();
    gfx_->clip(&r);
    for (size_t i = 0; i < n; ++i) {
        const Song& sg = a->songs[i];
        Rect row{x, sy + i * 75 * s, w, 75 * s};
        if (row.y + row.h < r.y || row.y > r.y + r.h) continue;
        const bool isplay = playing && playing->id == sg.id;
        const bool hov = u.hover(row) && r.contains(u.in.mx, u.in.my);
        if (isplay) gfx_->fill_round(row, 8 * s, alpha_c(th.accent, 0.12f)); else if (hov) gfx_->fill_round(row, 8 * s, th.hover);
        T(600, 15, row.x + 12 * s, row.y + 14 * s, 22.5f, sg.title, isplay ? th.accent : th.text, row.w - 100 * s);
        T(400, 13, row.x + 12 * s, row.y + 42.5f * s, 19.5f, sg.artist, th.sub, row.w - 100 * s);
        const std::string d = mmss(sg.duration);
        const float dw = gfx_->measure(F(400, 13), d);
        T(400, 13, row.x + row.w - 12 * s - dw, row.y + 28 * s, 19.5f, d, th.sub);
        ctx_hit(row, sg);
        if (u.clicked(row) && r.contains(u.in.mx, u.in.my)) play_songs(a->songs, i, false);
    }
    if (n == 0) draw_album_empty(x, sy, w);
}

// SongList empty states: offline album that was never downloaded, or an album without songs.
void App::draw_album_empty(float x, float y, float w) {
    const float s = S_; const Theme& th = ui_->th;
    if (offline_) {
        draw_library_state(x, y, w, 0xf05eu, "No Cached Songs", "This album isn't downloaded for offline playback.", false, "", 0);
        const std::string m = "Go online and click the Download button to cache this album.";
        T(400, 16, x + (w - gfx_->measure(F(400, 16), m)) / 2, y + 40 * s + 64 * s + 36 * s + 24 * s, 24, m, th.sub, w);
    } else draw_library_state(x, y, w, skin::Music, "No Songs Found", "This album has no songs.", false, "", 0);
}
