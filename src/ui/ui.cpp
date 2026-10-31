#include "ui.h"
#include <algorithm>
#include <cmath>
#include <SDL.h>

bool Ui::button(Rect r, const std::string& label, bool primary) {
    bool h = hover(r);
    g.fill_round(r, 8, primary ? (h ? th.primary_light : th.accent) : th.panel2);
    if (h && !primary) g.fill_round(r, 8, th.hover);
    float w = g.measure(f_body, label), lh = g.line_height(f_body);
    g.text(f_body, r.x + (r.w - w) / 2, r.y + (r.h - lh) / 2, label, primary ? th.accent_text : th.text, r.w - 8);
    return clicked(r);
}

bool Ui::icon_button(Rect r, Icon ic, bool on, float pad) {
    bool h = hover(r);
    if (h) g.fill_round(r, r.h / 2, th.hover);
    float p = r.w * pad;
    icon(ic, {r.x + p, r.y + p, r.w - 2 * p, r.h - 2 * p}, on ? th.accent : th.text);
    return clicked(r);
}

void Ui::icon(Icon ic, Rect r, Color c) {
    const float x = r.x, y = r.y, w = r.w, h = r.h, cx = x + w / 2, cy = y + h / 2, t = std::max(1.5f, w / 9);
    switch (ic) {
    case Icon::Play: g.tri(x + w * 0.2f, y, x + w * 0.2f, y + h, x + w, cy, c); break;
    case Icon::Pause: g.fill({x + w * 0.15f, y, w * 0.25f, h}, c); g.fill({x + w * 0.6f, y, w * 0.25f, h}, c); break;
    case Icon::Next: g.tri(x, y, x, y + h, x + w * 0.75f, cy, c); g.fill({x + w * 0.8f, y, w * 0.2f, h}, c); break;
    case Icon::Prev: g.tri(x + w, y, x + w, y + h, x + w * 0.25f, cy, c); g.fill({x, y, w * 0.2f, h}, c); break;
    case Icon::Shuffle:
        g.line(x, y + h * 0.25f, x + w * 0.35f, y + h * 0.25f, t, c); g.line(x + w * 0.35f, y + h * 0.25f, x + w * 0.65f, y + h * 0.75f, t, c); g.line(x + w * 0.65f, y + h * 0.75f, x + w * 0.85f, y + h * 0.75f, t, c);
        g.line(x, y + h * 0.75f, x + w * 0.35f, y + h * 0.75f, t, c); g.line(x + w * 0.35f, y + h * 0.75f, x + w * 0.65f, y + h * 0.25f, t, c); g.line(x + w * 0.65f, y + h * 0.25f, x + w * 0.85f, y + h * 0.25f, t, c);
        g.tri(x + w * 0.8f, y + h * 0.1f, x + w, y + h * 0.25f, x + w * 0.8f, y + h * 0.4f, c); g.tri(x + w * 0.8f, y + h * 0.6f, x + w, y + h * 0.75f, x + w * 0.8f, y + h * 0.9f, c);
        break;
    case Icon::Repeat: case Icon::RepeatOne: {
        g.stroke({x + w * 0.05f, y + h * 0.2f, w * 0.9f, h * 0.6f}, t, c);
        g.tri(x + w * 0.55f, y + h * 0.05f, x + w * 0.8f, y + h * 0.2f, x + w * 0.55f, y + h * 0.35f, c);
        if (ic == Icon::RepeatOne) g.fill({cx - t / 2, cy - h * 0.15f, t, h * 0.3f}, c);
        break;
    }
    case Icon::Heart: case Icon::HeartFilled: {
        float rr = w * 0.25f;
        if (ic == Icon::HeartFilled) {
            g.circle(x + w * 0.3f, y + h * 0.32f, rr, c); g.circle(x + w * 0.7f, y + h * 0.32f, rr, c);
            g.tri(x + w * 0.06f, y + h * 0.42f, x + w * 0.94f, y + h * 0.42f, cx, y + h * 0.95f, c);
        } else {
            g.ring(x + w * 0.3f, y + h * 0.32f, rr, t, c); g.ring(x + w * 0.7f, y + h * 0.32f, rr, t, c);
            g.line(x + w * 0.08f, y + h * 0.45f, cx, y + h * 0.95f, t, c); g.line(x + w * 0.92f, y + h * 0.45f, cx, y + h * 0.95f, t, c);
        }
        break;
    }
    case Icon::Volume: case Icon::Mute:
        g.fill({x, y + h * 0.35f, w * 0.25f, h * 0.3f}, c); g.tri(x + w * 0.25f, y + h * 0.35f, x + w * 0.55f, y + h * 0.1f, x + w * 0.55f, y + h * 0.9f, c);
        g.tri(x + w * 0.25f, y + h * 0.35f, x + w * 0.55f, y + h * 0.9f, x + w * 0.25f, y + h * 0.65f, c);
        if (ic == Icon::Volume) { g.line(x + w * 0.72f, y + h * 0.35f, x + w * 0.78f, y + h * 0.65f, t, c); g.line(x + w * 0.86f, y + h * 0.2f, x + w * 0.95f, y + h * 0.8f, t, c); }
        else { g.line(x + w * 0.7f, y + h * 0.3f, x + w, y + h * 0.7f, t, c); g.line(x + w, y + h * 0.3f, x + w * 0.7f, y + h * 0.7f, t, c); }
        break;
    case Icon::Queue: for (int i = 0; i < 3; ++i) g.fill({x, y + h * (0.1f + 0.3f * i), w * (i == 2 ? 0.6f : 1.0f), t}, c); break;
    case Icon::Home: g.tri(x, cy, cx, y, x + w, cy, c); g.fill({x + w * 0.15f, cy, w * 0.7f, h * 0.5f}, c); break;
    case Icon::Library: g.fill({x, y, w * 0.2f, h}, c); g.fill({x + w * 0.33f, y, w * 0.2f, h}, c); g.line(x + w * 0.65f, y + h * 0.05f, x + w * 0.95f, y + h * 0.95f, w * 0.2f, c); break;
    case Icon::Settings: g.ring(cx, cy, w * 0.3f, t * 1.4f, c); for (int i = 0; i < 4; ++i) { float a = 0.7853982f + i * 1.5707963f; g.line(cx + std::cos(a) * w * 0.38f, cy + std::sin(a) * h * 0.38f, cx + std::cos(a) * w * 0.5f, cy + std::sin(a) * h * 0.5f, t * 1.4f, c); } break;
    case Icon::Search: g.ring(x + w * 0.42f, y + h * 0.42f, w * 0.32f, t, c); g.line(x + w * 0.66f, y + h * 0.66f, x + w * 0.95f, y + h * 0.95f, t * 1.2f, c); break;
    case Icon::Close: g.line(x, y, x + w, y + h, t, c); g.line(x + w, y, x, y + h, t, c); break;
    case Icon::Back: g.line(x + w * 0.65f, y, x + w * 0.2f, cy, t * 1.2f, c); g.line(x + w * 0.2f, cy, x + w * 0.65f, y + h, t * 1.2f, c); break;
    case Icon::Download: g.fill({cx - t / 2, y, t, h * 0.6f}, c); g.tri(x + w * 0.2f, y + h * 0.45f, x + w * 0.8f, y + h * 0.45f, cx, y + h * 0.78f, c); g.fill({x + w * 0.1f, y + h * 0.9f, w * 0.8f, t}, c); break;
    case Icon::Plus: g.fill({cx - t / 2, y, t, h}, c); g.fill({x, cy - t / 2, w, t}, c); break;
    }
}

