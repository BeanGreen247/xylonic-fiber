// Cache location: folder picker, background move with progress, and the dialogs for it.
#include "app.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {
Color alpha(Color c, float a) { c.a = static_cast<uint8_t>(std::lround(a * 255)); return c; }
std::string fmt_size(uint64_t b) {
    char t[32];
    if (b < (1u << 20)) std::snprintf(t, sizeof t, "%llu KB", static_cast<unsigned long long>((b + 512) / 1024));
    else if (b < (1ull << 30)) std::snprintf(t, sizeof t, "%.0f MB", b / 1048576.0);
    else std::snprintf(t, sizeof t, "%.1f GB", b / 1073741824.0);
    return t;
}
}  // namespace

void App::choose_cache_folder() {
    if (move_state_ == 1) return;
    vols_ = storage_volumes();
    if (!vols_.empty()) {   // mobile: pick between the volumes the OS exposes
        vol_choice_ = -1;
        for (size_t i = 0; i < vols_.size(); ++i) if (vols_[i].path == cache_base()) vol_choice_ = static_cast<int>(i);
        move_error_.clear(); move_state_ = 0; modal_ = Modal::StoragePick; mark();
        return;
    }
    const std::string dir = pick_folder(cache_base());
    if (dir.empty()) { toast_ = "No folder chosen (a folder dialog needs zenity or kdialog)"; toast_until_ = SDL_GetTicks() + 3500; mark(); return; }
    start_cache_move(dir);
}

void App::start_cache_move(const std::string& to) {
    if (move_state_ == 1) return;
    const std::string nb = resolve_cache_root(to, "");
    const std::string ob = cache_base();
    if (nb.empty()) { move_error_ = "That folder cannot be used"; move_state_ = 2; modal_ = Modal::MoveCache; mark(); return; }
    if (nb == ob) { toast_ = "The cache is already there"; toast_until_ = SDL_GetTicks() + 2500; mark(); return; }
    downloads_.reset();   // stops the download threads before files move
    move_state_ = 1; move_done_ = 0; move_error_.clear(); move_target_ = nb;
    move_total_ = tree_bytes(ob + "downloads") + tree_bytes(ob + "stream");
    modal_ = Modal::MoveCache; mark();
    worker_->post([this, ob, nb] {
        std::string err;
        const bool ok = move_tree(ob + "downloads", nb + "downloads", move_done_, err) && move_tree(ob + "stream", nb + "stream", move_done_, err);
        ui_post([this, ok, err, nb] {
            if (ok) {
                cfg_.cache_root = nb == pref_ ? std::string() : nb;
                cfg_.save(cfg_path_);
                move_state_ = 0; modal_ = Modal::None;
                toast_ = "Cache moved"; toast_until_ = SDL_GetTicks() + 3000;
            } else { move_error_ = err.empty() ? "The move failed. Nothing was changed." : err; move_state_ = 2; }
            if (!cfg_.server_url.empty() && client_) open_account_data();   // re-resolves the folders and re-roots the rows
            mark();
        });
    });
}

