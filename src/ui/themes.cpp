#include "themes.h"
#include <algorithm>
#include <cmath>

namespace {
const ThemeDef kPresets[kPresetCount] = {
    {"Cyan Wave", 0x00bcd4, 0x4dd0e1, 0x0097a7, 0x03a9f4, 0x4fc3f7, 0x0288d1, 0x00e5ff, 0x84ffff},
    {"Purple Dream", 0x9c27b0, 0xba68c8, 0x7b1fa2, 0xab47bc, 0xce93d8, 0x8e24aa, 0xe040fb, 0xea80fc},
    {"Forest Green", 0x4caf50, 0x81c784, 0x388e3c, 0x66bb6a, 0xa5d6a7, 0x43a047, 0x69f0ae, 0xb9f6ca},
    {"Crimson Fire", 0xf44336, 0xe57373, 0xd32f2f, 0xef5350, 0xef9a9a, 0xc62828, 0xff5252, 0xff8a80},
    {"Ocean Blue", 0x2196f3, 0x64b5f6, 0x1976d2, 0x42a5f5, 0x90caf9, 0x1565c0, 0x448aff, 0x82b1ff},
    {"Sunset Orange", 0xff9800, 0xffb74d, 0xf57c00, 0xffa726, 0xffcc80, 0xef6c00, 0xffab40, 0xffd180},
    {"Bubblegum Pink", 0xe91e63, 0xf06292, 0xc2185b, 0xec407a, 0xf48fb1, 0xad1457, 0xff4081, 0xff80ab},
    {"Tropical Teal", 0x009688, 0x4db6ac, 0x00796b, 0x26a69a, 0x80cbc4, 0x00695c, 0x64ffda, 0xa7ffeb},
};

struct Surfaces { uint32_t surface, variant, bg, ink; };
const Surfaces kDark = {0x1e1e1e, 0x2a2a2a, 0x121212, 0xffffff};
const Surfaces kLight = {0xffffff, 0xf0f0f2, 0xf7f7f8, 0x1b1b1f};

struct Rgb { float r, g, b; };
Rgb unpack(uint32_t c) { return {float((c >> 16) & 255), float((c >> 8) & 255), float(c & 255)}; }
uint32_t pack(float r, float g, float b) {
    auto q = [](float v) { return static_cast<uint32_t>(std::lround(std::max(0.0f, std::min(255.0f, v)))); };
    return (q(r) << 16) | (q(g) << 8) | q(b);
}
float lum(uint32_t c) {
    Rgb v = unpack(c);
    auto lin = [](float x) { float s = x / 255.0f; return s <= 0.03928f ? s / 12.92f : std::pow((s + 0.055f) / 1.055f, 2.4f); };
    return 0.2126f * lin(v.r) + 0.7152f * lin(v.g) + 0.0722f * lin(v.b);
}
struct Hsl { float h, s, l; };
Hsl to_hsl(uint32_t c) {
    Rgb v = unpack(c);
    float rn = v.r / 255, gn = v.g / 255, bn = v.b / 255;
    float mx = std::max({rn, gn, bn}), mn = std::min({rn, gn, bn}), l = (mx + mn) / 2;
    if (mx == mn) return {0, 0, l};
    float d = mx - mn, s = l > 0.5f ? d / (2 - mx - mn) : d / (mx + mn);
    float h = mx == rn ? (gn - bn) / d + (gn < bn ? 6 : 0) : mx == gn ? (bn - rn) / d + 2 : (rn - gn) / d + 4;
    return {h * 60, s, l};
}
uint32_t from_hsl(float h, float s, float l) {
    auto k = [&](float n) { return std::fmod(n + h / 30, 12.0f); };
    float a = s * std::min(l, 1 - l);
    auto f = [&](float n) { return l - a * std::max(-1.0f, std::min(std::min(k(n) - 3, 9 - k(n)), 1.0f)); };
    return pack(f(0) * 255, f(8) * 255, f(4) * 255);
}
Color rgba_of(uint32_t ink, float a) { Rgb v = unpack(ink); return {uint8_t(v.r), uint8_t(v.g), uint8_t(v.b), uint8_t(std::lround(a * 255))}; }
Color solid(uint32_t c) { return rgb(c); }
}  // namespace

const ThemeDef& preset(int i) { return kPresets[i < 0 || i >= kPresetCount ? 0 : i]; }

ThemeDef default_custom(int slot) {
    ThemeDef d = kPresets[0];
    d.name = "My Theme " + std::to_string(slot + 1);
    return d;
}

uint32_t color_variant(uint32_t c, int delta) {
    Rgb v = unpack(c);
    return pack(v.r + delta, v.g + delta, v.b + delta);  // pack clamps to 0..255
}

