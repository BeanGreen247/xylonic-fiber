// Song context menu (SongContextMenu.tsx) and the keyboard help dialog (KeyboardHelp.tsx).
#include <algorithm>
#include <cmath>
#include "app.h"

void App::open_ctx(const Song& s, float x, float y) {
    ctx_song_ = s;
    ctx_x_ = x;
    ctx_y_ = y;
    mark();
}

bool App::ctx_hit(Rect r, const Song& s) {
    Ui& u = *ui_;
    if (u.in.rclick && r.contains(u.in.rx, u.in.ry)) { open_ctx(s, u.in.rx, u.in.ry); return true; }
    return false;
}

void App::draw_ctx_menu(float W, float H) {
    if (!ctx_song_) return;
    Ui& u = *ui_;
    const float s = S_;
    const Theme& th = u.th;
    struct Item { uint32_t icon; const char* label; int id; };
    std::vector<Item> items = {{skin::Play, "Play Now", 0}, {skin::StepFwd, "Play Next", 1}, {skin::ListUl, "Add to Queue", 2}, {0, "", -1},
                               {0xf067, "Add to Playlist\xE2\x80\xA6", 3}};
    if (downloads_ && !offline_) { items.push_back({0, "", -1}); items.push_back({skin::Download, "Download", 4}); }
    float mw = 220 * s, hh = 52 * s, ih = 46 * s;
    float mh = 8 * s + hh + 1 * s;
    for (auto& it : items) mh += it.id < 0 ? 9 * s : ih;
    float mx = std::min(ctx_x_, W - mw - 8 * s), my = std::min(ctx_y_, H - mh - 8 * s);
    Rect m{mx, my, mw, mh};
    // backdrop: any press outside closes
    bool inside = m.contains(u.in.mx, u.in.my);
    if ((u.in.pressed || u.in.rclick) && !inside) { ctx_song_.reset(); mark(); return; }
    for (int i = 4; i >= 1; --i) gfx_->fill_round({m.x - i * 2 * s, m.y + i * 3 * s, m.w + 4 * i * s, m.h}, 12 * s + i * 2 * s, {0, 0, 0, 20});
    gfx_->fill_round({m.x - 1, m.y - 1, m.w + 2, m.h + 2}, 13 * s, th.border);
    gfx_->fill_round(m, 12 * s, th.bg_light);
    T(600, 14, m.x + 16 * s, m.y + 14 * s, 20, ctx_song_->title, th.text, m.w - 32 * s);
    T(400, 12, m.x + 16 * s, m.y + 34 * s, 16, ctx_song_->artist, th.sub, m.w - 32 * s);
    float y = m.y + 4 * s + hh;
    gfx_->fill({m.x, y, m.w, 1 * s}, th.border_subtle);
    y += 1 * s;
    Song sg = *ctx_song_;
    for (auto& it : items) {
        if (it.id < 0) { gfx_->fill({m.x, y + 4 * s, m.w, 1 * s}, th.border_subtle); y += 9 * s; continue; }
        Rect r{m.x + 4 * s, y, m.w - 8 * s, ih};
        if (u.hover(r)) gfx_->fill_round(r, 8 * s, th.hover);
        fa(it.icon, r.x + 24 * s, r.y + ih / 2, 14, th.sub);
        T(400, 14, r.x + 46 * s, r.y, ih / s, it.label, th.text);
        if (u.clicked(r)) {
            ctx_song_.reset();
            switch (it.id) {
            case 0: { std::vector<Song> one{sg}; play_songs(one, 0, false); break; }
            case 1: queue_.play_next(sg); break;
            case 2: queue_.add(sg); break;
            case 3: add_song_ = sg; break;
            case 4: if (downloads_) { downloads_->enqueue({sg}); prefetch_covers({sg}); } break;
            }
            mark();
            return;
        }
        y += ih;
    }
}

