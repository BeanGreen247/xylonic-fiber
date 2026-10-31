// Renders a scene on the software renderer (no display needed), checks pixels, and writes a BMP when given a path.
#include <cstdio>
#include "../src/ui/gfx.h"

static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

int main(int argc, char** argv) {
    SDL_Init(SDL_INIT_VIDEO);
    SDL_Window* w = SDL_CreateWindow("t", 0, 0, 1024, 768, SDL_WINDOW_HIDDEN);
    SDL_Renderer* r = SDL_CreateRenderer(w, -1, SDL_RENDERER_SOFTWARE);
    CHECK(r != nullptr);
    Gfx g;
    CHECK(g.init(w, r));
    int f = g.add_font("assets/fonts/Inter-Regular.ttf", 18), fb = g.add_font("assets/fonts/Inter-SemiBold.ttf", 28);
    CHECK(f >= 0 && fb >= 0);
    CHECK(g.add_font("nope.ttf", 12) == -1);
    float w1 = g.measure(f, "Hello"), w2 = g.measure(f, "Hello, world");
    CHECK(w1 > 20 && w2 > w1);
    CHECK(g.measure(f, "é\xE2\x80\x94") > 0);  // multibyte
    g.begin_frame(rgb(0x14141e));
    g.fill_round({20, 20, 300, 60}, 12, rgb(0x2a2a40));
    g.text(fb, 36, 28, "Xylonic fiber", rgb(0xffffff));
    g.text(f, 36, 100, "Truncated text that is much longer than its box allows", rgb(0xcccccc), 200);
    g.stroke({20, 150, 200, 40}, 2, rgb(0x00bcd4));
    g.fill({400, 400, 50, 50}, rgb(0xff0000));
    g.end_frame();
    CHECK(g.draw_calls() == 1);  // everything batched into one draw call
    SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, 1024, 768, 32, SDL_PIXELFORMAT_ARGB8888);
    SDL_RenderReadPixels(r, nullptr, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch);
    auto px = [&](int x, int y) { return static_cast<uint32_t*>(s->pixels)[y * (s->pitch / 4) + x] & 0xFFFFFF; };
    CHECK(px(425, 425) == 0xff0000);
    CHECK(px(900, 700) == 0x14141e);
    CHECK(px(150, 50) == 0x2a2a40 || px(150, 50) != 0x14141e);  // inside the rounded panel
    CHECK(px(21, 21) == 0x14141e);                              // rounded corner is cut
    int lit = 0;
    for (int y = 28; y < 60; ++y) for (int x = 36; x < 220; ++x) if (px(x, y) > 0xA0A0A0) ++lit;
    CHECK(lit > 200);  // title glyphs were rasterised
    if (argc > 1) SDL_SaveBMP(s, argv[1]);
    std::printf(fails ? "FAILED\n" : "OK\n");
    return fails ? 1 : 0;
}
