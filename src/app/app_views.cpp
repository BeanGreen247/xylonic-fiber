// Screens and widgets of the app (see app.cpp for session, playback and the event loop).
#include <algorithm>
#include <cmath>
#include "app.h"

namespace {
std::string mmss(int s) {
    if (s < 0) s = 0;
    char b[16];
    std::snprintf(b, sizeof b, "%d:%02d", s / 60, s % 60);
    return b;
}
}  // namespace

// ---------------------------------------------------------------- login

void App::draw_login(float W, float H) {
    Ui& u = *ui_;
    const float s = S_;
    float cw = std::min(W - 40 * s, 440 * s), ch = 440 * s;
    Rect card{(W - cw) / 2, std::max(20 * s, (H - ch) / 2), cw, ch};
    gfx_->fill_round(card, 14 * s, u.th.panel);
    float x = card.x + 32 * s, w = card.w - 64 * s, y = card.y + 28 * s;
    u.label(u.f_title, x, y, "xylonic-fiber", u.th.text);
    y += 46 * s;
    u.label(u.f_small, x, y, "Subsonic / Navidrome server", u.th.sub);
    y += 32 * s;
    float fh = 42 * s;
    u.text_field(kUrl, {x, y, w, fh}, f_url_, "http://192.168.1.100:4533");
    y += fh + 12 * s;
    u.text_field(kUser, {x, y, w, fh}, f_user_, "Username");
    y += fh + 12 * s;
    u.text_field(kPass, {x, y, w, fh}, f_pass_, "Password", true);
    y += fh + 20 * s;
    if (u.in.tab) u.focus = u.focus == kUrl ? kUser : u.focus == kUser ? kPass : kUrl;
    bool go = u.button({x, y, w, 44 * s}, connecting_ ? "Connecting..." : "Connect", true) || (u.in.enter && u.focus != 0);
    if (go && !connecting_) connect(false);
    y += 56 * s;
    if (cert_prompt_) {
        u.label(u.f_small, x, y, login_msg_, u.th.danger, w);
        y += 20 * s;
        u.label(u.f_small, x, y, "SHA-256 " + cert_fp_.substr(0, 32), u.th.sub, w);
        y += 18 * s;
        u.label(u.f_small, x, y, cert_fp_.substr(32), u.th.sub, w);
        y += 24 * s;
        if (u.button({x, y, w, 38 * s}, "Trust this certificate and connect")) connect(true);
    } else if (!login_msg_.empty()) {
        u.label(u.f_small, x, y, login_msg_, login_msg_ == "Connecting..." ? u.th.sub : u.th.danger, w);
    }
}

// ---------------------------------------------------------------- main layout

void App::draw_main(float W, float H) {
    const float s = S_;
    float side = W < 760 * s ? 64 * s : 200 * s, bar = 96 * s;
    draw_sidebar({0, 0, side, H - bar});
    Rect content{side, 0, W - side, H - bar};
    View& v = views_.back();
    Ui& u = *ui_;
    // header
    float hh = 60 * s;
    float tx = content.x + 24 * s;
    if (views_.size() > 1 && u.icon_button({content.x + 12 * s, 14 * s, 32 * s, 32 * s}, Icon::Back)) { pop_view(); return; }
    if (views_.size() > 1) tx += 36 * s;
    u.label(u.f_title, tx, 14 * s, v.title, u.th.text, content.w - 48 * s);
    Rect body{content.x, hh, content.w, content.h - hh};
    gfx_->clip(&body);
    if (v.kind == ViewKind::Artist) draw_artist(body);
    else if (v.kind == ViewKind::Album) draw_album(body);
    else switch (nav_) {
        case Nav::Home: draw_home(body); break;
        case Nav::Library: draw_library(body); break;
        case Nav::Search: draw_search(body); break;
        case Nav::Queue: draw_queue(body); break;
        case Nav::Settings: draw_settings(body); break;
    }
    gfx_->clip(nullptr);
    draw_player_bar({0, H - bar, W, bar});
}

