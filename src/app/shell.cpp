// Exact port of the old app's shell: header, icon nav, content frame, player bar and the Discover view.
// Every number below is measured from the old UI at 1024x768 (docs/ui-ref/*.png and docs/ui-spec.md).
#include <algorithm>
#include <cmath>
#include "app.h"
#include "../ui/svg.h"

namespace {
std::string mmss(int s) {
    if (s < 0) s = 0;
    char b[16];
    std::snprintf(b, sizeof b, "%d:%02d", s / 60, s % 60);
    return b;
}
Color alpha(Color c, float a) { c.a = static_cast<uint8_t>(std::lround(a * 255)); return c; }
const char* kSectionTitle[4] = {"Recently Added", "Recently Played", "Most Played", "Random Mix"};
const char* kSectionType[4] = {"newest", "recent", "frequent", "random"};
const uint32_t kSectionIcon[4] = {skin::CalendarPlus, skin::History, skin::Fire, skin::Shuffle};
}  // namespace

// ---------------------------------------------------------------- fonts and primitives

int App::F(int weight, float px, bool italic) {
    int w = weight >= 800 && italic ? 900 : weight >= 600 ? 700 : weight >= 500 ? 500 : 400;
    int key = w * 100000 + static_cast<int>(px * 10) + (italic ? 50000000 : 0);
    auto it = font_ids_.find(key);
    if (it != font_ids_.end()) return it->second;
    const char* file = italic ? (w == 900 ? "Roboto-BlackItalic.ttf" : "Roboto-BoldItalic.ttf")
                              : (w == 700 ? "Roboto-Bold.ttf" : w == 500 ? "Roboto-Medium.ttf" : "Roboto-Regular.ttf");
    int id = gfx_->add_font((assets_dir_ + file).c_str(), px * S_);
    font_ids_[key] = id;
    return id;
}

int App::FA(float px) {
    int key = -static_cast<int>(px * 10) - 1;
    auto it = font_ids_.find(key);
    if (it != font_ids_.end()) return it->second;
    int id = gfx_->add_font((assets_dir_ + "fa-solid-subset.ttf").c_str(), px * S_);
    font_ids_[key] = id;
    return id;
}

float App::T(int weight, float px, float x, float y_top, float line_h, const std::string& s, Color c, float max_w, bool italic) {
    int f = F(weight, px, italic);
    float y = y_top + (line_h * S_ - gfx_->line_height(f)) / 2;
    return gfx_->text(f, x, y, s, c, max_w);
}

void App::fa(uint32_t cp, float cx, float cy, float px, Color c) {
    int f = FA(px);
    std::string g = skin::utf8(cp);
    float w = gfx_->measure(f, g), lh = gfx_->line_height(f);
    gfx_->text(f, cx - w / 2, cy - lh / 2, g, c);
}

void App::svg(int which, Rect r, Color tint) {
    static const char* files[3] = {"logo.svg", "github.svg", "lastfm.svg"};
    int px = static_cast<int>(std::lround(r.w));
    if (!svg_tex_[which] || svg_px_[which] != px) {
        if (svg_tex_[which]) SDL_DestroyTexture(svg_tex_[which]);
        svg_tex_[which] = svg_texture_from_file(ren_, (assets_dir_ + "../icons/" + files[which]).c_str(), px, px);
        svg_px_[which] = px;
    }
    if (svg_tex_[which]) gfx_->image(svg_tex_[which], r, tint);
}

// ---------------------------------------------------------------- frame

