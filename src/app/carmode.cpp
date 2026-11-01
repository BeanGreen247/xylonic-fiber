// Car Mode (CarMode.tsx): landscape driving display. Layout measured at 1024x768; the Pulse Deck spectrum and the
// per-song EQ are not ported yet (see docs/port-map.md).
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
std::string upper(std::string s) { for (auto& c : s) if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 32); return s; }
}  // namespace

void App::set_car_mode(bool on) {
    car_open_ = on;
    if (!on) player_.enable_spectrum(false);
    if (on && cfg_.car_keep_on) SDL_DisableScreenSaver(); else SDL_EnableScreenSaver();
    mark();
}

void App::draw_car_mode(float W, float H) {
    Ui& u = *ui_;
    const float s = S_;
    const Theme& th = u.th;
    const Song* cur = queue_.current();
    bool minimal = cfg_.car_minimal == 1 || (cfg_.car_minimal == 0 && H / s < 480) || car_layout_min_;
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
    auto icon_btn = [&](Rect r, uint32_t ic, bool active) {
        float rad = r.h / 2;
        if (active) gfx_->fill_round(r, rad, alpha(th.accent, 0.14f));
        gfx_->stroke_round(r, rad, 1 * s, active ? alpha(th.accent, 0.45f) : alpha(th.text, 0.21f));
        if (u.hover(r)) gfx_->fill_round(r, rad, th.hover);
        fa(ic, r.x + r.w / 2, r.y + r.h / 2, 17, active ? th.accent : th.sub);
        return u.clicked(r);
    };
    auto battery = [&](float rx, float cy) {
        int pct = -1;
        SDL_PowerState st = SDL_GetPowerInfo(nullptr, &pct);
        if (pct < 0 || st == SDL_POWERSTATE_NO_BATTERY || st == SDL_POWERSTATE_UNKNOWN) return;
        std::string t = std::to_string(pct) + "%";
        float w = gfx_->measure(F(600, 13), t);
        T(600, 13, rx - w, cy - 10 * s, 20, t, th.sub);
        if (st == SDL_POWERSTATE_CHARGING) fa(skin::Bolt, rx - w - 12 * s, cy, 13, th.sub);
    };
    auto chrome = [&](float right, float y, float size) {
        float x = right;
        // order right-to-left: battery, pulse, keep-awake, layout, flip
        battery(x, y + size / 2);
        int pct = -1;
        SDL_PowerState st = SDL_GetPowerInfo(nullptr, &pct);
        if (pct >= 0 && st != SDL_POWERSTATE_NO_BATTERY && st != SDL_POWERSTATE_UNKNOWN) x -= (46 + 12) * s;
        if (icon_btn({x - size, y, size, size}, 0xf080, cfg_.car_pulse)) { cfg_.car_pulse = !cfg_.car_pulse; cfg_.save(cfg_path_); }
        x -= size + 12 * s;
        if (icon_btn({x - size, y, size, size}, 0xf0eb, cfg_.car_keep_on)) { cfg_.car_keep_on = !cfg_.car_keep_on; cfg_.save(cfg_path_); set_car_mode(true); }
        x -= size + 12 * s;
        if (icon_btn({x - size, y, size, size}, minimal ? 0xf065u : static_cast<uint32_t>(skin::Compress), false)) { car_layout_min_ = !car_layout_min_; if (cfg_.car_minimal == 1) cfg_.car_minimal = 2; }
        x -= size + 12 * s;
        if (icon_btn({x - size, y, size, size}, skin::Sync, car_flip_)) car_flip_ = !car_flip_;
    };
    auto transport = [&](float x, float y, float w_, float bs, float ps, float extra_y, bool show_extra) {
        // prev / play / next on the first row; shuffle / like / repeat on the second
        auto b = [&](Rect r, uint32_t ic, float px, Color c, bool active) {
            if (u.hover(r)) gfx_->fill_round(r, r.h / 2, th.hover);
            fa(ic, r.x + r.w / 2, r.y + r.h / 2, px, active ? th.accent : c);
            return u.clicked(r);
        };
        if (b({x, y + (ps - bs) / 2, bs, bs}, skin::StepBack, 30, th.text, false)) prev_track();
        Rect pp{x + (w_ - ps) / 2, y, ps, ps};
        for (int i = 3; i >= 1; --i) gfx_->circle(pp.x + ps / 2, pp.y + ps / 2 + i * 2 * s, ps / 2 + i * 6 * s, {0, 0, 0, 14});
        gfx_->circle(pp.x + ps / 2, pp.y + ps / 2, ps / 2, u.hover(pp) ? rgb(0xe8e8e8) : rgb(0xffffff));
        fa(player_.playing() ? skin::Pause : skin::Play, pp.x + ps / 2 + (player_.playing() ? 0 : 2 * s), pp.y + ps / 2, 36, rgb(0x121212));
        if (u.clicked(pp)) toggle_play();
        if (b({x + w_ - bs, y + (ps - bs) / 2, bs, bs}, skin::StepFwd, 30, th.text, false)) next_track();
        if (show_extra) {
            float ey = extra_y;
            if (b({x, ey, bs, bs}, skin::Shuffle, 26, th.sub, queue_.shuffle())) queue_.set_shuffle(!queue_.shuffle());
            bool liked = cur && cur->starred;
            if (b({x + (w_ - bs) / 2, ey, bs, bs}, skin::Heart, 26, th.sub, liked) && cur && client_) {
                bool on = !cur->starred;
                worker_->post([c = client_, id = cur->id, on] { std::string e; c->set_star(id, on, e); });
                const_cast<Song*>(queue_.current())->starred = on;
            }
            if (b({x + w_ - bs, ey, bs, bs}, skin::Repeat, 26, th.sub, queue_.repeat() != Repeat::Off))
                queue_.set_repeat(queue_.repeat() == Repeat::Off ? Repeat::All : queue_.repeat() == Repeat::All ? Repeat::One : Repeat::Off);
        }
    };

    if (!minimal) {
        // header
        gfx_->fill({0, 0, W, 64 * s}, th.panel);
        gfx_->fill({0, 64 * s - 1, W, 1 * s}, th.border_subtle);
        if (icon_btn({20 * s, 8 * s, 48 * s, 48 * s}, skin::Xmark, false) || u.in.escape) { set_car_mode(false); return; }
        chrome(W - 20 * s, 8 * s, 48 * s);
        // main
        float mt = 64 * s, mh = H - 64 * s - 84 * s;
        float colw = (W - 82 * s - 40 * s) / 2;
        float lx = (car_flip_ ? 41 * s + colw + 40 * s : 41 * s), ax = (car_flip_ ? 41 * s : 41 * s + colw + 40 * s);
        float side = std::min(colw, mh - 40 * s);
        // left block (290 tall at 1024x768) centred vertically
        float bh = 290 * s, by = mt + (mh - bh) / 2;
        if (cur) {
            std::string tt = upper(cur->title), at = upper(cur->artist);
            gfx_->set_letter_spacing(-0.4f * s);
            T(800, 42, lx, by, 48, tt, th.text, colw, true);
            gfx_->set_letter_spacing(0);
            T(700, 24, lx, by + 57 * s, 36, at, th.accent, colw, true);
        } else T(700, 24, lx, by, 36, "NO SONG PLAYING", th.sub, colw);
        transport(lx, by + 106 * s, colw, 76 * s, 96 * s, by + 214 * s, true);
        // art or Pulse Deck
        Rect af{ax + (colw - side) / 2, mt + (mh - side) / 2, side, side};
        if (cfg_.car_pulse && !pulse_off_) draw_pulse_deck(af);
        else {
        gfx_->fill_round(af, 16 * s, alpha(th.text, 0.21));
        Rect ai{af.x + 1 * s, af.y + 1 * s, af.w - 2 * s, af.h - 2 * s};
        gfx_->fill_round(ai, 15 * s, th.panel2);
        // Car Mode uses the larger artwork variant so the dashboard view does
        // not upscale the normal library thumbnail on high-DPI displays.
        if (cur) if (SDL_Texture* t = cover(cur->cover_art, 900)) gfx_->image_round(t, ai, 15 * s);
        }
        // footer
        float fy = H - 84 * s;
        Color ft = {static_cast<uint8_t>((th.bg.r * 7 + th.accent.r) / 8 + 6), static_cast<uint8_t>((th.bg.g * 7 + th.accent.g) / 8 + 6), static_cast<uint8_t>((th.bg.b * 7 + th.accent.b) / 8 + 6), 255};
        gfx_->fill({0, fy, W, 84 * s}, ft);
        gfx_->fill({0, fy, W, 1 * s}, th.border_subtle);
        float tl = 28 * s, tr = W - 28 * s;
        float shown = seek_bar({98 * s, fy + 35 * s, W - 98 * s - 98 * s, 14 * s}, 52 * s, 14 * s);
        int f18 = F(400, 18);
        T(400, 18, tl, fy + 28.5f * s, 27, mmss(static_cast<int>(shown)), rgb(0xb5b5b5));
        std::string tot = mmss(static_cast<int>(dur));
        T(400, 18, tr - gfx_->measure(f18, tot), fy + 28.5f * s, 27, tot, rgb(0xb5b5b5));
    } else {
        // minimal: seek row on top, centred title/artist, controls; no art, no bars
        float pad = 20 * s;
        if (icon_btn({pad, pad, 48 * s, 48 * s}, skin::Xmark, false) || u.in.escape) { set_car_mode(false); return; }
        chrome(W - pad, pad, 48 * s);
        float sy = pad + 64 * s;
        auto nb = [&](float x, uint32_t ic, bool prev) {
            Rect r{x, sy - 8 * s, 52 * s, 52 * s};
            if (u.hover(r)) gfx_->fill_round(r, 26 * s, th.hover);
            fa(ic, r.x + 26 * s, r.y + 26 * s, 24, th.text);
            if (u.clicked(r)) { if (prev) prev_track(); else next_track(); }
        };
        nb(pad, skin::StepBack, true);
        nb(W - pad - 52 * s, skin::StepFwd, false);
        float shown = seek_bar({pad + 160 * s, sy + 12 * s, W - 2 * pad - 320 * s, 14 * s}, 52 * s, 14 * s);
        T(400, 18, pad + 64 * s, sy + 5 * s, 27, mmss(static_cast<int>(shown)), rgb(0xb5b5b5));
        std::string tot = mmss(static_cast<int>(dur));
        T(400, 18, W - pad - 64 * s - gfx_->measure(F(400, 18), tot), sy + 5 * s, 27, tot, rgb(0xb5b5b5));
        float cy = sy + 80 * s;
        if (cur) {
            std::string tt = upper(cur->title), at = upper(cur->artist);
            float tw = std::min(gfx_->measure(F(800, 36, true), tt), W - 80 * s);
            T(800, 36, (W - tw) / 2, cy, 44, tt, th.text, W - 80 * s, true);
            float aw = std::min(gfx_->measure(F(700, 22, true), at), W - 80 * s);
            T(700, 22, (W - aw) / 2, cy + 50 * s, 32, at, th.accent, W - 80 * s, true);
        }
        float cw = std::min(W - 80 * s, 520 * s);
        transport((W - cw) / 2, cy + 100 * s, cw, 76 * s, 96 * s, cy + 100 * s, false);
        float ex = (W - cw) / 2, ey = cy + 210 * s;
        auto b = [&](float x, uint32_t ic, bool active) {
            Rect r{x, ey, 64 * s, 64 * s};
            if (u.hover(r)) gfx_->fill_round(r, 32 * s, th.hover);
            fa(ic, r.x + 32 * s, r.y + 32 * s, 24, active ? th.accent : th.sub);
            return u.clicked(r);
        };
        if (b(ex, skin::Shuffle, queue_.shuffle())) queue_.set_shuffle(!queue_.shuffle());
        if (b(ex + (cw - 64 * s) / 2, skin::Heart, cur && cur->starred) && cur && client_) { bool on = !cur->starred; worker_->post([c = client_, id = cur->id, on] { std::string e; c->set_star(id, on, e); }); const_cast<Song*>(queue_.current())->starred = on; }
        if (b(ex + cw - 64 * s, skin::Repeat, queue_.repeat() != Repeat::Off)) queue_.set_repeat(queue_.repeat() == Repeat::Off ? Repeat::All : queue_.repeat() == Repeat::All ? Repeat::One : Repeat::Off);
    }
}

