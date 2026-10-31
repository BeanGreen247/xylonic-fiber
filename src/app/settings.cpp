// Settings page (SettingsView.tsx + sections), the Accent Theme dialog and the Sleep Timer picker.
// Measured at 1024x768: content column 680 wide, cards radius 16 with a 1px rgba(255,255,255,.06) border,
// rows 52/61/71 high, section titles 12px 600 accent uppercase.
#include <algorithm>
#include <cmath>
#include <ctime>
#include "app.h"
#include "secrets.h"

namespace {
Color alpha(Color c, float a) { c.a = static_cast<uint8_t>(std::lround(a * 255)); return c; }
}  // namespace

// ---------------------------------------------------------------- sleep timer

void App::set_sleep_timer(int minutes) {
    sleep_end_ms_ = minutes > 0 ? SDL_GetTicks64() + static_cast<uint64_t>(minutes) * 60000ULL : 0;
    mark();
}

std::string App::sleep_remaining() const {
    if (!sleep_end_ms_) return "";
    uint64_t now = SDL_GetTicks64();
    int rem = sleep_end_ms_ > now ? static_cast<int>((sleep_end_ms_ - now + 999) / 1000) : 0;
    char b[24];
    if (rem >= 3600) std::snprintf(b, sizeof b, "%d:%02d:%02d", rem / 3600, (rem % 3600) / 60, rem % 60);
    else std::snprintf(b, sizeof b, "%d:%02d", rem / 60, rem % 60);
    return b;
}

void App::tick_sleep() {
    if (sleep_end_ms_ && SDL_GetTicks64() >= sleep_end_ms_) { sleep_end_ms_ = 0; if (player_.playing()) player_.pause(); mark(); }
}

// ---------------------------------------------------------------- modal helpers

