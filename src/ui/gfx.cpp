#define STB_TRUETYPE_IMPLEMENTATION
#include "gfx.h"
#include <cstring>
#include <cstdlib>
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
    SDL_SetTextureScaleMode(atlas_, SDL_ScaleModeNearest);
    reset_atlas();
    return true;
}

void Gfx::reset_atlas() {
    std::vector<uint32_t> blank(static_cast<size_t>(aw_) * ah_, 0);
    for (int y = 0; y < 2; ++y) for (int x = 0; x < 2; ++x) blank[y * aw_ + x] = 0xFFFFFFFFu;  // white texel
    SDL_UpdateTexture(atlas_, nullptr, blank.data(), aw_ * 4);
    shelf_x_ = 3; shelf_y_ = 0; shelf_h_ = 0;
    shelf_y_ = 3;
    shapes_.clear();
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
    f->scale = stbtt_ScaleForMappingEmToPixels(&f->info, px);
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

bool Gfx::has_glyph(int fi, uint32_t cp) const {
    if (fi < 0 || fi >= static_cast<int>(fonts_.size())) return false;
    return stbtt_FindGlyphIndex(&fonts_[fi]->info, static_cast<int>(cp)) != 0;
}

const Gfx::Glyph* Gfx::glyph(int fi, uint32_t cp) {
    if (fi < 0 || fi >= static_cast<int>(fonts_.size())) return nullptr;
    FontFace& f = *fonts_[fi];
    auto it = f.glyphs.find(cp);
    if (it != f.glyphs.end()) return &it->second;
    int gi = stbtt_FindGlyphIndex(&f.info, static_cast<int>(cp));
    if (gi == 0 && cp != 0xFFFD && cp != '?') return glyph(fi, '?');
    int adv = 0, lsb = 0, x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    stbtt_GetGlyphHMetrics(&f.info, gi, &adv, &lsb);
    stbtt_GetGlyphBitmapBox(&f.info, gi, f.scale, f.scale, &x0, &y0, &x1, &y1);
    Glyph g{};
    g.adv = adv * f.scale;
    g.xoff = static_cast<float>(x0);
    g.yoff = static_cast<float>(y0);
    int w = x1 - x0, h = y1 - y0;
    if (w > 0 && h > 0) {
        int ax = 0, ay = 0;
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

void Gfx::shift_y(int dy) {
    flush();
    if (dy <= 0) { SDL_RenderSetViewport(ren_, nullptr); return; }
    int w = 0, h = 0;
    SDL_GetRendererOutputSize(ren_, &w, &h);
    const SDL_Rect vp{0, dy, w, std::max(1, h - dy)};
    SDL_RenderSetViewport(ren_, &vp);
}

void Gfx::end_frame() {
    flush();
    if (!capture_path_.empty()) {   // read the finished frame before the present leaves the back buffer undefined
        int w = 0, h = 0;
        SDL_GetRendererOutputSize(ren_, &w, &h);
        if (SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888)) {
            SDL_RenderReadPixels(ren_, nullptr, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch);
            SDL_SaveBMP(s, capture_path_.c_str());
            SDL_FreeSurface(s);
        }
        capture_path_.clear();
    }
    SDL_RenderFlush(ren_);   // runs the queued draw commands, so the time below is the window update alone
    const uint64_t t0 = SDL_GetPerformanceCounter();
    SDL_RenderPresent(ren_);
    present_ms_ = static_cast<double>(SDL_GetPerformanceCounter() - t0) * 1000.0 / static_cast<double>(SDL_GetPerformanceFrequency());
}

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

const Gfx::Shape* Gfx::shape(int R, int t) {
    uint64_t key = (static_cast<uint64_t>(R) << 16) | static_cast<uint64_t>(t);
    auto it = shapes_.find(key);
    if (it != shapes_.end()) return &it->second;
    int ax = 0, ay = 0;
    if (!pack(R + 2, R + 2, ax, ay)) { flush(); reset_atlas(); if (!pack(R + 2, R + 2, ax, ay)) return nullptr; }
    ax += 1; ay += 1;  // 1px gutter against bleeding
    std::vector<uint32_t> px(static_cast<size_t>(R) * R);
    for (int j = 0; j < R; ++j)
        for (int i = 0; i < R; ++i) {
            float dx = R - (i + 0.5f), dy = R - (j + 0.5f), d = std::sqrt(dx * dx + dy * dy);
            float outer = std::min(1.0f, std::max(0.0f, R - d + 0.5f));
            float inner = t > 0 ? std::min(1.0f, std::max(0.0f, (R - t) - d + 0.5f)) : 0.0f;
            float cov = outer - inner;
            px[static_cast<size_t>(j) * R + i] = 0x00FFFFFFu | (static_cast<uint32_t>(std::lround(cov * 255)) << 24);
        }
    SDL_Rect rc{ax, ay, R, R};
    SDL_UpdateTexture(atlas_, &rc, px.data(), R * 4);
    Shape sh{float(ax) / aw_, float(ay) / ah_, float(ax + R) / aw_, float(ay + R) / ah_};
    return &(shapes_[key] = sh);
}

void Gfx::corner(float x, float y, int R, bool fx, bool fy, const Shape& sh, Color c) {
    float u0 = fx ? sh.u1 : sh.u0, u1 = fx ? sh.u0 : sh.u1, v0 = fy ? sh.v1 : sh.v0, v1 = fy ? sh.v0 : sh.v1;
    quad(x, y, x + R, y + R, u0, v0, u1, v1, c);
}

void Gfx::fill_round(Rect r, float rad, Color c) {
    float x0 = std::round(r.x), y0 = std::round(r.y), x1 = std::round(r.x + r.w), y1 = std::round(r.y + r.h);
    float w = x1 - x0, h = y1 - y0;
    if (w <= 0 || h <= 0) return;
    int R = std::min(static_cast<int>(std::lround(rad)), static_cast<int>(std::min(w, h) / 2));  // 2R <= size so translucent fills never double-blend a seam
    if (R < 2 || R > 160) { fill({x0, y0, w, h}, c); return; }
    const Shape* sh = shape(R, 0);
    if (!sh) { fill({x0, y0, w, h}, c); return; }
    corner(x0, y0, R, false, false, *sh, c);
    corner(x1 - R, y0, R, true, false, *sh, c);
    corner(x0, y1 - R, R, false, true, *sh, c);
    corner(x1 - R, y1 - R, R, true, true, *sh, c);
    if (w - 2 * R > 0) { fill({x0 + static_cast<float>(R), y0, w - 2.0f * R, static_cast<float>(R)}, c); fill({x0 + static_cast<float>(R), y1 - R, w - 2.0f * R, static_cast<float>(R)}, c); }
    if (h - 2 * R > 0) fill({x0, y0 + R, w, h - 2 * R}, c);
}

void Gfx::stroke_round(Rect r, float rad, float t, Color c, Color) {
    float x0 = std::round(r.x), y0 = std::round(r.y), x1 = std::round(r.x + r.w), y1 = std::round(r.y + r.h);
    float w = x1 - x0, h = y1 - y0;
    int T = std::max(1, static_cast<int>(std::lround(t)));
    int R = std::min(static_cast<int>(std::lround(rad)), static_cast<int>(std::min(w, h) / 2));
    if (R < 2 || R > 160) { stroke({x0, y0, w, h}, static_cast<float>(T), c); return; }
    const Shape* sh = shape(R, T);
    if (!sh) { stroke({x0, y0, w, h}, static_cast<float>(T), c); return; }
    corner(x0, y0, R, false, false, *sh, c);
    corner(x1 - R, y0, R, true, false, *sh, c);
    corner(x0, y1 - R, R, false, true, *sh, c);
    corner(x1 - R, y1 - R, R, true, true, *sh, c);
    const float Tf = static_cast<float>(T), Rf = static_cast<float>(R);
    if (w - 2 * Rf > 0) { fill({x0 + Rf, y0, w - 2 * Rf, Tf}, c); fill({x0 + Rf, y1 - Tf, w - 2 * Rf, Tf}, c); }
    if (h - 2 * Rf > 0) { fill({x0, y0 + Rf, Tf, h - 2 * Rf}, c); fill({x1 - Tf, y0 + Rf, Tf, h - 2 * Rf}, c); }
}

void Gfx::radial(float cx, float cy, float rad, Color centre, Color edge) {
    const float u = 1.0f / aw_, v = 1.0f / ah_;
    int n = 48;
    int base = static_cast<int>(v_.size());
    v_.push_back({{cx, cy}, {centre.r, centre.g, centre.b, centre.a}, {u, v}});
    for (int i = 0; i < n; ++i) {
        float a = 6.2831853f * i / n;
        v_.push_back({{cx + std::cos(a) * rad, cy + std::sin(a) * rad}, {edge.r, edge.g, edge.b, edge.a}, {u, v}});
    }
    for (int i = 0; i < n; ++i) { idx_.push_back(base); idx_.push_back(base + 1 + i); idx_.push_back(base + 1 + (i + 1) % n); }
}

void Gfx::quad_gradient(Rect r, Color tl, Color tr, Color br, Color bl) {
    const float u = 1.0f / aw_, v = 1.0f / ah_;
    int b = static_cast<int>(v_.size());
    v_.push_back({{r.x, r.y}, {tl.r, tl.g, tl.b, tl.a}, {u, v}});
    v_.push_back({{r.x + r.w, r.y}, {tr.r, tr.g, tr.b, tr.a}, {u, v}});
    v_.push_back({{r.x + r.w, r.y + r.h}, {br.r, br.g, br.b, br.a}, {u, v}});
    v_.push_back({{r.x, r.y + r.h}, {bl.r, bl.g, bl.b, bl.a}, {u, v}});
    for (int i : {0, 1, 2, 0, 2, 3}) idx_.push_back(b + i);
}

void Gfx::vgradient(Rect r, Color top, Color bottom) {
    const float u = 1.0f / aw_, v = 1.0f / ah_;
    int b = static_cast<int>(v_.size());
    v_.push_back({{r.x, r.y}, {top.r, top.g, top.b, top.a}, {u, v}});
    v_.push_back({{r.x + r.w, r.y}, {top.r, top.g, top.b, top.a}, {u, v}});
    v_.push_back({{r.x + r.w, r.y + r.h}, {bottom.r, bottom.g, bottom.b, bottom.a}, {u, v}});
    v_.push_back({{r.x, r.y + r.h}, {bottom.r, bottom.g, bottom.b, bottom.a}, {u, v}});
    for (int i : {0, 1, 2, 0, 2, 3}) idx_.push_back(b + i);
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

void Gfx::arc(float cx, float cy, float r, float t, float start, float sweep, Color c, bool round_caps) {
    if (sweep <= 0 || t <= 0) return;
    const float u = 1.0f / aw_, v = 1.0f / ah_;
    const int steps = std::max(2, static_cast<int>(std::ceil(std::fabs(sweep) / 0.06f)));
    const float radii[4] = {r - t / 2 - 0.8f, r - t / 2 + 0.2f, r + t / 2 - 0.2f, r + t / 2 + 0.8f};   // 1px soft edges on both sides
    const uint8_t alphas[4] = {0, c.a, c.a, 0};
    const int base = static_cast<int>(v_.size());
    for (int i = 0; i <= steps; ++i) {
        const float a = start + sweep * i / steps, cs = std::cos(a), sn = std::sin(a);
        for (int k = 0; k < 4; ++k) v_.push_back({{cx + cs * radii[k], cy + sn * radii[k]}, {c.r, c.g, c.b, alphas[k]}, {u, v}});
    }
    for (int i = 0; i < steps; ++i)
        for (int k = 0; k < 3; ++k) {
            const int a0 = base + i * 4 + k, a1 = a0 + 1, b0 = a0 + 4, b1 = b0 + 1;
            for (int ix : {a0, a1, b0, a1, b1, b0}) idx_.push_back(ix);
        }
    if (round_caps) { circle(cx + std::cos(start) * r, cy + std::sin(start) * r, t / 2, c); circle(cx + std::cos(start + sweep) * r, cy + std::sin(start + sweep) * r, t / 2, c); }
}

void Gfx::circle(float cx, float cy, float r, Color c) { fill_round({cx - r, cy - r, 2 * r, 2 * r}, r, c); }

void Gfx::ring(float cx, float cy, float r, float t, Color c) { stroke_round({cx - r, cy - r, 2 * r, 2 * r}, r, t, c); }

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

void Gfx::image_round(SDL_Texture* tex, Rect d, float rad, Color tint) {
    rad = std::min(rad, std::min(d.w, d.h) / 2);
    if (rad < 1.5f) { image(tex, d, tint); return; }
    flush();
    std::vector<SDL_Vertex> vs;
    std::vector<int> ix;
    SDL_Color sc{tint.r, tint.g, tint.b, tint.a};
    auto uv = [&](float x, float y) { return SDL_FPoint{(x - d.x) / d.w, (y - d.y) / d.h}; };
    vs.push_back({{d.x + d.w / 2, d.y + d.h / 2}, sc, uv(d.x + d.w / 2, d.y + d.h / 2)});
    const int segs = std::min(16, std::max(4, static_cast<int>(rad / 2)));
    const float cx[4] = {d.x + d.w - rad, d.x + rad, d.x + rad, d.x + d.w - rad};
    const float cy[4] = {d.y + rad, d.y + rad, d.y + d.h - rad, d.y + d.h - rad};
    int n = 0;
    for (int k = 0; k < 4; ++k)
        for (int s = 0; s <= segs; ++s) {
            float a = (float(k) + float(s) / segs) * 1.5707963f, x = cx[k] + std::cos(a) * rad, y = cy[k] - std::sin(a) * rad;
            vs.push_back({{x, y}, sc, uv(x, y)});
            ++n;
        }
    for (int i = 0; i < n; ++i) { ix.push_back(0); ix.push_back(1 + i); ix.push_back(1 + (i + 1) % n); }
    SDL_SetTextureColorMod(tex, 255, 255, 255);
    SDL_SetTextureAlphaMod(tex, 255);
    SDL_RenderGeometry(ren_, tex, vs.data(), static_cast<int>(vs.size()), ix.data(), static_cast<int>(ix.size()));
    ++draw_calls_;
}

void Gfx::clip(const Rect* r) {
    flush();
    if (!r) {
        if (has_base_) SDL_RenderSetClipRect(ren_, &base_); else SDL_RenderSetClipRect(ren_, nullptr);
        return;
    }
    SDL_Rect c{int(r->x), int(r->y), std::max(0, int(r->w)), std::max(0, int(r->h))};   // SDL turns clipping off for a negative size; an empty rect clips everything
    if (has_base_) {
        if (grow_to_ > grow_from_ && std::abs((c.y + c.h) - grow_from_) <= 1) c.h = std::max(0, grow_to_ - c.y);
        SDL_Rect out{0, 0, 0, 0};
        if (!SDL_IntersectRect(&c, &base_, &out)) out = SDL_Rect{0, 0, 0, 0};
        c = out;
    }
    SDL_RenderSetClipRect(ren_, &c);
}

void Gfx::set_base_clip(const Rect* r, float grow_from, float grow_to) {
    flush();
    if (!r) { has_base_ = false; grow_from_ = grow_to_ = 0; SDL_RenderSetClipRect(ren_, nullptr); return; }
    base_ = SDL_Rect{int(r->x), int(r->y), std::max(0, int(r->w)), std::max(0, int(r->h))};
    has_base_ = true;
    grow_from_ = int(grow_from); grow_to_ = int(grow_to);
    SDL_RenderSetClipRect(ren_, &base_);
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
    uint64_t key = 1469598103934665603ULL ^ static_cast<uint64_t>(font + 1);
    uint32_t sp = 0;
    std::memcpy(&sp, &spacing_, sizeof sp);
    key = (key ^ sp) * 1099511628211ULL;
    for (unsigned char c : s) { key ^= c; key *= 1099511628211ULL; }
    auto hit = measure_cache_.find(key);
    if (hit != measure_cache_.end()) return hit->second;
    if (measure_cache_.size() > 16384) measure_cache_.clear();
    float w = 0;
    for (size_t i = 0; i < s.size();) {
        uint32_t cp;
        i += utf8(s, i, cp);
        if (const Glyph* g = glyph(font, cp)) w += g->adv + spacing_;
    }
    measure_cache_[key] = w;
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