void App::draw_main(float W, float H) {
    const float s = S_;
    Ui& uin = *ui_;
    Input saved = uin.in;
    if (modal_ != Modal::None || add_song_ || show_help_ || ctx_song_) { uin.in.mx = uin.in.my = -1000; uin.in.pressed = uin.in.released = false; uin.in.wheel = 0; uin.in.down = false; uin.in.escape = 0; }
    struct Restore { App* a; Input saved; ~Restore() { } } restore{this, saved};
    (void)restore;
    if (car_open_) { draw_car_mode(W, H); if (modal_ != Modal::None) { uin.in = saved; draw_modal(W, H); } return; }
    if (np_open_) { draw_now_playing(W, H); if (modal_ != Modal::None) { uin.in = saved; draw_modal(W, H); } if (ctx_song_) { uin.in = saved; draw_ctx_menu(W, H); } return; }
    draw_nav(H);
    draw_header(W);
    float navw = skin::kNavW * s, top = skin::kHeaderH * s, bar = skin::kPlayerH * s;
    Rect content{navw, top, W - navw, H - top - bar};
    View& v = views_.back();
    Rect body{content.x, content.y, content.w, content.h};
    gfx_->clip(&body);
    if (v.kind == ViewKind::Artist) draw_artist(body);
    else if (v.kind == ViewKind::Album) draw_album(body);
    else switch (nav_) {
        case Nav::Home: draw_discover(body); break;
        case Nav::Library: draw_library(body); break;
        case Nav::Search: draw_search(body); break;
        case Nav::Queue: draw_queue(body); break;
        case Nav::Downloads: draw_downloads(body); break;
        case Nav::Settings: draw_settings(body); break;
    }
    gfx_->clip(nullptr);
    draw_player_bar({navw, H - bar, W - navw, bar});
    draw_right_panel(W, H);
    if (add_song_) { uin.in = saved; draw_add_to_playlist(W, H); return; }
    if (show_help_) { uin.in = saved; draw_help(W, H); return; }
    if (ctx_song_) { uin.in = saved; draw_ctx_menu(W, H); }
    if (modal_ != Modal::None) { uin.in = saved; draw_modal(W, H); }
}