void App::draw_modal(float W, float H) {
    if (modal_ == Modal::None) return;
    Ui& u = *ui_;
    const float s = S_;
    const Theme& th = u.th;
    gfx_->fill({0, 0, W, H}, {0, 0, 0, 170});  // the old modal blurs the page; a dark scrim keeps the cost at zero
    auto frame = [&](float w, float h, uint32_t icon, const char* title) {
        Rect b{(W - w * s) / 2, (H - h * s) / 2, w * s, h * s};
        gfx_->fill_round({b.x - 1, b.y - 1, b.w + 2, b.h + 2}, 3 * s, th.border);
        gfx_->fill_round(b, 2 * s, th.bg_light);
        fa(icon, b.x + 28 * s, b.y + 32 * s, 15, th.accent);
        T(600, 16, b.x + 44 * s, b.y + 20 * s, 24, title, th.text);
        Rect cl{b.x + b.w - 52 * s, b.y + 16 * s, 32 * s, 32 * s};
        if (u.hover(cl)) gfx_->fill_round(cl, 16 * s, th.hover);
        fa(skin::Xmark, cl.x + 16 * s, cl.y + 16 * s, 16, th.sub);
        if (u.clicked(cl) || u.in.escape) modal_ = Modal::None;
        return b;
    };
    if (modal_ == Modal::ConfirmWipe) {
        Rect b = frame(420, 190, skin::Warning, "Clear all app data?");
        T(400, 13, b.x + 20 * s, b.y + 64 * s, 20, "This removes downloaded songs, cover art, credentials and all settings.", th.sub, b.w - 40 * s);
        T(400, 13, b.x + 20 * s, b.y + 86 * s, 20, "It cannot be undone.", th.sub);
        if (u.button({b.x + 20 * s, b.y + b.h - 56 * s, 150 * s, 36 * s}, "Cancel")) modal_ = Modal::None;
        Rect go{b.x + b.w - 20 * s - 190 * s, b.y + b.h - 56 * s, 190 * s, 36 * s};
        gfx_->fill_round(go, 8 * s, u.hover(go) ? alpha(th.danger, 0.9f) : th.danger);
        T(600, 14, go.x + (go.w - gfx_->measure(F(600, 14), "Clear everything")) / 2, go.y, 36, "Clear everything", rgb(0xffffff));
        if (u.clicked(go)) { wipe_all_data(); modal_ = Modal::None; }
    } else if (modal_ == Modal::Remote) { draw_remote_picker(W, H);
    } else if (modal_ == Modal::Sleep) {
        Rect b = frame(420, 336, 0xf186, "Sleep Timer");
        T(400, 12, b.x + 18 * s, b.y + 52 * s, 18, "Playback stops automatically after the selected time.", th.sub);
        static const int mins[4] = {15, 30, 45, 60};
        static const char* labels[4] = {"15 minutes", "30 minutes", "45 minutes", "1 hour"};
        static const char* descs[4] = {"Quick listen", "Half hour", "Wind down session", "Full hour"};
        float y = b.y + 82 * s;
        if (!sleep_remaining().empty()) {
            T(400, 12, b.x + 18 * s, b.y + 52 * s, 18, "", th.sub);
            T(400, 12, b.x + 18 * s, b.y + 52 * s, 18, "Stops in " + sleep_remaining(), th.accent);
        }
        for (int i = 0; i < 4; ++i) {
            Rect row{b.x, y + i * 62 * s, b.w, 62 * s};
            if (u.hover(row)) gfx_->fill(row, th.hover);
            if (i) gfx_->fill({row.x, row.y, row.w, 1 * s}, th.border_subtle);
            fa(0xf186, row.x + 35 * s, row.y + 31 * s, 18, th.accent);
            T(700, 15, row.x + 66 * s, row.y + 10 * s, 22, labels[i], th.text);
            T(400, 12, row.x + 66 * s, row.y + 33 * s, 18, descs[i], th.text);
            if (u.clicked(row)) { set_sleep_timer(mins[i]); modal_ = Modal::None; return; }
        }
        if (sleep_end_ms_ && u.button({b.x + 18 * s, b.y + b.h - 44 * s, 120 * s, 32 * s}, "Cancel timer")) { set_sleep_timer(0); modal_ = Modal::None; }
    } else if (modal_ == Modal::Theme) {
        Rect b = frame(520, 420, 0xf53f, "Accent Theme");
        float x = b.x + 24 * s, y = b.y + 64 * s, sw = 44 * s, gap = 10 * s;
        for (int i = 0; i < kPresetCount + kCustomSlots; ++i) {
            int col = i % 6, row = i / 6;
            Rect sq{x + col * (sw + gap + 28 * s), y + row * (sw + gap + 24 * s), sw, sw};
            ThemeDef d = theme_def(i);
            gfx_->fill_round(sq, 10 * s, rgb(d.primary));
            if (cfg_.theme == i) gfx_->stroke_round({sq.x - 3 * s, sq.y - 3 * s, sq.w + 6 * s, sq.h + 6 * s}, 12 * s, 2 * s, th.text);
            T(400, 11, sq.x - 6 * s, sq.y + sw + 4 * s, 16, d.name.substr(0, 12), th.sub, sw + 40 * s);
            if (u.clicked(sq)) { cfg_.theme = i; apply_theme(); cfg_.save(cfg_path_); }
        }
        y += 2 * (sw + gap + 24 * s) + 8 * s;
        T(600, 13, x, y, 20, "Custom theme", th.text);
        y += 28 * s;
        for (int i = 0; i < kCustomSlots; ++i)
            if (u.button({x + i * 96 * s, y, 88 * s, 32 * s}, "Slot " + std::to_string(i + 1), edit_slot_ == i)) { edit_slot_ = i; edit_loaded_ = false; }
        y += 44 * s;
        if (!edit_loaded_) {
            edit_name_ = cfg_.custom[edit_slot_].name;
            char hx[16]; std::snprintf(hx, sizeof hx, "#%06x", cfg_.custom[edit_slot_].primary);
            edit_hex_ = hx;
            edit_loaded_ = true;
        }
        u.text_field(kEditName, {x, y, 200 * s, 38 * s}, edit_name_, "Theme name");
        u.text_field(kEditHex, {x + 212 * s, y, 130 * s, 38 * s}, edit_hex_, "#rrggbb");
        unsigned hexv = 0;
        bool valid = edit_hex_.size() == 7 && edit_hex_[0] == '#' && std::sscanf(edit_hex_.c_str() + 1, "%6x", &hexv) == 1;
        if (valid) gfx_->fill_round({x + 352 * s, y, 38 * s, 38 * s}, 8 * s, rgb(hexv));
        y += 50 * s;
        if (u.button({x, y, 110 * s, 34 * s}, "Apply", true) && valid) {
            cfg_.custom[edit_slot_] = {edit_name_.empty() ? "My Theme " + std::to_string(edit_slot_ + 1) : edit_name_, hexv};
            cfg_.theme = kPresetCount + edit_slot_;
            apply_theme(); cfg_.save(cfg_path_);
        }
        if (u.button({x + 122 * s, y, 110 * s, 34 * s}, "Reset")) {
            cfg_.custom[edit_slot_] = {"My Theme " + std::to_string(edit_slot_ + 1), 0x00bcd4};
            if (cfg_.theme == kPresetCount + edit_slot_) cfg_.theme = 0;  // ThemeContext.resetCustomTheme
            edit_loaded_ = false;
            apply_theme(); cfg_.save(cfg_path_);
        }
    }
}

