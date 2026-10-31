#define NANOSVG_IMPLEMENTATION
#define NANOSVGRAST_IMPLEMENTATION
#include "svg.h"
#include <vector>
#include "../../third_party/nanosvg.h"
#include "../../third_party/nanosvgrast.h"

SDL_Texture* svg_texture_from_file(SDL_Renderer* r, const char* path, int w, int h) {
    SDL_RWops* rw = SDL_RWFromFile(path, "rb");
    if (!rw) return nullptr;
    Sint64 n = SDL_RWsize(rw);
    if (n <= 0) { SDL_RWclose(rw); return nullptr; }
    std::vector<char> buf(static_cast<size_t>(n) + 1, 0);
    SDL_RWread(rw, buf.data(), 1, static_cast<size_t>(n));
    SDL_RWclose(rw);
    NSVGimage* img = nsvgParse(buf.data(), "px", 96.0f);
    if (!img) return nullptr;
    NSVGrasterizer* rast = nsvgCreateRasterizer();
    SDL_Texture* tex = nullptr;
    if (rast && img->width > 0 && img->height > 0) {
        std::vector<unsigned char> px(static_cast<size_t>(w) * h * 4, 0);
        float scale = std::min(w / img->width, h / img->height);
        nsvgRasterize(rast, img, 0, 0, scale, px.data(), w, h, w * 4);
        tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, w, h);
        if (tex) {
            SDL_UpdateTexture(tex, nullptr, px.data(), w * 4);
            SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
            SDL_SetTextureScaleMode(tex, SDL_ScaleModeLinear);
        }
    }
    if (rast) nsvgDeleteRasterizer(rast);
    nsvgDelete(img);
    return tex;
}