void App::draw_storage_modal(float W, float H) {
    Ui& u = *ui_;
    const float s = S_;
    const Theme& th = u.th;
    const bool pick = modal_ == Modal::StoragePick;
    const float bw = std::min(460.0f, W / s - 24), bh = pick ? 330.0f : 220.0f;
    Rect b{(W - bw * s) / 2, (H - bh * s) / 2, bw * s, bh * s};
    gfx_->fill_round({b.x - 1, b.y - 1, b.w + 2, b.h + 2}, 3 * s, th.border);
    gfx_->fill_round(b, 2 * s, th.bg_light);
    T(600, 16, b.x + 20 * s, b.y + 18 * s, 24, "Permanent cache location", th.text);
    const bool moving = move_state_ == 1;
    if (!moving) {
        Rect cl{b.x + b.w - 52 * s, b.y + 14 * s, 32 * s, 32 * s};
        if (u.hover(cl)) gfx_->fill_round(cl, 16 * s, th.hover);
        fa(skin::Xmark, cl.x + 16 * s, cl.y + 16 * s, 16, th.sub);
        if (u.clicked(cl) || u.in.escape) { modal_ = Modal::None; move_state_ = 0; }
    }
    const float cw = b.w - 40 * s;
    if (pick) {
        T(400, 13, b.x + 20 * s, b.y + 52 * s, 20, "Downloaded songs and indexes are stored here. Choosing another location moves what is already cached.", th.sub, cw);
        float y = b.y + 100 * s;
        for (size_t i = 0; i < vols_.size(); ++i) {
            Rect o{b.x + 20 * s, y, cw, 52 * s};
            const bool sel = static_cast<int>(i) == vol_choice_;
            gfx_->fill_round(o, 8 * s, sel ? alpha(th.accent, 0.15f) : (u.hover(o) ? th.hover : alpha(th.text, 0.05f)));
            if (sel) gfx_->stroke_round(o, 8 * s, 1 * s, th.accent);
            fa(sel ? 0xf058 : 0xf111, o.x + 22 * s, o.y + o.h / 2, 16, sel ? th.accent : th.sub);
            const bool cur = vols_[i].path == cache_base();
            T(600, 14, o.x + 44 * s, o.y + 8 * s, 20, vols_[i].label + (cur ? " (current)" : ""), th.text, o.w - 56 * s);
            T(400, 12, o.x + 44 * s, o.y + 29 * s, 18, fmt_size(vols_[i].free_bytes) + " free of " + fmt_size(vols_[i].total_bytes), th.sub);
            if (u.clicked(o)) vol_choice_ = static_cast<int>(i);
            y += 60 * s;
        }
        const bool can = vol_choice_ >= 0 && vols_[vol_choice_].path != cache_base();
        if (can) T(400, 12, b.x + 20 * s, b.y + b.h - 92 * s, 18, "Moves " + fmt_size(tree_bytes(cache_base() + "downloads")) + " to " + vols_[vol_choice_].label + ". Keep the app open.", th.sub, cw);
        if (u.button({b.x + 20 * s, b.y + b.h - 56 * s, 120 * s, 36 * s}, "Close")) modal_ = Modal::None;
        Rect go{b.x + b.w - 20 * s - 140 * s, b.y + b.h - 56 * s, 140 * s, 36 * s};
        gfx_->fill_round(go, 8 * s, can ? (u.hover(go) ? alpha(th.accent, 0.85f) : th.accent) : alpha(th.text, 0.12f));
        T(600, 14, go.x + (go.w - gfx_->measure(F(600, 14), "Move here")) / 2, go.y, 36, "Move here", can ? rgb(0xffffff) : th.sub);
        if (can && u.clicked(go)) start_cache_move(vols_[vol_choice_].path);
        return;
    }
    if (moving) {
        const uint64_t done = move_done_.load();
        const int pct = move_total_ ? static_cast<int>(std::min<uint64_t>(100, done * 100 / move_total_)) : 100;
        T(400, 13, b.x + 20 * s, b.y + 56 * s, 20, "Moving to " + move_target_, th.sub, cw);
        Rect bar{b.x + 20 * s, b.y + 100 * s, cw, 8 * s};
        gfx_->fill_round(bar, 4 * s, {255, 255, 255, 40});
        gfx_->fill_round({bar.x, bar.y, std::max(8 * s, bar.w * pct / 100), bar.h}, 4 * s, th.accent);
        T(400, 13, b.x + 20 * s, b.y + 124 * s, 20, "Moving... " + std::to_string(pct) + "% (" + fmt_size(done) + " of " + fmt_size(move_total_) + ")", th.sub);
        mark();   // keep the bar animating
    } else {
        T(400, 13, b.x + 20 * s, b.y + 56 * s, 20, move_error_, th.danger, cw);
        if (u.button({b.x + b.w - 20 * s - 120 * s, b.y + b.h - 56 * s, 120 * s, 36 * s}, "Close")) { modal_ = Modal::None; move_state_ = 0; }
    }
}