void App::draw_header(float W) {
    Ui& u = *ui_;
    const float s = S_;
    const Theme& th = u.th;
    Rect hdr{0, 0, W, skin::kHeaderH * s};
    gfx_->fill(hdr, th.panel);
    gfx_->fill({0, hdr.h, W, 1 * s}, {0, 0, 0, 77});   // elevation-2 shadow, approximated by three fading rows
    gfx_->fill({0, hdr.h + s, W, 2 * s}, {0, 0, 0, 38});
    gfx_->fill({0, hdr.h + 3 * s, W, 2 * s}, {0, 0, 0, 16});
    // brand
    svg(0, {24 * s, 18 * s, 28 * s, 28 * s}, {255, 255, 255, 255});
    gfx_->set_letter_spacing(-0.5f * s);
    T(600, 24, 64 * s, 14 * s, 36, "Xylonic", th.accent);
    gfx_->set_letter_spacing(0);
    auto round_btn = [&](Rect r, float hover_r) {
        if (u.hover(r)) gfx_->fill_round(r, hover_r, th.hover);
        return u.clicked(r);
    };
    Rect gh{155 * s, 10 * s, 44 * s, 44 * s};
    if (round_btn(gh, 22 * s)) SDL_OpenURL("https://github.com/BeanGreen247/xylonic-fiber");
    svg(1, {gh.x + 13 * s, gh.y + 13 * s, 18 * s, 18 * s}, th.sub);
    Rect sup{215 * s, 10 * s, 44 * s, 44 * s};
    if (round_btn(sup, 22 * s)) SDL_OpenURL("https://github.com/sponsors/BeanGreen247");
    fa(skin::Heart, sup.x + 22 * s, sup.y + 22 * s, 18, th.sub);

    // centre group: search, Remote, Mini (measured: 200 + 12 + 116 + 12 + 95, centred in the header centre area)
    float gx = 394 * s;
    Color edge = alpha(th.primary_light, 0.464f);
    Rect sb{gx, 14 * s, 200 * s, 35 * s};
    gfx_->fill_round(sb, 17.5f * s, th.panel);
    gfx_->stroke_round(sb, 17.5f * s, 1 * s, edge, th.panel);
    fa(skin::Search, sb.x + 16 * s, sb.y + sb.h / 2, 12, th.sub);
    if (u.clicked(sb) && u.focus != kSearch) { u.focus = kSearch; }
    {
        Rect fld{sb.x + 1 * s, sb.y + 1 * s, sb.w - 2 * s, sb.h - 2 * s};
        std::string before = search_q_;
        bool changed = u.text_field(kSearch, fld, search_q_, "Search", false, false, 36 * s, F(400, 13));
        if (u.focus == kSearch && nav_ != Nav::Search) { search_prev_nav_ = nav_; nav_ = Nav::Search; views_.clear(); views_.push_back({ViewKind::Root, "", "Search", {}}); }
        if (changed || search_q_ != before) { search_pending_ = true; search_due_ = SDL_GetTicks() + 300; if (search_q_.empty()) run_search(); }
        if (nav_ != Nav::Search && search_q_.empty() && u.focus != kSearch) {
            T(400, 11, sb.x + 36 * s + 46 * s, sb.y + 1 * s, 35, "Ctrl+K", alpha(th.sub, 0.7f));
        }
        if (!search_q_.empty()) {
            Rect xb{sb.x + sb.w - 30 * s, sb.y + 7 * s, 20 * s, 20 * s};
            gfx_->circle(xb.x + 10 * s, xb.y + 10 * s, 10 * s, {255, 255, 255, 46});
            fa(skin::Xmark, xb.x + 10 * s, xb.y + 10 * s, 10, th.text);
            if (u.clicked(xb)) { search_q_.clear(); run_search(); }
        }
    }
    auto pill = [&](Rect r, uint32_t icon, const std::string& label) {
        bool h = u.hover(r);
        gfx_->fill_round(r, r.h / 2, h ? th.hover : th.panel);
        gfx_->stroke_round(r, r.h / 2, 1 * s, edge, th.panel);
        fa(icon, r.x + 28 * s, r.y + r.h / 2, 14, th.text);
        T(500, 14, r.x + 46 * s, r.y, r.h / s, label, th.text);
        return u.clicked(r);
    };
    if (pill({606 * s, 10 * s, 116 * s, 43 * s}, skin::Satellite, "Remote")) { /* remote control lands with the LAN module */ }
    if (pill({734 * s, 10 * s, 95 * s, 43 * s}, skin::Compress, "Mini")) { /* mini player lands with its module */ }
    // offline toggle (globe)
    Rect gl{W - 60 * s, 14 * s, 36 * s, 36 * s};
    if (round_btn(gl, 18 * s)) { offline_ = !offline_; load_home(); load_artists(); for (int i = 0; i < 4; ++i) load_section(i); }
    fa(skin::Globe, gl.x + 18 * s, gl.y + 18 * s, 14, offline_ ? th.accent : th.sub);
}

void App::draw_nav(float H) {
    Ui& u = *ui_;
    const float s = S_;
    const Theme& th = u.th;
    float top = skin::kHeaderH * s;
    gfx_->fill({0, top, skin::kNavW * s, H - top}, th.panel);
    struct Item { Nav n; uint32_t icon; const char* tip; };
    static const Item up[] = {{Nav::Home, skin::House, "Home"}, {Nav::Library, skin::BookOpen, "Library"}, {Nav::Downloads, skin::Download, "Downloads"}};
    auto item = [&](float y, uint32_t icon, bool active) {
        Rect r{6 * s, y, 52 * s, 41 * s};
        if (active) gfx_->fill_round(r, 12 * s, alpha(th.accent, 0.15f));
        else if (u.hover(r)) gfx_->fill_round(r, 12 * s, th.hover);
        fa(icon, r.x + r.w / 2, r.y + r.h / 2, 17, active ? th.accent : th.sub);
        return u.clicked(r);
    };
    float y = top + 16 * s;
    for (const Item& it : up) {
        bool on = (nav_ == Nav::Search ? search_prev_nav_ : nav_) == it.n;
        if (item(y, it.icon, on)) set_nav(it.n);
        y += 43 * s;
    }
    float by = H - 16 * s - 41 * s;
    if (item(by, skin::SignOut, false)) logout();
    if (item(by - 43 * s, skin::CircleQuestion, show_help_)) show_help_ = !show_help_;
    if (item(by - 86 * s, skin::Gear, nav_ == Nav::Settings)) set_nav(Nav::Settings);
}

