#define STB_TRUETYPE_IMPLEMENTATION
#include "gfx.h"
#include <cmath>
#include "../../third_party/stb_truetype.h"


class FontFace {
public:
    std::shared_ptr<std::vector<uint8_t>> data;  // shared between sizes of the same file
    stbtt_fontinfo info{};
    float size = 16, scale = 1;
    int ascent = 0, descent = 0, gap = 0;
    std::unordered_map<uint32_t, Gfx::Glyph> glyphs;
};

Gfx::Gfx() = default;
Gfx::~Gfx() { if (atlas_) SDL_DestroyTexture(atlas_); }

bool Gfx::init(SDL_Window*, SDL_Renderer* ren) {
    ren_ = ren;
    atlas_ = SDL_CreateTexture(ren_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, aw_, ah_);
    if (!atlas_) return false;
    SDL_SetTextureBlendMode(atlas_, SDL_BLENDMODE_BLEND);
    reset_atlas();
    return true;
}

void Gfx::reset_atlas() {
    std::vector<uint32_t> blank(static_cast<size_t>(aw_) * ah_, 0);
    for (int y = 0; y < 2; ++y) for (int x = 0; x < 2; ++x) blank[y * aw_ + x] = 0xFFFFFFFFu;  // white texel
    SDL_UpdateTexture(atlas_, nullptr, blank.data(), aw_ * 4);
    shelf_x_ = 3; shelf_y_ = 0; shelf_h_ = 0;
    shelf_y_ = 3;
    for (auto& f : fonts_) f->glyphs.clear();
}

int Gfx::add_font(const char* path, float px) {
    SDL_RWops* rw = SDL_RWFromFile(path, "rb");
    if (!rw) return -1;
    auto f = std::make_unique<FontFace>();
    auto cached = font_data_.find(path);
    if (cached != font_data_.end()) {
        SDL_RWclose(rw);
        f->data = cached->second;
    } else {
        Sint64 n = SDL_RWsize(rw);
        if (n <= 0) { SDL_RWclose(rw); return -1; }
        f->data = std::make_shared<std::vector<uint8_t>>(static_cast<size_t>(n));
        size_t got = SDL_RWread(rw, f->data->data(), 1, f->data->size());
        SDL_RWclose(rw);
        if (got != f->data->size()) return -1;
        font_data_[path] = f->data;
    }
    if (!stbtt_InitFont(&f->info, f->data->data(), stbtt_GetFontOffsetForIndex(f->data->data(), 0))) return -1;
    f->size = px;
    f->scale = stbtt_ScaleForPixelHeight(&f->info, px);
    stbtt_GetFontVMetrics(&f->info, &f->ascent, &f->descent, &f->gap);
    fonts_.push_back(std::move(f));
    return static_cast<int>(fonts_.size()) - 1;
}

bool Gfx::pack(int w, int h, int& x, int& y) {
    if (shelf_x_ + w + 1 > aw_) { shelf_y_ += shelf_h_ + 1; shelf_x_ = 1; shelf_h_ = 0; }
    if (shelf_y_ + h + 1 > ah_) return false;
    x = shelf_x_; y = shelf_y_;
    shelf_x_ += w + 1;
    if (h > shelf_h_) shelf_h_ = h;
    return true;
}

const Gfx::Glyph* Gfx::glyph(int fi, uint32_t cp) {
    if (fi < 0 || fi >= static_cast<int>(fonts_.size())) return nullptr;
    FontFace& f = *fonts_[fi];
    auto it = f.glyphs.find(cp);
    if (it != f.glyphs.end()) return &it->second;
    int gi = stbtt_FindGlyphIndex(&f.info, static_cast<int>(cp));
    if (gi == 0 && cp != 0xFFFD && cp != '?') return glyph(fi, '?');
    int adv, lsb, x0, y0, x1, y1;
    stbtt_GetGlyphHMetrics(&f.info, gi, &adv, &lsb);
    stbtt_GetGlyphBitmapBox(&f.info, gi, f.scale, f.scale, &x0, &y0, &x1, &y1);
    Glyph g{};
    g.adv = adv * f.scale;
    g.xoff = static_cast<float>(x0);
    g.yoff = static_cast<float>(y0);
    int w = x1 - x0, h = y1 - y0;
    if (w > 0 && h > 0) {
        int ax, ay;
        if (!pack(w, h, ax, ay)) { flush(); reset_atlas(); if (!pack(w, h, ax, ay)) return nullptr; }
        std::vector<uint8_t> a(static_cast<size_t>(w) * h);
        stbtt_MakeGlyphBitmap(&f.info, a.data(), w, h, w, f.scale, f.scale, gi);
        std::vector<uint32_t> px(a.size());
        for (size_t i = 0; i < a.size(); ++i) px[i] = 0x00FFFFFFu | (uint32_t(a[i]) << 24);  // RGBA32 little-endian: A in top byte
        SDL_Rect r{ax, ay, w, h};
        SDL_UpdateTexture(atlas_, &r, px.data(), w * 4);
        g.u0 = float(ax) / aw_; g.v0 = float(ay) / ah_; g.u1 = float(ax + w) / aw_; g.v1 = float(ay + h) / ah_;
        g.w = float(w); g.h = float(h);
    }
    return &(f.glyphs[cp] = g);
}

