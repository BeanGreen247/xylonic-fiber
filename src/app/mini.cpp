// Mini player, redrawn as the classic Winamp 2.x main window (275x116 units: title bar, seven-segment time, spectrum, scrolling
// title, volume, seek bar and the six transport buttons). Desktop: an always-on-top window with its own renderer that hides the
// main window. Phones have no second window, so the same widget is a full-screen overlay (mini_open_).
#include <algorithm>
#include <cmath>
#include "app.h"
#include "android_bridge.h"

namespace {
Color lerp_c(Color a, Color b, float t) { auto m = [&](uint8_t x, uint8_t y) { return static_cast<uint8_t>(x + (y - x) * t); }; return {m(a.r, b.r), m(a.g, b.g), m(a.b, b.b), 255}; }
Color rgbf(uint32_t v) { return {static_cast<uint8_t>(v >> 16), static_cast<uint8_t>(v >> 8), static_cast<uint8_t>(v), 255}; }
const uint8_t kSeg[10] = {0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f};   // a b c d e f g bits
}  // namespace

bool App::mini_available() const {
#if defined(XF_MOBILE)
    return !mobile_;   // phones only: a Winamp-sized widget makes no sense there; tablets, laptops and desktops get it
#else
    return true;
#endif
}

// The classic Winamp 2.x main window drawn from the active theme: the body, bevels and LCD take their colours from the theme
// tokens (accent for the LCD, spectrum and title bar), so every preset, light/dark mode and custom theme restyles it.
bool App::draw_winamp(Rect area) {
    Ui& u = *ui_;
    const Theme& th = u.th;
    const float s = S_;
    const float k = area.w / 275.0f;      // physical pixels per Winamp unit
    const float c = k / s;                // the same, in the CSS pixels T() and fa() take
    auto X = [&](float x) { return area.x + x * k; };
    auto Y = [&](float y) { return area.y + y * k; };
    auto R = [&](float x, float y, float w, float h) { return Rect{X(x), Y(y), w * k, h * k}; };
    const float edge = std::max(1.0f, 0.6f * k);
    // palette
    const Color white = rgbf(0xffffff), black = rgbf(0x000000);
    const Color body_hi = lerp_c(th.panel2, th.accent, 0.16f), body_lo = lerp_c(th.bg, th.accent, 0.06f);
    const Color lcd_bg = lerp_c(black, th.accent, 0.04f);
    const Color lcd_on = lerp_c(th.accent, white, 0.25f), lcd_mid = lerp_c(th.accent, black, 0.25f), lcd_off = lerp_c(lcd_bg, th.accent, 0.13f), lcd_dim = lerp_c(lcd_bg, th.accent, 0.38f);
    const Color face_hi = lerp_c(th.light ? white : th.panel2, white, th.light ? 0.0f : 0.62f), face_lo = lerp_c(th.panel2, th.accent, 0.2f);
    const Color ink = th.light ? th.text : lerp_c(th.bg, black, 0.3f), shade = lerp_c(th.bg, black, 0.6f);
    const Color tb_hi = lerp_c(th.accent, white, 0.1f), tb_lo = lerp_c(th.accent, black, 0.62f);
    bool close = false;
    const Song* cur = queue_.current();
    const uint32_t now = SDL_GetTicks();
    const bool playing = player_.playing();
    double dur = player_.duration_seconds(), pos = player_.position_seconds();
    if (u.active == kSeek && dur > 0) pos = seek_drag_v_ * dur;
    auto face = [&](Rect r, bool pressed, bool hot) {   // a raised bevelled key
        gfx_->fill_round(r, 1.5f * k, shade);
        const Color a = pressed ? face_lo : hot ? lerp_c(face_hi, white, 0.25f) : face_hi, b = pressed ? lerp_c(face_lo, black, 0.2f) : face_lo;
        gfx_->quad_gradient({r.x + edge, r.y + edge, r.w - 2 * edge, r.h - 2 * edge}, a, a, b, b);
        gfx_->fill({r.x + edge, r.y + edge, r.w - 2 * edge, std::max(1.0f, 0.5f * k)}, {255, 255, 255, static_cast<uint8_t>(pressed ? 20 : 120)});
    };
    // body
    gfx_->quad_gradient(area, body_hi, body_hi, body_lo, body_lo);
    gfx_->fill({area.x, area.y, area.w, edge}, lerp_c(body_hi, white, 0.35f));
    gfx_->fill({area.x, area.y + area.h - edge, area.w, edge}, shade);
    gfx_->fill({area.x, area.y, edge, area.h}, lerp_c(body_hi, white, 0.2f));
    gfx_->fill({area.x + area.w - edge, area.y, edge, area.h}, shade);
    // title bar: the grooved bar with a centred logotype and the three window buttons
    gfx_->quad_gradient(R(0, 0, 275, 14), tb_hi, tb_hi, tb_lo, tb_lo);
    for (int i = 0; i < 4; ++i) { gfx_->fill(R(5, 3 + i * 2.5f, 104, 0.8f), {255, 255, 255, 70}); gfx_->fill(R(5, 3.8f + i * 2.5f, 104, 0.8f), {0, 0, 0, 90}); }
    for (int i = 0; i < 4; ++i) { gfx_->fill(R(166, 3 + i * 2.5f, 78, 0.8f), {255, 255, 255, 70}); gfx_->fill(R(166, 3.8f + i * 2.5f, 78, 0.8f), {0, 0, 0, 90}); }
    { gfx_->set_letter_spacing(0.5f * k); const float w = gfx_->measure(F(800, 8 * c), "XYLONIC"); T(800, 8 * c, area.x + (275 * k - w) / 2, Y(1.5f), 10 * c, "XYLONIC", white); gfx_->set_letter_spacing(0); }
    auto tiny = [&](float x, int kind, const char* tip) {
        Rect r = R(x, 3, 9, 9);
        u.tip(r, tip);
        face(r, u.hover(r) && u.in.down, u.hover(r));
        const Color g = ink;
        const float lw = std::max(1.0f, 0.9f * k);
        if (kind == 0) gfx_->fill({r.x + 2 * k, r.y + 6 * k, 5 * k, 1.2f * k}, g);
        else if (kind == 1) gfx_->fill({r.x + 2 * k, r.y + 2.5f * k, 5 * k, 1.4f * k}, g);   // shade: a thin bar
        else { gfx_->line(r.x + 2 * k, r.y + 2 * k, r.x + 7 * k, r.y + 7 * k, lw, g); gfx_->line(r.x + 7 * k, r.y + 2 * k, r.x + 2 * k, r.y + 7 * k, lw, g); }
        return u.clicked(r);
    };
    if (tiny(244, 0, "Back to the main window")) close = true;
    if (tiny(254, 1, winamp_shade_ ? "Restore" : "Windowshade")) { winamp_shade_ = !winamp_shade_; mini_resize(); mark(); }
    if (tiny(264, 2, "Close")) close = true;
    if (winamp_shade_) {   // windowshade: just the title bar, with the time and title squeezed in
        const int sec = static_cast<int>(std::max(0.0, pos));
        char tb[16] = {}; std::snprintf(tb, sizeof tb, "%d:%02d", sec / 60, sec % 60);
        T(700, 7 * c, X(6), Y(2.5f), 9 * c, tb, white);
        const std::string t = cur ? cur->title : "XYLONIC";
        Rect tp = R(34, 2, 70, 10);
        gfx_->clip(&tp); T(600, 7 * c, tp.x, Y(2.5f), 9 * c, t, white); gfx_->clip(nullptr);
        if (playing) u.wants_frames = true;
        return close;
    }
    // LCD block (one dark panel: clutter bar, play state, time, spectrum)
    Rect lcd = R(10, 16, 97, 44);
    gfx_->fill(lcd, lcd_bg);
    gfx_->stroke(lcd, edge, shade);
    // clutter bar: O A I D V
    {
        const char* lbl[5] = {"O", "A", "I", "D", "V"};
        const char* tips[5] = {"Options: themes", "Always on top", "Track info", "Double size", "Visualisation mode"};
        const bool on[5] = {false, winamp_ontop_, winamp_info_, winamp_double_, winamp_vis_ != 2};
        for (int i = 0; i < 5; ++i) {
            Rect r = R(11, 17 + i * 8.4f, 8, 8);
            u.tip(r, tips[i]);
            if (u.hover(r)) gfx_->fill(r, {255, 255, 255, 30});
            T(800, 6 * c, r.x + (r.w - gfx_->measure(F(800, 6 * c), lbl[i])) / 2, r.y + 0.3f * k, 8 * c, lbl[i], on[i] ? lcd_on : lcd_dim);
            if (!u.clicked(r)) continue;
            if (i == 0) { close = true; modal_ = Modal::Theme; }
            else if (i == 1) { winamp_ontop_ = !winamp_ontop_; if (mini_win_) SDL_SetWindowAlwaysOnTop(mini_win_, winamp_ontop_ ? SDL_TRUE : SDL_FALSE); }
            else if (i == 2) winamp_info_ = !winamp_info_;
            else if (i == 3) { winamp_double_ = !winamp_double_; mini_resize(); }
            else winamp_vis_ = (winamp_vis_ + 1) % 3;
            mark();
        }
    }
    {
        const float ix = X(24), iy = Y(21);
        if (playing) gfx_->tri(ix, iy, ix, iy + 8 * k, ix + 6 * k, iy + 4 * k, lcd_on);
        else if (pos > 0.5) { gfx_->fill({ix, iy, 2 * k, 8 * k}, lcd_on); gfx_->fill({ix + 4 * k, iy, 2 * k, 8 * k}, lcd_on); }
        else gfx_->fill({ix, iy, 6 * k, 6 * k}, lcd_on);
    }
    // time (click flips elapsed / remaining)
    {
        Rect tr = R(34, 18, 60, 22);
        u.tip(tr, "Click: elapsed / remaining");
        if (u.clicked(tr)) { winamp_remaining_ = !winamp_remaining_; mark(); }
        const double shown = winamp_remaining_ && dur > 0 ? std::max(0.0, dur - pos) : std::max(0.0, pos);
        const int sec = static_cast<int>(shown), mm = std::min(99, sec / 60), ss = sec % 60;
        if (winamp_remaining_) gfx_->fill(R(36, 28, 4, 1.6f), lcd_on);   // the minus sign
        const int dgt[4] = {mm / 10, mm % 10, ss / 10, ss % 10};
        const float dx[4] = {42, 54, 70, 82};
        for (int i = 0; i < 4; ++i) {
            const uint8_t m = kSeg[dgt[i]];
            const float x = dx[i], y = 19, w = 9, h = 17, t = 1.8f;
            const Rect seg[7] = {{x + t, y, w - 2 * t, t}, {x + w - t, y + t, t, h / 2 - t}, {x + w - t, y + h / 2, t, h / 2 - t}, {x + t, y + h - t, w - 2 * t, t},
                                 {x, y + h / 2, t, h / 2 - t}, {x, y + t, t, h / 2 - t}, {x + t, y + h / 2 - t / 2, w - 2 * t, t}};
            for (int g = 0; g < 7; ++g) gfx_->fill(R(seg[g].x, seg[g].y, seg[g].w, seg[g].h), (m >> g) & 1 ? lcd_on : lcd_off);
        }
        gfx_->fill(R(66, 23.5f, 2, 2), lcd_on); gfx_->fill(R(66, 30, 2, 2), lcd_on);
    }
    // visualisation: bars with peak caps, a contour line, or off
    {
        Rect vis = R(24, 43, 76, 16);
        const bool want = winamp_vis_ != 2 && playing;
        player_.enable_spectrum(want || (car_open_ && cfg_.car_pulse));
        float raw[19] = {};
        const float dt = winamp_last_ms_ ? std::min(0.1f, static_cast<float>(now - winamp_last_ms_) / 1000.0f) : 0.05f;
        winamp_last_ms_ = now;
        if (want) player_.spectrum(raw, 19);
        const float up = 1.0f - std::exp(-dt * 30.0f), dn = 1.0f - std::exp(-dt * 8.0f);
        float py = 0, px_ = 0;
        for (int i = 0; i < 19; ++i) {
            winamp_sm_[i] += (raw[i] - winamp_sm_[i]) * (raw[i] > winamp_sm_[i] ? up : dn);
            if (winamp_sm_[i] >= winamp_cap_[i]) { winamp_cap_[i] = winamp_sm_[i]; winamp_capv_[i] = 0; }
            else { winamp_capv_[i] += dt * 1.2f; winamp_cap_[i] = std::max(winamp_sm_[i], winamp_cap_[i] - winamp_capv_[i] * dt); }
            if (winamp_vis_ == 2) continue;
            const int hpx = static_cast<int>(std::min(1.0f, winamp_sm_[i]) * 16);
            if (winamp_vis_ == 0) {
                for (int y = 0; y < hpx; ++y) {   // one lit row per pixel: low rows deep, high rows bright (the classic ramp, in the theme's colours)
                    const float f = static_cast<float>(y) / 15.0f;
                    const Color col = f < 0.5f ? lerp_c(lcd_mid, th.accent, f * 2) : lerp_c(th.accent, th.accent2_light, (f - 0.5f) * 2);
                    gfx_->fill(R(24 + i * 4, 58 - y, 3, 1), f > 0.85f ? lerp_c(col, th.danger, 0.55f) : col);
                }
                const int cy = static_cast<int>(std::min(1.0f, winamp_cap_[i]) * 16);
                if (cy > 0) gfx_->fill(R(24 + i * 4, 58 - std::min(15, cy), 3, 1), lerp_c(th.text, lcd_on, 0.3f));
            } else {
                const float x = X(25.5f + i * 4), y = Y(58.5f - std::min(1.0f, winamp_sm_[i]) * 15);
                if (i > 0) gfx_->line(px_, py, x, y, std::max(1.0f, 0.9f * k), lcd_on);
                px_ = x; py = y;
            }
        }
        if (u.clicked(vis)) { winamp_vis_ = (winamp_vis_ + 1) % 3; mark(); }
    }
    // marquee + technical readout
    {
        Rect tp = R(111, 24, 154, 11);
        gfx_->fill(tp, lcd_bg);
        gfx_->stroke(tp, edge, shade);
        gfx_->clip(&tp);
        std::string label = cur ? (cur->artist.empty() ? cur->title : cur->artist + " - " + cur->title) : "XYLONIC";
        if (cur && winamp_info_) {
            label = cur->title + " *** " + cur->artist + " *** " + cur->album;
            if (cur->year) label += " (" + std::to_string(cur->year) + ")";
            if (!cur->suffix.empty()) { std::string sf = cur->suffix; for (auto& ch : sf) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch))); label += " *** " + sf; }
        } else if (cur && !cur->album.empty()) label += "  ***  " + cur->album;
        const int f = F(500, 8.5f * c);
        const float tw = gfx_->measure(f, label), gap = 24 * k;
        if (tw <= tp.w - 4 * k) T(500, 8.5f * c, tp.x + 2 * k, tp.y + 0.5f * k, 10 * c, label, lcd_on);
        else {
            const float off = std::fmod(static_cast<float>(now) / 1000.0f * 22 * k, tw + gap);
            T(500, 8.5f * c, tp.x - off, tp.y + 0.5f * k, 10 * c, label, lcd_on);
            T(500, 8.5f * c, tp.x - off + tw + gap, tp.y + 0.5f * k, 10 * c, label, lcd_on);
        }
        gfx_->clip(nullptr);
        const std::string kb = cur && cur->bit_rate > 0 ? std::to_string(cur->bit_rate) : "";
        const std::string khz = cur && cur->sampling_rate > 0 ? std::to_string(cur->sampling_rate / 1000) : "";
        T(700, 7 * c, X(111), Y(40), 9 * c, kb, lcd_on);
        T(500, 5.5f * c, X(131), Y(41), 8 * c, "kbps", lcd_mid);
        T(700, 7 * c, X(156), Y(40), 9 * c, khz, lcd_on);
        T(500, 5.5f * c, X(171), Y(41), 8 * c, "khz", lcd_mid);
        const bool st = cur && cur->channels != 1;
        T(700, 6.5f * c, X(212), Y(40.5f), 9 * c, "mono", cur && !st ? lcd_on : lcd_dim);
        T(700, 6.5f * c, X(232), Y(40.5f), 9 * c, "stereo", st ? lcd_on : lcd_dim);
    }
    // volume + balance sliders
    auto groove = [&](Rect g, float v, bool tint) {
        gfx_->fill(g, lcd_bg);
        gfx_->stroke(g, edge, shade);
        if (tint) gfx_->quad_gradient({g.x + 1, g.y + 3 * k, (g.w - 2) * v, g.h - 6 * k}, lcd_mid, lerp_c(lcd_mid, th.accent2_light, v), lerp_c(lcd_mid, th.accent2_light, v), lcd_mid);
        Rect kn{g.x + (g.w - 14 * k) * v, g.y - 0.5f * k, 14 * k, g.h + 1 * k};
        face(kn, false, u.hover(g));
    };
    {
        Rect g = R(107, 57, 68, 13);
        if (u.in.pressed && g.contains(u.in.mx, u.in.my)) u.active = kVol;
        if (u.active == kVol && (u.in.down || u.in.released)) { set_volume((u.in.mx - g.x - 7 * k) / (g.w - 14 * k)); if (u.in.released) { u.active = 0; cfg_.save(cfg_path_); } }
        { static char vt[24]; std::snprintf(vt, sizeof vt, "Volume: %d%%", static_cast<int>(vol_ * 100 + 0.5f)); u.tip(g, vt); }
        groove(g, vol_, true);
        groove(R(177, 57, 38, 13), 0.5f, false);
    }
    auto key = [&](Rect r, const char* label, bool on) {
        face(r, u.hover(r) && u.in.down, u.hover(r));
        const float w = gfx_->measure(F(800, 7 * c), label);
        T(800, 7 * c, r.x + (r.w - w) / 2, r.y + 1.5f * k, 9 * c, label, on ? lerp_c(th.accent, black, 0.15f) : ink);
        return u.clicked(r);
    };
    if (key(R(219, 58, 23, 12), "EQ", cfg_.car_pulse)) { close = true; set_car_mode(true); }
    if (key(R(242, 58, 23, 12), "PL", false)) { close = true; toggle_panel(PanelTab::Queue); }
    // seek bar
    {
        Rect g = R(16, 72, 248, 10);
        Rect hit{g.x, g.y - 3 * k, g.w, g.h + 6 * k};
        float v = dur > 0 ? static_cast<float>(std::min(1.0, pos / dur)) : 0;
        if (u.in.pressed && hit.contains(u.in.mx, u.in.my)) u.active = kSeek;
        if (u.active == kSeek && (u.in.down || u.in.released)) {
            const float nv = std::min(1.0f, std::max(0.0f, (u.in.mx - g.x - 14.5f * k) / (g.w - 29 * k)));
            seek_drag_v_ = nv; v = nv;
            if (u.in.released) { u.active = 0; if (dur > 0) player_.seek_seconds(nv * dur); }
        }
        gfx_->fill(g, lcd_bg);
        gfx_->stroke(g, edge, shade);
        if (cur && dur > 0) face({g.x + (g.w - 29 * k) * v, g.y, 29 * k, g.h}, u.active == kSeek, u.hover(hit));
    }
    // transport
    auto tbtn = [&](float x, float w, int icon, bool on, const char* tip) {
        Rect r = R(x, 88, w, 18);
        u.tip(r, tip);
        face(r, u.hover(r) && u.in.down, u.hover(r));
        const float cx = r.x + r.w / 2, cy = r.y + r.h / 2, q = 4 * k;
        const Color g = on ? lerp_c(th.accent, black, 0.15f) : ink;
        switch (icon) {
        case 0: gfx_->fill({cx - 4.5f * k, cy - q, 1.4f * k, 2 * q}, g); gfx_->tri(cx + 4 * k, cy - q, cx + 4 * k, cy + q, cx - 3 * k, cy, g); break;
        case 1: gfx_->tri(cx - 3 * k, cy - q, cx - 3 * k, cy + q, cx + 4 * k, cy, g); break;
        case 2: gfx_->fill({cx - 3.5f * k, cy - q, 2.4f * k, 2 * q}, g); gfx_->fill({cx + 1.1f * k, cy - q, 2.4f * k, 2 * q}, g); break;
        case 3: gfx_->fill({cx - 3.5f * k, cy - 3.5f * k, 7 * k, 7 * k}, g); break;
        case 4: gfx_->fill({cx + 3.1f * k, cy - q, 1.4f * k, 2 * q}, g); gfx_->tri(cx - 4 * k, cy - q, cx - 4 * k, cy + q, cx + 3 * k, cy, g); break;
        default: gfx_->tri(cx - 4 * k, cy + 0.5f * k, cx + 4 * k, cy + 0.5f * k, cx, cy - 3.5f * k, g); gfx_->fill({cx - 4 * k, cy + 2 * k, 8 * k, 1.6f * k}, g); break;
        }
        return u.clicked(r);
    };
    if (tbtn(16, 23, 0, false, "Previous")) prev_track();
    if (tbtn(39, 23, 1, playing, "Play")) { if (!playing) toggle_play(); }
    if (tbtn(62, 23, 2, false, "Pause")) { if (playing) toggle_play(); }
    if (tbtn(85, 23, 3, false, "Stop")) { player_.pause(); player_.seek_seconds(0); mark(); }
    if (tbtn(108, 22, 4, false, "Next")) next_track();
    if (tbtn(136, 22, 5, false, "Back to the main window")) close = true;
    if (key(R(164, 89, 47, 15), "SHUFFLE", queue_.shuffle())) queue_.set_shuffle(!queue_.shuffle());
    if (key(R(210, 89, 28, 15), queue_.repeat() == Repeat::One ? "REP 1" : "REP", queue_.repeat() != Repeat::Off))
        queue_.set_repeat(queue_.repeat() == Repeat::Off ? Repeat::All : queue_.repeat() == Repeat::All ? Repeat::One : Repeat::Off);
    T(800, 9 * c, X(250), Y(90), 12 * c, "XF", th.accent);
    if (playing) u.wants_frames = true;
    return close;
}

