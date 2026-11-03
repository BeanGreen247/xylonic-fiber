// Exact port of the old app's shell: header, icon nav, content frame, player bar and the Discover view.
// Every number below is measured from the old UI at 1024x768 (docs/ui-ref/*.png and docs/ui-spec.md).
#include <algorithm>
#include <cmath>
#include "app.h"
#include <ctime>
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
    if (modal_ != Modal::None || add_song_ || show_help_ || ctx_song_ || hamburger_open_ || pre_open_ || newcontent_open_ || prompt_ != Prompt::None) { uin.in.mx = uin.in.my = -1000; uin.in.pressed = uin.in.released = false; uin.in.wheel = 0; uin.in.down = false; uin.in.escape = 0; }
    mobile_ = W / s <= 767.0f;   // LayoutModeContext COMPACT_MAX; decided before any full-screen view
    struct Restore { App* a; Input saved; ~Restore() { } } restore{this, saved};
    (void)restore;
    if (car_open_) { draw_car_mode(W, H); if (modal_ != Modal::None) { uin.in = saved; draw_modal(W, H); } return; }
    if (np_open_) { draw_now_playing(W, H); if (modal_ != Modal::None) { uin.in = saved; draw_modal(W, H); } if (ctx_song_) { uin.in = saved; draw_ctx_menu(W, H); } return; }
    mobile_ = W / s <= 767.0f;   // LayoutModeContext COMPACT_MAX
    banner_h_ = (missing_banner_ && !offline_) ? (mobile_ ? 47.0f : 60.0f) * s : 0.0f;
    if (!mobile_) draw_nav(H);
    draw_header(W);
    const float hh = (mobile_ ? 56.0f : static_cast<float>(skin::kHeaderH)) * s;
    { float bh = 0; draw_missing_banner(W, hh, bh); }
    const float bnav = mobile_ ? 56 * s : 0;
    float navw = mobile_ ? 0.0f : (nav_expanded_ ? 200.0f : skin::kNavW) * s, top = hh, bar = (mobile_ ? 60.0f : skin::kPlayerH) * s;
    Rect content{navw, top + banner_h_, W - navw, H - top - banner_h_ - bar - bnav};
    View& v = views_.back();
    Rect body{content.x, content.y, content.w, content.h};
    // The player bar is drawn after the page, so shield the page from the same
    // mouse release. This also keeps an active seek/volume drag from reaching
    // rows underneath when the pointer leaves the bar.
    const bool player_input = (uin.in.mx >= navw && uin.in.my >= H - bar - bnav) || uin.active == kSeek || uin.active == kVol;
    if (player_input) {
        uin.in.mx = uin.in.my = -1000;
        uin.in.pressed = uin.in.released = uin.in.rclick = false;
        uin.in.down = false;
        uin.in.wheel = uin.in.hwheel = 0;
    }
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
    uin.in = saved;
    if (mobile_) { draw_player_bar_mobile({0, H - bar - bnav, W, bar}); uin.in = saved; draw_bottom_nav(W, H); }
    else draw_player_bar({navw, H - bar, W - navw, bar});
    draw_right_panel(W, H);
    if (hamburger_open_) { uin.in = saved; draw_hamburger(W, H); }
    if (add_song_) { uin.in = saved; draw_add_to_playlist(W, H); return; }
    if (show_help_) { uin.in = saved; draw_help(W, H); return; }
    if (ctx_song_) { uin.in = saved; draw_ctx_menu(W, H); }
    if (client_ && modal_ == Modal::None && !offline_ && client_->auth_failed()) { modal_ = Modal::Reauth; reauth_pass_.clear(); reauth_msg_.clear(); }
    if (modal_ != Modal::None) { uin.in = saved; draw_modal(W, H); }
}