void App::draw_help(float W, float H) {
    Ui& u = *ui_;
    const float s = S_;
    const Theme& th = u.th;
    static const char* keys[11] = {"Space", "Shift + \xE2\x86\x92", "Shift + \xE2\x86\x90", "\xE2\x86\x92", "\xE2\x86\x90", "Shift + \xE2\x86\x91", "Shift + \xE2\x86\x93", "S", "R", "M", "Ctrl + M"};
    static const char* acts[11] = {"Play / Pause", "Next Track", "Previous Track", "Seek Forward 5s", "Seek Backward 5s", "Volume Up 10%", "Volume Down 10%", "Toggle Shuffle", "Toggle Repeat", "Mute / Unmute", "Toggle Mini Player"};
    gfx_->fill({0, 0, W, H}, {0, 0, 0, 170});
    // Roboto has no arrow glyphs, so keys containing arrows are laid out in pieces with drawn arrows.
    auto key_width = [&](const std::string& k) {
        float w = 0; int fk = F(500, 12);
        for (size_t i = 0; i < k.size();) {
            if (static_cast<unsigned char>(k[i]) == 0xE2 && i + 2 < k.size() && static_cast<unsigned char>(k[i + 1]) == 0x86) { w += 12 * S_; i += 3; }
            else { size_t j = i; while (j < k.size() && !(static_cast<unsigned char>(k[j]) == 0xE2)) ++j; w += gfx_->measure(fk, k.substr(i, j - i)); i = j; }
        }
        return w;
    };
    auto draw_key = [&](const std::string& k, float x, float top, float lh, Color c) {
        int fk = F(500, 12);
        for (size_t i = 0; i < k.size();) {
            if (static_cast<unsigned char>(k[i]) == 0xE2 && i + 2 < k.size() && static_cast<unsigned char>(k[i + 1]) == 0x86) {
                unsigned char d = static_cast<unsigned char>(k[i + 2]);  // 0x90 left, 0x91 up, 0x92 right, 0x93 down
                float cx = x + 6 * S_, cy = top + lh * S_ / 2, a = 4.5f * S_, t = 1.5f * S_;
                if (d == 0x90 || d == 0x92) { float sg = d == 0x90 ? -1 : 1; gfx_->line(cx - sg * a, cy, cx + sg * a, cy, t, c); gfx_->line(cx + sg * a, cy, cx + sg * (a - 3.5f * S_), cy - 3.5f * S_, t, c); gfx_->line(cx + sg * a, cy, cx + sg * (a - 3.5f * S_), cy + 3.5f * S_, t, c); }
                else { float sg = d == 0x91 ? -1 : 1; gfx_->line(cx, cy - sg * a, cx, cy + sg * a, t, c); gfx_->line(cx, cy + sg * a, cx - 3.5f * S_, cy + sg * (a - 3.5f * S_), t, c); gfx_->line(cx, cy + sg * a, cx + 3.5f * S_, cy + sg * (a - 3.5f * S_), t, c); }
                x += 12 * S_; i += 3;
            } else { size_t j = i; while (j < k.size() && static_cast<unsigned char>(k[j]) != 0xE2) ++j; x += gfx_->text(fk, x, top + (lh * S_ - gfx_->line_height(fk)) / 2, k.substr(i, j - i), c); i = j; }
        }
    };
    float bw = 420 * s, bh = (76 + 11 * 40) * s;
    Rect b{(W - bw) / 2, (H - bh) / 2, bw, bh};
    gfx_->fill_round({b.x - 1, b.y - 1, b.w + 2, b.h + 2}, 3 * s, th.border);
    gfx_->fill_round(b, 2 * s, th.bg_light);
    fa(0xf11c, b.x + 28 * s, b.y + 32 * s, 15, th.accent);
    T(600, 16, b.x + 44 * s, b.y + 20 * s, 24, "Keyboard Shortcuts", th.text);
    Rect cl{b.x + b.w - 52 * s, b.y + 16 * s, 32 * s, 32 * s};
    if (u.hover(cl)) gfx_->fill_round(cl, 16 * s, th.hover);
    fa(skin::Xmark, cl.x + 16 * s, cl.y + 16 * s, 16, th.sub);
    if (u.clicked(cl) || u.in.escape || (u.in.pressed && !b.contains(u.in.mx, u.in.my))) { show_help_ = false; mark(); return; }
    float y = b.y + 68 * s;
    for (int i = 0; i < 11; ++i) {
        int fk = F(500, 12);
        (void)fk;
        float kw = key_width(keys[i]) + 20 * s;
        Rect kb{b.x + 24 * s, y + 6 * s, std::max(kw, 54 * s), 26 * s};
        gfx_->fill_round(kb, 6 * s, th.bg);
        gfx_->stroke_round(kb, 6 * s, 1 * s, th.border);
        draw_key(keys[i], kb.x + (kb.w - key_width(keys[i])) / 2, kb.y, 26, th.text);
        T(400, 14, b.x + 150 * s, y, 38, acts[i], th.sub);
        y += 40 * s;
    }
}
