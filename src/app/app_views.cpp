// Screens and widgets of the app (see app.cpp for session, playback and the event loop).
#include <algorithm>
#include <cmath>
#include "app.h"

namespace {
std::string mmss(int s) {
    if (s < 0) s = 0;
    char b[16];
    std::snprintf(b, sizeof b, "%d:%02d", s / 60, s % 60);
    return b;
}
}  // namespace

// ---------------------------------------------------------------- login

void App::draw_login(float W, float H) {
    Ui& u = *ui_;
    const float s = S_;
    float cw = std::min(W - 40 * s, 440 * s), ch = 440 * s;
    Rect card{(W - cw) / 2, std::max(20 * s, (H - ch) / 2), cw, ch};
    gfx_->fill_round(card, 14 * s, u.th.panel);
    float x = card.x + 32 * s, w = card.w - 64 * s, y = card.y + 28 * s;
    u.label(u.f_title, x, y, "xylonic-fiber", u.th.text);
    y += 46 * s;
    u.label(u.f_small, x, y, "Subsonic / Navidrome server", u.th.sub);
    y += 32 * s;
    float fh = 42 * s;
    u.text_field(kUrl, {x, y, w, fh}, f_url_, "http://192.168.1.100:4533");
    y += fh + 12 * s;
    u.text_field(kUser, {x, y, w, fh}, f_user_, "Username");
    y += fh + 12 * s;
    u.text_field(kPass, {x, y, w, fh}, f_pass_, "Password", true);
    y += fh + 20 * s;
    if (u.in.tab) u.focus = u.focus == kUrl ? kUser : u.focus == kUser ? kPass : kUrl;
    bool go = u.button({x, y, w, 44 * s}, connecting_ ? "Connecting..." : "Connect", true) || (u.in.enter && u.focus != 0);
    if (go && !connecting_) connect(false);
    y += 56 * s;
    if (!cfg_.server_url.empty() && !cfg_.username.empty() && !connecting_ && !cert_prompt_) {
        if (u.button({x, y, w, 36 * s}, "Offline mode (downloaded music)")) { enter_offline(); return; }
        y += 46 * s;
    }
    if (cert_prompt_) {
        u.label(u.f_small, x, y, login_msg_, u.th.danger, w);
        y += 20 * s;
        u.label(u.f_small, x, y, "SHA-256 " + cert_fp_.substr(0, 32), u.th.sub, w);
        y += 18 * s;
        u.label(u.f_small, x, y, cert_fp_.substr(32), u.th.sub, w);
        y += 24 * s;
        if (u.button({x, y, w, 38 * s}, "Trust this certificate and connect")) connect(true);
    } else if (!login_msg_.empty()) {
        u.label(u.f_small, x, y, login_msg_, login_msg_ == "Connecting..." ? u.th.sub : u.th.danger, w);
    }
}

// ---------------------------------------------------------------- main layout

// ---------------------------------------------------------------- content views

float App::draw_album_grid(Rect r, const std::vector<Album>& albums, float y, float& hit) {
    Ui& u = *ui_;
    const float s = S_;
    float cw = 164 * s, gap = 16 * s, pad = 24 * s;
    int cols = std::max(1, static_cast<int>((r.w - 2 * pad + gap) / (cw + gap)));
    float ch = cw + 44 * s;
    for (size_t i = 0; i < albums.size(); ++i) {
        int col = static_cast<int>(i) % cols, row = static_cast<int>(i) / cols;
        float x = r.x + pad + col * (cw + gap), yy = y + row * (ch + gap);
        if (yy + ch < r.y || yy > r.y + r.h) continue;
        const Album& a = albums[i];
        Rect cell{x, yy, cw, ch};
        if (u.hover(cell) && r.contains(u.in.mx, u.in.my)) gfx_->fill_round({x - 6 * s, yy - 6 * s, cw + 12 * s, ch + 6 * s}, 10 * s, u.th.panel);
        if (SDL_Texture* t = cover(a.cover_art, 164)) gfx_->image(t, {x, yy, cw, cw});
        else gfx_->fill_round({x, yy, cw, cw}, 8 * s, u.th.panel2);
        u.label(u.f_bold, x, yy + cw + 6 * s, a.name, u.th.text, cw);
        u.label(u.f_small, x, yy + cw + 26 * s, a.artist, u.th.sub, cw);
        if (u.clicked(cell) && r.contains(u.in.mx, u.in.my)) { push_view(ViewKind::Album, a.id, a.name); load_album(a.id); }
    }
    (void)hit;
    int rows = static_cast<int>((albums.size() + cols - 1) / cols);
    return rows * (ch + gap);
}