void App::draw_sidebar(Rect r) {
    Ui& u = *ui_;
    const float s = S_;
    gfx_->fill(r, u.th.panel);
    bool compact = r.w < 100 * s;
    struct Item { Nav n; Icon ic; const char* name; };
    static const Item items[] = {{Nav::Home, Icon::Home, "Home"}, {Nav::Library, Icon::Library, "Library"}, {Nav::Search, Icon::Search, "Search"},
                                 {Nav::Queue, Icon::Queue, "Queue"}, {Nav::Settings, Icon::Settings, "Settings"}};
    float y = 16 * s;
    if (!compact) u.label(u.f_bold, r.x + 20 * s, y, "xylonic-fiber", u.th.accent, r.w - 30 * s);
    y += 44 * s;
    for (const Item& it : items) {
        Rect row{r.x + 8 * s, y, r.w - 16 * s, 42 * s};
        bool on = nav_ == it.n;
        if (on) gfx_->fill_round(row, 8 * s, u.th.panel2);
        else if (u.hover(row)) gfx_->fill_round(row, 8 * s, u.th.hover);
        u.icon(it.ic, {row.x + 12 * s, row.y + 11 * s, 20 * s, 20 * s}, on ? u.th.accent : u.th.sub);
        if (!compact) u.label_in(u.f_body, {row.x + 46 * s, row.y, row.w - 54 * s, row.h}, it.name, on ? u.th.text : u.th.sub);
        if (u.clicked(row)) set_nav(it.n);
        y += 46 * s;
    }
    if (!compact) {
        u.label(u.f_small, r.x + 20 * s, r.y + r.h - 70 * s, cfg_.username, u.th.sub, r.w - 30 * s);
        if (u.button({r.x + 16 * s, r.y + r.h - 44 * s, r.w - 32 * s, 32 * s}, "Log out")) logout();
    }
}

// ---------------------------------------------------------------- player bar

void App::draw_player_bar(Rect r) {
    Ui& u = *ui_;
    const float s = S_;
    gfx_->fill(r, u.th.panel);
    gfx_->fill({r.x, r.y, r.w, 1}, u.th.panel2);
    const Song* cur = queue_.current();
    float cs = 64 * s, cy = r.y + (r.h - cs) / 2;
    // left: cover + titles
    if (cur) {
        if (SDL_Texture* t = cover(cur->cover_art, 64)) gfx_->image(t, {r.x + 16 * s, cy, cs, cs});
        else gfx_->fill_round({r.x + 16 * s, cy, cs, cs}, 6 * s, u.th.panel2);
        float tx = r.x + 16 * s + cs + 12 * s, tw = std::max(60 * s, r.w * 0.22f - cs);
        u.label(u.f_bold, tx, cy + 8 * s, cur->title, u.th.text, tw);
        u.label(u.f_small, tx, cy + 32 * s, cur->artist, u.th.sub, tw);
        if (track_loading_) u.label(u.f_small, tx, cy + 50 * s, "Loading...", u.th.accent);
        else if (!play_err_.empty()) u.label(u.f_small, tx, cy + 50 * s, play_err_, u.th.danger, tw);
    } else {
        u.label(u.f_small, r.x + 20 * s, cy + 22 * s, "Nothing playing", u.th.sub);
    }
    // centre: transport + seek
    float mid = r.x + r.w / 2, by = r.y + 10 * s, bs = 36 * s;
    if (u.icon_button({mid - 128 * s, by, bs, bs}, Icon::Shuffle, queue_.shuffle())) queue_.set_shuffle(!queue_.shuffle());
    if (u.icon_button({mid - 80 * s, by, bs, bs}, Icon::Prev)) prev_track();
    Rect pp{mid - 24 * s, by - 4 * s, 48 * s, 44 * s};
    gfx_->circle(pp.x + pp.w / 2, pp.y + pp.h / 2, 22 * s, u.hover(pp) ? rgb(0x4fd3e2) : u.th.accent);
    u.icon(player_.playing() ? Icon::Pause : Icon::Play, {pp.x + 14 * s, pp.y + 12 * s, 20 * s, 20 * s}, u.th.accent_text);
    if (u.clicked(pp)) toggle_play();
    if (u.icon_button({mid + 44 * s, by, bs, bs}, Icon::Next)) next_track();
    Icon ric = queue_.repeat() == Repeat::One ? Icon::RepeatOne : Icon::Repeat;
    if (u.icon_button({mid + 92 * s, by, bs, bs}, ric, queue_.repeat() != Repeat::Off))
        queue_.set_repeat(queue_.repeat() == Repeat::Off ? Repeat::All : queue_.repeat() == Repeat::All ? Repeat::One : Repeat::Off);
    float dur = static_cast<float>(player_.duration_seconds()), pos = static_cast<float>(player_.position_seconds());
    float sw = std::min(r.w * 0.4f, 520 * s), sx = mid - sw / 2, sy = r.y + 56 * s;
    float v = dur > 0 ? std::min(1.0f, pos / dur) : 0;
    bool was = u.active == kSeek;
    if (was) v = seek_drag_v_;
    if (u.slider(kSeek, {sx, sy, sw, 20 * s}, v)) seek_drag_v_ = v;
    if (was && u.active == 0 && dur > 0) { player_.seek_seconds(seek_drag_v_ * dur); }
    u.label(u.f_small, sx - 44 * s, sy + 2 * s, mmss(static_cast<int>(was ? seek_drag_v_ * dur : pos)), u.th.sub);
    u.label(u.f_small, sx + sw + 8 * s, sy + 2 * s, mmss(static_cast<int>(dur)), u.th.sub);
    // right: volume
    float vx = r.x + r.w - 150 * s;
    bool muted = vol_ <= 0.001f;
    if (u.icon_button({vx - 36 * s, r.y + 30 * s, 32 * s, 32 * s}, muted ? Icon::Mute : Icon::Volume)) set_volume(muted ? 0.8f : 0);
    float vv = vol_;
    if (u.slider(kVol, {vx, r.y + 34 * s, 110 * s, 24 * s}, vv)) set_volume(vv);
}

