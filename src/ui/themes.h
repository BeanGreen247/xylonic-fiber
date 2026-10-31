// Theme derivation: one accent colour + light/dark gives every UI colour, with contrast guaranteed (WCAG ratios).
#pragma once
#include <cstdint>
#include "ui.h"

struct ThemePreset { const char* name; uint32_t accent; };
constexpr int kPresetCount = 8;
constexpr int kCustomSlots = 4;
const ThemePreset& preset(int i);

float luminance(uint32_t rgb);
float contrast(uint32_t a, uint32_t b);  // WCAG contrast ratio, 1..21
Theme make_theme(uint32_t accent, bool light);
