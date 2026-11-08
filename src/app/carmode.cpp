// Car Mode (CarMode.tsx): landscape driving display. Layout measured at 1024x768; the Riverbero spectrum and the
// per-song EQ are not ported yet (see docs/port-map.md).
#include <algorithm>
#include <cmath>
#include <unistd.h>
#include "app.h"
#include "android_bridge.h"

namespace {
Color alpha(Color c, float a) { c.a = static_cast<uint8_t>(std::lround(a * 255)); return c; }
std::string mmss(int s) {
    if (s < 0) s = 0;
    char b[16] = {};
    std::snprintf(b, sizeof b, "%d:%02d", s / 60, s % 60);
    return b;
}
Color lerp_col(Color a, Color b, float t) { auto m = [&](uint8_t x, uint8_t y) { return static_cast<uint8_t>(x + (y - x) * t); }; return {m(a.r, b.r), m(a.g, b.g), m(a.b, b.b), 255}; }
std::string upper(std::string s) { for (auto& c : s) if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 32); return s; }
}  // namespace

void App::set_car_mode(bool on) {
    car_open_ = on;
    if (on) { rail_until_ = SDL_GetTicks() + 10000; rail_vis_ = 1; }   // the rail shows for the first 10 s so it is discoverable
    // Dashboard use is landscape; this is honoured on Android and ignored by desktop window managers.
    SDL_SetHint(SDL_HINT_ORIENTATIONS, on ? "LandscapeLeft LandscapeRight" : "LandscapeLeft LandscapeRight Portrait PortraitUpsideDown");
    if (!on) player_.enable_spectrum(false);
#if defined(__ANDROID__)
    android_bridge::set_immersive_lock(on);
    android_bridge::set_landscape(on);
    if (win_) SDL_SetWindowFullscreen(win_, on ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);   // immersive: hide the status and navigation bars while driving
#endif
    if (on && cfg_.car_keep_on) SDL_DisableScreenSaver(); else SDL_EnableScreenSaver();
    mark();
}