// ---------------------------------------------------------------- player bar

void App::draw_player_bar(Rect r) {
    Ui& u = *ui_;
    const float s = S_;
    const Theme& th = u.th;
    gfx_->fill(r, th.panel);
    gfx_->fill({r.x, r.y - 4 * s, r.w, 4 * s}, {0, 0, 0, 22});  // shadow above (0 -4px 12px)
    const Song* cur = queue_.current();
    float x0 = r.x + 16 * s, w = r.w - 32 * s;
    // progress
    float dur = static_cast<float>(player_.duration_seconds()), pos = static_cast<float>(player_.position_seconds());
    Rect pb{x0, r.y + 7 * s, w, 6 * s};
    float v = dur > 0 ? std::min(1.0f, pos / dur) : 0;
    bool was = u.active == kSeek;
    if (was) v = seek_drag_v_;
    Rect hit_r{x0, r.y + 2 * s, w, 16 * s};
    if (u.in.pressed && hit_r.contains(u.in.mx, u.in.my)) u.active = kSeek;
    if (u.active == kSeek && (u.in.down || u.in.released)) {
        float nv = std::min(1.0f, std::max(0.0f, (u.in.mx - pb.x) / pb.w));
        seek_drag_v_ = nv; v = nv;
        if (u.in.released) { u.active = 0; if (dur > 0) player_.seek_seconds(nv * dur); }
    }
    gfx_->fill_round(pb, 3 * s, {255, 255, 255, 46});
    if (v > 0) gfx_->fill_round({pb.x, pb.y, std::max(6 * s, pb.w * v), pb.h}, 3 * s, th.accent);
    if (u.hover(hit_r) || u.active == kSeek) gfx_->circle(pb.x + pb.w * v, pb.y + 3 * s, 7 * s, th.text);
    float shown = (u.active == kSeek && dur > 0) ? seek_drag_v_ * dur : pos;
    T(400, 10, x0 + 4 * s, r.y + 15 * s, 15, mmss(static_cast<int>(shown)), th.sub);
    std::string total = mmss(static_cast<int>(dur));
    int f10 = F(400, 10);
    T(400, 10, x0 + w - 4 * s - gfx_->measure(f10, total), r.y + 15 * s, 15, total, th.sub);

    float cy = r.y + 34 * s;  // controls container top (716 - 682)
    // left: cover + titles
    Rect info{x0 + 10 * s, cy, 200 * s, 44 * s};
    if (cur) {
        Rect cv{info.x, info.y, 44 * s, 44 * s};
        if (SDL_Texture* t = cover(cur->cover_art, 44)) gfx_->image_round(t, cv, 8 * s);
        else gfx_->fill_round(cv, 8 * s, th.panel2);
        if (u.clicked({info.x, info.y, 200 * s, 44 * s})) { np_open_ = true; mark(); }
        T(500, 14, info.x + 56 * s, info.y + 1 * s, 21, cur->title, th.text, 144 * s);
        T(400, 11, info.x + 56 * s, info.y + 26 * s, 17, track_loading_ ? "Loading..." : !play_err_.empty() ? play_err_ : cur->artist, play_err_.empty() ? th.sub : th.danger, 144 * s);
    }
    // right: speed, quality, volume
    float right = x0 + w - 10 * s;
    Rect vol{right - 80 * s, cy + 10 * s, 80 * s, 24 * s};
    Rect qual{vol.x - 16 * s - 120 * s, cy + 6 * s, 120 * s, 32 * s};
    Rect spd{qual.x - 16 * s - 52 * s, cy + 9 * s, 52 * s, 26 * s};
    Color edge = alpha(th.primary_light, 0.464f);
    gfx_->fill_round(spd, 8 * s, th.panel);
    gfx_->stroke_round(spd, 8 * s, 1 * s, edge, th.panel);
    if (u.hover(spd)) gfx_->fill_round(spd, 8 * s, th.hover);
    fa(skin::Bolt, spd.x + 17 * s, spd.y + spd.h / 2, 11, th.accent);
    {
        char sb[16];
        double sp = player_.speed();
        if (sp == static_cast<int>(sp)) std::snprintf(sb, sizeof sb, "%d\xC3\x97", static_cast<int>(sp)); else if (sp * 100 == static_cast<int>(sp * 100) && static_cast<int>(sp * 100) % 10 == 0) std::snprintf(sb, sizeof sb, "%.1f\xC3\x97", sp); else std::snprintf(sb, sizeof sb, "%.2f\xC3\x97", sp);
        T(600, 12, spd.x + 28 * s, spd.y, 26, sb, th.sub);
    }
    if (u.clicked(spd)) { speed_popup_ = !speed_popup_; mark(); }
    if (speed_popup_) {
        static const double opts[7] = {0.5, 0.75, 1.0, 1.25, 1.5, 1.75, 2.0};
        float pw = 120 * s, ih = 34 * s, ph = 7 * ih + 8 * s;
        Rect pop{spd.x + spd.w - pw, spd.y - 6 * s - ph, pw, ph};
        gfx_->fill_round({pop.x - 1, pop.y - 1, pop.w + 2, pop.h + 2}, 13 * s, th.border);
        gfx_->fill_round(pop, 12 * s, th.bg_light);
        for (int i = 0; i < 7; ++i) {
            Rect ir{pop.x + 4 * s, pop.y + 4 * s + i * ih, pop.w - 8 * s, ih};
            bool on = std::fabs(player_.speed() - opts[i]) < 1e-6;
            if (u.hover(ir)) gfx_->fill_round(ir, 8 * s, th.hover);
            if (on) fa(skin::Check, ir.x + 18 * s, ir.y + ih / 2, 11, th.accent);
            char lb[16]; std::snprintf(lb, sizeof lb, opts[i] == 1.0 ? "Normal" : "%g\xC3\x97", opts[i]);
            T(on ? 600 : 400, 13, ir.x + 36 * s, ir.y, ih / s, lb, on ? th.accent : th.text);
            if (u.clicked(ir)) { player_.set_speed(opts[i]); speed_popup_ = false; mark(); }
        }
        if (u.in.pressed && !pop.contains(u.in.mx, u.in.my) && !spd.contains(u.in.mx, u.in.my)) speed_popup_ = false;
    }
    gfx_->fill_round(qual, 16 * s, th.panel);
    gfx_->stroke_round(qual, 16 * s, 1 * s, edge, th.panel);
    if (u.hover(qual)) gfx_->fill_round(qual, 16 * s, th.hover);
    int bars = cfg_.stream_quality == 0 ? 6 : cfg_.stream_quality >= 320 ? 5 : cfg_.stream_quality >= 256 ? 4 : cfg_.stream_quality >= 192 ? 3 : cfg_.stream_quality >= 128 ? 2 : 1;
    for (int i = 0; i < 6; ++i) {
        float bh = (5 + 3 * i) * s;
        gfx_->fill_round({qual.x + 9 * s + i * 5 * s, qual.y + 26 * s - bh, 3 * s, bh}, 1.5f * s, i < bars ? th.accent : alpha(th.text, 0.25f));
    }
    fa(skin::Cloud, qual.x + 51 * s, qual.y + 16 * s, 14, th.accent);
    T(600, 12, qual.x + 65 * s, qual.y, 32, cfg_.stream_quality ? std::to_string(cfg_.stream_quality) + "k" : "Original", th.text);
    if (u.clicked(qual)) { static const int q[] = {0, 320, 256, 192, 128, 64}; int idx = 0; for (int i = 0; i < 6; ++i) if (q[i] == cfg_.stream_quality) idx = i; cfg_.stream_quality = q[(idx + 1) % 6]; cfg_.save(cfg_path_); }
    bool muted = vol_ <= 0.001f;
    Rect sp{vol.x, vol.y, 24 * s, 24 * s};
    fa(skin::VolumeHigh, sp.x + 12 * s, sp.y + 12 * s, 14, muted ? th.muted : th.sub);
    if (u.clicked(sp)) set_volume(muted ? 0.8f : 0);
    Rect vs{vol.x + 32 * s, vol.y + 10 * s, 48 * s, 4 * s};
    float vv = vol_;
    Rect vhit{vs.x, vol.y, vs.w, vol.h};
    if (u.in.pressed && vhit.contains(u.in.mx, u.in.my)) u.active = kVol;
    if (u.active == kVol && (u.in.down || u.in.released)) { vv = std::min(1.0f, std::max(0.0f, (u.in.mx - vs.x) / vs.w)); set_volume(vv); if (u.in.released) u.active = 0; }
    gfx_->fill_round(vs, 2 * s, {255, 255, 255, 46});
    gfx_->fill_round({vs.x, vs.y, std::max(4 * s, vs.w * vol_), vs.h}, 2 * s, th.accent);

    // centre: transport, centred between the info block and the right controls
    float gap_l = info.x + 200 * s, gap_r = right - 366 * s;
    float mid = (gap_l + gap_r) / 2 + 3 * s;  // group centre: measured 464 at 1024 wide
    auto cbtn = [&](float cx, uint32_t ic, bool on) {
        Rect b{cx - 16.5f * s, cy + 12 * s, 33 * s, 27 * s};
        if (u.hover(b)) gfx_->fill_round(b, 13 * s, th.hover);
        fa(ic, b.x + b.w / 2, b.y + b.h / 2, 13.3f, on ? th.accent : th.sub);
        return u.clicked(b);
    };
    if (cbtn(mid - 102.5f * s, skin::Shuffle, queue_.shuffle())) queue_.set_shuffle(!queue_.shuffle());
    if (cbtn(mid - 67.5f * s, skin::StepBack, false)) prev_track();
    Rect pp{mid - 41 * s, cy + 2 * s, 40 * s, 40 * s};
    gfx_->circle(pp.x + 20 * s, pp.y + 20 * s, 20 * s, u.hover(pp) ? rgb(0xe8e8e8) : rgb(0xffffff));
    fa(player_.playing() ? skin::Pause : skin::Play, pp.x + 20 * s + (player_.playing() ? 0 : 1 * s), pp.y + 20 * s, 16, rgb(0x121212));
    if (u.clicked(pp)) toggle_play();
    if (cbtn(mid + 25.5f * s, skin::StepFwd, false)) next_track();
    if (cbtn(mid + 59.5f * s, skin::Repeat, queue_.repeat() != Repeat::Off))
        queue_.set_repeat(queue_.repeat() == Repeat::Off ? Repeat::All : queue_.repeat() == Repeat::All ? Repeat::One : Repeat::Off);
    if (queue_.repeat() == Repeat::One) T(700, 8, mid + 59.5f * s + 4 * s, cy + 12 * s, 10, "1", th.accent);
    bool liked = cur && cur->starred;
    if (cbtn(mid + 102.5f * s, skin::Heart, liked) && cur && client_) {
        Song sg = *cur;
        bool on = !sg.starred;
        worker_->post([c = client_, id = sg.id, on] { std::string e; c->set_star(id, on, e); });
        const_cast<Song*>(queue_.current())->starred = on;
    }
}