// ---------------------------------------------------------------- page

// Draws a dropdown for `id` under `anchor` when open; returns the chosen index or -1.
int App::dropdown(int id, Rect anchor, const std::vector<std::string>& opts, int current) {
    Ui& u = *ui_;
    const float s = S_;
    const Theme& th = u.th;
    // control pill
    bool open = dropdown_open_ == id;
    gfx_->fill_round(anchor, 14.5f * s, alpha(th.text, 0.06f));
    gfx_->stroke_round(anchor, 14.5f * s, 1 * s, alpha(th.text, 0.41f));
    T(400, 13, anchor.x + 14 * s, anchor.y, 29, opts[static_cast<size_t>(current)], th.text, anchor.w - 36 * s);
    fa(skin::ChevDown, anchor.x + anchor.w - 16 * s, anchor.y + 14.5f * s, 10, th.sub);
    if (u.clicked(anchor)) { dropdown_open_ = open ? 0 : id; mark(); }
    if (!open) return -1;
    float ih = 32 * s;
    Rect pop{anchor.x, anchor.y + anchor.h + 4 * s, std::max(anchor.w, 120 * s), ih * opts.size() + 8 * s};
    dropdown_draw_ = [this, pop, opts, current, ih, id]() mutable {
        Ui& uu = *ui_;
        const Theme& t = uu.th;
        gfx_->fill_round({pop.x - 1, pop.y - 1, pop.w + 2, pop.h + 2}, 9 * S_, t.border);
        gfx_->fill_round(pop, 8 * S_, t.bg_light);
        for (size_t i = 0; i < opts.size(); ++i) {
            Rect r{pop.x + 4 * S_, pop.y + 4 * S_ + i * ih, pop.w - 8 * S_, ih};
            if (uu.hover(r)) gfx_->fill_round(r, 6 * S_, t.hover);
            T(static_cast<int>(i) == current ? 600 : 400, 13, r.x + 10 * S_, r.y, ih / S_, opts[i], static_cast<int>(i) == current ? t.accent : t.text);
            if (uu.clicked(r)) { dropdown_choice_ = {id, static_cast<int>(i)}; }
        }
        if (uu.in.pressed && !pop.contains(uu.in.mx, uu.in.my)) dropdown_choice_ = {id, -2};
    };
    return -1;
}