void App::draw_car_mode(float W, float H) {
    Ui& u = *ui_;
    const float s = S_;
    const Theme& th = u.th;
    const Song* cur = queue_.current();
    {
        const uint32_t tnow = SDL_GetTicks();
        if (bat_ms_ == 0 || tnow - bat_ms_ > 10000) { bat_ms_ = tnow ? tnow : 1; int pc = -1; bat_state_ = SDL_GetPowerInfo(nullptr, &pc); bat_pct_ = pc; }
    }
    const int bat_pct = bat_pct_;
    const SDL_PowerState bat_state = static_cast<SDL_PowerState>(bat_state_);
    const bool has_bat = bat_pct >= 0 && bat_state != SDL_POWERSTATE_NO_BATTERY && bat_state != SDL_POWERSTATE_UNKNOWN;
    // The controls live in a rail down the right edge so they never cost vertical space in landscape; the layouts use the rest.
    const float t0 = std::min(1.0f, std::max(0.0f, (W / s - 568.0f) / 456.0f));
    const float rgap = 6 * s, rpad = 6 * s;
    const bool eq_dirty = cfg_.car_pulse && !pulse_off_ && (eq_bass_db_ != 0 || eq_treble_db_ != 0);
    const int nbtn = eq_dirty ? 6 : 5;
    const float rb = std::min((40 + 8 * t0) * s, (H - 2 * rpad - (has_bat ? 24 * s : 0) - (nbtn - 1) * rgap) / nbtn);
    const float rail_w = rb + 2 * rpad;
    // Riverbero wants every pixel: its rail hides and slides in over the content (swipe in from the right edge, or tap the edge); the plain
    // layouts keep the rail docked. It stays out for 10 s after it is revealed or touched.
    const bool autohide = cfg_.car_pulse && !pulse_off_;
    const float Wfull = W;
    const uint32_t tnow2 = SDL_GetTicks();
    if (autohide) {
        // The reveal swipe must not start on the very edge pixels: there the system treats it as "show the navigation bar". It starts in the strip
        // 14 px .. 110 px in from the edge (the handle sits there) and moves left; a tap on the handle works too.
        const float e0 = 14 * s, e1 = 110 * s;   // and only beside the handle (vertically), so EQ shaping works everywhere else on the stage
        if (u.in.pressed && u.in.mx >= Wfull - e1 && u.in.mx <= Wfull - e0 && std::fabs(u.in.my - H / 2) < 110 * s) { rail_swipe_ = true; rail_sx_ = u.in.mx; rail_sy_ = u.in.my; }
        if (rail_swipe_ && u.in.down && rail_sx_ - u.in.mx > 22 * s) { rail_until_ = tnow2 + 10000; rail_swipe_ = false; }
        if (rail_swipe_ && u.in.released) {
            const bool on_handle = std::fabs(u.in.my - H / 2) < 60 * s && u.in.mx >= Wfull - 40 * s;
            if (on_handle && std::fabs(rail_sx_ - u.in.mx) < 12 * s) rail_until_ = tnow2 + 10000;
            rail_swipe_ = false;
        }
    } else rail_vis_ = 1;
    const bool rail_want = !autohide || static_cast<int32_t>(rail_until_ - tnow2) > 0;
    {
        const float dt = rail_last_ms_ ? std::min(0.1f, static_cast<float>(tnow2 - rail_last_ms_) / 1000.0f) : 0.016f;
        rail_last_ms_ = tnow2;
        if (autohide) { rail_vis_ = std::min(1.0f, std::max(0.0f, rail_vis_ + (rail_want ? 1.0f : -1.0f) * dt / 0.18f)); if (rail_want || (rail_vis_ > 0.0f && rail_vis_ < 1.0f)) u.wants_frames = true; }   // keep ticking so the 10 s expiry repaints while paused
    }
    const float rail_ease = 1.0f - (1.0f - rail_vis_) * (1.0f - rail_vis_);
    float rail_x = autohide ? Wfull - rail_w * rail_ease : W - rail_w;
    if (!autohide) W = rail_x;   // docked: everything below lays out in the space left of the rail
    Input rail_saved = u.in;   // while the overlay rail is out its strip must not also click what lies under it
    if (autohide && rail_vis_ > 0.5f && u.in.mx >= Wfull - rail_w) { u.in.mx = u.in.my = -1000; u.in.pressed = u.in.released = false; u.in.down = false; }
    const float wc = W / s;
    const float tt = std::min(1.0f, std::max(0.0f, (wc - 568.0f) / 456.0f));   // 0 at a 568 px wide phone, 1 at 1024 and up (the original's clamp() steps)
    auto L = [&](float a, float b) { return (a + (b - a) * tt) * s; };
    const bool minimal = cfg_.car_minimal == 1 || (cfg_.car_minimal == 0 && H / s <= 420);
    const bool pulse_on = cfg_.car_pulse && !pulse_off_;
    gfx_->fill({0, 0, W, H}, th.bg);
    float dur = static_cast<float>(player_.duration_seconds()), pos = static_cast<float>(player_.position_seconds());
    auto seek_bar = [&](Rect track, float hit_h, float thumb_r) {
        float v = dur > 0 ? std::min(1.0f, pos / dur) : 0;
        Rect hit{track.x - 12 * s, track.y + track.h / 2 - hit_h / 2, track.w + 24 * s, hit_h};
        if (u.in.pressed && hit.contains(u.in.mx, u.in.my)) u.active = kSeek;
        if (u.active == kSeek && (u.in.down || u.in.released)) {
            float nv = std::min(1.0f, std::max(0.0f, (u.in.mx - track.x) / track.w));
            seek_drag_v_ = nv; v = nv;
            if (u.in.released) { u.active = 0; if (dur > 0) player_.seek_seconds(nv * dur); }
        } else if (u.active == kSeek) v = seek_drag_v_;
        gfx_->fill_round(track, track.h / 2, {255, 255, 255, 20});
        if (v > 0) gfx_->fill_round({track.x, track.y, std::max(track.h, track.w * v), track.h}, track.h / 2, th.accent);
        gfx_->circle(track.x + track.w * v, track.y + track.h / 2, thumb_r * 1.3f, alpha(th.accent, 0.25f));
        gfx_->circle(track.x + track.w * v, track.y + track.h / 2, thumb_r, th.accent);
        return (u.active == kSeek && dur > 0) ? seek_drag_v_ * dur : pos;
    };
    auto icon_btn = [&](Rect r, uint32_t ic, bool active, float px) {
        float rad = r.h / 2;
        if (active) gfx_->fill_round(r, rad, alpha(th.accent, 0.14f));
        gfx_->stroke_round(r, rad, 1 * s, active ? alpha(th.accent, 0.45f) : alpha(th.text, 0.24f));
        if (u.hover(r)) gfx_->fill_round(r, rad, th.hover);
        fa(ic, r.x + r.w / 2, r.y + r.h / 2, px, active ? th.accent : alpha(th.text, 0.78f));
        return u.clicked(r);
    };
    // The EQ-bars icon of the Riverbero toggle: four rounded bars, as in the original SVG.
    auto eq_icon = [&](Rect r, bool active) {
        const float sz = r.h * 0.5f, x0 = r.x + (r.w - sz) / 2, y0 = r.y + (r.h - sz) / 2, u1 = sz / 24;
        const Color c = active ? th.accent : alpha(th.text, 0.78f);
        const float bx[4] = {3, 8.5f, 14, 19.5f}, by[4] = {12, 5, 9, 14}, bw[4] = {3.5f, 3.5f, 3.5f, 3};
        for (int i = 0; i < 4; ++i) gfx_->fill_round({x0 + bx[i] * u1, y0 + by[i] * u1, bw[i] * u1, (21 - by[i]) * u1}, bw[i] * u1 / 2, c);
    };
    // the rail: close, flip, layout, keep-awake, Riverbero (and Reset EQ when the EQ was shaped), battery at the bottom; true when closed
    auto draw_rail = [&]() {
        if (autohide && rail_vis_ <= 0.01f) {   // hidden: only a faint edge handle, and Escape still leaves
            gfx_->fill_round({Wfull - 22 * s, H / 2 - 30 * s, 5 * s, 60 * s}, 2.5f * s, alpha(th.text, 0.30f));
            if (u.in.escape) { set_car_mode(false); return true; }
            return false;
        }
        if (autohide) { u.in = rail_saved; if (u.in.pressed || u.in.released || u.in.down) { if (u.in.mx >= rail_x) rail_until_ = SDL_GetTicks() + 10000; } }
        gfx_->fill({rail_x, 0, Wfull - rail_x, H}, th.panel);
        gfx_->fill({rail_x, 0, 1 * s, H}, th.border_subtle);
        const float px = 17;
        float y = rpad;
        const float x = rail_x + rpad;
        if (icon_btn({x, y, rb, rb}, skin::Sync, car_flip_, px)) car_flip_ = !car_flip_;
        y += rb + rgap;
        if (icon_btn({x, y, rb, rb}, minimal ? 0xf065u : static_cast<uint32_t>(skin::Compress), minimal, px)) { cfg_.car_minimal = minimal ? 2 : 1; cfg_.save(cfg_path_); }
        y += rb + rgap;
        if (icon_btn({x, y, rb, rb}, 0xf0eb, cfg_.car_keep_on, px)) { cfg_.car_keep_on = !cfg_.car_keep_on; cfg_.save(cfg_path_); if (cfg_.car_keep_on) SDL_DisableScreenSaver(); else SDL_EnableScreenSaver(); }
        y += rb + rgap;
        Rect pb{x, y, rb, rb};
        if (icon_btn(pb, 0xf080, pulse_on, 0.1f)) { cfg_.car_pulse = !cfg_.car_pulse; rail_until_ = SDL_GetTicks() + 10000; pulse_off_ = false; pulse_quality_ = 2; pulse_frames_ = 0; pulse_toast_ms_ = 0; cfg_.save(cfg_path_); }
        eq_icon(pb, pulse_on);
        y += rb + rgap;
        if (eq_dirty) {
            u.tip({x, y, rb, rb}, "Reset EQ");
            if (icon_btn({x, y, rb, rb}, 0xf0e2, true, px)) { eq_bass_db_ = eq_treble_db_ = 0; player_.set_eq(0, 0); pulse_drag_ = false; }
        }
        const float cy_close = H - rpad - rb;   // close sits on the lower edge, the battery just above it
        if (icon_btn({x, cy_close, rb, rb}, skin::Xmark, false, px) || u.in.escape) { set_car_mode(false); return true; }
        if (has_bat) {   // battery percentage centred above the close button
            const std::string t = std::to_string(bat_pct) + "%";
            const float w = gfx_->measure(F(600, 12), t);
            T(600, 12, rail_x + (rail_w - w) / 2, cy_close - rgap - 18 * s, 18, t, alpha(th.text, 0.78f));
            if (bat_state == SDL_POWERSTATE_CHARGING) fa(skin::Bolt, rail_x + rail_w / 2, cy_close - rgap - 28 * s, 11, alpha(th.text, 0.78f));
        }
        return false;
    };
    // transport primitives
    auto ctl = [&](Rect r, uint32_t ic, float px, bool active) {
        if (u.hover(r)) gfx_->fill_round(r, r.h / 2, th.hover);
        fa(ic, r.x + r.w / 2, r.y + r.h / 2, px, active ? th.accent : alpha(th.text, 0.78f));
        return u.clicked(r);
    };
    auto play_btn = [&](Rect r, float px) {
        for (int i = 3; i >= 1; --i) gfx_->circle(r.x + r.w / 2, r.y + r.h / 2 + i * 2 * s, r.w / 2 + i * 5 * s, {0, 0, 0, 14});
        gfx_->circle(r.x + r.w / 2, r.y + r.h / 2, r.w / 2, u.hover(r) ? rgb(0xe8e8e8) : rgb(0xffffff));
        if (track_loading_) {   // spinner while the song loads (npo-spinner)
            gfx_->arc(r.x + r.w / 2, r.y + r.h / 2, px * s * 0.55f, 3 * s, static_cast<float>(SDL_GetTicks() % 900) / 900.0f * 6.2832f, 4.2f, rgb(0x121212));
            u.wants_frames = true;
        } else fa(player_.playing() ? skin::Pause : skin::Play, r.x + r.w / 2 + (player_.playing() ? 0 : 2 * s), r.y + r.h / 2, px, rgb(0x121212));
        if (u.clicked(r) && cur) toggle_play();
    };
    auto shuffle_b = [&](Rect r, float px) { if (ctl(r, skin::Shuffle, px, queue_.shuffle())) queue_.set_shuffle(!queue_.shuffle()); };
    auto repeat_b = [&](Rect r, float px) {
        if (ctl(r, skin::Repeat, px, queue_.repeat() != Repeat::Off)) queue_.set_repeat(queue_.repeat() == Repeat::Off ? Repeat::All : queue_.repeat() == Repeat::All ? Repeat::One : Repeat::Off);
        if (queue_.repeat() == Repeat::One) T(800, 9, r.x + (r.w - gfx_->measure(F(800, 9), "1")) / 2, r.y + r.h / 2 - 6.5f * s, 13, "1", th.accent);   // the 1 sits inside the arrow
    };
    auto like_b = [&](Rect r, float px) {
        if (ctl(r, skin::Heart, px, cur && cur->starred) && cur && client_) set_current_liked(!cur->starred);
    };
    auto prev_b = [&](Rect r, float px) { if (ctl(r, skin::StepBack, px, false)) prev_track(); };
    auto next_b = [&](Rect r, float px) { if (ctl(r, skin::StepFwd, px, false)) next_track(); };
    auto time_label = [&](float x, float y, float w, bool right, int sec, float px) {
        const std::string t = mmss(sec);
        const float tw = gfx_->measure(F(400, px), t);
        T(400, px, right ? x + w - tw : x, y, px * 1.5f, t, alpha(th.text, 0.6f));
    };
    auto track_text = [&](float x, float y, float w, float h1, float h2, bool centred, bool shout) {
        std::string a = cur ? cur->title : "Nothing playing", b = cur ? cur->artist : "";
        if (shout) { a = upper(a); b = upper(b); }
        gfx_->set_letter_spacing((shout ? 0.03f : 0.02f) * h1 * s);
        const float aw = std::min(gfx_->measure(F(800, h1, shout), a), w);
        T(800, h1, centred ? x + (w - aw) / 2 : x, y, h1 * 1.15f, a, th.text, w, shout);
        gfx_->set_letter_spacing(0);
        gfx_->set_letter_spacing((shout ? 0.05f : 0.0f) * h2 * s);
        const float bw = std::min(gfx_->measure(F(shout ? 700 : 600, h2, shout), b), w);
        T(shout ? 700 : 600, h2, centred ? x + (w - bw) / 2 : x, y + (h1 * 1.15f + 2) * s, h2 * 1.3f, b, th.accent, w, shout);
        gfx_->set_letter_spacing(0);
    };
    auto cover_box = [&](Rect af, float radius) {
        gfx_->fill_round(af, radius, alpha(th.text, 0.24f));
        Rect ai{af.x + 1 * s, af.y + 1 * s, af.w - 2 * s, af.h - 2 * s};
        gfx_->fill_round(ai, radius - 1 * s, th.panel);
        gfx_->quad_gradient({ai.x, ai.y, ai.w, ai.h * 0.5f}, alpha(th.accent, 0.07f), alpha(th.accent, 0.07f), alpha(th.accent, 0), alpha(th.accent, 0));
        if (cur) {
            SDL_Texture* t = cover(cur->cover_art, 900, "", "", true);   // the larger artwork variant so the dashboard view does not upscale the thumbnail
            if (!t) t = cover(cur->cover_art, 300);
            if (!t) t = cover(cur->cover_art, 164);
            if (t) gfx_->image_round(t, ai, radius - 1 * s);
            else fa(skin::Music, ai.x + ai.w / 2, ai.y + ai.h / 2, std::max(24.0f, ai.w / s / 4), alpha(th.text, 0.35f));
        } else fa(skin::Music, ai.x + ai.w / 2, ai.y + ai.h / 2, std::max(24.0f, ai.w / s / 4), alpha(th.text, 0.35f));
    };
    // The Riverbero dock: shuffle, previous, play, next, repeat, then like + title + artwork (or, minimal, the four main controls + title).
    auto deck_dock = [&](Rect d, bool min_dock) {
        gfx_->fill_round(d, 16 * s, {0, 0, 0, 128});
        gfx_->stroke_round(d, 16 * s, 1 * s, alpha(th.text, 0.24f));
        const float side = min_dock ? 44 * s : L(44, 76), big = min_dock ? 56 * s : L(56, 96), gap = min_dock ? 4 * s : L(4, 8);
        const float pad = min_dock ? 10 * s : L(17, 30.7f);
        float x = d.x + pad;
        const float cy = d.y + d.h / 2;
        auto box = [&](float w) { Rect r{x, cy - w / 2, w, w}; x += w + gap; return r; };
        const float ipx = min_dock ? 18 : L(20, 28) / s, ppx = min_dock ? 22 : L(24, 34) / s;
        shuffle_b(box(side), ipx);
        if (!min_dock) prev_b(box(side), ipx);
        play_btn(box(big), ppx);
        if (!min_dock) next_b(box(side), ipx);
        repeat_b(box(side), ipx);
        if (min_dock) like_b(box(side), ipx);
        float right = d.x + d.w - pad;
        if (!min_dock) {
            like_b(box(side), ipx);
            if (wc >= 720) { const float as = d.h - 2 * L(12, 12.3f); Rect art{right - as, cy - as / 2, as, as}; cover_box(art, 12 * s); right = art.x - 12 * s; }
        }
        if (autohide && has_bat) {   // the rail (and its battery readout) is hidden: show the charge at the right end of the pill
            const std::string bt = std::to_string(bat_pct) + "%";
            const float bw = gfx_->measure(F(700, 13), bt);
            const float bx = right - bw;
            T(700, 13, bx, cy - 10 * s, 20, bt, alpha(th.text, 0.78f));
            if (bat_state == SDL_POWERSTATE_CHARGING) fa(skin::Bolt, bx - 9 * s, cy, 12, th.accent);
            right = bx - (bat_state == SDL_POWERSTATE_CHARGING ? 22 : 12) * s;
        }
        const float tx = x + 10 * s;
        track_text(tx, cy - L(19, 33), std::max(60 * s, right - tx), L(18, 34) / s, L(13, 22) / s, false, false);
    };
    auto stage_and_dock = [&](float top, float bottom, bool min_dock) {
        const float mg = min_dock ? 6 * s : L(6, 14), side = autohide ? 6 * s : min_dock ? 10 * s : L(17, 30.7f);
        const float dh = min_dock ? 66 * s : L(70, 121.5f), db = min_dock ? 6 * s : L(6, 12.3f);
        Rect dock{side, bottom - db - dh, W - 2 * side, dh};
        Rect st{autohide ? 8 * s : min_dock ? 22.7f * s : L(22.7f, 41), top + (min_dock ? 8 * s : L(8, 15.4f)), 0, dock.y - mg - top - (min_dock ? 8 * s : L(8, 15.4f))};
        st.w = W - 2 * st.x;
        draw_pulse_deck(st);
        deck_dock(dock, min_dock);
    };
    bool pulse_toast = pulse_toast_ms_ > SDL_GetTicks();
    auto toast = [&] {
        if (!pulse_toast) return;
        const std::string m = "Riverbero turned off \xE2\x80\x94 this device is too slow for it. Tap the bars icon to try again.";
        const float w = std::min(W - 40 * s, gfx_->measure(F(500, 13), m) + 32 * s);
        Rect r{(W - w) / 2, H - 120 * s, w, 38 * s};
        gfx_->fill_round(r, 19 * s, {0, 0, 0, 215});
        T(500, 13, r.x + 16 * s, r.y, 38, m, th.text, w - 32 * s);
    };

    if (!minimal) {
        const float hh = 0, fh = L(52, 84);
        const float mt = hh, mb = H - fh;
        if (pulse_on) stage_and_dock(mt, mb, false);
        else {
            const float padx = L(14, 41), gapc = L(22.7f, 41), colw = (W - 2 * padx - gapc) / 2;
            const float lx = car_flip_ ? padx + colw + gapc : padx, ax = car_flip_ ? padx : padx + colw + gapc;
            const float mh = mb - mt;
            const float h1 = L(18, 42) / s, h2 = L(13, 24) / s;
            const float side = L(52, 76), big = L(64, 96);
            const float blockh = (h1 * 1.15f + 2 + h2 * 1.3f) * s + L(12, 24) + big + L(12, 24) + side;
            const float by = mt + (mh - blockh) / 2;
            track_text(lx, by, colw, h1, h2, false, true);
            const float ty = by + (h1 * 1.15f + 2 + h2 * 1.3f) * s + L(12, 24);
            prev_b({lx, ty + (big - side) / 2, side, side}, L(20, 28) / s);
            play_btn({lx + (colw - big) / 2, ty, big, big}, L(24, 34) / s);
            next_b({lx + colw - side, ty + (big - side) / 2, side, side}, L(20, 28) / s);
            const float ey = ty + big + L(12, 24);
            shuffle_b({lx, ey, side, side}, L(18, 24) / s);
            like_b({lx + (colw - side) / 2, ey, side, side}, L(18, 24) / s);
            repeat_b({lx + colw - side, ey, side, side}, L(18, 24) / s);
            const float asz = std::min(colw, mh - 2 * L(8, 24));
            cover_box({ax + (colw - asz) / 2, mt + (mh - asz) / 2, asz, asz}, 16 * s);
        }
        // footer (progress)
        const float fy = H - fh;
        const Color ft = {static_cast<uint8_t>((th.bg.r * 7 + th.accent.r) / 8 + 6), static_cast<uint8_t>((th.bg.g * 7 + th.accent.g) / 8 + 6), static_cast<uint8_t>((th.bg.b * 7 + th.accent.b) / 8 + 6), 255};
        gfx_->fill({0, fy, W, fh}, ft);
        gfx_->fill({0, fy, W, 1 * s}, th.border);
        const float fp = std::max(12 * s, std::min(28 * s, wc * 0.03f * s)), tw = L(34, 56);
        const float shown = seek_bar({fp + tw + 14 * s, fy + fh / 2 - L(5, 7), W - 2 * (fp + tw + 14 * s), L(10, 14)}, fh, L(11, 14.5f));
        time_label(fp, fy + fh / 2 - 10 * s, tw, false, static_cast<int>(shown), 13);
        time_label(W - fp - tw, fy + fh / 2 - 10 * s, tw, true, static_cast<int>(dur), 13);
        toast();
        if (draw_rail()) return;
        return;
    }
    // minimal: seek row on top, centred title/artist and the four controls, chrome along the bottom
    {
        const float sh = L(58.4f, 100), nb = L(54.4f, 96), mx = L(11.4f, 20.5f);
        const float shown = seek_bar({mx + nb + L(48, 80) + 0.0f, sh / 2 - L(5, 7), W - 2 * (mx + nb + L(48, 80)), L(10, 14)}, sh, L(11, 14.5f));
        prev_b({mx, (sh - nb) / 2, nb, nb}, L(22.4f, 38) / s);
        next_b({W - mx - nb, (sh - nb) / 2, nb, nb}, L(22.4f, 38) / s);
        time_label(mx + nb + L(8, 14), sh / 2 - 10 * s, L(34, 56), false, static_cast<int>(shown), 13);
        time_label(W - mx - nb - L(8, 14) - L(34, 56), sh / 2 - 10 * s, L(34, 56), true, static_cast<int>(dur), 13);
        const float cy0 = H;
        if (pulse_on) {
            stage_and_dock(sh + 2 * s, H, true);
        } else {
            const float side = L(48, 76), big = L(67.2f, 108), gap = L(25.6f, 32);
            const float h1 = L(28.8f, 64) / s, h2 = L(16, 32) / s;
            const float blockh = (h1 * 1.15f + 2 + h2 * 1.3f) * s + L(14, 30) + big;
            const float top = sh + (cy0 - sh - blockh) / 2;
            track_text(24 * s, top, W - 48 * s, h1, h2, true, true);
            const float cy = top + (h1 * 1.15f + 2 + h2 * 1.3f) * s + L(14, 30) + big / 2;
            const float row = 3 * side + big + 0 + 3 * gap;
            float x = (W - (side + gap + big + gap + side + gap + side)) / 2;
            (void)row;
            shuffle_b({x, cy - side / 2, side, side}, L(20, 30) / s); x += side + gap;
            play_btn({x, cy - big / 2, big, big}, L(25.6f, 40) / s); x += big + gap;
            repeat_b({x, cy - side / 2, side, side}, L(20, 30) / s); x += side + gap;
            like_b({x, cy - side / 2, side, side}, L(20, 30) / s);
        }
        toast();
        if (draw_rail()) return;
    }
}

