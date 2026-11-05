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
    // Dashboard use is landscape; this is honoured on Android and ignored by desktop window managers.
    SDL_SetHint(SDL_HINT_ORIENTATIONS, on ? "LandscapeLeft LandscapeRight" : "LandscapeLeft LandscapeRight Portrait PortraitUpsideDown");
    if (!on) player_.enable_spectrum(false);
#if defined(__ANDROID__)
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
        if (icon_btn({x - size, y, size, size}, 0xf080, cfg_.car_pulse)) { cfg_.car_pulse = !cfg_.car_pulse; pulse_off_ = false; pulse_quality_ = 2; pulse_frames_ = 0; cfg_.save(cfg_path_); }
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
                set_current_liked(on);
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
        if (cfg_.car_pulse && !pulse_off_) draw_pulse_deck({ax, mt + 20 * s, colw, mh - 40 * s});
        else {
        gfx_->fill_round(af, 16 * s, alpha(th.text, 0.21));
        Rect ai{af.x + 1 * s, af.y + 1 * s, af.w - 2 * s, af.h - 2 * s};
        gfx_->fill_round(ai, 15 * s, th.panel2);
        // Car Mode uses the larger artwork variant so the dashboard view does
        // not upscale the normal library thumbnail on high-DPI displays.
        if (cur) {
            // 900 px while it loads or when it was never fetched (offline): fall back to the cached thumbnails, then to a glyph.
            SDL_Texture* t = cover(cur->cover_art, 900, "", "", true);   // hi:<id> at 2000 px
            if (!t) t = cover(cur->cover_art, 300);
            if (!t) t = cover(cur->cover_art, 164);
            if (t) gfx_->image_round(t, ai, 15 * s);
            else fa(skin::Music, ai.x + ai.w / 2, ai.y + ai.h / 2, std::max(24.0f, ai.w / s / 4), alpha(th.text, 0.35f));
        }
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
        if (b(ex + (cw - 64 * s) / 2, skin::Heart, cur && cur->starred) && cur && client_) { bool on = !cur->starred; set_current_liked(on); }
        if (b(ex + cw - 64 * s, skin::Repeat, queue_.repeat() != Repeat::Off)) queue_.set_repeat(queue_.repeat() == Repeat::Off ? Repeat::All : queue_.repeat() == Repeat::All ? Repeat::One : Repeat::Off);
    }
}

