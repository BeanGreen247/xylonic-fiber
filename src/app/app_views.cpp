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
