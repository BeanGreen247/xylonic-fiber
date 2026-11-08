// Desktop Now Playing (DesktopNowPlaying.tsx): flowing backdrop tinted from the cover, art, title, Up Next list,
// progress and transport. Measured from the old app at 1024x768.
#include <algorithm>
#include <cmath>
#include "app.h"

namespace {
std::string mmss(int s) {
    if (s < 0) s = 0;
    char b[16] = {};
    std::snprintf(b, sizeof b, "%d:%02d", s / 60, s % 60);
    return b;
}
}  // namespace

void App::draw_now_playing(float W, float H) {
    Ui& u = *ui_;
    const float s = S_;
    const Theme& th = u.th;
    const Song* cur = queue_.current();
    // Backdrop: the Electron view slowly drifts three soft cover-colour blobs.
    // The native loop throttles wants_frames to 2.5 Hz when this view is open,
    // keeping the effect visible without turning the whole app into a 60 Hz loop.
    u.wants_frames = true;
    uint32_t avg = 0x0a3a40;
    if (cur) { int dev = static_cast<int>(308 * S_); (void)images_->average(cover_key(cur->cover_art, dev), avg); }
    auto ch = [&](int sh) { return static_cast<float>((avg >> sh) & 255); };
    Color base{static_cast<uint8_t>(18 + ch(16) * 0.08f), static_cast<uint8_t>(18 + ch(8) * 0.12f), static_cast<uint8_t>(18 + ch(0) * 0.14f), 255};
    gfx_->fill({0, 0, W, H}, th.light ? rgb(0xf7f7f8) : base);
    auto blob = [&](float cx, float cy, float rad, float a) {
        Color mid{static_cast<uint8_t>(ch(16)), static_cast<uint8_t>(ch(8)), static_cast<uint8_t>(ch(0)), static_cast<uint8_t>(a * 255)};
        gfx_->radial(cx, cy, rad, mid, {mid.r, mid.g, mid.b, 0});
    };
    const float t = static_cast<float>(SDL_GetTicks64()) * 0.001f;
    blob((194 + std::sin(t * 0.23f) * 54) * s, (87 + std::cos(t * 0.19f) * 28) * s, 318 * s, 0.30f);
    blob((870 + std::cos(t * 0.17f) * 62) * s, (742 + std::sin(t * 0.21f) * 34) * s, 318 * s, 0.26f);
    blob((553 + std::sin(t * 0.13f + 1.4f) * 46) * s, (855 + std::cos(t * 0.16f) * 30) * s, 266 * s, 0.24f);
    gfx_->radial(W / 2, H / 2, std::max(W, H) * 0.8f, {0, 0, 0, 0}, {0, 0, 0, 120});  // vignette
    if (mobile_) { draw_now_playing_compact(W, H, cur); return; }   // NowPlayingOverlay at <=767 px
    // handle + top bar
    gfx_->fill_round({494 * s, 10 * s, 36 * s, 4 * s}, 2 * s, {255, 255, 255, 105});
    gfx_->set_letter_spacing(1.04f * s);
    T(600, 13, 32 * s, 40 * s, 20, "NOW PLAYING", th.sub);
    gfx_->set_letter_spacing(0);
    Rect cl{W - 68 * s, 32 * s, 36 * s, 36 * s};
    if (u.hover(cl)) gfx_->fill_round(cl, 18 * s, th.hover);
    fa(skin::Xmark, cl.x + 18 * s, cl.y + 18 * s, 18, th.sub);
    if (u.clicked(cl) || u.in.escape) { np_open_ = false; mark(); return; }
    // The desktop reference uses two columns, but that geometry cannot fit on
    // a phone-sized window. Keep the overlay modal and put the same controls in
    // a single, touch-friendly column for narrow displays.
    if (W / s < 900.0f) {
        float top = 78 * s;
        float art_size = std::min({280.0f * s, W - 48 * s, H - 360 * s});
        art_size = std::max(120.0f * s, art_size);
        Rect ar{(W - art_size) / 2, top + 12 * s, art_size, art_size};
        if (cur) {
            if (SDL_Texture* t = cover(cur->cover_art, static_cast<int>(art_size / s))) gfx_->image_round(t, ar, 10 * s);
            else gfx_->fill_round(ar, 10 * s, th.panel2);
            T(700, 21, 24 * s, ar.y + ar.h + 16 * s, 26, cur->title, th.text, W - 48 * s);
            T(400, 14, 24 * s, ar.y + ar.h + 47 * s, 20, cur->artist, th.sub, W - 48 * s);
            Rect lk{W - 62 * s, ar.y + ar.h + 15 * s, 38 * s, 38 * s};
            fa(skin::Heart, lk.x + 19 * s, lk.y + 19 * s, 20, cur->starred ? th.accent : th.sub);
            if (u.clicked(lk) && client_) {
                bool on = !cur->starred;
                set_current_liked(on);
            }
        }
        float cy = H - 136 * s, px = 24 * s, pw = W - 48 * s;
        float dur = static_cast<float>(player_.duration_seconds()), pos = static_cast<float>(player_.position_seconds());
        Rect pb{px, cy, pw, 6 * s}, hit{px, cy - 8 * s, pw, 22 * s};
        float v = dur > 0 ? std::min(1.0f, pos / dur) : 0;
        if (u.in.pressed && hit.contains(u.in.mx, u.in.my)) u.active = kSeek;
        if (u.active == kSeek && (u.in.down || u.in.released)) {
            float nv = std::min(1.0f, std::max(0.0f, (u.in.mx - pb.x) / pb.w));
            seek_drag_v_ = nv; v = nv;
            if (u.in.released) { u.active = 0; if (dur > 0) player_.seek_seconds(nv * dur); }
        }
        gfx_->fill_round(pb, 3 * s, {255, 255, 255, 46});
        if (v > 0) gfx_->fill_round({pb.x, pb.y, std::max(6 * s, pb.w * v), pb.h}, 3 * s, th.accent);
        T(400, 10, px, cy + 12 * s, 15, mmss(static_cast<int>((u.active == kSeek ? seek_drag_v_ * dur : pos))), th.sub);
        std::string total = mmss(static_cast<int>(dur));
        T(400, 10, px + pw - gfx_->measure(F(400, 10), total), cy + 12 * s, 15, total, th.sub);
        float ry = cy + 33 * s, mid = W / 2;
        auto cb = [&](float cx, uint32_t ic, float size, bool on) {
            Rect b{cx - 23 * s, ry + 20 * s, 46 * s, 46 * s};
            if (u.hover(b)) gfx_->fill_round(b, 23 * s, th.hover);
            fa(ic, b.x + 23 * s, b.y + 23 * s, size, on ? th.accent : th.sub);
            return u.clicked(b);
        };
        if (cb(mid - 112 * s, skin::Shuffle, 16, queue_.shuffle())) queue_.set_shuffle(!queue_.shuffle());
        if (cb(mid - 58 * s, skin::StepBack, 18, false)) prev_track();
        Rect pp{mid - 30 * s, ry + 12 * s, 60 * s, 60 * s};
        gfx_->circle(pp.x + 30 * s, pp.y + 30 * s, 30 * s, th.accent);
        fa(player_.playing() ? skin::Pause : skin::Play, pp.x + 31 * s, pp.y + 30 * s, 22, rgb(0x000000));
        if (u.clicked(pp)) toggle_play();
        if (cb(mid + 58 * s, skin::StepFwd, 18, false)) next_track();
        if (cb(mid + 112 * s, skin::Repeat, 16, queue_.repeat() != Repeat::Off))
            queue_.set_repeat(queue_.repeat() == Repeat::Off ? Repeat::All : queue_.repeat() == Repeat::All ? Repeat::One : Repeat::Off);
        return;
    }
    float body_top = 80 * s, body_h = H - 80 * s - 139 * s;
    // left column
    float lw = 432 * s, lx = 32 * s;
    float art = std::min(307.0f * s, body_h - 160 * s);
    Rect ar{lx + (lw - art) / 2, body_top + 48 * s, art, art};
    if (cur) {
        gfx_->fill_round({ar.x - 6 * s, ar.y + 6 * s, ar.w + 12 * s, ar.h + 12 * s}, 14 * s, {0, 0, 0, 40});  // soft shadow
        if (SDL_Texture* t = cover(cur->cover_art, 308)) gfx_->image_round(t, ar, 8 * s);
        else gfx_->fill_round(ar, 8 * s, th.panel2);
        int ft = F(700, 22);
        float tw = std::min(gfx_->measure(ft, cur->title), ar.w);
        T(700, 22, ar.x + (ar.w - tw) / 2, ar.y + ar.h + 21 * s, 26.4f, cur->title, th.text, ar.w);
        float aw = std::min(gfx_->measure(F(400, 15), cur->artist), ar.w);
        T(400, 15, ar.x + (ar.w - aw) / 2, ar.y + ar.h + 53 * s, 22.5f, cur->artist, th.sub, ar.w);
        Rect lk{lx + (lw - 44 * s) / 2, ar.y + ar.h + 96 * s, 44 * s, 34 * s};
        bool liked = cur->starred;
        fa(skin::Heart, lk.x + 22 * s, lk.y + 17 * s, 22, liked ? th.accent : rgb(0xb5b5b5));
        if (u.clicked(lk) && client_) {
            bool on = !cur->starred;
            set_current_liked(on);
        }
    }
    // divider + Up Next
    gfx_->fill({496 * s, body_top, 1 * s, body_h - 16 * s}, {255, 255, 255, 20});
    float qx = 529 * s, qw = 459 * s;
    gfx_->set_letter_spacing(0.78f * s);
    T(700, 13, qx, body_top + 8 * s, 19.5f, "UP NEXT", th.sub);
    gfx_->set_letter_spacing(0);
    Rect list{qx, body_top + 44 * s, qw + 4 * s, body_h - 44 * s - 16 * s};
    float rh = 62 * s;
    if (u.hover(list)) np_scroll_.y -= u.in.wheel * 62 * s;
    np_scroll_.clamp(rh * queue_.size(), list.h);
    gfx_->clip(&list);
    for (size_t i = 0; i < queue_.size(); ++i) {
        const Song& sg = queue_.songs()[i];
        Rect row{qx, list.y + i * rh - np_scroll_.y, qw, 60 * s};
        if (row.y + row.h < list.y || row.y > list.y + list.h) continue;
        bool is_cur = i == queue_.index();
        bool hov = u.hover(row) && list.contains(u.in.mx, u.in.my);
        if (is_cur) gfx_->fill_round(row, 8 * s, {255, 255, 255, 23});
        else if (hov) gfx_->fill_round(row, 8 * s, th.hover);
        Rect qa{row.x + 10 * s, row.y + 8 * s, 44 * s, 44 * s};
        if (SDL_Texture* t = cover(sg.cover_art, 44)) gfx_->image(t, qa);
        else gfx_->fill_round(qa, 4 * s, th.panel2);
        if (is_cur) { gfx_->fill_round(qa, 4 * s, {0, 0, 0, 130}); fa(skin::VolumeHigh, qa.x + 22 * s, qa.y + 22 * s, 16, th.accent); }
        T(500, 14, row.x + 66 * s, row.y + 9 * s, 21, sg.title, is_cur ? th.accent : th.text, row.w - 66 * s - 80 * s);
        T(400, 12, row.x + 66 * s, row.y + 32 * s, 18, sg.artist, th.sub, row.w - 66 * s - 80 * s);
        std::string d = mmss(sg.duration);
        T(400, 12, row.x + row.w - 10 * s - gfx_->measure(F(400, 12), d), row.y + 21 * s, 18, d, rgb(0xb5b5b5));
        if (u.clicked(row) && list.contains(u.in.mx, u.in.my)) { queue_.jump(i); start_current(); gfx_->clip(nullptr); return; }
    }
    gfx_->clip(nullptr);
    // fade at the bottom of the list
    gfx_->vgradient({qx, list.y + list.h - 48 * s, qw + 4 * s, 48 * s}, {base.r, base.g, base.b, 0}, base);
    // progress + transport
    float cy = H - 139 * s;
    float px = 40 * s, pw = W - 80 * s;
    float dur = static_cast<float>(player_.duration_seconds()), pos = static_cast<float>(player_.position_seconds());
    Rect pb{px, cy + 8 * s, pw, 6 * s};
    float v = dur > 0 ? std::min(1.0f, pos / dur) : 0;
    Rect hit_r{px, cy + 2 * s, pw, 16 * s};
    if (u.in.pressed && hit_r.contains(u.in.mx, u.in.my)) u.active = kSeek;
    if (u.active == kSeek && (u.in.down || u.in.released)) {
        float nv = std::min(1.0f, std::max(0.0f, (u.in.mx - pb.x) / pb.w));
        seek_drag_v_ = nv; v = nv;
        if (u.in.released) { u.active = 0; if (dur > 0) player_.seek_seconds(nv * dur); }
    }
    gfx_->fill_round(pb, 3 * s, {255, 255, 255, 46});
    if (v > 0) gfx_->fill_round({pb.x, pb.y, std::max(6 * s, pb.w * v), pb.h}, 3 * s, th.accent);
    float shown = (u.active == kSeek && dur > 0) ? seek_drag_v_ * dur : pos;
    T(400, 10, px + 4 * s, cy + 16 * s, 15, mmss(static_cast<int>(shown)), th.sub);
    std::string total = mmss(static_cast<int>(dur));
    T(400, 10, px + pw - 4 * s - gfx_->measure(F(400, 10), total), cy + 16 * s, 15, total, th.sub);
    float ry = cy + 35 * s, mid = W / 2;
    auto cb = [&](float cx, uint32_t ic, float px_, bool on) {
        Rect b{cx - 22 * s, ry + 24 * s, 44 * s, 44 * s};
        if (u.hover(b)) gfx_->fill_round(b, 22 * s, th.hover);
        fa(ic, b.x + 22 * s, b.y + 22 * s, px_, on ? th.accent : th.sub);
        return u.clicked(b);
    };
    if (cb(mid - 120 * s, skin::Shuffle, 16, queue_.shuffle())) queue_.set_shuffle(!queue_.shuffle());
    if (cb(mid - 64 * s, skin::StepBack, 18, false)) prev_track();
    Rect pp{mid - 30 * s, ry + 16 * s, 60 * s, 60 * s};
    gfx_->circle(pp.x + 30 * s, pp.y + 30 * s, 30 * s, u.hover(pp) ? th.primary_light : th.accent);
    fa(player_.playing() ? skin::Pause : skin::Play, pp.x + 31 * s, pp.y + 30 * s, 22, rgb(0x000000));
    if (u.clicked(pp)) toggle_play();
    if (cb(mid + 64 * s, skin::StepFwd, 18, false)) next_track();
    if (cb(mid + 120 * s, skin::Repeat, 16, queue_.repeat() != Repeat::Off))
        queue_.set_repeat(queue_.repeat() == Repeat::Off ? Repeat::All : queue_.repeat() == Repeat::All ? Repeat::One : Repeat::Off);
}