// Pulse Deck: 24 log-spaced bands with peak caps. Drag horizontally to shape bass (left third) / treble (right third)
// for the current song; Reset EQ restores flat. Switches itself off if it cannot hold ~30 fps.
void App::draw_pulse_deck(Rect r) {
    Ui& u = *ui_;
    const float s = S_;
    const Theme& th = u.th;
    float bands[24];
    player_.enable_spectrum(true);
    player_.spectrum(bands, 24);
    // lag guard: frame interval average over 45 frames
    uint32_t now = SDL_GetTicks();
    if (pulse_last_ms_) {
        uint32_t dt = now - pulse_last_ms_;
        pulse_avg_ms_ = pulse_avg_ms_ * 0.95f + static_cast<float>(dt) * 0.05f;
        if (++pulse_frames_ > 90 && pulse_avg_ms_ > 45.0f && player_.playing()) { pulse_off_ = true; pulse_toast_ms_ = now + 6000; }
    }
    pulse_last_ms_ = now;
    gfx_->fill_round(r, 16 * s, alpha(th.text, 0.05f));
    gfx_->stroke_round(r, 16 * s, 1 * s, alpha(th.text, 0.21));
    float px = r.x + 24 * s, pw = r.w - 48 * s, ph = r.h - 70 * s, py = r.y + 24 * s;
    float bw = pw / 24.0f;
    // flowing gradient swell with the bass
    float bass = (bands[0] + bands[1] + bands[2]) / 3.0f;
    gfx_->clip(&r);
    gfx_->radial(r.x + r.w / 2, r.y + r.h, r.w * (0.5f + bass * 0.3f), alpha(th.accent, 0.10f + bass * 0.25f), alpha(th.accent, 0));
    gfx_->clip(nullptr);
    for (int i = 0; i < 24; ++i) {
        float v = bands[i];
        pulse_caps_[i] = std::max(v, pulse_caps_[i] - 0.012f);
        float h = std::max(3 * s, v * ph);
        Rect b{px + i * bw + 3 * s, py + ph - h, bw - 6 * s, h};
        gfx_->fill_round(b, std::min(5 * s, b.w / 2), th.accent);
        float cy = py + ph - pulse_caps_[i] * ph;
        gfx_->fill_round({b.x, cy - 4 * s, b.w, 3 * s}, 1.5f * s, th.text);
        if (i % 3 == 0) {
            double f = 40.0 * std::pow(16000.0 / 40.0, i / 24.0);
            char lb[16];
            if (f >= 1000) std::snprintf(lb, sizeof lb, "%.1fk", f / 1000.0); else std::snprintf(lb, sizeof lb, "%d", static_cast<int>(f));
            T(400, 11, b.x, py + ph + 10 * s, 16, lb, th.muted);
        }
    }
    // drag to shape bass / treble for this song
    if (r.contains(u.in.mx, u.in.my) && u.in.pressed) pulse_drag_ = true;
    if (pulse_drag_ && u.in.down) {
        float fx = (u.in.mx - r.x) / r.w, g = (0.5f - (u.in.my - r.y) / r.h) * 24.0f;  // +-12 dB over the height
        g = std::min(12.0f, std::max(-12.0f, g));
        if (fx < 0.34f) eq_bass_db_ = g; else if (fx > 0.66f) eq_treble_db_ = g; else { eq_bass_db_ = g; eq_treble_db_ = g; }
        player_.set_eq(eq_bass_db_, eq_treble_db_);
    }
    if (u.in.released) pulse_drag_ = false;
    if (eq_bass_db_ != 0 || eq_treble_db_ != 0) {
        Rect rb{r.x + r.w - 130 * s, r.y + 12 * s, 118 * s, 28 * s};
        gfx_->fill_round(rb, 14 * s, alpha(th.accent, 0.15f));
        fa(0xf0e2, rb.x + 16 * s, rb.y + 14 * s, 11, th.accent);
        T(600, 12, rb.x + 30 * s, rb.y, 28, "Reset EQ", th.accent);
        if (u.clicked(rb)) { eq_bass_db_ = eq_treble_db_ = 0; player_.set_eq(0, 0); }
    }
    if (pulse_toast_ms_ > now) T(500, 13, r.x + 20 * s, r.y + r.h - 34 * s, 20, "Pulse Deck turned off \xE2\x80\x94 this device is too slow for it.", th.sub, r.w - 40 * s);
}
