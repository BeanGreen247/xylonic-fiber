#include <cstdio>
#include "../src/ui/themes.h"
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)
static uint32_t pack(Color c) { return (uint32_t(c.r) << 16) | (uint32_t(c.g) << 8) | c.b; }
int main() {
    CHECK(contrast(0xffffff, 0x000000) > 20.9f && contrast(0x777777, 0x777777) < 1.01f);
    uint32_t extra[] = {0xffff00, 0x000000, 0xffffff, 0x808080, 0x00ff00};  // custom colours are arbitrary
    for (int light = 0; light < 2; ++light) {
        for (int i = 0; i < kPresetCount + 5; ++i) {
            uint32_t acc = i < kPresetCount ? preset(i).accent : extra[i - kPresetCount];
            Theme t = make_theme(acc, light);
            uint32_t bg = pack(t.bg), p2 = pack(t.panel2), pn = pack(t.panel);
            if (!(contrast(pack(t.text), bg) >= 7.0f && contrast(pack(t.text), p2) >= 7.0f && contrast(pack(t.sub), bg) >= 4.5f &&
                  contrast(pack(t.sub), p2) >= 4.5f && contrast(pack(t.accent), bg) >= 3.0f && contrast(pack(t.accent), p2) >= 3.0f &&
                  contrast(pack(t.accent_text), pack(t.accent)) >= 4.5f && contrast(pack(t.danger), pn) >= 3.0f)) {
                std::printf("FAIL contrast preset=%d light=%d\n", i, light);
                ++fails;
            }
        }
    }
    std::printf(fails ? "FAILED\n" : "OK\n");
    return fails ? 1 : 0;
}