// ---------------------------------------------------------------- content views

float App::draw_album_grid(Rect r, const std::vector<Album>& albums, float y, float& hit) {
    Ui& u = *ui_;
    const float s = S_;
    float cw = 164 * s, gap = 16 * s, pad = 24 * s;
    int cols = std::max(1, static_cast<int>((r.w - 2 * pad + gap) / (cw + gap)));
    float ch = cw + 44 * s;
    for (size_t i = 0; i < albums.size(); ++i) {
        int col = static_cast<int>(i) % cols, row = static_cast<int>(i) / cols;
        float x = r.x + pad + col * (cw + gap), yy = y + row * (ch + gap);
        if (yy + ch < r.y || yy > r.y + r.h) continue;
        const Album& a = albums[i];
        Rect cell{x, yy, cw, ch};
        if (u.hover(cell) && r.contains(u.in.mx, u.in.my)) gfx_->fill_round({x - 6 * s, yy - 6 * s, cw + 12 * s, ch + 6 * s}, 10 * s, u.th.panel);
        if (SDL_Texture* t = cover(a.cover_art, 164)) gfx_->image(t, {x, yy, cw, cw});
        else gfx_->fill_round({x, yy, cw, cw}, 8 * s, u.th.panel2);
        u.label(u.f_bold, x, yy + cw + 6 * s, a.name, u.th.text, cw);
        u.label(u.f_small, x, yy + cw + 26 * s, a.artist, u.th.sub, cw);
        if (u.clicked(cell) && r.contains(u.in.mx, u.in.my)) { push_view(ViewKind::Album, a.id, a.name); load_album(a.id); }
    }
    (void)hit;
    int rows = static_cast<int>((albums.size() + cols - 1) / cols);
    return rows * (ch + gap);
}