// Pulse Deck: 16..48 log-spaced bands (one per ~22 px) with smoothed motion, falling peak caps, a bass-kick glow and an
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
    const int nb = std::min(48, std::max(16, static_cast<int>((r.w / s - 48) / 22)));
    float raw[48];
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

    // panel (+ the cover as a dim backdrop)
    gfx_->fill_round(r, 16 * s, alpha(th.text, 0.05f));
    const Song* cur = queue_.current();
    SDL_Texture* art = cur ? cover(cur->cover_art, 300) : nullptr;
    if (pulse_quality_ >= 2 && art) gfx_->image_round(art, r, 16 * s, {255, 255, 255, 34});
    gfx_->stroke_round(r, 16 * s, 1 * s, alpha(th.text, 0.21));
    gfx_->clip(&r);
    if (pulse_quality_ >= 1) gfx_->radial(r.x + r.w / 2, r.y + r.h, r.w * (0.5f + bass * 0.3f + pulse_kick_ * 0.15f), alpha(th.accent, 0.10f + bass * 0.22f + pulse_kick_ * 0.18f), alpha(th.accent, 0));

    // caption row: cover thumbnail that kicks with the bass, name, hint
    const float pad = 20 * s;
    const float th_sz = (44 + 6 * pulse_kick_) * s;
    Rect tr{r.x + pad + (50 * s - th_sz) / 2, r.y + pad + (50 * s - th_sz) / 2, th_sz, th_sz};
    if (art) gfx_->image_round(art, tr, 8 * s);
    else { gfx_->fill_round(tr, 8 * s, alpha(th.text, 0.1f)); fa(skin::Music, tr.x + tr.w / 2, tr.y + tr.h / 2, 16, alpha(th.text, 0.4f)); }
    gfx_->set_letter_spacing(1.2f * s);
    T(700, 12, r.x + pad + 62 * s, r.y + pad + 4 * s, 18, "PULSE DECK", th.accent);
    gfx_->set_letter_spacing(0);
    const bool eq_on = eq_bass_db_ != 0 || eq_treble_db_ != 0;
    if (!eq_on && !pulse_drag_) T(400, 12, r.x + pad + 62 * s, r.y + pad + 26 * s, 18, "Drag across the bars to shape bass and treble", th.muted, r.w - 2 * pad - 62 * s);
    else {
        char eb[64]; std::snprintf(eb, sizeof eb, "Bass %+.0f dB  \xC2\xB7  Treble %+.0f dB", eq_bass_db_, eq_treble_db_);
        T(500, 12, r.x + pad + 62 * s, r.y + pad + 26 * s, 18, eb, th.sub);
    }

    // bars
    const float top = r.y + pad + 64 * s, lab = 26 * s;
    const bool refl = pulse_quality_ >= 2;
    const float avail = r.y + r.h - pad - lab - top;
    const float ph = refl ? avail * 0.80f : avail, base = top + ph;
    const float px = r.x + pad, pw = r.w - 2 * pad, bw = pw / nb;
    const float gap = std::max(2 * s, bw * 0.22f);
    for (int i = 0; i < nb; ++i) {
        const float v = std::min(1.0f, pulse_sm_[i]);
        const float h = std::max(3 * s, v * ph);
        Rect b{px + i * bw + gap / 2, base - h, bw - gap, h};
        const float rad = std::min(4 * s, b.w / 2);
        gfx_->quad_gradient(b, th.primary_light, th.primary_light, th.accent, th.accent);
        (void)rad;
        const float cy = base - std::min(1.0f, pulse_caps_[i]) * ph;
        gfx_->fill({b.x, cy - 4 * s, b.w, 2.5f * s}, alpha(th.text, 0.9f));
        if (refl) {
            const float rh = std::min(h * 0.55f, avail - ph);
            if (rh > 1) gfx_->quad_gradient({b.x, base + 2 * s, b.w, rh}, alpha(th.accent, 0.30f), alpha(th.accent, 0.30f), alpha(th.accent, 0), alpha(th.accent, 0));
        }
    }
    gfx_->fill({px, base, pw, 1 * s}, alpha(th.text, 0.18f));
    const int step = std::max(1, nb / 8);
    for (int i = 0; i < nb; i += step) {
        const double f = 40.0 * std::pow(16000.0 / 40.0, static_cast<double>(i) / nb);
        char lb[16];
        if (f >= 1000) std::snprintf(lb, sizeof lb, "%.1fk", f / 1000.0); else std::snprintf(lb, sizeof lb, "%d", static_cast<int>(f));
        T(400, 11, px + i * bw, r.y + r.h - pad - 16 * s, 16, lb, th.muted);
    }

    // drag to shape bass / treble for this song
    if (r.contains(u.in.mx, u.in.my) && u.in.pressed) pulse_drag_ = true;
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
    if (eq_on) {
        Rect rb{r.x + r.w - 130 * s, r.y + 16 * s, 114 * s, 28 * s};
        gfx_->fill_round(rb, 14 * s, u.hover(rb) ? alpha(th.accent, 0.25f) : alpha(th.accent, 0.15f));
        fa(0xf0e2, rb.x + 16 * s, rb.y + 14 * s, 11, th.accent);
        T(600, 12, rb.x + 30 * s, rb.y, 28, "Reset EQ", th.accent);
        if (u.clicked(rb)) { eq_bass_db_ = eq_treble_db_ = 0; player_.set_eq(0, 0); pulse_drag_ = false; }
    }
    gfx_->clip(nullptr);
    if (pulse_toast_ms_ > now) T(500, 13, r.x + 20 * s, r.y + r.h - 34 * s, 20, "Pulse Deck turned off \xE2\x80\x94 this device is too slow for it.", th.sub, r.w - 40 * s);
}