void App::draw_song_rows(Rect r, const std::vector<Song>& songs, Scroll& sc, bool show_cover, int base_id) {
    Ui& u = *ui_;
    const float s = S_;
    float rh = (show_cover ? 56.0f : 44.0f) * s;
    if (u.hover(r)) sc.y -= u.in.wheel * 60 * s;
    sc.clamp(rh * songs.size() + 16 * s, r.h);
    size_t i0 = static_cast<size_t>(std::max(0.0f, std::floor(sc.y / rh)));
    size_t i1 = std::min(songs.size(), static_cast<size_t>(std::ceil((sc.y + r.h) / rh)) + 1);
    const Song* playing = queue_.current();
    for (size_t i = i0; i < i1; ++i) {
        const Song& sg = songs[i];
        Rect row{r.x + 12 * s, r.y + i * rh - sc.y, r.w - 24 * s, rh};
        bool isplay = playing && playing->id == sg.id;
        bool hov = u.hover(row) && r.contains(u.in.mx, u.in.my);
        if (hov) gfx_->fill_round(row, 8 * s, u.th.hover);
        float x = row.x + 12 * s;
        if (show_cover) {
            if (SDL_Texture* t = cover(sg.cover_art, 44)) gfx_->image(t, {x, row.y + 6 * s, 44 * s, 44 * s});
            else gfx_->fill_round({x, row.y + 6 * s, 44 * s, 44 * s}, 5 * s, u.th.panel2);
            x += 56 * s;
        } else {
            u.label_in(u.f_body, {x, row.y, 36 * s, row.h}, sg.track > 0 ? std::to_string(sg.track) : "", isplay ? u.th.accent : u.th.sub);
            x += 44 * s;
        }
        float tw = row.w - (x - row.x) - 90 * s;
        if (show_cover) {
            u.label(u.f_body, x, row.y + 8 * s, sg.title, isplay ? u.th.accent : u.th.text, tw);
            u.label(u.f_small, x, row.y + 30 * s, sg.artist + (sg.album.empty() ? "" : "  -  " + sg.album), u.th.sub, tw);
        } else {
            u.label_in(u.f_body, {x, row.y, tw, row.h}, sg.title, isplay ? u.th.accent : u.th.text);
        }
        u.label_in(u.f_small, {row.x + row.w - 72 * s, row.y, 60 * s, row.h}, mmss(sg.duration), u.th.sub);
        auto ds = dl_state_.find(sg.id);
        if (ds != dl_state_.end()) {
            Rect dot{row.x + row.w - 100 * s, row.y + row.h / 2 - 5 * s, 10 * s, 10 * s};
            if (ds->second.state == Downloads::Done) gfx_->circle(dot.x + 5 * s, dot.y + 5 * s, 4 * s, u.th.accent);
            else if (ds->second.state == Downloads::Failed) gfx_->circle(dot.x + 5 * s, dot.y + 5 * s, 4 * s, u.th.danger);
            else gfx_->ring(dot.x + 5 * s, dot.y + 5 * s, 4 * s, 1.5f * s, u.th.sub);
        }
        if (u.clicked(row) && r.contains(u.in.mx, u.in.my)) {
            play_songs(songs, i, false);
        }
    }
    (void)base_id;
}

void App::draw_search(Rect r) {
    Ui& u = *ui_;
    const float s = S_;
    std::string before = search_q_;
    u.text_field(kSearch, {r.x + 24 * s, r.y + 4 * s, std::min(r.w - 48 * s, 520 * s), 40 * s}, search_q_, "Search songs");
    if (search_q_ != before) run_search();
    Rect list{r.x, r.y + 56 * s, r.w, r.h - 56 * s};
    gfx_->clip(&list);
    if (search_res_.empty() && search_q_.size() >= 2) u.label(u.f_body, r.x + 24 * s, list.y + 10 * s, "No results yet", u.th.sub);
    draw_song_rows(list, search_res_, search_scroll_, true, 200);
}