void Gfx::begin_frame(Color c) {
    SDL_SetRenderDrawColor(ren_, c.r, c.g, c.b, c.a);
    SDL_RenderClear(ren_);
    v_.clear(); idx_.clear();
}

void Gfx::flush() {
    if (v_.empty()) return;
    SDL_RenderGeometry(ren_, atlas_, v_.data(), static_cast<int>(v_.size()), idx_.data(), static_cast<int>(idx_.size()));
    ++draw_calls_;
    v_.clear(); idx_.clear();
}

void Gfx::end_frame() { flush(); SDL_RenderPresent(ren_); }

void Gfx::quad(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, Color c) {
    int b = static_cast<int>(v_.size());
    SDL_Color sc{c.r, c.g, c.b, c.a};
    v_.push_back({{x0, y0}, sc, {u0, v0}});
    v_.push_back({{x1, y0}, sc, {u1, v0}});
    v_.push_back({{x1, y1}, sc, {u1, v1}});
    v_.push_back({{x0, y1}, sc, {u0, v1}});
    for (int i : {0, 1, 2, 0, 2, 3}) idx_.push_back(b + i);
}

void Gfx::fill(Rect r, Color c) {
    const float u = 1.0f / aw_, v = 1.0f / ah_;  // centre of the white texel block
    quad(r.x, r.y, r.x + r.w, r.y + r.h, u, v, u, v, c);
}

void Gfx::fill_round(Rect r, float rad, Color c) {
    if (rad > r.w / 2) rad = r.w / 2;
    if (rad > r.h / 2) rad = r.h / 2;
    if (rad < 1.5f) { fill(r, c); return; }
    const int segs = rad > 10 ? 6 : 4;
    const float u = 1.0f / aw_, v = 1.0f / ah_;
    SDL_Color sc{c.r, c.g, c.b, c.a};
    int base = static_cast<int>(v_.size());
    v_.push_back({{r.x + r.w / 2, r.y + r.h / 2}, sc, {u, v}});  // fan centre
    const float cx[4] = {r.x + r.w - rad, r.x + rad, r.x + rad, r.x + r.w - rad};
    const float cy[4] = {r.y + rad, r.y + rad, r.y + r.h - rad, r.y + r.h - rad};
    int n = 0;
    for (int k = 0; k < 4; ++k)
        for (int s = 0; s <= segs; ++s) {
            float a = (float(k) + float(s) / segs) * 1.5707963f;
            v_.push_back({{cx[k] + std::cos(a) * rad, cy[k] - std::sin(a) * rad}, sc, {u, v}});
            ++n;
        }
    for (int i = 0; i < n; ++i) {
        idx_.push_back(base);
        idx_.push_back(base + 1 + i);
        idx_.push_back(base + 1 + (i + 1) % n);
    }
}

void Gfx::tri(float x0, float y0, float x1, float y1, float x2, float y2, Color c) {
    const float u = 1.0f / aw_, v = 1.0f / ah_;
    SDL_Color sc{c.r, c.g, c.b, c.a};
    int b = static_cast<int>(v_.size());
    v_.push_back({{x0, y0}, sc, {u, v}});
    v_.push_back({{x1, y1}, sc, {u, v}});
    v_.push_back({{x2, y2}, sc, {u, v}});
    idx_.push_back(b); idx_.push_back(b + 1); idx_.push_back(b + 2);
}

void Gfx::line(float x0, float y0, float x1, float y1, float t, Color c) {
    float dx = x1 - x0, dy = y1 - y0, l = std::sqrt(dx * dx + dy * dy);
    if (l < 0.001f) return;
    float nx = -dy / l * t / 2, ny = dx / l * t / 2;
    tri(x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, c);
    tri(x0 + nx, y0 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny, c);
}

void Gfx::circle(float cx, float cy, float r, Color c) {
    int n = r < 8 ? 12 : r < 24 ? 20 : 32;
    for (int i = 0; i < n; ++i) {
        float a0 = 6.2831853f * i / n, a1 = 6.2831853f * (i + 1) / n;
        tri(cx, cy, cx + std::cos(a0) * r, cy + std::sin(a0) * r, cx + std::cos(a1) * r, cy + std::sin(a1) * r, c);
    }
}

void Gfx::ring(float cx, float cy, float r, float t, Color c) {
    int n = r < 8 ? 12 : r < 24 ? 20 : 32;
    for (int i = 0; i < n; ++i) {
        float a0 = 6.2831853f * i / n, a1 = 6.2831853f * (i + 1) / n;
        line(cx + std::cos(a0) * r, cy + std::sin(a0) * r, cx + std::cos(a1) * r, cy + std::sin(a1) * r, t, c);
    }
}

