// Exact port of the old app's theme engine (types/theme.ts, utils/colorContrast.ts, ThemeContext).
#pragma once
#include <cstdint>
#include <string>
#include "ui.h"

struct ThemeDef {
    std::string name;
    uint32_t primary, primary_light, primary_dark, secondary, secondary_light, secondary_dark, accent, accent_light;
};
enum class ThemeMode : uint8_t { System, Light, Dark };

constexpr int kPresetCount = 8;
constexpr int kCustomSlots = 4;
const ThemeDef& preset(int i);                       // 0..7: cyan purple green red blue orange pink teal
ThemeDef default_custom(int slot);                   // "My Theme N", cyan colours
ThemeDef make_custom(uint32_t primary, const std::string& name);  // generateColorVariants (+/-50 per channel)
uint32_t color_variant(uint32_t c, int delta);

float contrast_ratio(uint32_t a, uint32_t b);
uint32_t ensure_contrast(uint32_t hex, uint32_t bg, float min);  // walks lightness until `min` is reached
uint32_t on_color(uint32_t hex);                     // black or white, whichever reads better
uint32_t blend(uint32_t fg, uint32_t bg, float alpha);

struct Neutrals { uint32_t accent, text_secondary, text_muted; Color border, border_strong; };
Neutrals derive_neutrals(bool light, uint32_t accent_hex);

// Builds the full token set for a theme in a mode (what ThemeContext.applyTheme sets on <html>).
Theme make_theme(const ThemeDef& d, bool light);