void App::draw_home(Rect r) {
    Ui& u = *ui_;
    const float s = S_;
    float dummy = 0;
    View& v = views_.back();
    Scroll& sc = home_scroll_;
    (void)v;
    float sec = 36 * s;
    int cols = std::max(1, static_cast<int>((r.w - 48 * s + 16 * s) / (180 * s)));
    float gridh1 = std::ceil(static_cast<float>(recent_.size()) / cols) * (208 * s + 16 * s);
    float gridh2 = std::ceil(static_cast<float>(random_.size()) / cols) * (208 * s + 16 * s);
    float total = sec + gridh1 + sec + gridh2 + 24 * s;
    if (u.hover(r)) sc.y -= u.in.wheel * 60 * s;
    sc.clamp(total, r.h);
    float y = r.y - sc.y;
    if (recent_.empty() && loading_home_) u.label(u.f_body, r.x + 24 * s, r.y + 10 * s, "Loading...", u.th.sub);
    if (!load_err_.empty() && recent_.empty()) u.label(u.f_body, r.x + 24 * s, r.y + 40 * s, load_err_, u.th.danger, r.w - 48 * s);
    u.label(u.f_bold, r.x + 24 * s, y + 6 * s, "Recently added", u.th.text);
    draw_album_grid(r, recent_, y + sec, dummy);
    float y2 = y + sec + gridh1;
    u.label(u.f_bold, r.x + 24 * s, y2 + 6 * s, "Random mix", u.th.text);
    if (u.button({r.x + 24 * s + 140 * s, y2 + 2 * s, 110 * s, 28 * s}, "Refresh")) { random_.clear(); loading_home_ = false; load_home(); }
    draw_album_grid(r, random_, y2 + sec, dummy);
}

void App::draw_song_rows(Rect r, const std::vector<Song>& songs, Scroll& sc, bool show_cover, int base_id) {
    Ui& u = *ui_;
    const float s = S_;
    float rh = (show_cover ? 56.0f : 44.0f) * s;
    if (u.hover(r)) sc.y -= u.in.wheel * 60 * s;
    sc.clamp(rh * songs.size() + 16 * s, r.h);
    size_t i0 = static_cast<size_t>(std::max(0.0f, std::floor(sc.y / rh)));
    size_t i1 = std::min(songs.size(), static_cast<size_t>(std::ceil((sc.y + r.h) / rh)) + 1);
    const Song* playing = queue_.current();
    for (size_t i = i0; i < i1; ++i) {
        const Song& sg = songs[i];
        Rect row{r.x + 12 * s, r.y + i * rh - sc.y, r.w - 24 * s, rh};
        bool isplay = playing && playing->id == sg.id;
        bool hov = u.hover(row) && r.contains(u.in.mx, u.in.my);
        if (hov) gfx_->fill_round(row, 8 * s, u.th.hover);
        float x = row.x + 12 * s;
        if (show_cover) {
            if (SDL_Texture* t = cover(sg.cover_art, 44)) gfx_->image(t, {x, row.y + 6 * s, 44 * s, 44 * s});
            else gfx_->fill_round({x, row.y + 6 * s, 44 * s, 44 * s}, 5 * s, u.th.panel2);
            x += 56 * s;
        } else {
            u.label_in(u.f_body, {x, row.y, 36 * s, row.h}, sg.track > 0 ? std::to_string(sg.track) : "", isplay ? u.th.accent : u.th.sub);
            x += 44 * s;
        }
        float tw = row.w - (x - row.x) - 90 * s;
        if (show_cover) {
            u.label(u.f_body, x, row.y + 8 * s, sg.title, isplay ? u.th.accent : u.th.text, tw);
            u.label(u.f_small, x, row.y + 30 * s, sg.artist + (sg.album.empty() ? "" : "  -  " + sg.album), u.th.sub, tw);
        } else {
            u.label_in(u.f_body, {x, row.y, tw, row.h}, sg.title, isplay ? u.th.accent : u.th.text);
        }
        u.label_in(u.f_small, {row.x + row.w - 72 * s, row.y, 60 * s, row.h}, mmss(sg.duration), u.th.sub);
        if (u.clicked(row) && r.contains(u.in.mx, u.in.my)) {
            play_songs(songs, i, false);
        }
    }
    (void)base_id;
}