ThemeDef make_custom(uint32_t primary, const std::string& name) {
    uint32_t light = color_variant(primary, 50), dark = color_variant(primary, -50);
    return {name, primary, light, dark, primary, light, dark, light, light};  // as ThemeContext.updateCustomTheme
}

float contrast_ratio(uint32_t a, uint32_t b) {
    float hi = lum(a), lo = lum(b);
    if (hi < lo) std::swap(hi, lo);
    return (hi + 0.05f) / (lo + 0.05f);
}

uint32_t ensure_contrast(uint32_t hex, uint32_t bg, float min) {
    if (contrast_ratio(hex, bg) >= min) return hex;
    Hsl c = to_hsl(hex);
    bool lighten = lum(bg) < 0.4f;
    uint32_t best = hex;
    for (int step = 1; step <= 100; ++step) {
        float nl = lighten ? std::min(1.0f, c.l + step / 100.0f) : std::max(0.0f, c.l - step / 100.0f);
        best = from_hsl(c.h, c.s, nl);
        if (contrast_ratio(best, bg) >= min) return best;
        if (nl == 0 || nl == 1) break;
    }
    return best;
}

uint32_t on_color(uint32_t hex) { return contrast_ratio(hex, 0x000000) >= contrast_ratio(hex, 0xffffff) ? 0x000000u : 0xffffffu; }

uint32_t blend(uint32_t fg, uint32_t bg, float alpha) {
    Rgb a = unpack(fg), b = unpack(bg);
    return pack(a.r * alpha + b.r * (1 - alpha), a.g * alpha + b.g * (1 - alpha), a.b * alpha + b.b * (1 - alpha));
}

Neutrals derive_neutrals(bool light, uint32_t accent_hex) {
    const Surfaces& m = light ? kLight : kDark;
    uint32_t bgs[5] = {m.surface, m.variant, m.bg, blend(accent_hex, m.surface, 0.16f), blend(accent_hex, m.variant, 0.22f)};
    uint32_t accent = ensure_contrast(accent_hex, m.variant, 4.5f);
    for (uint32_t b : bgs) accent = ensure_contrast(accent, b, 4.5f);
    auto worst = [&](uint32_t fg) { float w = 1e9f; for (uint32_t b : bgs) w = std::min(w, contrast_ratio(fg, b)); return w; };
    auto ink = [&](float floor, float target) {
        for (float t = floor; t <= 1.0001f; t += 0.02f) {
            uint32_t c = blend(m.ink, m.surface, std::min(1.0f, t));
            if (worst(c) >= target) return c;
        }
        return m.ink;
    };
    auto edge = [&](float floor, float target) {
        for (float a = floor; a <= 0.6f; a += 0.01f) {
            float w = 1e9f;
            for (uint32_t b : bgs) w = std::min(w, contrast_ratio(blend(m.ink, b, a), b));
            if (w >= target) return rgba_of(m.ink, std::round(a * 100) / 100);
        }
        return rgba_of(m.ink, 0.6f);
    };
    return {accent, ink(0.7f, 7), ink(0.55f, 4.5f), edge(0.2f, 1.8f), edge(0.32f, 3)};
}

Theme make_theme(const ThemeDef& d, bool light) {
    const Surfaces& m = light ? kLight : kDark;
    Neutrals n = derive_neutrals(light, d.primary);
    Theme t;
    t.light = light;
    t.bg = solid(m.bg);
    t.bg_medium = solid(light ? 0xefeff1 : 0x1e1e1e);
    t.bg_light = solid(light ? 0xe4e4e7 : 0x2a2a2a);
    t.panel = solid(m.surface);
    t.panel2 = solid(m.variant);
    t.text = solid(m.ink);
    t.sub = solid(n.text_secondary);
    t.muted = solid(n.text_muted);
    t.border_subtle = light ? Color{0, 0, 0, 31} : Color{255, 255, 255, 31};
    t.border = n.border;
    t.border_strong = n.border_strong;
    t.hover = light ? Color{0, 0, 0, 10} : Color{255, 255, 255, 15};
    t.active = light ? Color{0, 0, 0, 18} : Color{255, 255, 255, 33};
    t.scrim = light ? Color{0, 0, 0, 82} : Color{0, 0, 0, 140};
    t.accent = solid(n.accent);
    t.primary_light = solid(ensure_contrast(d.primary_light, m.variant, 4.5f));
    t.primary_dark = solid(d.primary_dark);
    t.secondary = solid(ensure_contrast(d.secondary, m.variant, 4.5f));
    t.secondary_light = solid(d.secondary_light);
    t.secondary_dark = solid(d.secondary_dark);
    t.accent2 = solid(ensure_contrast(d.accent, m.variant, 4.5f));
    t.accent2_light = solid(ensure_contrast(d.accent_light, m.variant, 3.0f));
    t.accent_text = solid(on_color(n.accent));
    return t;
}