void Gfx::stroke_round(Rect r, float rad, float t, Color c) {
    rad = std::min(rad, std::min(r.w, r.h) / 2);
    if (rad < 1.5f) { stroke(r, t, c); return; }
    const int segs = rad > 10 ? 8 : 5;
    const float u = 1.0f / aw_, v = 1.0f / ah_;
    SDL_Color sc{c.r, c.g, c.b, c.a};
    int base = static_cast<int>(v_.size()), n = 0;
    const float ri = std::max(0.0f, rad - t);
    const float cx[4] = {r.x + r.w - rad, r.x + rad, r.x + rad, r.x + r.w - rad};
    const float cy[4] = {r.y + rad, r.y + rad, r.y + r.h - rad, r.y + r.h - rad};
    for (int k = 0; k < 4; ++k)
        for (int s = 0; s <= segs; ++s) {
            float a = (float(k) + float(s) / segs) * 1.5707963f, ca = std::cos(a), sa = -std::sin(a);
            v_.push_back({{cx[k] + ca * rad, cy[k] + sa * rad}, sc, {u, v}});
            v_.push_back({{cx[k] + ca * ri, cy[k] + sa * ri}, sc, {u, v}});
            ++n;
        }
    for (int i = 0; i < n; ++i) {
        int a = base + 2 * i, b = base + 2 * ((i + 1) % n);
        for (int q : {a, a + 1, b, a + 1, b + 1, b}) idx_.push_back(q);
    }
}

void Gfx::stroke(Rect r, float t, Color c) {
    fill({r.x, r.y, r.w, t}, c);
    fill({r.x, r.y + r.h - t, r.w, t}, c);
    fill({r.x, r.y + t, t, r.h - 2 * t}, c);
    fill({r.x + r.w - t, r.y + t, t, r.h - 2 * t}, c);
}

void Gfx::image(SDL_Texture* tex, Rect d, Color tint) {
    flush();
    SDL_SetTextureColorMod(tex, tint.r, tint.g, tint.b);
    SDL_SetTextureAlphaMod(tex, tint.a);
    SDL_FRect dst{d.x, d.y, d.w, d.h};
    SDL_RenderCopyF(ren_, tex, nullptr, &dst);
    ++draw_calls_;
}

void Gfx::clip(const Rect* r) {
    flush();
    if (!r) { SDL_RenderSetClipRect(ren_, nullptr); return; }
    SDL_Rect c{int(r->x), int(r->y), int(r->w), int(r->h)};
    SDL_RenderSetClipRect(ren_, &c);
}

namespace {
// Decodes one UTF-8 code point; returns bytes consumed (1 on invalid input).
size_t utf8(const std::string& s, size_t i, uint32_t& cp) {
    unsigned char c = static_cast<unsigned char>(s[i]);
    size_t n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 1;
    if (n == 1 || i + n > s.size()) { cp = c < 0x80 ? c : 0xFFFD; return 1; }
    cp = c & (0xFF >> (n + 1));
    for (size_t k = 1; k < n; ++k) {
        unsigned char d = static_cast<unsigned char>(s[i + k]);
        if ((d & 0xC0) != 0x80) { cp = 0xFFFD; return 1; }
        cp = (cp << 6) | (d & 0x3F);
    }
    return n;
}
}  // namespace

float Gfx::measure(int font, const std::string& s) {
    float w = 0;
    for (size_t i = 0; i < s.size();) {
        uint32_t cp;
        i += utf8(s, i, cp);
        if (const Glyph* g = glyph(font, cp)) w += g->adv + spacing_;
    }
    return w;
}

float Gfx::line_height(int font) const {
    if (font < 0 || font >= static_cast<int>(fonts_.size())) return 0;
    const FontFace& f = *fonts_[font];
    return (f.ascent - f.descent + f.gap) * f.scale;
}

float Gfx::text(int font, float x, float y, const std::string& s, Color c, float max_w) {
    if (font < 0 || font >= static_cast<int>(fonts_.size())) return 0;
    const FontFace& f = *fonts_[font];
    float base = y + f.ascent * f.scale;
    float pen = x;
    float ell = 0;
    bool trunc = false;
    if (max_w > 0 && measure(font, s) > max_w) {
        trunc = true;
        if (const Glyph* d = glyph(font, '.')) ell = d->adv * 3;
    }
    for (size_t i = 0; i < s.size();) {
        uint32_t cp;
        i += utf8(s, i, cp);
        const Glyph* g = glyph(font, cp);
        if (!g) continue;
        if (trunc && pen + g->adv > x + max_w - ell) break;
        if (g->w > 0) {
            float gx = std::floor(pen + g->xoff), gy = std::floor(base + g->yoff);
            quad(gx, gy, gx + g->w, gy + g->h, g->u0, g->v0, g->u1, g->v1, c);
        }
        pen += g->adv + spacing_;
    }
    if (trunc) {
        const Glyph* d = glyph(font, '.');
        for (int k = 0; d && k < 3; ++k) {
            if (d->w > 0) quad(std::floor(pen + d->xoff), std::floor(base + d->yoff), std::floor(pen + d->xoff) + d->w, std::floor(base + d->yoff) + d->h, d->u0, d->v0, d->u1, d->v1, c);
            pen += d->adv;
        }
    }
    return pen - x;
}