bool Ui::slider(int id, Rect r, float& v) {
    bool changed = false;
    if (in.pressed && r.contains(in.mx, in.my)) active = id;
    if (active == id) {
        if (in.down || in.released) {
            float nv = std::min(1.0f, std::max(0.0f, (in.mx - r.x) / r.w));
            if (nv != v) { v = nv; changed = true; }
            if (in.pressed) changed = true;
        }
        if (in.released) active = 0;
    }
    float ty = r.y + r.h / 2 - 2;
    g.fill_round({r.x, ty, r.w, 4}, 2, th.panel2);
    g.fill_round({r.x, ty, r.w * v, 4}, 2, th.accent);
    if (hover(r) || active == id) g.circle(r.x + r.w * v, ty + 2, 6, th.text);
    return changed;
}

namespace {
size_t prev_char(const std::string& s, size_t i) { if (i == 0) return 0; --i; while (i > 0 && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) --i; return i; }
size_t next_char(const std::string& s, size_t i) { if (i >= s.size()) return s.size(); ++i; while (i < s.size() && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) ++i; return i; }
}  // namespace

bool Ui::text_field(int id, Rect r, std::string& s, const char* placeholder, bool password, bool bg, float pad_l, int font) {
    bool changed = false;
    if (font < 0) font = f_body;
    if (pad_l < 0) pad_l = 12;
    if (in.pressed && r.contains(in.mx, in.my)) { focus = id; caret = s.size(); }
    else if (in.pressed && focus == id) focus = 0;
    bool f = focus == id;
    if (f) {
        if (caret > s.size()) caret = s.size();
        if (!in.text.empty()) { s.insert(caret, in.text); caret += in.text.size(); changed = true; }
        for (int i = 0; i < in.backspace && caret > 0; ++i) { size_t p = prev_char(s, caret); s.erase(p, caret - p); caret = p; changed = true; }
        for (int i = 0; i < in.del && caret < s.size(); ++i) { size_t n = next_char(s, caret); s.erase(caret, n - caret); changed = true; }
        for (int i = 0; i < in.left; ++i) caret = prev_char(s, caret);
        for (int i = 0; i < in.right; ++i) caret = next_char(s, caret);
        if (in.home) caret = 0;
        if (in.end) caret = s.size();
        wants_frames = true;
    }
    if (bg) {
        g.fill_round(r, 8, th.panel2);
        if (f) g.stroke_round(r, 8, 1.5f, th.accent);
    }
    std::string shown = password ? std::string() : s;
    size_t chars = 0;
    for (size_t i = 0; i < s.size(); i = next_char(s, i)) ++chars;
    if (password) shown.assign(chars, '*');
    float lh = g.line_height(font), ty = r.y + (r.h - lh) / 2;
    Rect clip{r.x + 4, r.y, r.w - 8, r.h};
    g.clip(&clip);
    if (shown.empty() && !f) g.text(font, r.x + pad_l, ty, placeholder, th.sub);
    else {
        float cw = 0;
        if (f) {
            size_t cc = 0;
            for (size_t i = 0; i < caret; i = next_char(s, i)) ++cc;
            cw = g.measure(font, password ? std::string(cc, '*') : s.substr(0, caret));
        }
        float off = (f && cw > r.w - pad_l - 16) ? cw - (r.w - pad_l - 16) : 0;
        g.text(font, r.x + pad_l - off, ty, shown, th.text);
        if (f && ((SDL_GetTicks() / 500) % 2 == 0)) g.fill({r.x + pad_l - off + cw, ty, 1.5f, lh}, th.text);
    }
    g.clip(nullptr);
    return changed;
}

void Ui::label_in(int font, Rect r, const std::string& s, Color c) {
    float lh = g.line_height(font);
    g.text(font, r.x, r.y + (r.h - lh) / 2, s, c, r.w);
}