void App::draw_library(Rect r) {
    Ui& u = *ui_;
    const float s = S_;
    Scroll& sc = lib_scroll_;
    float rh = 48 * s;
    if (u.hover(r)) sc.y -= u.in.wheel * 60 * s;
    sc.clamp(rh * artists_.size() + 16 * s, r.h);
    if (artists_.empty()) u.label(u.f_body, r.x + 24 * s, r.y + 10 * s, loading_artists_ ? "Loading..." : "No artists", u.th.sub);
    size_t i0 = static_cast<size_t>(std::max(0.0f, std::floor(sc.y / rh)));
    size_t i1 = std::min(artists_.size(), static_cast<size_t>(std::ceil((sc.y + r.h) / rh)) + 1);
    for (size_t i = i0; i < i1; ++i) {
        const Artist& a = artists_[i];
        Rect row{r.x + 12 * s, r.y + i * rh - sc.y, r.w - 24 * s, rh};
        if (u.hover(row) && r.contains(u.in.mx, u.in.my)) gfx_->fill_round(row, 8 * s, u.th.hover);
        u.label_in(u.f_body, {row.x + 14 * s, row.y, row.w - 120 * s, row.h}, a.name, u.th.text);
        u.label_in(u.f_small, {row.x + row.w - 100 * s, row.y, 90 * s, row.h}, std::to_string(a.album_count) + " albums", u.th.sub);
        if (u.clicked(row) && r.contains(u.in.mx, u.in.my)) { push_view(ViewKind::Artist, a.id, a.name); load_artist(a.id); }
    }
}

void App::draw_artist(Rect r) {
    Ui& u = *ui_;
    View& v = views_.back();
    auto it = artist_cache_.find(v.id);
    if (it == artist_cache_.end() || it->second.id.empty()) { u.label(u.f_body, r.x + 24 * S_, r.y + 10 * S_, "Loading...", u.th.sub); return; }
    float dummy = 0;
    if (u.hover(r)) v.scroll.y -= u.in.wheel * 60 * S_;
    v.scroll.clamp(it->second.albums.size() / 4.0f * 260 * S_ + 300 * S_, r.h);
    draw_album_grid(r, it->second.albums, r.y - v.scroll.y + 8 * S_, dummy);
}

void App::draw_album(Rect r) {
    Ui& u = *ui_;
    const float s = S_;
    View& v = views_.back();
    auto it = album_cache_.find(v.id);
    if (it == album_cache_.end() || it->second.id.empty()) { u.label(u.f_body, r.x + 24 * s, r.y + 10 * s, "Loading...", u.th.sub); return; }
    const Album& a = it->second;
    float hh = 150 * s;
    if (SDL_Texture* t = cover(a.cover_art, 132)) gfx_->image(t, {r.x + 24 * s, r.y + 8 * s, 132 * s, 132 * s});
    else gfx_->fill_round({r.x + 24 * s, r.y + 8 * s, 132 * s, 132 * s}, 8 * s, u.th.panel2);
    float tx = r.x + 172 * s;
    u.label(u.f_title, tx, r.y + 8 * s, a.name, u.th.text, r.w - tx - 24 * s);
    u.label(u.f_body, tx, r.y + 46 * s, a.artist + (a.year ? "  -  " + std::to_string(a.year) : ""), u.th.sub, r.w - tx - 24 * s);
    u.label(u.f_small, tx, r.y + 70 * s, std::to_string(a.songs.size()) + " songs  -  " + mmss(a.duration), u.th.sub);
    if (u.button({tx, r.y + 98 * s, 100 * s, 38 * s}, "Play", true)) play_songs(a.songs, 0, false);
    if (u.button({tx + 112 * s, r.y + 98 * s, 100 * s, 38 * s}, "Shuffle")) play_songs(a.songs, 0, true);
    Rect list{r.x, r.y + hh, r.w, r.h - hh};
    gfx_->clip(&list);
    draw_song_rows(list, a.songs, v.scroll, false, 100);
}

void App::draw_search(Rect r) {
    Ui& u = *ui_;
    const float s = S_;
    std::string before = search_q_;
    u.text_field(kSearch, {r.x + 24 * s, r.y + 4 * s, std::min(r.w - 48 * s, 520 * s), 40 * s}, search_q_, "Search songs");
    if (search_q_ != before) run_search();
    Rect list{r.x, r.y + 56 * s, r.w, r.h - 56 * s};
    gfx_->clip(&list);
    if (search_res_.empty() && search_q_.size() >= 2) u.label(u.f_body, r.x + 24 * s, list.y + 10 * s, "No results yet", u.th.sub);
    draw_song_rows(list, search_res_, search_scroll_, true, 200);
}

