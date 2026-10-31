// Ported from the old app's colorContrast.test.ts, plus exact preset values.
#include <cstdio>
#include "../src/ui/themes.h"
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)
static bool near(float a, float b, float eps) { return a > b - eps && a < b + eps; }
int main() {
    CHECK(near(contrast_ratio(0x000000, 0xffffff), 21, 0.5f) && near(contrast_ratio(0x777777, 0x777777), 1, 1e-4f));
    CHECK(ensure_contrast(0x4caf50, 0x2a2a2a, 4.5f) == 0x4caf50);
    uint32_t f = ensure_contrast(0x00bcd4, 0xf0f0f2, 4.5f);
    CHECK(f != 0x00bcd4 && contrast_ratio(f, 0xf0f0f2) >= 4.5f);
    uint32_t g = ensure_contrast(0x9c27b0, 0x2a2a2a, 4.5f);
    CHECK(contrast_ratio(g, 0x2a2a2a) >= 4.5f);
    CHECK(on_color(0xffeb3b) == 0x000000 && on_color(0x1a237e) == 0xffffff);
    // custom theme variants
    ThemeDef c = make_custom(0x1020f0, "X");
    CHECK(c.primary_light == 0x4252ff && c.primary_dark == 0x0000be && c.accent == c.primary_light && c.secondary == 0x1020f0);
    CHECK(default_custom(2).name == "My Theme 3" && default_custom(0).primary == 0x00bcd4);
    // every preset, both modes (deriveNeutrals contract)
    for (int light = 0; light < 2; ++light) {
        uint32_t surface = light ? 0xffffff : 0x1e1e1e, variant = light ? 0xf0f0f2 : 0x2a2a2a;
        for (int i = 0; i < kPresetCount; ++i) {
            uint32_t acc = preset(i).primary;
            Neutrals n = derive_neutrals(light, acc);
            uint32_t bgs[4] = {surface, variant, blend(acc, surface, 0.16f), blend(acc, variant, 0.22f)};
            for (uint32_t b : bgs) {
                CHECK(contrast_ratio(n.text_muted, b) >= 4.5f);
                CHECK(contrast_ratio(n.text_secondary, b) >= contrast_ratio(n.text_muted, b) - 0.01f);
                CHECK(contrast_ratio(n.accent, b) >= 4.5f);
            }
        }
    }
    Theme t = make_theme(preset(1), false);
    CHECK(t.bg.r == 0x12 && t.panel.r == 0x1e && t.panel2.r == 0x2a && !t.light);
    Theme l = make_theme(preset(1), true);
    CHECK(l.bg.r == 0xf7 && l.text.r == 0x1b && l.light);
    std::printf(fails ? "FAILED\n" : "OK\n");
    return fails ? 1 : 0;
}