void App::draw_queue(Rect r) {
    Ui& u = *ui_;
    const float s = S_;
    if (queue_.empty()) { u.label(u.f_body, r.x + 24 * s, r.y + 10 * s, "Queue is empty. Play something to get started.", u.th.sub); return; }
    u.label(u.f_small, r.x + 24 * s, r.y + 8 * s, std::to_string(queue_.size()) + " songs", u.th.sub);
    if (u.button({r.x + r.w - 124 * s, r.y + 2 * s, 100 * s, 30 * s}, "Clear")) { queue_.clear(); player_.stop(); ++play_gen_; return; }
    Rect list{r.x, r.y + 40 * s, r.w, r.h - 40 * s};
    gfx_->clip(&list);
    float rh = 56 * s;
    Scroll& sc = queue_scroll_;
    if (u.hover(list)) sc.y -= u.in.wheel * 60 * s;
    sc.clamp(rh * queue_.size() + 16 * s, list.h);
    size_t i0 = static_cast<size_t>(std::max(0.0f, std::floor(sc.y / rh)));
    size_t i1 = std::min(queue_.size(), static_cast<size_t>(std::ceil((sc.y + list.h) / rh)) + 1);
    for (size_t i = i0; i < i1; ++i) {
        const Song& sg = queue_.songs()[i];
        Rect row{list.x + 12 * s, list.y + i * rh - sc.y, list.w - 24 * s, rh};
        bool cur = i == queue_.index();
        bool hov = u.hover(row) && list.contains(u.in.mx, u.in.my);
        if (hov || cur) gfx_->fill_round(row, 8 * s, cur ? u.th.panel2 : u.th.hover);
        if (SDL_Texture* t = cover(sg.cover_art, 44)) gfx_->image(t, {row.x + 10 * s, row.y + 6 * s, 44 * s, 44 * s});
        else gfx_->fill_round({row.x + 10 * s, row.y + 6 * s, 44 * s, 44 * s}, 5 * s, u.th.panel2);
        float tw = row.w - 150 * s;
        u.label(u.f_body, row.x + 66 * s, row.y + 8 * s, sg.title, cur ? u.th.accent : u.th.text, tw);
        u.label(u.f_small, row.x + 66 * s, row.y + 30 * s, sg.artist, u.th.sub, tw);
        Rect x{row.x + row.w - 40 * s, row.y + 12 * s, 32 * s, 32 * s};
        if (hov && u.icon_button(x, Icon::Close, false, 0.3f)) { bool was_cur = cur; queue_.remove(i); if (was_cur) { if (queue_.empty()) { player_.stop(); ++play_gen_; } else start_current(); } return; }
        if (u.clicked(row) && !x.contains(u.in.mx, u.in.my) && list.contains(u.in.mx, u.in.my)) { queue_.jump(i); start_current(); return; }
    }
}

void App::draw_settings(Rect r) {
    Ui& u = *ui_;
    const float s = S_;
    // Scrollable column; total height computed first so the wheel clamps correctly.
    const float total = 920 * s;
    if (u.hover(r)) settings_scroll_.y -= u.in.wheel * 60 * s;
    settings_scroll_.clamp(total, r.h);
    float x = r.x + 24 * s, y = r.y + 8 * s - settings_scroll_.y;
    auto head = [&](const std::string& t) { u.label(u.f_bold, x, y, t, u.th.text); y += 28 * s; };
    head("Server");
    u.label(u.f_body, x, y, cfg_.server_url + (cfg_.pin_sha256.empty() ? "" : "   (pinned certificate)"), u.th.sub, r.w - 48 * s); y += 24 * s;
    u.label(u.f_body, x, y, "User: " + cfg_.username, u.th.sub); y += 40 * s;

    head("Appearance");
    // Theme grid: 8 presets + 4 custom slots
    float sw = 44 * s, gap = 10 * s;
    for (int i = 0; i < kPresetCount + kCustomSlots; ++i) {
        int col = i % 6, row = i / 6;
        Rect b{x + col * (sw + gap + 80 * s), y + row * (sw + gap), sw, sw};
        ThemeDef d = theme_def(i);
        gfx_->fill_round(b, 10 * s, rgb(d.primary));
        if (cfg_.theme == i) gfx_->stroke({b.x - 3 * s, b.y - 3 * s, b.w + 6 * s, b.h + 6 * s}, 2 * s, u.th.text);
        u.label(u.f_small, b.x + sw + 8 * s, b.y + sw / 2 - 8 * s, d.name.substr(0, 11), u.th.sub, 70 * s);
        if (u.clicked(b)) { cfg_.theme = i; apply_theme(); cfg_.save(cfg_path_); }
    }
    y += 2 * (sw + gap) + 12 * s;
    static const char* modes[] = {"System", "Light", "Dark"};
    for (int i = 0; i < 3; ++i)
        if (u.button({x + i * 96 * s, y, 88 * s, 34 * s}, modes[i], cfg_.theme_mode == i)) { cfg_.theme_mode = i; apply_theme(); cfg_.save(cfg_path_); }
    y += 52 * s;
    // Custom theme editor
    head("Custom theme");
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
        if (cfg_.theme == kPresetCount + edit_slot_) cfg_.theme = 0;  // as ThemeContext.resetCustomTheme
        edit_loaded_ = false;
        apply_theme(); cfg_.save(cfg_path_);
    }
    y += 54 * s;

    head("Streaming quality (kbps, 0 = original)");
    static const int q[] = {0, 320, 256, 192, 128, 64};
    for (int i = 0; i < 6; ++i) {
        Rect b{x + i * 84 * s, y, 76 * s, 34 * s};
        bool on = cfg_.stream_quality == q[i];
        if (u.button(b, q[i] ? std::to_string(q[i]) : "Original", on)) { cfg_.stream_quality = q[i]; cfg_.save(cfg_path_); }
    }
    y += 54 * s;
    head("Cache");
    if (u.button({x, y, 190 * s, 34 * s}, "Clear cover memory")) images_->clear_memory();
}