void App::draw_settings(Rect r) {
    Ui& u = *ui_;
    const float s = S_;
    const Theme& th = u.th;
    const float x = r.x + 24 * s, w = std::min(680 * s, r.w - 48 * s - 12 * s);
    float total = 3260 * s;
    if (u.hover(r)) settings_scroll_.y -= u.in.wheel * 60 * s;
    settings_scroll_.clamp(total, r.h);
    float y = r.y + 24 * s - settings_scroll_.y;
    gfx_->clip(&r);
    dropdown_draw_ = nullptr;
    // pending dropdown selection from the previous frame's popup
    int pick_id = dropdown_choice_.first, pick = dropdown_choice_.second;
    dropdown_choice_ = {0, -1};
    if (pick_id && pick == -2) { dropdown_open_ = 0; }
    // header
    fa(skin::Gear, x + 20 * s, y + 24 * s, 32, th.accent);
    gfx_->set_letter_spacing(-0.32f * s);
    T(700, 32, x + 52 * s, y, 48, "Settings", th.text);
    gfx_->set_letter_spacing(0);
    y += 80 * s;

    auto section = [&](const char* title) {
        gfx_->set_letter_spacing(0.72f * s);
        T(600, 12, x, y, 18, title, th.accent);
        gfx_->set_letter_spacing(0);
        y += 26 * s;
    };
    int row_i = 0;
    // Row primitive. kind: 0 info, 1 button, 2 danger. Returns true when clicked (kind != info).
    auto row = [&](uint32_t icon, const std::string& title, const std::string& sub, int kind, bool chevron, const std::string& pill, bool pill_on, float h_logical) {
        if (row_i++) { gfx_->fill({x + 17 * s, y, w - 34 * s, 1 * s}, {255, 255, 255, 15}); y += 1 * s; }
        Rect rr{x + 1 * s, y, w - 2 * s, h_logical * s};
        bool hov = kind != 0 && u.hover(rr) && r.contains(u.in.mx, u.in.my);
        if (hov) gfx_->fill(rr, th.hover);
        Color ic = kind == 2 ? th.danger : th.sub;
        fa(icon, rr.x + 28 * s, rr.y + rr.h / 2, 18, ic);
        bool has_sub = !sub.empty();
        Color tc = kind == 2 ? th.danger : th.text;
        if (has_sub) {
            T(400, 15, rr.x + 54 * s, rr.y + 10 * s, 22.5f, title, tc, rr.w - 54 * s - 140 * s);
            T(400, 12, rr.x + 54 * s, rr.y + 33 * s, 18, sub, rgb(0xb5b5b5), rr.w - 54 * s - 140 * s);
        } else {
            T(400, 15, rr.x + 54 * s, rr.y, h_logical, title, tc, rr.w - 54 * s - 100 * s);
        }
        float rx = rr.x + rr.w - 16 * s;
        if (chevron) { fa(skin::ChevRight, rx - 7 * s, rr.y + rr.h / 2, 14, rgb(0xb5b5b5)); rx -= 30 * s; }
        if (!pill.empty()) {
            int fp = F(600, 12);
            float pw = gfx_->measure(fp, pill) + 24 * s;
            Rect pr{rx - pw, rr.y + rr.h / 2 - 12 * s, pw, 24 * s};
            gfx_->fill_round(pr, 12 * s, pill_on ? alpha(th.accent, 0.15f) : alpha(th.text, 0.06f));
            T(600, 12, pr.x + 12 * s, pr.y, 24, pill, pill_on ? th.accent : th.sub);
        }
        y += h_logical * s;
        return kind != 0 && u.clicked(rr) && r.contains(u.in.mx, u.in.my);
    };
    auto card_fill = [&](float top, float h) {
        gfx_->fill_round({x, top, w, h}, 16 * s, {255, 255, 255, 15});
        gfx_->fill_round({x + 1 * s, top + 1 * s, w - 2 * s, h - 2 * s}, 15 * s, th.panel);
    };

    // ---- APPEARANCE
    section("APPEARANCE");
    card_fill(y, 126 * s);
    {
        row(0xf186, "Mode", "Light / dark \xE2\x80\x94 follows your system by default", 0, false, "", false, 71);
        Rect sel{x + w - 16 * s - 120 * s, y - 71 * s + 21 * s, 120 * s, 29 * s};
        int cur = cfg_.theme_mode;
        int p = dropdown(1, sel, {"System", "Light", "Dark"}, cur);
        (void)p;
        if (pick_id == 1 && pick >= 0) { cfg_.theme_mode = pick; apply_theme(); cfg_.save(cfg_path_); dropdown_open_ = 0; }
        if (row(0xf53f, "Accent Theme", "", 1, true, "", false, 52)) modal_ = Modal::Theme;
    }
    y += (1 + 32) * s; row_i = 0;
    // ---- PLAYBACK
    section("PLAYBACK");
    card_fill(y, 54 * s);
    if (row(0xf186, "Sleep Timer", "", 1, true, sleep_remaining(), true, 52)) modal_ = Modal::Sleep;
    y += (1 + 32) * s; row_i = 0;
    // ---- DRIVING DISPLAY
    section("DRIVING DISPLAY");
    card_fill(y, 311 * s);
    if (row(skin::Car, "Car Mode", "A high-contrast widescreen player for your dashboard", 1, true, "Launch", true, 61)) set_car_mode(true);
    { static const char* m[3] = {"Auto", "Always", "Never"}; if (row(0xf422, "Minimal layout", "Automatic: no top and bottom bars on very small screens", 1, false, m[cfg_.car_minimal], cfg_.car_minimal != 2, 61)) { cfg_.car_minimal = (cfg_.car_minimal + 1) % 3; cfg_.save(cfg_path_); } }
    if (row(0xf0eb, "Keep screen on", "Stop the screen from dimming, sleeping or locking while Car Mode is open", 1, false, cfg_.car_keep_on ? "On" : "Off", cfg_.car_keep_on, 61)) { cfg_.car_keep_on = !cfg_.car_keep_on; cfg_.save(cfg_path_); }
    if (row(0xf011, "Start in Car Mode", "Open the driving display automatically when the app starts", 1, false, cfg_.car_start ? "On" : "Off", cfg_.car_start, 61)) { cfg_.car_start = !cfg_.car_start; cfg_.save(cfg_path_); }
    if (row(0xf080, "Start in Pulse Deck", "Open the animated spectrum visualizer when the app starts", 1, false, cfg_.car_pulse ? "On" : "Off", cfg_.car_pulse, 61)) { cfg_.car_pulse = !cfg_.car_pulse; cfg_.save(cfg_path_); }
    y += (1 + 32) * s; row_i = 0;
    // ---- ACCOUNT
    section("ACCOUNT");
    card_fill(y, 126 * s);
    row(0xf0c1, "Last.fm", "Scrobbling is handled by your server \xE2\x80\x94 enable Last.fm in your server's admin panel", 0, false, "Server", false, 71);
    if (row(0xf362, "Switch Server", "", 1, true, "", false, 52)) logout();
    y += (1 + 32) * s; row_i = 0;
    // ---- OFFLINE & CACHE
    section("OFFLINE & CACHE");
    card_fill(y, 302 * s);
    if (row(skin::Cloud, "Offline Mode", offline_ ? "Playing downloaded music only" : "Streaming from server", 1, false, offline_ ? "On" : "Off", offline_, 61)) {
        offline_ = !offline_; load_home(); load_artists();
    }
    row(0xf012, "Auto-offline on mobile data", "Download songs first to enable", 1, false, "Off", false, 61);
    if (row(skin::Download, "Downloads", "", 1, true, "", false, 52)) set_nav(Nav::Downloads);
    if (row(skin::Sync, "Rebuild Cache", "Re-fetches library index; keeps downloaded files", 1, false, "", false, 61)) {
        if (store_) { store_->erase_prefix("home:"); store_->erase_prefix("artist"); store_->erase_prefix("album"); }
        album_cache_.clear(); artist_cache_.clear(); load_home(); load_artists(); alb_loaded_page_ = -1; song_loaded_page_ = -1;
    }
    if (row(0xf1f8, "Clear All Caches", "Deletes artwork, offline data, and precache flags", 2, false, "", false, 61)) {
        images_->clear_memory();
        if (store_) { store_->erase_prefix("home:"); store_->erase_prefix("artist"); store_->erase_prefix("album"); }
        album_cache_.clear(); artist_cache_.clear();
    }
    y += (1 + 32) * s; row_i = 0;
    // ---- REMOTE
    section("REMOTE");
    card_fill(y, 249 * s);
    if (row(skin::Satellite, "Be Controlled", "Let other Xylonic devices on your network control playback here", 1, false, cfg_.be_controlled ? "On" : "Off", cfg_.be_controlled, 61)) { cfg_.be_controlled = !cfg_.be_controlled; cfg_.save(cfg_path_); if (remote_) remote_->set_enabled(cfg_.be_controlled); }
    if (row(0xf11b, "Control Others", "Discover and control other Xylonic devices on your network", 1, false, cfg_.control_others ? "On" : "Off", cfg_.control_others, 61)) { cfg_.control_others = !cfg_.control_others; cfg_.save(cfg_path_); }
    if (row(0xf6ff, "Remote Devices", remote_ && !remote_->devices().empty() ? std::to_string(remote_->devices().size()) + " device(s) found" : "No devices found yet", 1, true, "", false, 61)) modal_ = Modal::Remote;
    row(0xf7e4, "Firewall Setup", "Open ports 7766 (UDP) and 7767 (TCP) for remote discovery", 1, true, "", false, 61);
    y += (1 + 32) * s; row_i = 0;
    // ---- STREAMING
    section("STREAMING");
    card_fill(y, 73 * s);
    {
        row(0xf012, "Streaming Quality", "Applies to the next track loaded", 0, false, "", false, 71);
        static const int q[] = {0, 320, 256, 192, 128, 64};
        std::vector<std::string> o = {"Original", "320 kbps", "256 kbps", "192 kbps", "128 kbps", "64 kbps"};
        int cur = 0; for (int i = 0; i < 6; ++i) if (q[i] == cfg_.stream_quality) cur = i;
        dropdown(2, {x + w - 16 * s - 120 * s, y - 71 * s + 21 * s, 120 * s, 29 * s}, o, cur);
        if (pick_id == 2 && pick >= 0) { cfg_.stream_quality = q[pick]; cfg_.save(cfg_path_); dropdown_open_ = 0; }
    }
    y += (1 + 32) * s; row_i = 0;
    // ---- DOWNLOADS
    section("DOWNLOADS");
    card_fill(y, 145 * s);
    {
        row(0xf1de, "Download Quality", "", 0, false, "", false, 71);
        static const int q[] = {0, 320, 256, 128, 64};
        std::vector<std::string> o = {"Original", "320 kbps", "256 kbps", "128 kbps", "64 kbps"};
        int cur = 0; for (int i = 0; i < 5; ++i) if (q[i] == cfg_.download_quality) cur = i;
        dropdown(3, {x + w - 16 * s - 120 * s, y - 71 * s + 21 * s, 120 * s, 29 * s}, o, cur);
        if (pick_id == 3 && pick >= 0) { cfg_.download_quality = q[pick]; cfg_.save(cfg_path_); dropdown_open_ = 0; }
        row(0xf5fd, "Concurrent Downloads", "Up to " + std::to_string(cfg_.concurrent_downloads) + " songs at once", 0, false, "", false, 71);
        std::vector<std::string> n = {"1", "2", "3", "4", "5", "6", "7", "8"};
        dropdown(4, {x + w - 16 * s - 120 * s, y - 71 * s + 21 * s, 120 * s, 29 * s}, n, cfg_.concurrent_downloads - 1);
        if (pick_id == 4 && pick >= 0) { cfg_.concurrent_downloads = pick + 1; cfg_.save(cfg_path_); dropdown_open_ = 0; }
    }
    y += (1 + 32) * s; row_i = 0;
    // ---- LIBRARY
    section("LIBRARY");
    card_fill(y, 73 * s);
    {
        row(skin::BookOpen, "Default View", "", 0, false, "", false, 71);
        std::vector<std::string> o = {"Artists", "Albums", "Songs", "Liked"};
        dropdown(5, {x + w - 16 * s - 120 * s, y - 71 * s + 21 * s, 120 * s, 29 * s}, o, cfg_.default_view);
        if (pick_id == 5 && pick >= 0) { cfg_.default_view = pick; lib_mode_ = pick; cfg_.save(cfg_path_); dropdown_open_ = 0; }
    }
    y += (1 + 32) * s; row_i = 0;
    // ---- ADVANCED (performance tiers)
    section("ADVANCED");
    card_fill(y, 335 * s);
    {
        static const char* names[3] = {"Balanced Mode", "Gaming Mode", "Eco Mode"};
        static const char* descs[3] = {"Default \xE2\x80\x94 60 fps, full GPU effects, standard prefetch",
                                       "Minimises system load for a foreground game \xE2\x80\x94 15 fps, GPU effects and translucency off, no prefetch, small image cache, CPU priority yielded",
                                       "10 fps, every GPU effect off, pixelated art, no prefetch, CPU priority yielded \xE2\x80\x94 maximum battery life"};
        static const uint32_t icons[3] = {0xf111, 0xf11b, 0xf06c};
        static const int order[3] = {1, 0, 2};  // Gaming, Balanced, Eco as listed in the old page
        for (int k = 0; k < 3; ++k) {
            int m = order[k];
            if (row(icons[m], names[m], descs[m], 1, false, cfg_.perf_mode == m ? "Active" : "", cfg_.perf_mode == m, m == 1 ? 86 : 61)) { cfg_.perf_mode = m; apply_perf_mode(); cfg_.save(cfg_path_); }
        }
        if (row(0xf15b, "Debug Logging", "Writes detailed logs to disk", 1, false, cfg_.debug_log ? "On" : "Off", cfg_.debug_log, 61)) { cfg_.debug_log = !cfg_.debug_log; cfg_.save(cfg_path_); }
        if (row(0xf201, "Render Timer HUD", "Shows frame time and redraw rate for performance checks", 1, false, cfg_.render_timer ? "On" : "Off", cfg_.render_timer, 61)) { cfg_.render_timer = !cfg_.render_timer; cfg_.save(cfg_path_); mark(); }
    }
    y += (1 + 32) * s; row_i = 0;
    // ---- DANGER ZONE
    section("DANGER ZONE");
    card_fill(y, 80 * s);
    if (row(0xf1f8, "Clear All App Data", "Removes downloaded songs, cover art, credentials, and all settings. Cannot be undone.", 2, false, "", false, 78)) modal_ = Modal::ConfirmWipe;
    y += (1 + 32) * s; row_i = 0;
    // ---- ABOUT
    section("ABOUT");
    card_fill(y, 266 * s);
    row(0xf05a, "Version", "", 0, false, kVersion, false, 52);
    row(0xf466, "Build Type", "", 0, false, "Release", false, 52);
    row(0xf073, "Built", "", 0, false, kBuildDate, false, 52);
    row(0xf5fd, "Tech Stack", "", 1, true, "", false, 52);
    row(0xf24e, "Licenses", "", 1, true, "", false, 52);
    gfx_->clip(nullptr);
    if (dropdown_draw_) dropdown_draw_();
}

void App::wipe_all_data() {
    secret_clear(cfg_.server_url + "|" + cfg_.username);
    if (downloads_) downloads_->clear_all();
    if (store_) { store_->erase_prefix(""); }
    cfg_ = Config{};
    cfg_.save(cfg_path_);
    logout();
    cfg_ = Config{};
    apply_theme();
}

void App::apply_perf_mode() {
    // Balanced = default; Gaming and Eco trim the image cache and drop look-ahead (see perfModeService). Frame caps are
    // moot here: the UI only redraws when something changes.
    size_t tex = cfg_.perf_mode == 0 ? 96 : cfg_.perf_mode == 1 ? 40 : 60;
    if (images_) images_->set_max_textures(tex);
    mark();
}