// Riverbero: 16..48 log-spaced bands (one per ~22 px) with smoothed motion, falling peak caps, a bass-kick glow and an
// optional reflection and cover backdrop. Drag horizontally to shape bass (left third) / treble (right third) for the
// current song (dB readout while dragging); Reset EQ restores flat. If a frame costs over ~28 ms it first drops the
// reflection/backdrop, then the glow, and only then switches itself off.
void App::draw_pulse_deck(Rect r) {
    Ui& u = *ui_;
    const float s = S_;
    const Theme& th = u.th;
    const uint32_t now = SDL_GetTicks();
    const float dt = pulse_last_ms_ ? std::min(0.1f, static_cast<float>(now - pulse_last_ms_) / 1000.0f) : 0.033f;
    if (pulse_last_ms_) {
        pulse_avg_ms_ = pulse_avg_ms_ * 0.95f + static_cast<float>(render_ms_) * 0.05f;   // cost of a whole frame, not the loop interval
        if (++pulse_frames_ > 90 && pulse_avg_ms_ > 28.0f && player_.playing()) {
            pulse_frames_ = 0; pulse_avg_ms_ = 16.0f;
            if (pulse_quality_ > 0) --pulse_quality_;
            else { pulse_off_ = true; pulse_toast_ms_ = now + 6000; }
        }
    }
    pulse_last_ms_ = now;
    player_.enable_spectrum(true);
    const int nb = 24;   // BAR_COUNT in CarMode.tsx
    float raw[48] = {};
    player_.spectrum(raw, nb);
    const bool playing = player_.playing();
    const float t = static_cast<float>(now) / 1000.0f;
    const float k_up = 1.0f - std::exp(-dt * 40.0f), k_dn = 1.0f - std::exp(-dt * 9.0f);
    for (int i = 0; i < nb; ++i) {
        float v = raw[i];
        if (!playing) v = 0.05f + 0.025f * std::sin(t * 1.6f + i * 0.45f);   // idle breathing
        pulse_sm_[i] += (v - pulse_sm_[i]) * (v > pulse_sm_[i] ? k_up : k_dn);
        if (pulse_sm_[i] >= pulse_caps_[i]) { pulse_caps_[i] = pulse_sm_[i]; pulse_capv_[i] = 0; }
        else { pulse_capv_[i] += dt * 1.6f; pulse_caps_[i] = std::max(pulse_sm_[i], pulse_caps_[i] - pulse_capv_[i] * dt); }
    }
    const int nbass = std::max(2, nb / 8);
    float bass = 0;
    for (int i = 0; i < nbass; ++i) bass += pulse_sm_[i];
    bass /= nbass;
    pulse_kick_ = std::max(pulse_kick_ * std::exp(-dt * 6.0f), std::min(1.0f, std::max(0.0f, bass - pulse_prev_bass_) * 5.0f));
    pulse_prev_bass_ = bass;

    // backdrop (the FlowingBackdrop: a near-black base with a hint of the accent, the cover as a dim wash, a bass-kick glow)
    gfx_->fill_round(r, 16 * s, lerp_col(rgb(0x05070a), th.accent, 0.06f));
    const Song* cur = queue_.current();
    SDL_Texture* art = cur ? cover(cur->cover_art, 300) : nullptr;
    gfx_->clip(&r);
    if (pulse_quality_now() >= 2 && art) {   // the cover wash keeps its aspect ratio: a square as wide as the panel (or as tall, if taller), centred, the overflow clipped
        const float sz = std::max(r.w, r.h);
        gfx_->image(art, {r.x + (r.w - sz) / 2, r.y + (r.h - sz) / 2, sz, sz}, {255, 255, 255, 34});
    }
    gfx_->clip(nullptr);
    gfx_->stroke_round(r, 16 * s, 1 * s, alpha(th.text, 0.24f));
    gfx_->clip(&r);
    if (pulse_quality_now() >= 1) gfx_->radial(r.x + r.w / 2, r.y + r.h, r.w * (0.5f + bass * 0.3f + pulse_kick_ * 0.15f), alpha(th.accent, 0.10f + bass * 0.22f + pulse_kick_ * 0.18f), alpha(th.accent, 0));
    // chips: bass / both / treble, each a pill with the dB value
    auto chip = [&](float x, const std::string& txt, float a, bool right) {
        const float tw = gfx_->measure(F(700, 11), txt), w = tw + 20 * s, h = 23 * s;
        Rect c{right ? x - w : x, r.y + 4 * s, w, h};
        gfx_->fill_round(c, h / 2, {0, 0, 0, static_cast<uint8_t>(140 * a)});
        gfx_->stroke_round(c, h / 2, 1 * s, alpha(th.text, 0.24f * a));
        T(700, 11, c.x + 10 * s, c.y, 23, txt, alpha(th.text, 0.78f * a));
    };
    auto fmt_db = [](float db) { char b[24] = {}; std::snprintf(b, sizeof b, "%s%d dB", db > 0 ? "+" : db < 0 ? "\xE2\x88\x92" : "", static_cast<int>(std::fabs(db))); return std::string(b); };
    chip(r.x + 8 * s, "Bass " + fmt_db(eq_bass_db_), 1.0f, false);
    chip(r.x + r.w - 8 * s, "Treble " + fmt_db(eq_treble_db_), 1.0f, true);
    { const float mw = gfx_->measure(F(700, 11), "Both") + 20 * s; chip(r.x + (r.w - mw) / 2, "Both", 0.6f, false); }

    // bars: bottom up #00bcd4 -> light cyan -> white, rounded tops, falling white peak caps, labels every 4th band
    const float lab = 28 * s, top = r.y + 32 * s;
    const bool refl = pulse_quality_now() >= 2;
    const float avail = r.y + r.h - lab - top;
    const float ph = (refl ? avail * 0.9f : avail) * 0.8f / 0.8f, base = top + ph;
    const float px = r.x + 8 * s, pw = r.w - 16 * s, bw = pw / nb;
    const float gap = std::max(2 * s, bw * 0.18f);
    for (int i = 0; i < nb; ++i) {
        const float v = std::min(1.0f, pulse_sm_[i]) * 0.8f;
        const float h = std::max(3 * s, v * ph);
        Rect b{px + i * bw + gap / 2, base - h, bw - gap, h};
        gfx_->quad_gradient(b, rgb(0xffffff), rgb(0xffffff), rgb(0x00bcd4), rgb(0x00bcd4));
        gfx_->quad_gradient({b.x, b.y + h * 0.3f, b.w, h * 0.7f}, th.accent2_light, th.accent2_light, th.accent, th.accent);
        const float cy = base - std::min(1.0f, pulse_caps_[i]) * 0.8f * ph;
        gfx_->fill({b.x, cy - 4 * s, b.w, 4 * s}, {255, 255, 255, 217});
        if (refl) {
            const float rh = std::min(h * 0.4f, r.y + r.h - lab - base);
            if (rh > 1) gfx_->quad_gradient({b.x, base + 2 * s, b.w, rh}, alpha(th.accent, 0.28f), alpha(th.accent, 0.28f), alpha(th.accent, 0), alpha(th.accent, 0));
        }
    }
    gfx_->fill({px, base, pw, 1 * s}, alpha(th.text, 0.18f));
    for (int i = 0; i < nb; i += 4) {
        const double f = 40.0 * std::pow(16000.0 / 40.0, (i + 0.5) / nb);
        char lb[16] = {};
        if (f >= 1000) std::snprintf(lb, sizeof lb, f >= 10000 ? "%.0fk" : "%.1fk", f / 1000.0); else std::snprintf(lb, sizeof lb, "%d", static_cast<int>(std::lround(f)));
        T(700, 10, px + i * bw, r.y + r.h - lab + 6 * s, 14, lb, alpha(th.text, 0.78f));
    }

    // drag to shape bass / treble for this song
    if (r.contains(u.in.mx, u.in.my) && u.in.pressed && !rail_swipe_) pulse_drag_ = true;   // a swipe that starts in the rail strip reveals the rail, it does not shape the EQ
    if (rail_swipe_) pulse_drag_ = false;
    if (pulse_drag_ && u.in.down) {
        const float fx = (u.in.mx - r.x) / r.w;
        float g = (0.5f - (u.in.my - r.y) / r.h) * 24.0f;   // +-12 dB over the height
        g = std::min(12.0f, std::max(-12.0f, g));
        if (fx < 0.34f) eq_bass_db_ = g; else if (fx > 0.66f) eq_treble_db_ = g; else { eq_bass_db_ = g; eq_treble_db_ = g; }
        player_.set_eq(eq_bass_db_, eq_treble_db_);
        gfx_->fill({r.x, u.in.my - 0.5f * s, r.w, 1 * s}, alpha(th.accent, 0.5f));
        gfx_->fill({r.x, r.y + r.h / 2 - 0.5f * s, r.w, 1 * s}, alpha(th.text, 0.18f));   // 0 dB
    }
    if (u.in.released) pulse_drag_ = false;
    gfx_->clip(nullptr);
}

int App::pulse_quality_now() const {
    const bool low = bat_pct_ >= 0 && bat_pct_ <= 20 && bat_state_ != SDL_POWERSTATE_CHARGING && bat_state_ != SDL_POWERSTATE_CHARGED;
    return low ? std::min(pulse_quality_, 1) : pulse_quality_;   // a nearly flat battery skips the reflection and the cover wash
}

uint32_t App::pulse_interval_ms() const {
    const int q = pulse_quality_now();
    return q >= 2 ? 33 : q == 1 ? 40 : 66;
}