void App::draw_queue(Rect r) {
    Ui& u = *ui_;
    const float s = S_;
    if (queue_.empty()) { u.label(u.f_body, r.x + 24 * s, r.y + 10 * s, "Queue is empty. Play something to get started.", u.th.sub); return; }
    u.label(u.f_small, r.x + 24 * s, r.y + 8 * s, std::to_string(queue_.size()) + " songs", u.th.sub);
    if (u.button({r.x + r.w - 124 * s, r.y + 2 * s, 100 * s, 30 * s}, "Clear")) { queue_.clear(); player_.stop(); ++play_gen_; return; }
    Rect list{r.x, r.y + 40 * s, r.w, r.h - 40 * s};
    gfx_->clip(&list);
    float rh = 56 * s;
    Scroll& sc = queue_scroll_;
    if (u.hover(list)) sc.y -= u.in.wheel * 60 * s;
    sc.clamp(rh * queue_.size() + 16 * s, list.h);
    size_t i0 = static_cast<size_t>(std::max(0.0f, std::floor(sc.y / rh)));
    size_t i1 = std::min(queue_.size(), static_cast<size_t>(std::ceil((sc.y + list.h) / rh)) + 1);
    for (size_t i = i0; i < i1; ++i) {
        const Song& sg = queue_.songs()[i];
        Rect row{list.x + 12 * s, list.y + i * rh - sc.y, list.w - 24 * s, rh};
        bool cur = i == queue_.index();
        bool hov = u.hover(row) && list.contains(u.in.mx, u.in.my);
        if (hov || cur) gfx_->fill_round(row, 8 * s, cur ? u.th.panel2 : u.th.hover);
        if (SDL_Texture* t = cover(sg.cover_art, 44)) gfx_->image(t, {row.x + 10 * s, row.y + 6 * s, 44 * s, 44 * s});
        else gfx_->fill_round({row.x + 10 * s, row.y + 6 * s, 44 * s, 44 * s}, 5 * s, u.th.panel2);
        float tw = row.w - 150 * s;
        u.label(u.f_body, row.x + 66 * s, row.y + 8 * s, sg.title, cur ? u.th.accent : u.th.text, tw);
        u.label(u.f_small, row.x + 66 * s, row.y + 30 * s, sg.artist, u.th.sub, tw);
        Rect x{row.x + row.w - 40 * s, row.y + 12 * s, 32 * s, 32 * s};
        if (hov && u.icon_button(x, Icon::Close, false, 0.3f)) { bool was_cur = cur; queue_.remove(i); if (was_cur) { if (queue_.empty()) { player_.stop(); ++play_gen_; } else start_current(); } return; }
        if (u.clicked(row) && !x.contains(u.in.mx, u.in.my) && list.contains(u.in.mx, u.in.my)) { queue_.jump(i); start_current(); return; }
    }
}

void App::draw_settings(Rect r) {
    Ui& u = *ui_;
    const float s = S_;
    float x = r.x + 24 * s, y = r.y + 8 * s;
    u.label(u.f_bold, x, y, "Server", u.th.text); y += 26 * s;
    u.label(u.f_body, x, y, cfg_.server_url + (cfg_.pin_sha256.empty() ? "" : "   (pinned certificate)"), u.th.sub, r.w - 48 * s); y += 24 * s;
    u.label(u.f_body, x, y, "User: " + cfg_.username, u.th.sub); y += 40 * s;
    u.label(u.f_bold, x, y, "Streaming quality (kbps, 0 = original)", u.th.text); y += 30 * s;
    static const int q[] = {0, 320, 256, 192, 128, 64};
    for (int i = 0; i < 6; ++i) {
        Rect b{x + i * 84 * s, y, 76 * s, 34 * s};
        bool on = cfg_.stream_quality == q[i];
        if (u.button(b, q[i] ? std::to_string(q[i]) : "Original", on)) { cfg_.stream_quality = q[i]; cfg_.save(cfg_path_); }
    }
    y += 54 * s;
    u.label(u.f_bold, x, y, "Cache", u.th.text); y += 30 * s;
    if (u.button({x, y, 190 * s, 34 * s}, "Clear cover memory")) images_->clear_memory();
}