// NowPlayingOverlay (compact): handle, header, square art, title/artist with like and playlist buttons, progress, transport,
// quality + speed selectors, and the audio stats row (Format, Bitrate, Sample Rate, Bit Depth, File Size). Geometry from the CSS.
void App::draw_now_playing_compact(float W, float H, const Song* cur) {
    Ui& u = *ui_;
    const float s = S_;
    const Theme& th = u.th;
    const float lw = W / s, lh = H / s;
    // NowPlayingOverlay touch handlers: a swipe down anywhere closes, a swipe left/right skips the track, a tap on the artwork plays or pauses.
    // A slider that took the touch (progress bar) cancels the gesture.
    {
        const Input& in = u.in;
        if (in.pressed) { np_gx_ = in.mx; np_gy_ = in.my; np_gdx_ = np_gdy_ = 0; np_gdir_ = 0; np_gon_ = true; }
        if (np_gon_ && in.down) {
            if (u.active != 0) { np_gon_ = false; np_gdir_ = 0; }
            else {
                np_gdx_ = in.mx - np_gx_; np_gdy_ = in.my - np_gy_;
                if (np_gdir_ == 0) {
                    if (std::fabs(np_gdx_) > 10 * s && std::fabs(np_gdx_) > std::fabs(np_gdy_)) np_gdir_ = 1;
                    else if (np_gdy_ > 12 * s && np_gdy_ > std::fabs(np_gdx_) * 1.2f) np_gdir_ = 2;
                }
                if (np_gdir_ != 0) u.wants_frames = true;
            }
        }
        if (np_gon_ && !in.down && !in.pressed) {   // finger up (a touch that travelled never sets `released`, so test the button state)
            np_gon_ = false;
            if (np_gdir_ == 2) { np_anim_from_ = np_off_; np_anim_t0_ = SDL_GetTicks(); np_anim_close_ = np_gdy_ > 50 * s; np_anim_ = true; }   // finish the slide down, or spring back
            if (np_gdir_ == 1 && std::fabs(np_gdx_) > 50 * s) {
                if (np_gdx_ < 0) next_track();
                else if (remote_mode_) remote_send("previous");
                else if (queue_.prev()) start_current();   // a swipe always changes track, no "restart when past 3 s"
            }
        }
    }
    // the sheet follows the finger while it is pulled down (translateY in the original), then slides out (0.2 s ease-in) or springs back
    if (np_gon_ && np_gdir_ == 2) { np_off_ = np_gdy_ > 0 ? std::min(np_gdy_ * 0.55f, 180.0f * s) : 0.0f; np_anim_ = false; }
    else if (np_anim_) {
        const float t = std::min(1.0f, static_cast<float>(SDL_GetTicks() - np_anim_t0_) / (np_anim_close_ ? 200.0f : 260.0f));
        if (np_anim_close_) np_off_ = np_anim_from_ + (H - np_anim_from_) * t * t;
        else np_off_ = np_anim_from_ * (1.0f - (1.0f - (1.0f - t) * (1.0f - t)));
        u.wants_frames = true;
        if (t >= 1.0f) { np_anim_ = false; np_off_ = 0; if (np_anim_close_) { np_open_ = false; np_gdir_ = 0; gfx_->shift_y(0); mark(); return; } }
    } else np_off_ = 0;
    if (np_off_ > 0.5f) { gfx_->shift_y(static_cast<int>(np_off_)); u.in.pressed = u.in.released = false; }   // drawn lower than it hit-tests: no taps while the sheet moves
    gfx_->fill_round({W / 2 - 18 * s, 10 * s, 36 * s, 4 * s}, 2 * s, {255, 255, 255, 89});
    Rect cl{8 * s, 18 * s, 44 * s, 44 * s};
    if (u.hover(cl)) gfx_->fill_round(cl, 22 * s, th.hover);
    fa(0xf078, cl.x + 22 * s, cl.y + 22 * s, 18, {255, 255, 255, 217});
    if (u.clicked(cl) || u.in.escape) { gfx_->shift_y(0); np_open_ = false; mark(); return; }
    gfx_->set_letter_spacing(1.32f * s);
    const float lblw = gfx_->measure(F(700, 11), "NOW PLAYING");
    T(700, 11, W / 2 - lblw / 2, 18 * s, 44, "NOW PLAYING", {255, 255, 255, 153});
    gfx_->set_letter_spacing(0);
    // art: max(320, min(100vh - 406, 100vw - 16)) (tighter below 680 px of height)
    const bool short_h = lh <= 680;
    const float art_px = short_h ? std::max(180.0f, std::min(lh - 335, lw - 16)) : std::max(320.0f, std::min(lh - 406, lw - 16));
    const float wrap_h = short_h ? std::max(188.0f, std::min(lh - 327, lw - 8)) : std::max(332.0f, std::min(lh - 390, lw - 8));
    float y = 70 * s;
    Rect ar{(W - art_px * s) / 2, y, art_px * s, art_px * s};
    const Rect art_hit = ar;   // the tap target stays put while the artwork follows a horizontal swipe
    if (np_gon_ && np_gdir_ == 1) ar.x += std::max(-W * 0.6f, std::min(W * 0.6f, np_gdx_));
    const uint8_t art_a = static_cast<uint8_t>(255 - std::min(150.0f, std::fabs(ar.x - art_hit.x) / W * 250.0f));
    if (cur) { if (SDL_Texture* t = cover(cur->cover_art, static_cast<int>(art_px), cur->artist, cur->album)) gfx_->image_round(t, ar, 6 * s, {255, 255, 255, art_a}); else draw_art_fallback(ar, cur->cover_art, 6 * s); }
    if (cur && np_gdir_ == 0 && u.clicked(art_hit)) toggle_play();
    else gfx_->fill_round(ar, 6 * s, {255, 255, 255, 15});
    y += wrap_h * s;
    // info row
    y += 6 * s;
    const float bx_pl = W - 22 * s - 44 * s, bx_like = bx_pl - 12 * s - 44 * s;
    const float title_px = short_h ? 17.0f : 20.0f;
    T(700, title_px, 22 * s, y, title_px * 1.25f, cur ? cur->title : "No song playing", rgb(0xffffff), bx_like - 34 * s);
    T(400, short_h ? 13.0f : 14.0f, 22 * s, y + (title_px * 1.25f + 3) * s, 21, cur ? cur->artist : "\xE2\x80\x94", {255, 255, 255, 166}, bx_like - 34 * s);
    const float cy_row = y + 24.5f * s;
    Rect lk{bx_like, cy_row - 22 * s, 44 * s, 44 * s}, pl{bx_pl, cy_row - 22 * s, 44 * s, 44 * s};
    const bool liked = cur && cur->starred;
    fa(skin::Heart, lk.x + 22 * s, lk.y + 22 * s, 22, liked ? th.accent : Color{255, 255, 255, 140});
    fa(skin::ListUl, pl.x + 22 * s, pl.y + 22 * s, 18, {255, 255, 255, 140});
    if (cur && u.clicked(lk) && client_) set_current_liked(!cur->starred);
    if (cur && u.clicked(pl)) add_song_ = *cur;
    y += 53 * s;                                   // row bottom (title block + 4)
    // progress: 10 px full-width bar
    y += 8 * s;
    const float dur = static_cast<float>(player_.duration_seconds()), pos = static_cast<float>(player_.position_seconds());
    Rect pb{22 * s, y, W - 44 * s, 10 * s}, hit{pb.x, pb.y - 8 * s, pb.w, 26 * s};
    float v = dur > 0 ? std::min(1.0f, pos / dur) : 0;
    if (u.in.pressed && hit.contains(u.in.mx, u.in.my)) u.active = kSeek;
    if (u.active == kSeek && (u.in.down || u.in.released)) {
        const float nv = std::min(1.0f, std::max(0.0f, (u.in.mx - pb.x) / pb.w));
        seek_drag_v_ = nv; v = nv;
        if (u.in.released) { u.active = 0; if (dur > 0) player_.seek_seconds(nv * dur); }
    }
    gfx_->fill_round(pb, 5 * s, {255, 255, 255, 46});
    if (v > 0) gfx_->fill_round({pb.x, pb.y, std::max(10 * s, pb.w * v), pb.h}, 5 * s, th.accent);
    y += 10 * s;
    // controls: space-between inside 16 px side padding, widths 56 56 72 56 56
    y += short_h ? 8 * s : 20 * s;
    const float ctrl_h = short_h ? 62.0f : 72.0f, small = short_h ? 48.0f : 56.0f;
    const float inner = W - 32 * s, total_w = (small * 4 + ctrl_h) * s, gapw = (inner - total_w) / 4;
    const float ccy = y + ctrl_h * s / 2;
    float cx = 16 * s;
    auto slot = [&](float w_) { const float c = cx + w_ * s / 2; cx += w_ * s + gapw; return c; };
    auto glyph = [&](float c, uint32_t ic, float px, Color col, float box) {
        Rect b{c - box * s / 2, ccy - box * s / 2, box * s, box * s};
        if (u.hover(b)) gfx_->fill_round(b, box * s / 2, {255, 255, 255, 15});
        fa(ic, c, ccy, px, col);
        return u.clicked(b);
    };
    if (glyph(slot(small), skin::Shuffle, 20, queue_.shuffle() ? th.accent : Color{255, 255, 255, 128}, small)) queue_.set_shuffle(!queue_.shuffle());
    if (glyph(slot(small), skin::StepBack, 22, {255, 255, 255, 230}, small)) prev_track();
    {
        const float c = slot(ctrl_h);
        Rect b{c - ctrl_h * s / 2, ccy - ctrl_h * s / 2, ctrl_h * s, ctrl_h * s};
        gfx_->fill_round({b.x - 2 * s, b.y + 6 * s, b.w + 4 * s, b.h + 4 * s}, b.w / 2, {0, 0, 0, 60});
        gfx_->circle(c, ccy, ctrl_h * s / 2, u.hover(b) ? rgb(0xffffff) : Color{255, 255, 255, 242});
        fa(player_.playing() ? skin::Pause : skin::Play, c + (player_.playing() ? 0 : 1.5f * s), ccy, short_h ? 20 : 26, rgb(0x111111));
        if (u.clicked(b)) toggle_play();
    }
    if (glyph(slot(small), skin::StepFwd, 22, {255, 255, 255, 230}, small)) next_track();
    {
        const bool on = queue_.repeat() != Repeat::Off;
        const float c = slot(small);
        if (glyph(c, skin::Repeat, 20, on ? th.accent : Color{255, 255, 255, 128}, small)) queue_.set_repeat(queue_.repeat() == Repeat::Off ? Repeat::All : queue_.repeat() == Repeat::All ? Repeat::One : Repeat::Off);
        if (queue_.repeat() == Repeat::One) T(800, 9, c - gfx_->measure(F(800, 9), "1") / 2, ccy - 6.5f * s, 13, "1", th.accent);   // the 1 sits inside the arrow
    }
    y += ctrl_h * s + (short_h ? 10.0f : 24.0f) * s;
    // selectors row (quality, speed): padding 4 22 0, gap 8, 12 px/600
    y += 4 * s;
    const float sw = (W - 44 * s - 8 * s) / 2, sh = 38 * s;
    Rect qb{22 * s, y, sw, sh}, sp{22 * s + sw + 8 * s, y, sw, sh};
    // Like StreamingQualitySelector.tsx: a downloaded song shows the drive icon and the quality it was saved at, a streamed one the cloud and
    // the streaming quality; in offline mode the button is dimmed and cannot open the picker ("Playing from cache").
    const bool cached_cur = cur && !perm_path(*cur).empty();
    int cached_q = -2;   // -2 not downloaded, 0 original, else kbps
    if (cached_cur && store_) { Store::Row row; if (store_->dl_get(cur->id, row)) cached_q = row.quality < 0 ? cfg_.download_quality : row.quality; }
    const float dim = offline_ ? 0.45f : 1.0f;
    auto dimmed = [&](Color c) { c.a = static_cast<uint8_t>(c.a * dim); return c; };
    for (Rect* r : {&qb, &sp}) {
        const bool dead = r == &qb && offline_;
        gfx_->fill_round(*r, 6 * s, dimmed(Color{255, 255, 255, static_cast<uint8_t>(!dead && u.hover(*r) ? 41 : 33)}));
        gfx_->stroke_round(*r, 6 * s, 1 * s, r == &qb ? dimmed(Color{255, 255, 255, 102}) : Color{255, 255, 255, 102});
    }
    if (offline_) quality_popup_ = false;
    {
        const int shown_kbps = cached_q != -2 ? cached_q : cfg_.stream_quality;   // 0 = original
        const std::string ql = shown_kbps ? std::to_string(shown_kbps) + "k" : "Original";
        const int fq = F(600, 12);
        const float tw = gfx_->measure(fq, ql), total = 38 * s + 14 * s + 5 * s + tw;
        float bx = qb.x + (qb.w - total) / 2;
        const int bars = shown_kbps == 0 ? 6 : shown_kbps >= 320 ? 6 : shown_kbps >= 256 ? 5 : shown_kbps >= 192 ? 4 : shown_kbps >= 128 ? 3 : 2;   // getQualityBars of the original
        for (int i = 0; i < 6; ++i) { const float bh = (4 + 2.4f * i) * s; gfx_->fill_round({bx + i * 5.5f * s, qb.y + qb.h / 2 + 7 * s - bh, 3 * s, bh}, 1.5f * s, i < bars ? dimmed(th.accent) : dimmed(Color{255, 255, 255, 64})); }
        fa(cached_q != -2 ? 0xf0a0u : static_cast<uint32_t>(skin::Cloud), bx + 38 * s + 7 * s, qb.y + qb.h / 2, 13, dimmed(th.accent));   // hard drive when played from the downloads
        T(600, 12, bx + 38 * s + 19 * s, qb.y, sh / s, ql, dimmed(Color{255, 255, 255, 191}));
        char sb[16] = {}; const double spd = player_.speed();
        if (spd == static_cast<int>(spd)) std::snprintf(sb, sizeof sb, "%d\xC3\x97", static_cast<int>(spd)); else std::snprintf(sb, sizeof sb, "%g\xC3\x97", spd);
        const float stw = gfx_->measure(fq, sb);
        fa(skin::Bolt, sp.x + (sp.w - stw - 16 * s) / 2 + 5 * s, sp.y + sp.h / 2, 11, th.accent);
        T(600, 12, sp.x + (sp.w - stw - 16 * s) / 2 + 16 * s, sp.y, sh / s, sb, {255, 255, 255, 191});
    }
    if (u.clicked(qb) && !offline_) { quality_popup_ = !quality_popup_; speed_popup_ = false; }
    if (u.clicked(sp)) { speed_popup_ = !speed_popup_; quality_popup_ = false; }
    y += sh;
    // audio stats: Format, Bitrate, Sample Rate, Bit Depth, File Size (song data, else getSong)
    if (cur && np_meta_id_ != cur->id) {
        np_meta_id_ = cur->id; np_meta_ = Song{};
        if (client_ && !offline_ && cur->suffix.empty() && cur->bit_rate == 0 && cur->size == 0 && cur->sampling_rate == 0 && cur->bit_depth == 0)
            worker_->post([this, c = client_, id = cur->id, s2 = session_] { Song sg; std::string e; if (c->song(id, sg, e)) ui_post([this, s2, id, sg] { if (s2 == session_ && np_meta_id_ == id) { np_meta_ = sg; mark(); } }); });
    }
    const Song m = cur ? *cur : Song{};
    auto pick = [&](auto a, auto b) { return a ? a : b; };
    const std::string suffix = !m.suffix.empty() ? m.suffix : np_meta_.suffix;
    const Player::Info pi = player_.info();   // what the decoder really reads beats the server's tags (and works offline)
    const int br = pick(m.bit_rate, np_meta_.bit_rate);
    const int sr = pi.rate ? static_cast<int>(pi.rate) : pick(m.sampling_rate, np_meta_.sampling_rate);
    const int bd = pi.rate ? static_cast<int>(pi.bits) : pick(m.bit_depth, np_meta_.bit_depth);
    const uint64_t size = m.size ? m.size : np_meta_.size;
    std::string f_fmt = pi.rate && !pi.codec.empty() ? pi.codec : suffix.empty() ? "\xE2\x80\x94" : suffix; for (auto& ch : f_fmt) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    char tmp[48] = {};
    const std::string f_br = br ? std::to_string(br) + " kbps" : "\xE2\x80\x94";
    std::string f_sr = "\xE2\x80\x94"; if (sr) { if (sr >= 1000) std::snprintf(tmp, sizeof tmp, "%.1f kHz", sr / 1000.0); else std::snprintf(tmp, sizeof tmp, "%d Hz", sr); f_sr = tmp; }
    const std::string f_bd = bd ? std::to_string(bd) + "-bit" : "\xE2\x80\x94";
    std::string f_sz = "\xE2\x80\x94"; if (size) { if (size >= 1073741824ull) std::snprintf(tmp, sizeof tmp, "%.1f GB", size / 1073741824.0); else if (size >= 1048576ull) std::snprintf(tmp, sizeof tmp, "%.1f MB", size / 1048576.0); else std::snprintf(tmp, sizeof tmp, "%.0f KB", size / 1024.0); f_sz = tmp; }
    const char* labels[5] = {"FORMAT", "BITRATE", "SAMPLE RATE", "BIT DEPTH", "FILE SIZE"};
    const std::string vals[5] = {f_fmt, f_br, f_sr, f_bd, f_sz};
    y += (short_h ? 4.0f : 10.0f) * s;
    const float colw = (W - 44 * s - 4 * 4 * s) / 5;
    for (int i = 0; i < 5; ++i) {
        const float x0 = 22 * s + i * (colw + 4 * s), cxm = x0 + colw / 2;
        gfx_->set_letter_spacing(0.48f * s);
        const float lw2 = gfx_->measure(F(700, short_h ? 7.0f : 8.0f), labels[i]);
        T(700, short_h ? 7.0f : 8.0f, cxm - lw2 / 2, y, 12, labels[i], {255, 255, 255, 89}, colw);
        gfx_->set_letter_spacing(0);
        const float vw = std::min(colw, gfx_->measure(F(600, short_h ? 10.0f : 11.0f), vals[i]));
        T(600, short_h ? 10.0f : 11.0f, cxm - vw / 2, y + 13 * s, 16.5f, vals[i], {255, 255, 255, 179}, colw);
    }
    // popups above the selectors
    auto popup = [&](Rect anchor, const std::vector<std::string>& labels2, int cur_i, bool& open, const std::function<void(int)>& on_pick) {
        if (!open) return;
        const float pw = std::max(anchor.w, 140 * s), ih = 36 * s, ph = labels2.size() * ih + 8 * s;
        Rect pop{anchor.x + anchor.w - pw, anchor.y - 6 * s - ph, pw, ph};
        gfx_->fill_round({pop.x - 1, pop.y - 1, pop.w + 2, pop.h + 2}, 13 * s, th.border);
        gfx_->fill_round(pop, 12 * s, th.bg_light);
        for (size_t i = 0; i < labels2.size(); ++i) {
            Rect ir{pop.x + 4 * s, pop.y + 4 * s + i * ih, pop.w - 8 * s, ih};
            const bool on = static_cast<int>(i) == cur_i;
            if (u.hover(ir)) gfx_->fill_round(ir, 8 * s, th.hover);
            if (on) fa(skin::Check, ir.x + 16 * s, ir.y + ih / 2, 11, th.accent);
            T(on ? 600 : 400, 13, ir.x + 34 * s, ir.y, 36, labels2[i], on ? th.accent : th.text);
            if (u.clicked(ir)) { on_pick(static_cast<int>(i)); open = false; mark(); }
        }
        if (u.in.pressed && !pop.contains(u.in.mx, u.in.my) && !anchor.contains(u.in.mx, u.in.my)) open = false;
    };
    static const int qvals[] = {0, 320, 256, 192, 128, 64};
    int qi = 0; for (int i = 0; i < 6; ++i) if (qvals[i] == cfg_.stream_quality) qi = i;
    popup(qb, {"Original", "320 kbps", "256 kbps", "192 kbps", "128 kbps", "64 kbps"}, qi, quality_popup_, [&](int i) {
        cfg_.stream_quality = qvals[i]; cfg_.save(cfg_path_);
        np_toast_ = std::string("Quality \xE2\x80\xBA ") + (qvals[i] ? std::to_string(qvals[i]) + " kbps" : "Original") + " (next track)"; np_toast_until_ = SDL_GetTicks() + 3500;
    });
    static const double svals[] = {0.5, 0.75, 1.0, 1.25, 1.5, 1.75, 2.0};
    int si = 2; for (int i = 0; i < 7; ++i) if (std::fabs(player_.speed() - svals[i]) < 1e-6) si = i;
    popup(sp, {"0.5\xC3\x97", "0.75\xC3\x97", "Normal", "1.25\xC3\x97", "1.5\xC3\x97", "1.75\xC3\x97", "2\xC3\x97"}, si, speed_popup_, [&](int i) { player_.set_speed(svals[i]); });
    if (SDL_GetTicks() < np_toast_until_) { T(500, 11, W / 2 - gfx_->measure(F(500, 11), np_toast_) / 2, qb.y - 22 * s, 17, np_toast_, th.accent); u.wants_frames = true; }
    gfx_->shift_y(0);
}
