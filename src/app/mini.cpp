// Mini player (MiniPlayer.tsx, createMiniPlayer in electron.js): a 350x100 always-on-top window with its own renderer.
// Opening it hides the main window; closing it (or the expand button, or Ctrl+M) brings the main window back.
#include <algorithm>
#include <cmath>
#include "app.h"

void App::toggle_mini() {
    if (mini_win_) { close_mini(); return; }
    if (!queue_.current()) return;  // "A song must be playing to open Mini Player"
    mini_win_ = SDL_CreateWindow("Xylonic Mini Player", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 350, 100, SDL_WINDOW_ALWAYS_ON_TOP | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!mini_win_) return;
    mini_ren_ = SDL_CreateRenderer(mini_win_, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!mini_ren_) mini_ren_ = SDL_CreateRenderer(mini_win_, -1, SDL_RENDERER_SOFTWARE);
    if (!mini_ren_) { SDL_DestroyWindow(mini_win_); mini_win_ = nullptr; return; }
    SDL_SetRenderDrawBlendMode(mini_ren_, SDL_BLENDMODE_BLEND);
    mini_gfx_ = std::make_unique<Gfx>();
    mini_gfx_->init(mini_win_, mini_ren_);
    mini_ui_ = std::make_unique<Ui>(*mini_gfx_);
    mini_fonts_.clear();
    mini_id_ = SDL_GetWindowID(mini_win_);
    if (win_) SDL_HideWindow(win_);
    mark();
}

void App::close_mini() {
    if (!mini_win_) return;
    mini_ui_.reset();
    mini_gfx_.reset();
    if (mini_ren_) SDL_DestroyRenderer(mini_ren_);
    SDL_DestroyWindow(mini_win_);
    mini_ren_ = nullptr;
    mini_win_ = nullptr;
    mini_id_ = 0;
    if (win_) { SDL_ShowWindow(win_); SDL_RaiseWindow(win_); }
    mark();
}

void App::draw_mini() {
    if (!mini_win_) return;
    std::swap(gfx_, mini_gfx_);
    std::swap(ui_, mini_ui_);
    std::swap(font_ids_, mini_fonts_);
    Ui& u = *ui_;
    u.tip_text.clear();
    u.th = ui_theme_cache_;
    const float s = S_;
    int dw = 0, dh = 0;
    SDL_GetRendererOutputSize(mini_ren_, &dw, &dh);
    float W = static_cast<float>(dw), H = static_cast<float>(dh);
    const Song* cur = queue_.current();
    uint32_t avg = 0x0a3a40;
    if (cur) images_->average(cover_key(cur->cover_art, static_cast<int>(308 * S_)), avg);
    auto ch = [&](int sh) { return static_cast<float>((avg >> sh) & 255); };
    Color base{static_cast<uint8_t>(10 + ch(16) * 0.10f), static_cast<uint8_t>(12 + ch(8) * 0.16f), static_cast<uint8_t>(14 + ch(0) * 0.18f), 255};
    gfx_->begin_frame(base);
    gfx_->quad_gradient({0, 0, W, H}, {base.r, base.g, base.b, 255}, {static_cast<uint8_t>(base.r / 2), static_cast<uint8_t>(base.g / 2), static_cast<uint8_t>(base.b / 2), 255},
                        {static_cast<uint8_t>(base.r / 2), static_cast<uint8_t>(base.g / 2), static_cast<uint8_t>(base.b / 2), 255}, {base.r, base.g, base.b, 255});
    if (cur) {
        T(600, 13, 15 * s, 31 * s, 20, cur->title, rgb(0xffffff), 160 * s);
        T(400, 11, 15 * s, 53 * s, 17, cur->artist, {255, 255, 255, 204}, 160 * s);
    }
    auto circle_btn = [&](float x, float y, float d, float a, uint32_t ic, float px, const char* tip) {
        Rect r{x * s, y * s, d * s, d * s};
        u.tip(r, tip);
        gfx_->circle(r.x + r.w / 2, r.y + r.h / 2, r.w / 2, {255, 255, 255, static_cast<uint8_t>((a + (u.hover(r) ? 0.15f : 0.0f)) * 255)});
        fa(ic, r.x + r.w / 2, r.y + r.h / 2, px, rgb(0xffffff));
        return u.clicked(r);
    };
    if (circle_btn(185, 34, 32, 0.4f, skin::StepBack, 12, "Previous")) prev_track();
    if (circle_btn(223, 32, 36, 0.25f, player_.playing() ? skin::Pause : skin::Play, 14, track_loading_ ? "Loading\xE2\x80\xA6" : player_.playing() ? "Pause" : "Play")) toggle_play();
    if (circle_btn(265, 34, 32, 0.4f, skin::StepFwd, 12, "Next")) next_track();
    Rect ret{307 * s, 36 * s, 28 * s, 28 * s};
    gfx_->fill_round(ret, 6 * s, {255, 255, 255, static_cast<uint8_t>((u.hover(ret) ? 0.25f : 0.13f) * 255)});
    fa(0xf065, ret.x + 14 * s, ret.y + 14 * s, 11, {255, 255, 255, 179});
    u.tip(ret, "Return to main window (Ctrl+M)");
    bool close = u.clicked(ret);
    double dur = player_.duration_seconds(), pos = player_.position_seconds();
    float frac = dur > 0 ? static_cast<float>(std::min(1.0, pos / dur)) : 0;
    gfx_->fill({0, H - 3 * s, W, 3 * s}, {255, 255, 255, 102});
    gfx_->fill({0, H - 3 * s, W * frac, 3 * s}, ui_theme_cache_.accent);
    draw_tooltip(W, H);
    gfx_->end_frame();
    u.in.new_frame();
    std::swap(gfx_, mini_gfx_);
    std::swap(ui_, mini_ui_);
    std::swap(font_ids_, mini_fonts_);
    if (close) close_mini();
}

void App::handle_mini(const SDL_Event& e) {
    Input& in = mini_ui_->in;
    switch (e.type) {
    case SDL_WINDOWEVENT:
        if (e.window.event == SDL_WINDOWEVENT_CLOSE) close_mini();
        else mark();
        break;
    case SDL_MOUSEMOTION: in.mx = e.motion.x * M_; in.my = e.motion.y * M_; mark(); break;
    case SDL_MOUSEBUTTONDOWN: if (e.button.button == SDL_BUTTON_LEFT) { in.mx = e.button.x * M_; in.my = e.button.y * M_; in.down = in.pressed = true; in.press_x = in.mx; in.press_y = in.my; mark(); } break;
    case SDL_MOUSEBUTTONUP: if (e.button.button == SDL_BUTTON_LEFT) { in.mx = e.button.x * M_; in.my = e.button.y * M_; in.down = false; in.released = true; mark(); } break;
    case SDL_KEYDOWN:
        if ((e.key.keysym.mod & KMOD_CTRL) && e.key.keysym.sym == SDLK_m) close_mini();
        else if (e.key.keysym.sym == SDLK_SPACE) toggle_play();
        break;
    default: break;
    }
}