void App::draw_downloads(Rect r) {
    Ui& u = *ui_;
    const float s = S_;
    if (!downloads_) { u.label(u.f_body, r.x + 24 * s, r.y + 10 * s, "Downloads are unavailable.", u.th.sub); return; }
    auto items = downloads_->snapshot();
    int done = 0, active = 0, failed = 0;
    int64_t bytes = 0;
    for (auto& it : items) { if (it.state == Downloads::Done) { ++done; bytes += it.total; } else if (it.state == Downloads::Failed) ++failed; else ++active; }
    char hdr[128];
    std::snprintf(hdr, sizeof hdr, "%d downloaded (%.1f MB), %d pending, %d failed", done, bytes / 1048576.0, active, failed);
    u.label(u.f_small, r.x + 24 * s, r.y + 10 * s, hdr, u.th.sub);
    float bx = r.x + r.w - 24 * s - 120 * s;
    if (u.button({bx, r.y + 2 * s, 120 * s, 30 * s}, "Clear all")) { downloads_->clear_all(); return; }
    if (u.button({bx - 130 * s, r.y + 2 * s, 120 * s, 30 * s}, "Verify")) downloads_->verify();
    if (failed && u.button({bx - 260 * s, r.y + 2 * s, 120 * s, 30 * s}, "Retry failed")) downloads_->retry_failed();
    Rect list{r.x, r.y + 40 * s, r.w, r.h - 40 * s};
    gfx_->clip(&list);
    float rh = 60 * s;
    if (items.empty()) u.label(u.f_body, r.x + 24 * s, list.y + 8 * s, "Nothing downloaded yet. Open an album and press Download.", u.th.sub);
    if (u.hover(list)) dl_scroll_.y -= u.in.wheel * 60 * s;
    dl_scroll_.clamp(rh * items.size() + 16 * s, list.h);
    size_t i0 = static_cast<size_t>(std::max(0.0f, std::floor(dl_scroll_.y / rh)));
    size_t i1 = std::min(items.size(), static_cast<size_t>(std::ceil((dl_scroll_.y + list.h) / rh)) + 1);
    static const char* names[] = {"Queued", "Downloading", "Done", "Failed", "Paused"};
    for (size_t i = i0; i < i1; ++i) {
        const auto& it = items[i];
        Rect row{list.x + 12 * s, list.y + i * rh - dl_scroll_.y, list.w - 24 * s, rh};
        bool hov = u.hover(row) && list.contains(u.in.mx, u.in.my);
        if (hov) gfx_->fill_round(row, 8 * s, u.th.hover);
        float tw = row.w - 260 * s;
        u.label(u.f_body, row.x + 14 * s, row.y + 8 * s, it.song.title, u.th.text, tw);
        u.label(u.f_small, row.x + 14 * s, row.y + 32 * s, it.song.artist + "  -  " + names[it.state], it.state == Downloads::Failed ? u.th.danger : u.th.sub, tw);
        if (it.state == Downloads::Active || it.state == Downloads::Paused) {
            float frac = it.total > 0 ? static_cast<float>(it.bytes) / it.total : 0;
            Rect pb{row.x + row.w - 230 * s, row.y + row.h / 2 - 2 * s, 140 * s, 4 * s};
            gfx_->fill_round(pb, 2 * s, u.th.panel2);
            gfx_->fill_round({pb.x, pb.y, pb.w * std::min(1.0f, frac), pb.h}, 2 * s, u.th.accent);
        }
        Rect act{row.x + row.w - 80 * s, row.y + 14 * s, 32 * s, 32 * s};
        Rect del{row.x + row.w - 44 * s, row.y + 14 * s, 32 * s, 32 * s};
        if (it.state == Downloads::Active || it.state == Downloads::Queued) { if (u.icon_button(act, Icon::Pause)) downloads_->pause(it.song.id); }
        else if (it.state == Downloads::Paused || it.state == Downloads::Failed) { if (u.icon_button(act, Icon::Play)) downloads_->resume(it.song.id); }
        if (u.icon_button(del, Icon::Close, false, 0.3f)) { downloads_->remove(it.song.id); return; }
    }
}