// ---------------------------------------------------------------- Discover

void App::load_section(int i) {
    Section& sec = sections_[i];
    std::string key = std::string("home:") + kSectionType[i];
    std::string j;
    if (store_ && store_->get(key, j)) { std::vector<Album> a; if (from_json(j, a)) { sec.albums = a; sec.loading = false; } }
    if (offline_ || !client_) {
        if (i == 0 && downloads_) { sec.albums = downloads_->done_albums(); }
        sec.loading = false;
        mark();
        return;
    }
    sec.loading = true;
    sec.error.clear();
    worker_->post([this, c = client_, s = session_, i, key] {
        std::vector<Album> a; std::string e;
        bool ok = c->album_list(kSectionType[i], 20, 0, a, e);
        ui_post([this, s, i, key, a = std::move(a), ok, e]() mutable {
            if (s != session_) return;
            Section& sec = sections_[i];
            sec.loading = false;
            if (ok) { if (store_) store_->put(key, to_json(a)); sec.albums = std::move(a); sec.error.clear(); }
            else sec.error = e.empty() ? "Failed to load" : e;
            mark();
        });
    });
    mark();
}

void App::draw_discover(Rect r) {
    Ui& u = *ui_;
    const float s = S_;
    const Theme& th = u.th;
    if (offline_) {
        // offline: the old app shows a single message; here the downloaded albums are listed instead (Discover needs a server)
    }
    const float pad = skin::kContentPad * s;
    float cw = skin::kCardW * s, ch = skin::kCardH * s, gap = skin::kCardGap * s;
    float total = 48 * s + 32 * s + 4 * (32 * s + 16 * s + 254 * s + 48 * s);
    if (u.hover(r)) discover_scroll_.y -= u.in.wheel * 60 * s;
    discover_scroll_.clamp(total + 2 * pad, r.h);
    float x = r.x + pad, y = r.y + pad - discover_scroll_.y, w = r.w - 2 * pad - 12 * s;  // 12px scrollbar gutter
    // title row
    gfx_->circle(x + 18 * s, y + 24 * s, 17 * s, th.accent);
    fa(skin::Compass, x + 18 * s, y + 24 * s, 22, th.accent_text);
    gfx_->set_letter_spacing(-0.32f * s);
    T(700, 32, x + 52 * s, y, 48, "Discover", th.text);
    gfx_->set_letter_spacing(0);
    y += 80 * s;
    for (int i = 0; i < 4; ++i) {
        Section& sec = sections_[i];
        // header
        fa(kSectionIcon[i], x + 10 * s, y + 16 * s, 18, th.accent);
        T(500, 20, x + 33 * s, y, 32, kSectionTitle[i], th.text);
        Rect rf{x + w - 32 * s, y, 32 * s, 32 * s};
        if (u.hover(rf)) gfx_->fill_round(rf, 16 * s, th.hover);
        fa(skin::Sync, rf.x + 16 * s, rf.y + 16 * s, 13.3f, th.sub);
        if (u.clicked(rf)) load_section(i);
        if (i == 3 && !sec.albums.empty() && !sec.loading) {
            Rect pa{rf.x - 8 * s - 96 * s, y + 2 * s, 96 * s, 28 * s};
            gfx_->fill_round(pa, 14 * s, u.hover(pa) ? th.primary_light : th.accent);
            fa(skin::Play, pa.x + 20 * s, pa.y + 14 * s, 11, th.accent_text);
            T(600, 12, pa.x + 34 * s, pa.y, 28, "Play All", th.accent_text);
            if (u.clicked(pa)) play_random_mix();
        }
        y += 48 * s;
        // scroll row
        Rect row{x, y, w, 254 * s};
        float content_w = sec.albums.size() * (cw + gap) - gap;
        float max_sx = std::max(0.0f, content_w - w);
        sec.scroll_x = std::min(std::max(0.0f, sec.scroll_x), max_sx);
        if (!sec.error.empty()) {
            fa(skin::Exclaim, x + 10 * s, y + 20 * s, 16, rgb(0xff3b30));
            T(400, 14, x + 30 * s, y + 8 * s, 24, sec.error, th.sub, w - 40 * s);
        } else if (sec.loading && sec.albums.empty()) {
            for (int k = 0; k < 4; ++k) {
                Rect c{x + k * (cw + gap), y, cw, ch};
                gfx_->fill_round(c, 16 * s, th.panel);
                gfx_->fill_round({c.x + 16 * s, c.y + 16 * s, 148 * s, 148 * s}, 4 * s, th.panel2);
                gfx_->fill_round({c.x + 16 * s, c.y + 180 * s, 100 * s, 12 * s}, 6 * s, th.panel2);
                gfx_->fill_round({c.x + 16 * s, c.y + 206 * s, 70 * s, 10 * s}, 5 * s, th.panel2);
            }
        } else if (sec.albums.empty()) {
            fa(skin::CompactDisc, x + 10 * s, y + 20 * s, 16, th.sub);
            T(400, 14, x + 30 * s, y + 8 * s, 24, "No albums found", th.sub);
        } else {
            gfx_->clip(&row);
            for (size_t k = 0; k < sec.albums.size(); ++k) {
                float cx = x + k * (cw + gap) - sec.scroll_x;
                if (cx + cw < row.x || cx > row.x + row.w) continue;
                const Album& a = sec.albums[k];
                Rect card{cx, y, cw, ch};
                bool hov = u.hover(card) && row.contains(u.in.mx, u.in.my);
                gfx_->fill_round(card, 16 * s, th.panel);
                if (hov) gfx_->fill_round(card, 16 * s, th.hover);
                Rect cov{cx + 16 * s, y + 16 * s, 148 * s, 148 * s};
                if (SDL_Texture* t = cover(a.cover_art, 148)) gfx_->image_round(t, cov, 4 * s);
                else gfx_->fill_round(cov, 4 * s, th.panel2);
                int fn = F(600, 15.2f);
                float nw = gfx_->measure(fn, a.name);
                T(600, 15.2f, cov.x + std::max(0.0f, (cov.w - std::min(nw, cov.w)) / 2), y + 176 * s, 23, a.name, th.text, cov.w);
                std::string sub = (a.year ? std::to_string(a.year) + " \xC2\xB7 " : std::string()) + a.artist;
                T(400, 13.6f, cov.x, y + 203 * s, 20, sub, th.sub, cov.w);
                if (u.clicked(card) && row.contains(u.in.mx, u.in.my)) { push_view(ViewKind::Album, a.id, a.name); load_album(a.id); }
            }
            gfx_->clip(&r);
            // scroll arrows (36px circles, visible only when there is more to scroll)
            auto arrow = [&](float ax, uint32_t icon, float delta) {
                Rect ar{ax, y + 72 * s, 36 * s, 36 * s};
                gfx_->circle(ar.x + 18 * s, ar.y + 18 * s, 18 * s, {10, 10, 10, 191});
                gfx_->ring(ar.x + 18 * s, ar.y + 18 * s, 17.5f * s, 1 * s, {255, 255, 255, 105});
                fa(icon, ar.x + 18 * s, ar.y + 18 * s, 13, th.text);
                if (u.clicked(ar)) sec.scroll_x += delta;
            };
            if (sec.scroll_x > 1) arrow(x + 6 * s, skin::ChevLeft, -560 * s);
            if (sec.scroll_x < max_sx - 1) arrow(x + w - 42 * s, skin::ChevRight, 560 * s);
            // wheel + shift scrolls the row under the cursor horizontally
            if (row.contains(u.in.mx, u.in.my) && u.in.hwheel != 0) sec.scroll_x -= u.in.hwheel * 60 * s;
        }
        y += 254 * s + 48 * s;
    }
}

void App::play_random_mix() {
    if (playing_random_ || !client_ || sections_[3].albums.empty()) return;
    playing_random_ = true;
    worker_->post([this, c = client_, s = session_, albums = sections_[3].albums] {
        std::vector<Song> songs;
        for (const Album& a : albums) { Album full; std::string e; if (c->album(a.id, full, e)) for (auto& sg : full.songs) songs.push_back(sg); }
        ui_post([this, s, songs = std::move(songs)] { if (s != session_) return; playing_random_ = false; if (!songs.empty()) play_songs(songs, 0, false); mark(); });
    });
}