void App::draw_header(float W) {
    Ui& u = *ui_;
    const float s = S_;
    const Theme& th = u.th;
    const bool mob = W / s <= 767.0f;
    Rect hdr{0, 0, W, (mob ? 56.0f : static_cast<float>(skin::kHeaderH)) * s};
    gfx_->fill(hdr, th.panel);
    gfx_->fill({0, hdr.h, W, 1 * s}, {0, 0, 0, 77});   // elevation-2 shadow, approximated by three fading rows
    gfx_->fill({0, hdr.h + s, W, 2 * s}, {0, 0, 0, 38});
    gfx_->fill({0, hdr.h + 3 * s, W, 2 * s}, {0, 0, 0, 16});
    // brand
    const bool brand_text = true;
    if (mob) {   // .header (compact): 56 px, padding 0 12, logo + 20 px title; search, Remote, Mini and the links are hidden
        svg(0, {12 * s, 14 * s, 28 * s, 28 * s}, {255, 255, 255, 255});
        gfx_->set_letter_spacing(-0.5f * s);
        T(600, 20, 52 * s, 10 * s, 36, "Xylonic", th.accent);
        gfx_->set_letter_spacing(0);
    } else {
        svg(0, {24 * s, 18 * s, 28 * s, 28 * s}, {255, 255, 255, 255});
        gfx_->set_letter_spacing(-0.5f * s);
        T(600, 24, 64 * s, 14 * s, 36, "Xylonic", th.accent);
        gfx_->set_letter_spacing(0);
    }
    (void)brand_text;
    auto round_btn = [&](Rect r, float hover_r) {
        if (u.hover(r)) gfx_->fill_round(r, hover_r, th.hover);
        return u.clicked(r);
    };
    if (!mob) {
        Rect gh{155 * s, 10 * s, 44 * s, 44 * s};
        if (round_btn(gh, 22 * s)) SDL_OpenURL("https://github.com/BeanGreen247/xylonic-fiber");
        svg(1, {gh.x + 13 * s, gh.y + 13 * s, 18 * s, 18 * s}, th.sub);
        Rect sup{215 * s, 10 * s, 44 * s, 44 * s};
        if (round_btn(sup, 22 * s)) SDL_OpenURL("https://github.com/sponsors/BeanGreen247");
        fa(skin::Heart, sup.x + 22 * s, sup.y + 22 * s, 18, th.sub);
    }

    if (!mob) {
    // centre group: search, Remote, Mini (measured: 200 + 12 + 116 + 12 + 95, centred in the header centre area)
    const bool mid = W / s < 900;   // 768-899 px: the search shrinks to 160 and the group re-centres (measured on the original at 850 px)
    const float sw = (mid ? 160.0f : 200.0f) * s;
    float gx = mid ? ((259.0f + (W / s - 24 - 36)) / 2 - (160 + 12 + 116 + 12 + 95) / 2.0f) * s : 394 * s;
    Color edge = alpha(th.primary_light, 0.464f);
    Rect sb{gx, 14 * s, sw, 35 * s};
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
        if (!mob && nav_ != Nav::Search && search_q_.empty() && u.focus != kSearch) {
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
    {
        const Rect rb{gx + sw + 12 * s, 10 * s, 116 * s, 43 * s}, mb{rb.x + rb.w + 12 * s, 10 * s, 95 * s, 43 * s};
        if (pill(rb, skin::Satellite, remote_mode_ ? "Remote \xE2\x97\x8F" : "Remote")) modal_ = Modal::Remote;
        const bool has_song = queue_.current() != nullptr;           // Mini Player is disabled until a song is loaded
        if (pill(mb, skin::Compress, "Mini") && has_song) toggle_mini();
        if (!has_song) gfx_->fill_round(mb, 21.5f * s, {th.panel.r, th.panel.g, th.panel.b, 150});
    }
    }
    // .header-right: offline toggle (globe; plane + "Offline" when active), then the Gaming/Eco badge
    {
        float right = W - (mob ? 12.0f : 24.0f) * s;
        const float hy = ((mob ? 56.0f : 64.0f) - 36.0f) / 2 * s;
        if (cfg_.perf_mode != 0) {
            const bool gaming = cfg_.perf_mode == 1;
            Rect bd{right - 36 * s, hy, 36 * s, 36 * s};
            const Color col = gaming ? rgb(0xff9f0a) : rgb(0x1db954);
            gfx_->circle(bd.x + 18 * s, bd.y + 18 * s, 18 * s, alpha(col, 0.12f));
            gfx_->ring(bd.x + 18 * s, bd.y + 18 * s, 18 * s, 1 * s, alpha(col, 0.35f));
            fa(gaming ? 0xf11bu : 0xf06cu, bd.x + 18 * s, bd.y + 18 * s, 15, col);
            right -= 36 * s + 12 * s;
        }
        const int fl = F(600, 12);
        const float lw = offline_ ? gfx_->measure(fl, "Offline") + 6 * s : 0;
        const float gw = offline_ ? 10 * s + 14 * s + lw + 12 * s : 36 * s;
        Rect gl{right - gw, hy, gw, 36 * s};
        if (offline_) {
            gfx_->fill_round(gl, 18 * s, alpha(th.accent, u.hover(gl) ? 0.22f : 0.14f));
            gfx_->stroke_round(gl, 18 * s, 1 * s, alpha(th.accent, 0.45f), th.panel);
            fa(0xf072, gl.x + 10 * s + 7 * s, gl.y + 18 * s, 14, th.accent);
            gfx_->set_letter_spacing(0.3f * s);
            T(600, 12, gl.x + 10 * s + 20 * s, gl.y, 36, "Offline", th.accent);
            gfx_->set_letter_spacing(0);
        } else {
            if (u.hover(gl)) gfx_->fill_round(gl, 18 * s, th.hover);
            fa(skin::Globe, gl.x + 18 * s, gl.y + 18 * s, 14, th.sub);
        }
        if (u.clicked(gl)) toggle_offline();
    }
}

void App::draw_hamburger(float W, float H) {
    Ui& u = *ui_;
    const float s = S_;
    const Theme& th = u.th;
    const float mw = 320 * s;
    const float mh = 10 * 48 * s + 16 * s;
    Rect menu{std::max(8 * s, W - mw - 8 * s), skin::kHeaderH * s + 8 * s, mw, std::min(mh, H - skin::kHeaderH * s - 16 * s)};
    if (u.in.escape || (u.in.pressed && !menu.contains(u.in.mx, u.in.my))) { hamburger_open_ = false; return; }
    gfx_->fill_round({menu.x - 1, menu.y - 1, menu.w + 2, menu.h + 2}, 13 * s, th.border);
    gfx_->fill_round(menu, 12 * s, th.bg_light);
    struct Item { uint32_t icon; const char* label; int action; };
    static const Item items[] = {
        {skin::Satellite, "Remote devices", 0}, {skin::Compress, "Mini player", 1},
        {skin::Download, "Downloads", 2}, {skin::Gear, "Settings", 3},
        {skin::CircleQuestion, "Keyboard help", 4}, {skin::Globe, "Offline mode", 5},
        {skin::ListUl, "Queue", 7}, {skin::History, "Recently played", 8}, {skin::Music, "Playlists", 9},
        {skin::SignOut, "Sign out", 6}
    };
    for (int i = 0; i < 10; ++i) {
        Rect row{menu.x + 8 * s, menu.y + 8 * s + i * 48 * s, menu.w - 16 * s, 44 * s};
        if (u.hover(row)) gfx_->fill_round(row, 9 * s, th.hover);
        fa(items[i].icon, row.x + 24 * s, row.y + 22 * s, 16, th.accent);
        T(500, 14, row.x + 50 * s, row.y, 44, items[i].label, th.text);
        if (!u.clicked(row)) continue;
        hamburger_open_ = false;
        switch (items[i].action) {
        case 0: modal_ = Modal::Remote; break;
        case 1: toggle_mini(); break;
        case 2: set_nav(Nav::Downloads); break;
        case 3: set_nav(Nav::Settings); break;
        case 4: show_help_ = true; break;
        case 5: toggle_offline(); break;
        case 6: logout(); break;
        case 7: toggle_panel(PanelTab::Queue); break;
        case 8: toggle_panel(PanelTab::History); break;
        case 9: toggle_panel(PanelTab::Playlists); break;
        default: break;
        }
        mark();
        return;
    }
}

void App::draw_nav(float H) {
    Ui& u = *ui_;
    const float s = S_;
    const Theme& th = u.th;
    const bool expanded = nav_expanded_;
    const float top = skin::kHeaderH * s + banner_h_;
    const float nw = (expanded ? 200.0f : skin::kNavW) * s;
    gfx_->fill({0, top, nw, H - top}, th.panel);
    struct Item { Nav n; uint32_t icon; const char* tip; };
    static const Item up[] = {{Nav::Home, skin::House, "Home"}, {Nav::Library, skin::BookOpen, "Library"}, {Nav::Downloads, skin::Download, "Downloads"}};
    auto item = [&](float y, uint32_t icon, bool active, const char* label) {
        Rect r{expanded ? 8 * s : 6 * s, y, expanded ? nw - 16 * s : 52 * s, 41 * s};
        if (active) gfx_->fill_round(r, 12 * s, alpha(th.accent, 0.15f));
        else if (u.hover(r)) gfx_->fill_round(r, 12 * s, th.hover);
        fa(icon, expanded ? r.x + 24 * s : r.x + r.w / 2, r.y + r.h / 2, 17, active ? th.accent : th.sub);
        if (expanded) T(500, 14, r.x + 50 * s, r.y, 41, label, active ? th.accent : th.sub);
        return u.clicked(r);
    };
    // In collapsed mode the narrow top strip is an unobtrusive expand hit area
    // so the 1024px reference layout stays unchanged. Expanded mode shows the
    // same chevron control and "Collapse" label as the Electron AppNav.
    Rect collapse{expanded ? 8 * s : 0, top + 6 * s, expanded ? nw - 16 * s : nw, expanded ? 36 * s : 10 * s};
    if (expanded) {
        if (u.hover(collapse)) gfx_->fill_round(collapse, 9 * s, th.hover);
        fa(skin::ChevLeft, collapse.x + 24 * s, collapse.y + 18 * s, 12, th.sub);
        T(500, 13, collapse.x + 50 * s, collapse.y, 36, "Collapse", th.sub);
        if (u.clicked(collapse)) { nav_expanded_ = false; mark(); return; }
    } else if (u.clicked(collapse)) { nav_expanded_ = true; mark(); return; }
    const float first_y = expanded ? top + 52 * s : top + 16 * s;
    float y = first_y;
    for (const Item& it : up) {
        bool on = (nav_ == Nav::Search ? search_prev_nav_ : nav_) == it.n;
        if (item(y, it.icon, on, it.tip)) set_nav(it.n);
        y += 43 * s;
    }
    float by = H - 16 * s - 41 * s;
    if (item(by, skin::SignOut, false, "Logout")) logout();
    if (item(by - 43 * s, skin::CircleQuestion, show_help_, "Help")) show_help_ = !show_help_;
    if (item(by - 86 * s, skin::Gear, nav_ == Nav::Settings, "Settings")) set_nav(Nav::Settings);
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
    // remote mode: show the controlled device's state instead of the local player
    bool have_remote = false, rplaying = false;
    std::string rtitle, rartist, rcover;
    float rdur = 0, rpos = 0;
    if (remote_mode_ && remote_)
        for (auto& d : remote_->devices())
            if (d.id == remote_target_) {
                have_remote = true; rplaying = d.playing; rdur = static_cast<float>(d.duration);
                rpos = static_cast<float>(d.current_time + (d.playing ? (static_cast<double>(std::time(nullptr)) * 1000 - static_cast<double>(d.last_seen_ms)) / 1000.0 : 0.0));
                if (!d.song_json.empty()) {
                    Song rs; if (from_json(d.song_json, rs)) { rtitle = rs.title; rartist = rs.artist; rcover = rs.cover_art; }
                }
            }
    bool playing_now = have_remote ? rplaying : player_.playing();
    // progress
    float dur = have_remote ? rdur : static_cast<float>(player_.duration_seconds()), pos = have_remote ? std::min(rpos, rdur) : static_cast<float>(player_.position_seconds());
    Rect pb{x0, r.y + 7 * s, w, 6 * s};
    float v = dur > 0 ? std::min(1.0f, pos / dur) : 0;
    bool was = u.active == kSeek;
    if (was) v = seek_drag_v_;
    Rect hit_r{x0, r.y + 2 * s, w, 16 * s};
    if (u.in.pressed && hit_r.contains(u.in.mx, u.in.my)) u.active = kSeek;
    if (u.active == kSeek && (u.in.down || u.in.released)) {
        float nv = std::min(1.0f, std::max(0.0f, (u.in.mx - pb.x) / pb.w));
        seek_drag_v_ = nv; v = nv;
        if (u.in.released) { u.active = 0; if (dur > 0) { if (have_remote) remote_send("seek", "{\"time\":" + std::to_string(nv * dur) + "}"); else player_.seek_seconds(nv * dur); } }
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
    if (have_remote) {
        Rect cv{info.x, info.y, 44 * s, 44 * s};
        if (SDL_Texture* t = cover(rcover, 44)) gfx_->image_round(t, cv, 8 * s); else gfx_->fill_round(cv, 8 * s, th.panel2);
        T(500, 14, info.x + 56 * s, info.y + 1 * s, 21, rtitle, th.text, 144 * s);
        T(400, 11, info.x + 56 * s, info.y + 26 * s, 17, rartist, th.accent, 144 * s);
    } else if (cur) {
        Rect cv{info.x, info.y, 44 * s, 44 * s};
        if (SDL_Texture* t = cover(cur->cover_art, 44)) gfx_->image_round(t, cv, 8 * s);
        else gfx_->fill_round(cv, 8 * s, th.panel2);
        if (u.clicked({info.x, info.y, 200 * s, 44 * s})) { np_open_ = true; mark(); }
        T(500, 14, info.x + 56 * s, info.y + 1 * s, 21, cur->title, th.text, 144 * s);
        T(400, 11, info.x + 56 * s, info.y + 26 * s, 17, track_loading_ ? "Loading..." : !play_err_.empty() ? play_err_ : cur->artist, play_err_.empty() ? th.sub : th.danger, 144 * s);
    }
    // right: speed, quality, volume
    float right = x0 + w - 10 * s;
    // Queue, History, Playlists panel toggles (same tabs as the Q/H/P keys), far right of the bar.
    // Narrower windows keep the 1024x768 reference layout; the same panels are in the menu and on Q/H/P.
    if ((r.x + r.w) / s >= 1100.0f) {
        const struct { uint32_t icon; PanelTab tab; } tabs[3] = {{skin::ListUl, PanelTab::Queue}, {skin::History, PanelTab::History}, {skin::Music, PanelTab::Playlists}};
        for (int i = 0; i < 3; ++i) {
            Rect b{right - (3 - i) * 34 * s + 2 * s, cy + 7 * s, 30 * s, 30 * s};
            const bool on = panel_open_ && panel_tab_ == tabs[i].tab;
            if (on) gfx_->fill_round(b, 8 * s, alpha(th.accent, 0.16f));
            else if (u.hover(b)) gfx_->fill_round(b, 8 * s, th.hover);
            fa(tabs[i].icon, b.x + b.w / 2, b.y + b.h / 2, 14, on ? th.accent : th.sub);
            if (u.clicked(b)) toggle_panel(tabs[i].tab);
        }
        right -= 108 * s;
    }
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
    if (u.clicked(qual)) { quality_popup_ = !quality_popup_; speed_popup_ = false; mark(); }
    if (quality_popup_) {
        static const int qualities[] = {0, 320, 256, 192, 128, 64};
        static const char* labels[] = {"Original", "320 kbps", "256 kbps", "192 kbps", "128 kbps", "64 kbps"};
        const float pw = 164 * s, ih = 34 * s, ph = 6 * ih + 8 * s;
        Rect pop{qual.x + qual.w - pw, qual.y - 6 * s - ph, pw, ph};
        gfx_->fill_round({pop.x - 1, pop.y - 1, pop.w + 2, pop.h + 2}, 13 * s, th.border);
        gfx_->fill_round(pop, 12 * s, th.bg_light);
        for (int i = 0; i < 6; ++i) {
            Rect ir{pop.x + 4 * s, pop.y + 4 * s + i * ih, pop.w - 8 * s, ih};
            const bool on = cfg_.stream_quality == qualities[i];
            if (u.hover(ir)) gfx_->fill_round(ir, 8 * s, th.hover);
            if (on) fa(skin::Check, ir.x + 18 * s, ir.y + ih / 2, 11, th.accent);
            T(on ? 600 : 400, 13, ir.x + 36 * s, ir.y, ih / s, labels[i], on ? th.accent : th.text);
            if (u.clicked(ir)) {
                cfg_.stream_quality = qualities[i];
                cfg_.save(cfg_path_);
                quality_popup_ = false;
                mark();
            }
        }
        if (u.in.pressed && !pop.contains(u.in.mx, u.in.my) && !qual.contains(u.in.mx, u.in.my)) quality_popup_ = false;
    }
    bool muted = vol_ <= 0.001f;
    Rect sp{vol.x, vol.y, 24 * s, 24 * s};
    fa(skin::VolumeHigh, sp.x + 12 * s, sp.y + 12 * s, 14, muted ? th.muted : th.sub);
    if (u.clicked(sp)) set_volume(muted ? 0.8f : 0);
    Rect vs{vol.x + 32 * s, vol.y + 10 * s, 48 * s, 4 * s};
    float vv = vol_;
    Rect vhit{vs.x, vol.y, vs.w, vol.h};
    if (u.in.pressed && vhit.contains(u.in.mx, u.in.my)) u.active = kVol;
    if (u.active == kVol && (u.in.down || u.in.released)) { vv = std::min(1.0f, std::max(0.0f, (u.in.mx - vs.x) / vs.w)); set_volume(vv); if (have_remote && u.in.released) remote_send("setVolume", "{\"volume\":" + std::to_string(vv) + "}"); if (u.in.released) u.active = 0; }
    gfx_->fill_round(vs, 2 * s, {255, 255, 255, 46});
    gfx_->fill_round({vs.x, vs.y, std::max(4 * s, vs.w * vol_), vs.h}, 2 * s, th.accent);

    // centre: transport, centred between the info block and the right controls
    float gap_l = info.x + 200 * s, gap_r = right - 366 * s;  // right already excludes the panel toggles
    float mid = (gap_l + gap_r) / 2 + 3 * s;  // group centre: measured 464 at 1024 wide
    auto cbtn = [&](float cx, uint32_t ic, bool on) {
        Rect b{cx - 16.5f * s, cy + 12 * s, 33 * s, 27 * s};
        if (u.hover(b)) gfx_->fill_round(b, 13 * s, th.hover);
        fa(ic, b.x + b.w / 2, b.y + b.h / 2, 13.3f, on ? th.accent : th.sub);
        return u.clicked(b);
    };
    if (cbtn(mid - 102.5f * s, skin::Shuffle, queue_.shuffle())) { if (have_remote) remote_send("toggleShuffle"); else queue_.set_shuffle(!queue_.shuffle()); }
    if (cbtn(mid - 67.5f * s, skin::StepBack, false)) prev_track();
    Rect pp{mid - 41 * s, cy + 2 * s, 40 * s, 40 * s};
    gfx_->circle(pp.x + 20 * s, pp.y + 20 * s, 20 * s, u.hover(pp) ? rgb(0xe8e8e8) : rgb(0xffffff));
    fa(playing_now ? skin::Pause : skin::Play, pp.x + 20 * s + (playing_now ? 0 : 1 * s), pp.y + 20 * s, 16, rgb(0x121212));
    if (u.clicked(pp)) toggle_play();
    if (cbtn(mid + 25.5f * s, skin::StepFwd, false)) next_track();
    if (cbtn(mid + 59.5f * s, skin::Repeat, queue_.repeat() != Repeat::Off) && have_remote) remote_send("toggleRepeat");
    else if (!have_remote && u.clicked({mid + 59.5f * s - 16.5f * s, cy + 12 * s, 33 * s, 27 * s}))
        queue_.set_repeat(queue_.repeat() == Repeat::Off ? Repeat::All : queue_.repeat() == Repeat::All ? Repeat::One : Repeat::Off);
    if (queue_.repeat() == Repeat::One) T(700, 8, mid + 59.5f * s + 4 * s, cy + 12 * s, 10, "1", th.accent);
    bool liked = cur && cur->starred;
    if (cbtn(mid + 102.5f * s, skin::Heart, liked) && cur && client_) {
        set_current_liked(!cur->starred);
    }
}

// Compact player bar (PlaybackControls at <=680 px): 2 px progress line, 40 px cover, title/artist, previous, 52 px play, next.
// Tapping it while a song is loaded opens Now Playing.
void App::draw_player_bar_mobile(Rect r) {
    Ui& u = *ui_;
    const float s = S_;
    const Theme& th = u.th;
    gfx_->fill(r, th.panel);
    gfx_->fill({r.x, r.y - 4 * s, r.w, 4 * s}, {0, 0, 0, 22});
    const Song* cur = queue_.current();
    const float dur = static_cast<float>(player_.duration_seconds()), pos = static_cast<float>(player_.position_seconds());
    gfx_->fill({r.x, r.y, r.w, 2 * s}, {255, 255, 255, 46});
    if (dur > 0) gfx_->fill({r.x, r.y, r.w * std::min(1.0f, pos / dur), 2 * s}, th.accent);
    const float row_y = r.y + 2 * s, row_h = 58 * s;
    const float right = r.x + r.w - 12 * s;
    Rect nb{right - 44 * s, row_y + 7 * s, 44 * s, 44 * s};
    Rect pb{nb.x - 8 * s - 52 * s, row_y + 3 * s, 52 * s, 52 * s};
    Rect vb{pb.x - 8 * s - 44 * s, row_y + 7 * s, 44 * s, 44 * s};
    Rect cv{r.x + 12 * s, row_y + 9 * s, 40 * s, 40 * s};
    const float tx = cv.x + cv.w + 12 * s, tw = vb.x - 12 * s - tx;
    if (cur) {
        if (SDL_Texture* t = cover(cur->cover_art, 40, cur->artist, cur->album)) gfx_->image_round(t, cv, 8 * s); else draw_art_fallback(cv, cur->cover_art, 8 * s);
        T(600, 13, tx, row_y + 11 * s, 19.5f, cur->title, th.text, tw);
        T(400, 11, tx, row_y + 30 * s, 16.5f, track_loading_ ? "Loading..." : !play_err_.empty() ? play_err_ : cur->artist, play_err_.empty() ? th.sub : th.danger, tw);
    } else {
        gfx_->fill_round(cv, 8 * s, th.panel2);
        fa(skin::Music, cv.x + cv.w / 2, cv.y + cv.h / 2, 16, {255, 255, 255, 90});
        T(600, 13, tx, row_y + 11 * s, 19.5f, "No song playing", th.text, tw);
        T(400, 11, tx, row_y + 30 * s, 16.5f, "\xE2\x80\x94", th.sub);
    }
    if (cur && u.clicked({r.x, r.y, vb.x - r.x, r.h})) { np_open_ = true; mark(); }
    auto round_btn = [&](Rect b, uint32_t icon, float px, bool enabled) {
        if (enabled && u.hover(b)) gfx_->fill_round(b, b.w / 2, th.hover);
        fa(icon, b.x + b.w / 2, b.y + b.h / 2, px, enabled ? th.text : alpha(th.text, 0.4f));
        return enabled && u.clicked(b);
    };
    if (round_btn(vb, skin::StepBack, 18, cur != nullptr)) prev_track();
    const bool playing = player_.playing();
    gfx_->circle(pb.x + 26 * s, pb.y + 26 * s, 26 * s, cur ? (u.hover(pb) ? rgb(0xe8e8e8) : rgb(0xffffff)) : rgb(0x2d2d2d));
    fa(playing ? skin::Pause : skin::Play, pb.x + 26 * s + (playing ? 0 : 1 * s), pb.y + 26 * s, 20, cur ? rgb(0x121212) : alpha(th.text, 0.35f));
    if (cur && u.clicked(pb)) toggle_play();
    if (round_btn(nb, skin::StepFwd, 18, cur != nullptr)) next_track();
}

// MobileBottomNav: Home (disabled offline), Library, Search, Queue (submenu: Queue / History / Playlists), Settings.
void App::draw_bottom_nav(float W, float H) {
    Ui& u = *ui_;
    const float s = S_;
    const Theme& th = u.th;
    Rect r{0, H - 56 * s, W, 56 * s};
    gfx_->fill({r.x, r.y - 4 * s, r.w, 4 * s}, {0, 0, 0, 40});
    gfx_->fill(r, th.panel);
    gfx_->fill({r.x, r.y, r.w, 1 * s}, {255, 255, 255, 33});
    static const struct { PanelTab tab; uint32_t icon; const char* label; } panel_items[3] = {{PanelTab::Queue, skin::ListUl, "Queue"}, {PanelTab::History, skin::History, "History"}, {PanelTab::Playlists, skin::Music, "Playlists"}};
    int active_tab = 0; for (int i = 0; i < 3; ++i) if (panel_items[i].tab == panel_tab_) active_tab = i;
    const float iw = W / 5;
    auto item = [&](int i, uint32_t icon, const char* label, bool active, bool disabled) {
        Rect ir{r.x + i * iw, r.y, iw, r.h};
        const Color col = disabled ? alpha(th.sub, 0.35f) : active ? th.accent : th.sub;
        fa(icon, ir.x + iw / 2, ir.y + 21 * s, 20, col);
        const int f = F(500, 10);
        gfx_->set_letter_spacing(0.2f * s);
        gfx_->text(f, ir.x + (iw - gfx_->measure(f, label)) / 2, ir.y + 33 * s, label, col);
        gfx_->set_letter_spacing(0);
        return !disabled && u.clicked(ir);
    };
    const bool searching = nav_ == Nav::Search && !panel_open_;
    if (item(0, skin::House, "Home", nav_ == Nav::Home && !panel_open_ && !searching && views_.size() <= 1, offline_)) { panel_open_ = false; bottom_menu_open_ = false; set_nav(Nav::Home); }
    if (item(1, skin::BookOpen, "Library", nav_ == Nav::Library && !panel_open_ && !searching, false)) { panel_open_ = false; bottom_menu_open_ = false; set_nav(Nav::Library); }
    if (item(2, skin::Search, "Search", searching, false)) { panel_open_ = false; bottom_menu_open_ = false; set_nav(Nav::Search); ui_->focus = kSearch; }
    if (item(3, panel_open_ ? panel_items[active_tab].icon : skin::ListUl, panel_open_ ? panel_items[active_tab].label : "Queue", panel_open_ || bottom_menu_open_, false)) bottom_menu_open_ = !bottom_menu_open_;
    if (item(4, skin::Gear, "Settings", nav_ == Nav::Settings && !panel_open_, false)) { panel_open_ = false; bottom_menu_open_ = false; set_nav(Nav::Settings); }
    if (bottom_menu_open_) {   // .mobile-nav-submenu: 148+ wide, 3 rows of 43, 8 px above the nav, centred over the Queue button
        const float mw = 148 * s, rh = 43 * s;
        Rect m{r.x + 3 * iw + iw / 2 - mw / 2, r.y - 8 * s - rh * 3, mw, rh * 3};
        m.x = std::min(m.x, W - mw - 4 * s);
        gfx_->fill_round({m.x - 1, m.y - 1, m.w + 2, m.h + 2}, 12 * s, {255, 255, 255, 61});
        gfx_->fill_round(m, 12 * s, th.panel);
        bool picked = false;
        for (int i = 0; i < 3; ++i) {
            Rect ir{m.x, m.y + i * rh, m.w, rh};
            const bool on = panel_open_ && panel_tab_ == panel_items[i].tab;
            if (u.hover(ir)) gfx_->fill(ir, th.hover);
            if (i) gfx_->fill({ir.x, ir.y, ir.w, 1 * s}, {255, 255, 255, 15});
            fa(panel_items[i].icon, ir.x + 16 * s + 9 * s, ir.y + rh / 2, 16, on ? th.accent : th.sub);
            T(500, 14, ir.x + 16 * s + 28 * s, ir.y, 43, panel_items[i].label, on ? th.accent : th.sub);
            if (u.clicked(ir)) { toggle_panel(panel_items[i].tab); picked = true; }
        }
        if (picked || (u.in.pressed && !m.contains(u.in.mx, u.in.my) && !Rect{r.x + 3 * iw, r.y, iw, r.h}.contains(u.in.mx, u.in.my))) { bottom_menu_open_ = false; mark(); }
    }
}

void App::set_current_liked(bool on) {
    const Song* cur = queue_.current();
    if (!cur || !client_) return;
    worker_->post([c = client_, id = cur->id, on] { std::string e; c->set_star(id, on, e); });
    const_cast<Song*>(cur)->starred = on;
    if (store_) store_->set_liked(cur->id, on);
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
        // DiscoverView offline: a single centred message (.discover-offline: padding 80, gap 16, icon 52)
        const float cx = r.x + r.w / 2, top = r.y + (mobile_ ? 12.0f : 24.0f) * s + 80 * s;
        fa(0xf05e, cx, top + 26 * s, 52, {th.sub.r, th.sub.g, th.sub.b, 150});
        const std::string m = "Discover requires an online connection";
        T(400, 16, cx - gfx_->measure(F(400, 16), m) / 2, top + (52 + 16) * s, 24, m, th.sub);
        return;
    }
    // Compact (<=767 px): main-content padding 12, h2 24 px, 180x133 cards (5 px padding, 170x100 cover, 11 px name only), no arrows.
    const bool cp = mobile_;
    const float pad = (cp ? 12.0f : static_cast<float>(skin::kContentPad)) * s;
    float cw = (cp ? 180.0f : skin::kCardW) * s, ch = (cp ? 133.0f : skin::kCardH) * s, gap = (cp ? 24.0f : skin::kCardGap) * s;
    const float row_h = (cp ? 148.0f : 254.0f) * s;
    float total = (cp ? 52.0f : 80.0f) * s + 4 * (32 * s + 16 * s + row_h + 48 * s);
    if (u.hover(r)) discover_scroll_.y -= u.in.wheel * 60 * s;
    discover_scroll_.clamp(total + 2 * pad, r.h);
    float x = r.x + pad, y = r.y + pad - discover_scroll_.y, w = r.w - 2 * pad - 12 * s;  // 12px scrollbar gutter
    // title row
    if (cp) {
        gfx_->circle(x + 14 * s, y + 18 * s, 13 * s, th.accent);
        fa(skin::Compass, x + 14 * s, y + 18 * s, 17, th.accent_text);
        gfx_->set_letter_spacing(-0.24f * s);
        T(700, 24, x + 40 * s, y, 36, "Discover", th.text);
        gfx_->set_letter_spacing(0);
        y += 52 * s;
    } else {
        gfx_->circle(x + 18 * s, y + 24 * s, 17 * s, th.accent);
        fa(skin::Compass, x + 18 * s, y + 24 * s, 22, th.accent_text);
        gfx_->set_letter_spacing(-0.32f * s);
        T(700, 32, x + 52 * s, y, 48, "Discover", th.text);
        gfx_->set_letter_spacing(0);
        y += 80 * s;
    }
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
        Rect row{x, y, w, row_h};
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
                gfx_->fill_round(card, (cp ? 12.0f : 16.0f) * s, th.panel);
                if (hov) gfx_->fill_round(card, (cp ? 12.0f : 16.0f) * s, th.hover);
                if (cp) {   // cover object-fit: cover into 170x100: the square art is cropped through a clip
                    Rect cov{cx + 5 * s, y + 5 * s, 170 * s, 100 * s};
                    const float cx0 = std::max(cov.x, row.x), cx1 = std::min(cov.x + cov.w, row.x + row.w);
                    Rect vis{cx0, cov.y, std::max(0.0f, cx1 - cx0), cov.h};
                    gfx_->clip(&vis);
                    Rect sq{cov.x, cov.y - 35 * s, 170 * s, 170 * s};
                    if (SDL_Texture* t = cover(a.cover_art, 170, a.artist, a.name)) gfx_->image_round(t, sq, 4 * s);
                    else draw_art_fallback(sq, a.cover_art, 4 * s);
                    gfx_->clip(&row);
                    const int fn = F(600, 11);
                    T(600, 11, cov.x + std::max(0.0f, (cov.w - std::min(gfx_->measure(fn, a.name), cov.w)) / 2), y + 109 * s, 17, a.name, th.text, cov.w);
                } else {
                Rect cov{cx + 16 * s, y + 16 * s, 148 * s, 148 * s};
                if (SDL_Texture* t = cover(a.cover_art, 148, a.artist, a.name)) gfx_->image_round(t, cov, 4 * s);
                else draw_art_fallback(cov, a.cover_art, 4 * s);
                int fn = F(600, 15.2f);
                float nw = gfx_->measure(fn, a.name);
                T(600, 15.2f, cov.x + std::max(0.0f, (cov.w - std::min(nw, cov.w)) / 2), y + 176 * s, 23, a.name, th.text, cov.w);
                std::string sub = (a.year ? std::to_string(a.year) + " \xC2\xB7 " : std::string()) + a.artist;
                T(400, 13.6f, cov.x, y + 203 * s, 20, sub, th.sub, cov.w);
                }
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
            if (!cp && sec.scroll_x > 1) arrow(x + 6 * s, skin::ChevLeft, -560 * s);
            if (!cp && sec.scroll_x < max_sx - 1) arrow(x + w - 42 * s, skin::ChevRight, 560 * s);
            // wheel + shift scrolls the row under the cursor horizontally
            if (row.contains(u.in.mx, u.in.my) && u.in.hwheel != 0) sec.scroll_x -= u.in.hwheel * 60 * s;
        }
        y += row_h + 48 * s;
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
