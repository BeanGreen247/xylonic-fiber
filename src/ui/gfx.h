// 2D batch renderer over SDL_Renderer: one vertex batch, one glyph atlas (also holds a white texel for solid fills).
// Nothing is drawn unless the app asks for a frame, so an idle UI costs no CPU/GPU.
#pragma once
#include <SDL.h>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

struct Color {
    uint8_t r = 0, g = 0, b = 0, a = 255;
};
inline Color rgb(uint32_t hex, uint8_t a = 255) { return {uint8_t(hex >> 16), uint8_t(hex >> 8), uint8_t(hex), a}; }

struct Rect {
    float x = 0, y = 0, w = 0, h = 0;
    bool contains(float px, float py) const { return px >= x && py >= y && px < x + w && py < y + h; }
};

class FontFace;

class Gfx {
public:
    Gfx();
    ~Gfx();
    bool init(SDL_Window* win, SDL_Renderer* ren);  // ren is borrowed
    // Loads a TTF through SDL_RWops (works with Android assets). Returns an id for text(); -1 on failure.
    int add_font(const char* path, float pixel_size);

    void begin_frame(Color clear);
    void end_frame();
    double present_ms() const { return present_ms_; }   // SDL_RenderPresent of the last frame
    void capture_next(const std::string& path) { capture_path_ = path; }   // saves the next finished frame as BMP  // flushes and presents

    void fill(Rect r, Color c);
    void fill_round(Rect r, float radius, Color c);
    void stroke(Rect r, float t, Color c);
    // Rounded outline. With `under` (the solid colour behind it) it is drawn as two fills, which stays gap-free on the
    // software renderer; without it, as a triangle strip.
    void stroke_round(Rect r, float radius, float thickness, Color c, Color under = {0, 0, 0, 0});
    // Soft radial blob (centre colour fading to edge colour) and a vertical two-colour gradient.
    void radial(float cx, float cy, float radius, Color centre, Color edge);
    void vgradient(Rect r, Color top, Color bottom);
    // Four-corner gradient quad: top-left, top-right, bottom-right, bottom-left colours.
    void quad_gradient(Rect r, Color tl, Color tr, Color br, Color bl);
    void tri(float x0, float y0, float x1, float y1, float x2, float y2, Color c);
    void line(float x0, float y0, float x1, float y1, float thickness, Color c);
    void circle(float cx, float cy, float r, Color c);
    void ring(float cx, float cy, float r, float thickness, Color c);
    // Anti-aliased arc from start (radians, 0 = 3 o'clock, clockwise on screen) sweeping sweep radians, optional round caps.
    void arc(float cx, float cy, float r, float thickness, float start, float sweep, Color c, bool round_caps = true);
    // Draws a texture (cover art etc.) stretched into dst; flushes the batch first.
    void image(SDL_Texture* tex, Rect dst, Color tint = {255, 255, 255, 255});
    // Image with rounded corners (mesh with UVs; flushes the batch like image()).
    void image_round(SDL_Texture* tex, Rect dst, float radius, Color tint = {255, 255, 255, 255});
    void clip(const Rect* r);  // nullptr clears (back to the base clip when one is set)
    // A base clip every later clip() is intersected with, so a page that clips to its own row (a carousel scrolled half out of view) cannot
    // paint over the header. A clip whose bottom edge sits at grow_from is stretched down to grow_to: the page may draw into the strip
    // under the floating player. nullptr removes the base clip.
    void set_base_clip(const Rect* r, float grow_from = 0, float grow_to = 0);

    // Single-line text, top-left origin. Returns drawn width. max_w > 0 truncates with an ellipsis.
    float text(int font, float x, float y, const std::string& s, Color c, float max_w = 0);
    float measure(int font, const std::string& s);
    float line_height(int font) const;

    SDL_Renderer* renderer() const { return ren_; }
    // Text with a tracking offset (CSS letter-spacing) in pixels.
    void set_letter_spacing(float px) { spacing_ = px; }
    uint64_t draw_calls() const { return draw_calls_; }
    bool has_glyph(int font, uint32_t cp) const;

private:
    bool has_base_ = false;
    SDL_Rect base_{0, 0, 0, 0};
    int grow_from_ = 0, grow_to_ = 0;
    std::string capture_path_;
    friend class FontFace;
    struct Glyph { float u0, v0, u1, v1; float xoff, yoff, w, h, adv; };
    const Glyph* glyph(int font, uint32_t cp);
    struct Shape { float u0, v0, u1, v1; };
    // Anti-aliased quarter-circle masks (fill or ring) packed into the atlas; every rounded shape is built from them.
    const Shape* shape(int radius, int thickness);
    void corner(float x, float y, int r, bool flip_x, bool flip_y, const Shape& sh, Color c);
    void flush();
    void quad(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, Color c);
    void reset_atlas();
    bool pack(int w, int h, int& x, int& y);

    SDL_Renderer* ren_ = nullptr;
    SDL_Texture* atlas_ = nullptr;
    int aw_ = 1024, ah_ = 512;
    int shelf_x_ = 2, shelf_y_ = 2, shelf_h_ = 0;
    std::vector<SDL_Vertex> v_;
    std::vector<int> idx_;
    std::vector<std::unique_ptr<FontFace>> fonts_;
    std::unordered_map<std::string, std::shared_ptr<std::vector<uint8_t>>> font_data_;
    std::unordered_map<uint64_t, Shape> shapes_;
    uint64_t draw_calls_ = 0;
    double present_ms_ = 0;
    float spacing_ = 0;
};