void App::mini_resize() {
    if (!mini_win_) return;
    const int w = winamp_double_ ? 550 : 413, h = static_cast<int>((winamp_shade_ ? 14 : 116) * (w / 275.0f) + 0.5f);
    SDL_SetWindowSize(mini_win_, w, h);
}

void App::toggle_mini() {
    if (!mini_available()) return;
#if defined(__ANDROID__)
    if (mini_open_) { mini_open_ = false; player_.enable_spectrum(false); mark(); return; }
    if (!queue_.current()) return;
    mini_open_ = true; np_open_ = false; panel_open_ = false; car_open_ = false;
    android_bridge::set_landscape(false);
    mark();
    return;
#endif
    if (mini_win_) { close_mini(); return; }
    if (!queue_.current()) return;  // "A song must be playing to open Mini Player"
    mini_win_ = SDL_CreateWindow("Xylonic", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 413, 174, SDL_WINDOW_ALWAYS_ON_TOP | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!mini_win_) return;
    mini_ren_ = SDL_CreateRenderer(mini_win_, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!mini_ren_) mini_ren_ = SDL_CreateRenderer(mini_win_, -1, SDL_RENDERER_SOFTWARE);
    if (!mini_ren_) { SDL_DestroyWindow(mini_win_); mini_win_ = nullptr; return; }
    SDL_SetRenderDrawBlendMode(mini_ren_, SDL_BLENDMODE_BLEND);
    mini_gfx_ = std::make_unique<Gfx>();
    mini_gfx_->init(mini_win_, mini_ren_);
    mini_ui_ = std::make_unique<Ui>(*mini_gfx_);
    mini_fonts_.clear();
    mini_id_ = SDL_GetWindowID(mini_win_);
    if (win_) SDL_HideWindow(win_);
    mark();
}

