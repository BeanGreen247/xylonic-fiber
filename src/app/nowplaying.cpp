// Desktop Now Playing (DesktopNowPlaying.tsx): flowing backdrop tinted from the cover, art, title, Up Next list,
// progress and transport. Measured from the old app at 1024x768.
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
                worker_->post([c = client_, id = cur->id, on] { std::string e; c->set_star(id, on, e); });
                const_cast<Song*>(queue_.current())->starred = on;
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
            worker_->post([c = client_, id = cur->id, on] { std::string e; c->set_star(id, on, e); });
            const_cast<Song*>(queue_.current())->starred = on;
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
