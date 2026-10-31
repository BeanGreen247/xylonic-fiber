#include "themes.h"
#include <algorithm>
#include <cmath>

namespace {
const ThemePreset kPresets[kPresetCount] = {
    {"Cyan Wave", 0x22c3d6}, {"Purple Dream", 0x9575f0}, {"Forest Green", 0x4caf6a}, {"Crimson Fire", 0xe5484d},
    {"Ocean Blue", 0x4a8df0}, {"Sunset Orange", 0xf2893a}, {"Bubblegum Pink", 0xf06aa8}, {"Tropical Teal", 0x20b2a0},
};
float chan(float c) { return c <= 0.03928f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }
uint32_t mix(uint32_t a, uint32_t b, float t) {
    auto ch = [&](int sh) { return static_cast<uint32_t>(std::lround(((a >> sh) & 255) * (1 - t) + ((b >> sh) & 255) * t)); };
    return (ch(16) << 16) | (ch(8) << 8) | ch(0);
}
// Moves `fg` toward black or white (whichever raises contrast) until it reaches `min_ratio` against `bg`.
uint32_t ensure_contrast(uint32_t fg, uint32_t bg, float min_ratio) {
    uint32_t target = luminance(bg) > 0.5f ? 0x000000u : 0xffffffu;
    for (int i = 0; i <= 20 && contrast(fg, bg) < min_ratio; ++i) fg = mix(fg, target, 0.1f);
    return fg;
}
}  // namespace

const ThemePreset& preset(int i) { return kPresets[i < 0 || i >= kPresetCount ? 0 : i]; }

float luminance(uint32_t c) {
    return 0.2126f * chan(((c >> 16) & 255) / 255.0f) + 0.7152f * chan(((c >> 8) & 255) / 255.0f) + 0.0722f * chan((c & 255) / 255.0f);
}

float contrast(uint32_t a, uint32_t b) {
    float la = luminance(a), lb = luminance(b);
    if (la < lb) std::swap(la, lb);
    return (la + 0.05f) / (lb + 0.05f);
}

Theme make_theme(uint32_t accent, bool light) {
    uint32_t bg, panel, panel2, hover, text, sub;
    if (light) {
        bg = mix(0xf6f7fb, accent, 0.04f); panel = 0xffffff; panel2 = mix(0xe9ebf3, accent, 0.06f); hover = mix(0xdfe2ee, accent, 0.08f);
        text = 0x151823; sub = 0x4f566b;
    } else {
        bg = mix(0x0d0f16, accent, 0.05f); panel = mix(0x171a24, accent, 0.05f); panel2 = mix(0x212535, accent, 0.07f); hover = mix(0x2a2f43, accent, 0.09f);
        text = 0xeef0f6; sub = 0xa3a9bd;
    }
    sub = ensure_contrast(sub, panel2, 4.5f);   // subdued text stays readable on the raised surfaces too
    sub = ensure_contrast(sub, bg, 4.5f);
    uint32_t acc = ensure_contrast(accent, panel2, 3.0f);
    acc = ensure_contrast(acc, bg, 3.0f);
    // Text on the accent: white on dark accents (light themes), near-black on light accents (dark themes).
    // The accent moves away from that text colour if needed, which also keeps it distinct from the background.
    const uint32_t on_acc = light ? 0xffffffu : 0x06161au;
    for (int i = 0; i < 30 && contrast(on_acc, acc) < 4.5f; ++i) acc = mix(acc, light ? 0x000000u : 0xffffffu, 0.08f);
    Theme t;
    t.bg = rgb(bg); t.panel = rgb(panel); t.panel2 = rgb(panel2); t.hover = rgb(hover);
    t.text = rgb(text); t.sub = rgb(sub); t.accent = rgb(acc); t.accent_text = rgb(on_acc);
    t.danger = rgb(ensure_contrast(0xef5350, panel, 3.0f));
    return t;
}