void App::close_mini() {
    if (!mini_win_) return;
    mini_ui_.reset();
    mini_gfx_.reset();
    if (mini_ren_) SDL_DestroyRenderer(mini_ren_);
    SDL_DestroyWindow(mini_win_);
    mini_ren_ = nullptr;
    mini_win_ = nullptr;
    mini_id_ = 0;
    player_.enable_spectrum(car_open_ && cfg_.car_pulse);
    if (win_) { SDL_ShowWindow(win_); SDL_RaiseWindow(win_); }
    mark();
}

void App::draw_mini() {
    if (!mini_win_) return;
    std::swap(gfx_, mini_gfx_);
    std::swap(ui_, mini_ui_);
    std::swap(font_ids_, mini_fonts_);
    Ui& u = *ui_;
    u.tip_text.clear();
    u.th = ui_theme_cache_;
    int dw = 0, dh = 0;
    SDL_GetRendererOutputSize(mini_ren_, &dw, &dh);
    const float W = static_cast<float>(dw), H = static_cast<float>(dh);
    gfx_->begin_frame({0, 0, 0, 255});
    const float uh = winamp_shade_ ? 14.0f : 116.0f;
    const float k = std::min(W / 275.0f, H / uh);
    const bool close = draw_winamp({(W - 275 * k) / 2, (H - uh * k) / 2, 275 * k, uh * k});
    draw_tooltip(W, H);
    gfx_->end_frame();
    u.in.new_frame();
    std::swap(gfx_, mini_gfx_);
    std::swap(ui_, mini_ui_);
    std::swap(font_ids_, mini_fonts_);
    if (close) close_mini();
}

void App::handle_mini(const SDL_Event& e) {
    Input& in = mini_ui_->in;
    switch (e.type) {
    case SDL_WINDOWEVENT:
        if (e.window.event == SDL_WINDOWEVENT_CLOSE) close_mini();
        else mark();
        break;
    case SDL_MOUSEMOTION: in.mx = e.motion.x * M_; in.my = e.motion.y * M_; mark(); break;
    case SDL_MOUSEBUTTONDOWN: if (e.button.button == SDL_BUTTON_LEFT) { in.mx = e.button.x * M_; in.my = e.button.y * M_; in.down = in.pressed = true; in.press_x = in.mx; in.press_y = in.my; mark(); } break;
    case SDL_MOUSEBUTTONUP: if (e.button.button == SDL_BUTTON_LEFT) { in.mx = e.button.x * M_; in.my = e.button.y * M_; in.down = false; in.released = true; mark(); } break;
    case SDL_KEYDOWN:
        if ((e.key.keysym.mod & KMOD_CTRL) && e.key.keysym.sym == SDLK_m) close_mini();
        else if (e.key.keysym.sym == SDLK_SPACE) toggle_play();
        break;
    default: break;
    }
}
