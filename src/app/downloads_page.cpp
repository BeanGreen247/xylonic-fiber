// Downloads page = the original DownloadManagerWindow (inline): overall progress, controls, active downloads, queue list,
// Cache Statistics with the Manage Cache section (location, cached albums, integrity check, clear). Geometry measured from
// the running original at 1024x768; CSS values from dist/assets/index-*.css.
#include <sys/statvfs.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <map>
#include "app.h"
#include "skin.h"

namespace {
Color alpha(Color c, float a) { c.a = static_cast<uint8_t>(std::lround(a * 255)); return c; }
std::string fmt_bytes(uint64_t b) {   // formatBytes of the Download Manager
    char t[40] = {};
    if (b == 0) return "0 Bytes";
    if (b < 1024) std::snprintf(t, sizeof t, "%llu Bytes", static_cast<unsigned long long>(b));
    else if (b < 1048576ull) std::snprintf(t, sizeof t, "%.2f KB", b / 1024.0);
    else if (b < 1073741824ull) std::snprintf(t, sizeof t, "%.2f MB", b / 1048576.0);
    else std::snprintf(t, sizeof t, "%.2f GB", b / 1073741824.0);
    return t;
}
std::string mmss(int s) { if (s <= 0) return "--:--"; char b[16] = {}; std::snprintf(b, sizeof b, "%d:%02d", s / 60, s % 60); return b; }
}  // namespace

void App::draw_downloads(Rect r) {
    Ui& u = *ui_;
    const float s = S_;
    const Theme& th = u.th;
    // Phone: the page has no bottom-nav tab, so it needs its own way out. Back to where it was opened from, else Home.
    auto close_btn = [&](float cy) {
        if (!mobile_) return;
        Rect cl{r.x + r.w - 12 * s - 40 * s, cy - 20 * s, 40 * s, 40 * s};
        if (u.hover(cl)) gfx_->fill_round(cl, 20 * s, th.hover);
        fa(skin::Xmark, cl.x + 20 * s, cl.y + 20 * s, 18, th.text);
        if (u.clicked(cl) && r.contains(u.in.mx, u.in.my)) set_nav(dl_has_prev_ && dl_prev_nav_ != Nav::Downloads ? dl_prev_nav_ : Nav::Home);
    };
    if (!downloads_) { T(400, 15, r.x + 24 * s, r.y + 24 * s, 24, "Downloads are unavailable.", th.sub); close_btn(r.y + 30 * s); return; }
    const bool cp = mobile_;
    const float pad = (cp ? 12.0f : 24.0f) * s;
    const float x = r.x + pad, w = r.w - 2 * pad - (cp ? 0.0f : 12.0f * s);
    const auto all_snap = downloads_->snapshot();
    const auto& all = *all_snap;
    if (dm_snap_ != all_snap || dm_cleared_n_ != dm_cleared_.size()) {   // 5000 queued songs would otherwise be re-walked every frame
        DmStats& st = dm_stats_;
        st = DmStats{};
        for (const auto& it : all) if (!(it.state == Downloads::Done && dm_cleared_.count(it.song.id))) st.queue.push_back(&it);   // finished ones can be cleared from the list
        st.total = static_cast<int>(st.queue.size());
        for (auto* it : st.queue) { if (it->state == Downloads::Done) ++st.done; else if (it->state == Downloads::Failed) ++st.failed; else ++st.pending; if (it->state == Downloads::Active) st.active.push_back(it); }
        // cache statistics: every finished song is cached
        for (const auto& it : all) if (it.state == Downloads::Done) {
            ++st.cached_songs; st.cache_bytes += static_cast<uint64_t>(std::max<int64_t>(0, it.total));
            st.albums[it.song.album_id] = {it.song.album, it.song.artist}; ++st.album_count[it.song.album_id];
        }
        dm_snap_ = all_snap; dm_cleared_n_ = dm_cleared_.size();
    }
    const auto& queue = dm_stats_.queue;
    const auto& active = dm_stats_.active;
    const int total = dm_stats_.total, done = dm_stats_.done, failed = dm_stats_.failed, pending = dm_stats_.pending, cached_songs = dm_stats_.cached_songs;
    const uint64_t cache_bytes = dm_stats_.cache_bytes;
    const auto& albums = dm_stats_.albums;
    auto& album_count = dm_stats_.album_count;
    (void)done; (void)failed; (void)pending;
    const bool has_activity = total > 0;
    const bool paused = downloads_->paused_all();
    // ---- layout height first (for the scroll clamp)
    // Stat chips under the progress bar wrap onto further lines when the card is too narrow (a phone fits about three).
    struct Stat { std::string text; Color col {}; };
    std::vector<Stat> stats;
    stats.push_back({"Total: " + std::to_string(total), rgb(0xdbdbdb)});
    stats.push_back({"Done: " + std::to_string(done), rgb(0xdbdbdb)});
    if (failed) stats.push_back({"Failed: " + std::to_string(failed), rgb(0xdc3545)});
    stats.push_back({"Pending: " + std::to_string(pending), rgb(0xdbdbdb)});
    int stat_lines = 1;
    {
        const float avail = w - 40 * s;
        float lx = 0;
        for (const Stat& st : stats) {
            const float sw = gfx_->measure(F(400, 14.4f), st.text);
            if (lx > 0 && lx + sw > avail) { ++stat_lines; lx = 0; }
            lx += sw + 20 * s;
        }
    }
    auto card_h_progress = 106.0f + (stat_lines - 1) * 28.0f, card_h_controls = cp ? 112.0f : 73.0f;
    float content = 48 + 32;
    if (has_activity) content += card_h_progress + 28 + card_h_controls + 28;
    for (size_t i = 0; i < active.size(); ++i) content += (active.size() > 1 ? 88.0f : 128.0f) + 12;
    content += has_activity ? 0 : 53.6f + 20;
    if (has_activity) content += 40 + 12 + (dm_show_queue_ ? std::min(300.0f, queue.size() * 78.0f + 24) + 12 : 0) + 28;
    struct statvfs sv{};
    const bool have_disk = ::statvfs(perm_dir_.c_str(), &sv) == 0;
    // Manage Cache layout below the tiles (T = tiles bottom), measured on the original: location T+40 (154, or 106 without disk bar),
    // "Cached Albums" title +24, list +40 (4 + 89 per item, max 200; empty box 86), verify +20 (109, +32 with result), Clear +20 (41), bottom pad 20
    const float loc_h = have_disk ? 154.0f : 106.0f;
    const float list_h = albums.empty() ? 86.0f : std::min(200.0f, 4.0f + albums.size() * 89.0f);
    const float ver_h = dm_verified_done_ ? 141.0f : 109.0f;
    const float manage_h = 40 + loc_h + 24 + 40 + list_h + 20 + ver_h + 20 + 41 + 20 - 36;   // minus the 36 px the collapsed card keeps under the tiles
    content += 236 + (dm_manage_ ? manage_h : 0) + 24;
    if (u.hover(r)) dl_scroll_.y -= u.in.wheel * 60 * s;
    dl_scroll_.clamp(content * s + pad, r.h);
    float y = r.y + (cp ? 12.0f * s : pad) - dl_scroll_.y;
    // title: .library-title 32 px, icon + text
    if (cp) {   // compact: 24 px title in a 36 px row, 16 px gap
        fa(skin::Download, x + 13 * s, y + 18 * s, 22, th.accent);
        T(600, 24, x + 40 * s, y, 36, "Downloads", th.text);
        close_btn(y + 18 * s);
        y += (36 + 16) * s;
    } else {
        fa(skin::Download, x + 17 * s, y + 24 * s, 30, th.accent);
        gfx_->set_letter_spacing(-0.32f * s);
        T(700, 32, x + 52 * s, y, 48, "Downloads", th.text);
        gfx_->set_letter_spacing(0);
        y += (48 + 32) * s;
    }
    gfx_->clip(&r);
    auto card = [&](Rect c, float rad, Color fill) { gfx_->fill_round(c, rad * s, fill); };
    auto btn = [&](Rect b, const std::string& label, Color col, bool enabled = true, float px = 15.2f, float tx_off = -1) {
        if (enabled && u.hover(b)) gfx_->fill_round(b, 10 * s, {255, 255, 255, 18}); else gfx_->fill_round(b, 10 * s, {0, 0, 0, 36});
        T(600, px, tx_off >= 0 ? b.x + tx_off : b.x + (b.w - gfx_->measure(F(600, px), label)) / 2, b.y, b.h / s, label, enabled ? col : alpha(col, 0.4f));
        return enabled && u.clicked(b) && r.contains(u.in.mx, u.in.my);
    };
    if (has_activity) {
        // progress card: bar 32 + 12 + stats 22, padding 20
        Rect pc{x, y, w, card_h_progress * s};
        card(pc, 12, th.panel2);
        Rect bar{pc.x + 20 * s, pc.y + 20 * s, pc.w - 40 * s, 32 * s};
        gfx_->fill_round(bar, 16 * s, th.bg_light);
        gfx_->fill_round(bar, 16 * s, {255, 255, 255, 36});   // the track must show on every theme (bg_light can equal the card colour)
        const int pct = total > 0 ? static_cast<int>(std::lround(100.0 * done / total)) : 0;
        const float fw = bar.w * pct / 100.0f;
        if (fw > 0) {
            const float fwc = std::max(fw, 32 * s);
            gfx_->fill_round({bar.x, bar.y, fwc, bar.h}, 16 * s, th.accent);
            if (fwc > 40 * s) gfx_->quad_gradient({bar.x + 16 * s, bar.y, fwc - 32 * s, bar.h}, th.accent, th.primary_light, th.primary_light, th.accent);
            gfx_->circle(bar.x + fwc - 16 * s, bar.y + 16 * s, 16 * s, th.primary_light);
        }
        const std::string pt = std::to_string(pct) + "%";
        T(600, 14.4f, bar.x + (bar.w - gfx_->measure(F(600, 14.4f), pt)) / 2, bar.y, 32, pt, rgb(0xffffff));
        {
            float sx = pc.x + 20 * s, sy = bar.y + 44 * s;
            const float right = pc.x + pc.w - 20 * s;
            for (const Stat& st : stats) {
                const float sw = gfx_->measure(F(400, 14.4f), st.text);
                if (sx > pc.x + 20 * s && sx + sw > right) { sx = pc.x + 20 * s; sy += 28 * s; }
                T(400, 14.4f, sx, sy, 22, st.text, st.col);
                sx += sw + 20 * s;
            }
        }
        y += (card_h_progress + 28) * s;
        // controls card: padding 16, buttons wrap onto further rows (flex-wrap, gap 12; compact buttons are 34 high)
        const float bh = (cp ? 34.0f : 41.0f) * s, bpx = (cp ? 14.0f : 15.2f), bpad = (cp ? 32.0f : 40.0f) * s;
        struct Ctl { std::string label; Color col {}; bool enabled = false; int act = 0; };
        std::vector<Ctl> ctl;
        ctl.push_back({paused ? "Resume" : "Pause", rgb(0xffffff), total > 0, 0});
        if (failed) ctl.push_back({"Retry Failed (" + std::to_string(failed) + ")", rgb(0xffffff), true, 1});
        if (done > 0) ctl.push_back({"Clear Completed", rgb(0xffffff), true, 2});
        ctl.push_back({"Clear Queue", rgb(0xdc3545), total > 0, 3});
        std::vector<float> bw; float rows = 1, rowx = 0;
        for (auto& k : ctl) { bw.push_back(gfx_->measure(F(600, bpx), k.label) + bpad); }
        for (float v : bw) { if (rowx > 0 && rowx + v > w - 32 * s) { rows++; rowx = 0; } rowx += v + 12 * s; }
        const float ch = (32 + rows * (bh / s) + (rows - 1) * 12);
        Rect cc{x, y, w, ch * s};
        card(cc, 12, th.panel2);
        float bx = cc.x + 16 * s, by = cc.y + 16 * s;
        int hit = -1;
        for (size_t i = 0; i < ctl.size(); ++i) {
            if (bx > cc.x + 16 * s && bx + bw[i] > cc.x + cc.w - 16 * s) { bx = cc.x + 16 * s; by += bh + 12 * s; }
            if (btn({bx, by, bw[i], bh}, ctl[i].label, ctl[i].col, ctl[i].enabled, bpx)) hit = ctl[i].act;
            bx += bw[i] + 12 * s;
        }
        if (hit == 0) { if (paused) downloads_->resume_all(); else downloads_->pause_all(); }
        else if (hit == 1) downloads_->retry_failed();
        else if (hit == 2) { for (auto* it : queue) if (it->state == Downloads::Done) dm_cleared_.insert(it->song.id); mark(); }
        else if (hit == 3) { downloads_->clear_queue_async(); dm_cleared_.clear(); mark(); }
        y += (ch + 28) * s;
        // active downloads: one card each ("Now downloading" label only when there is one)
        const bool multi = active.size() > 1;
        for (auto* it : active) {
            const float ch = multi ? 88.0f : 128.0f;   // room for the percentage under the bar
            Rect ac{x, y, w, ch * s};
            card(ac, 12, th.accent); card({ac.x + (multi ? 1 : 2) * s, ac.y + (multi ? 1 : 2) * s, ac.w - (multi ? 2 : 4) * s, ac.h - (multi ? 2 : 4) * s}, 11, th.bg_light);
            float ty = ac.y + (multi ? 10 : 16) * s;
            if (!multi) { gfx_->set_letter_spacing(0.99f * s); fa(0xf0ab, ac.x + 20 * s + 6 * s, ty + 9 * s, 12, th.accent); T(700, 12.5f, ac.x + 20 * s + 20 * s, ty, 18, "NOW DOWNLOADING", th.accent); gfx_->set_letter_spacing(0); ty += 24 * s; }
            T(700, multi ? 14 : 16, ac.x + (multi ? 14 : 20) * s, ty, 24, it->song.title, th.text, ac.w - 40 * s);
            ty += (multi ? 20 : 24) * s;
            T(400, multi ? 12.2f : 13.6f, ac.x + (multi ? 14 : 20) * s, ty, 20, it->song.artist + " - " + it->song.album, th.sub, ac.w - 40 * s);
            ty += (multi ? 22 : 30) * s;
            const float frac = it->total > 0 ? std::min(1.0f, static_cast<float>(downloads_->live_bytes(it->song.id, it->bytes)) / it->total) : 0;
            Rect ab{ac.x + (multi ? 14 : 20) * s, ty, ac.w - (multi ? 28 : 40) * s, 6 * s};
            gfx_->fill_round(ab, 3 * s, {255, 255, 255, 61}); if (frac > 0) gfx_->fill_round({ab.x, ab.y, std::max(6 * s, ab.w * frac), ab.h}, 3 * s, th.accent);
            const std::string ps = std::to_string(static_cast<int>(frac * 100)) + "%";
            T(700, 13.1f, ab.x + ab.w - gfx_->measure(F(700, 13.1f), ps), ab.y + 10 * s, 18, ps, th.accent);
            y += (ch + 12) * s;
        }
        // queue toggle + list
        Rect qt{x, y, w, 40 * s};
        const bool qh = u.hover(qt);
        fa(dm_show_queue_ ? 0xf077 : 0xf078, qt.x + 16 * s + 6 * s, qt.y + 20 * s, 13, qh ? th.accent : rgb(0xdbdbdb));
        T(600, 14.4f, qt.x + 16 * s + 26 * s, qt.y, 40, "Show queue (" + std::to_string(total) + ")", qh ? th.accent : rgb(0xdbdbdb));
        if (dm_show_queue_) T(600, 14.4f, qt.x + 16 * s + 26 * s, qt.y, 40, "", th.accent);
        if (u.clicked(qt) && r.contains(u.in.mx, u.in.my)) { dm_show_queue_ = !dm_show_queue_; mark(); }
        y += (40 + 12) * s;
        if (dm_show_queue_) {
            const float lh = std::min(300.0f, queue.size() * 78.0f + 24);
            Rect ql{x, y, w, lh * s};
            card(ql, 12, th.bg_light);
            gfx_->clip(&ql);
            float iy = ql.y + 12 * s;
            static const char* st_name[] = {"Pending", "Downloading", "Completed", "Failed", "Paused"};
            for (auto* it : queue) {
                Rect ir{ql.x + 12 * s, iy, ql.w - 24 * s, 68 * s};
                if (ir.y + ir.h >= ql.y && ir.y <= ql.y + ql.h) {
                    card(ir, 10, th.bg_medium);
                    const bool dl = it->state == Downloads::Active, bad = it->state == Downloads::Failed;
                    if (dl) gfx_->stroke_round(ir, 10 * s, 2 * s, th.accent); else if (bad) gfx_->stroke_round(ir, 10 * s, 2 * s, rgb(0xdc3545));
                    const char* stn = st_name[it->state];
                    const float sw = gfx_->measure(F(500, 14), stn);
                    T(500, 14, ir.x + 16 * s, ir.y, 68, stn, dl ? th.accent : bad ? rgb(0xdc3545) : it->state == Downloads::Done ? th.accent : th.sub);
                    const float tx = ir.x + 16 * s + sw + 14 * s, tw = ir.w - (tx - ir.x) - 190 * s;
                    T(600, 14.4f, tx, ir.y + 14 * s, 22, it->song.title, th.text, tw);
                    T(400, 12.5f, tx, ir.y + 36 * s, 19, it->song.artist + " - " + it->song.album, th.sub, tw);
                    float rx = ir.x + ir.w - 16 * s;
                    const std::string q = it->quality == 0 || (it->quality < 0 && cfg_.download_quality == 0) ? "RAW" : std::to_string(it->quality < 0 ? cfg_.download_quality : it->quality) + "k";
                    const float qw = gfx_->measure(F(600, 12), q) + 16 * s;
                    gfx_->fill_round({rx - qw, ir.y + 24 * s, qw, 20 * s}, 4 * s, th.accent);
                    T(600, 12, rx - qw + 8 * s, ir.y + 24 * s, 20, q, rgb(0xffffff));
                    rx -= qw + 12 * s;
                    const std::string d = mmss(it->song.duration);
                    const float dw = gfx_->measure(F(400, 13.6f), d);
                    T(400, 13.6f, rx - dw, ir.y, 68, d, th.sub);
                }
                iy += 78 * s;
            }
            gfx_->clip(&r);
            y += (lh + 12) * s;
        }
        y += 28 * s - 12 * s;
    } else {
        const std::string msg = "No downloads in queue";
        T(400, 14.4f, x + (w - gfx_->measure(F(400, 14.4f, true), msg)) / 2, y + 16 * s, 21.6f, msg, th.sub, w, true);
        y += (53.6f + 20) * s;
    }
    // ---- Cache Statistics
    const float tile_h = 64 * s, tile_w = (w - 40 * s - 8 * s) / 2;
    Rect cs{x, y, w, (236 + (dm_manage_ ? manage_h : 0)) * s};
    card(cs, 12, th.panel2);
    T(600, 17.6f, cs.x + 20 * s, cs.y + 22 * s, 26, "Cache Statistics", th.text);
    const std::string tl = dm_manage_ ? "Hide Cache" : "Manage Cache";
    const float tw2 = gfx_->measure(F(600, 13.6f), tl) + 32 * s;
    Rect tb{cs.x + cs.w - 20 * s - tw2, cs.y + 20 * s, tw2, 31 * s};
    if (u.hover(tb)) gfx_->fill_round(tb, 8 * s, {255, 255, 255, 18});
    T(600, 13.6f, tb.x + 16 * s, tb.y, 31, tl, th.text);
    if (u.clicked(tb) && r.contains(u.in.mx, u.in.my)) { dm_manage_ = !dm_manage_; mark(); }
    struct Tile { const char* label; std::string value; };
    const Tile tiles[4] = {{"SONGS", std::to_string(cached_songs)}, {"ALBUMS", std::to_string(albums.size())}, {"YOUR CACHE", fmt_bytes(cache_bytes)}, {"TOTAL CACHE", cached_songs ? fmt_bytes(cache_bytes) : "N/A"}};
    for (int i = 0; i < 4; ++i) {
        Rect t{cs.x + 20 * s + (i % 2) * (tile_w + 8 * s), cs.y + 63 * s + (i / 2) * (tile_h + 8 * s), tile_w, tile_h};
        card(t, 8, th.panel);
        gfx_->set_letter_spacing(0.5f * s);
        T(600, 11.5f, t.x + 12 * s, t.y + 10 * s, 17, tiles[i].label, rgb(0xdbdbdb));
        gfx_->set_letter_spacing(0);
        T(700, 16.8f, t.x + 12 * s, t.y + 29 * s, 25, tiles[i].value, th.accent);
    }
    if (dm_manage_) {
        const float T0 = cs.y + 200 * s;                       // tiles bottom
        const float mx = cs.x + 20 * s, mw = cs.w - 40 * s;
        const Color bd{255, 255, 255, 14};
        gfx_->fill({mx, T0 + 20 * s, mw, 2 * s}, bd);          // .cache-manager border-top
        // Cache Location
        Rect ls{mx, T0 + 40 * s, mw, loc_h * s};
        card(ls, 10, th.bg_medium); gfx_->stroke_round(ls, 10 * s, 2 * s, bd);
        T(600, 16, ls.x + 16 * s, ls.y + 14 * s, 24, "Cache Location", th.text);
        const std::string chg = "Change Location";
        const float cbw = gfx_->measure(F(600, 15.2f), chg) + 64 * s;
        Rect cb{ls.x + ls.w - 16 * s - cbw, ls.y + 52 * s, cbw, 44 * s};
        Rect lp{ls.x + 16 * s, ls.y + 52 * s, cb.x - 12 * s - (ls.x + 16 * s), 44 * s};
        card(lp, 8, th.bg_light); gfx_->stroke_round(lp, 8 * s, 2 * s, bd);
        fa(skin::Folder, lp.x + 16 * s + 7 * s, lp.y + 22 * s, 13, th.sub);
        T(400, 13.6f, lp.x + 40 * s, lp.y, 44, perm_dir_, th.sub, lp.w - 56 * s);
        if (u.hover(cb)) gfx_->fill_round(cb, 10 * s, {255, 255, 255, 18}); else gfx_->fill_round(cb, 10 * s, {0, 0, 0, 36});
        fa(0xf07c, cb.x + 20 * s + 7 * s, cb.y + 22 * s, 14, rgb(0xffffff));
        T(600, 15.2f, cb.x + 44 * s, cb.y, 44, chg, rgb(0xffffff));
        if (u.clicked(cb) && r.contains(u.in.mx, u.in.my)) choose_cache_folder(0);
        if (have_disk) {
            const uint64_t avail = static_cast<uint64_t>(sv.f_bavail) * sv.f_frsize, totald = static_cast<uint64_t>(sv.f_blocks) * sv.f_frsize;
            const float ly = ls.y + 52 * s + 44 * s + 10 * s;
            fa(0xf0a0, ls.x + 16 * s + 7 * s, ly + 9 * s, 12, th.accent);
            T(600, 13.1f, ls.x + 36 * s, ly, 18, fmt_bytes(avail) + " free", th.accent);
            const std::string tt = fmt_bytes(totald) + " total";
            T(400, 13.1f, ls.x + ls.w - 16 * s - gfx_->measure(F(400, 13.1f), tt), ly, 18, tt, th.sub);
            Rect db{ls.x + 16 * s, ly + 24 * s, ls.w - 32 * s, 6 * s};
            gfx_->fill_round(db, 3 * s, {255, 255, 255, 61});
            const float used = totald ? 1.0f - static_cast<float>(avail) / totald : 0;
            gfx_->fill_round({db.x, db.y, std::max(6 * s, db.w * used), db.h}, 3 * s, th.accent);
        }
        // Cached Albums
        float ay = ls.y + ls.h + 24 * s;
        T(600, 16, mx, ay, 24, "Cached Albums", th.text);
        Rect ll{mx, ay + 40 * s, mw, list_h * s};
        gfx_->clip(&ll);
        float iy = ll.y + 2 * s;
        for (const auto& al : albums) {
            Rect ar{ll.x + 2 * s, iy, ll.w - 4 * s, 79 * s};
            card(ar, 10, th.bg_medium);
            const bool pend = dm_pending_album_ == al.first;
            const float dw = (pend ? 190.0f : 108.0f) * s;
            const float info_r = ar.x + ar.w - 14 * s - dw;
            const std::string nm = al.second.first;
            T(600, 15, ar.x + 14 * s + (info_r - ar.x - 14 * s - gfx_->measure(F(600, 15), nm)) / 2, ar.y + 14 * s, 22, nm, th.text, info_r - ar.x - 14 * s);
            T(400, 13, ar.x + 14 * s, ar.y + 40 * s, 19, al.second.second + " (" + std::to_string(album_count[al.first]) + " songs)", th.sub, info_r - ar.x - 14 * s);
            if (pend) {
                Rect no{ar.x + ar.w - 14 * s - 85 * s, ar.y + 19 * s, 85 * s, 41 * s}, yes{no.x - 8 * s - 97 * s, no.y, 97 * s, 41 * s};
                if (btn(yes, "Delete", rgb(0xdc3545))) { downloads_->remove_album(al.first); dm_pending_album_.clear(); mark(); gfx_->clip(nullptr); gfx_->clip(&r); return; }
                if (btn(no, "Cancel", rgb(0xffffff))) { dm_pending_album_.clear(); mark(); }
            } else {
                Rect del{ar.x + ar.w - 14 * s - 108 * s, ar.y + 17 * s, 108 * s, 45 * s};
                const bool dh = u.hover(del);
                gfx_->fill_round(del, 8 * s, dh ? rgb(0xdc3545) : th.bg_medium);
                if (dh) gfx_->stroke_round(del, 8 * s, 2 * s, rgb(0xdc3545));
                fa(0xf1f8, del.x + 18 * s + 7 * s, del.y + 22.5f * s, 16, dh ? rgb(0xffffff) : rgb(0xdc3545));
                T(600, 17.6f, del.x + 44 * s, del.y, 45, "Delete", dh ? rgb(0xffffff) : rgb(0xdc3545));
                if (u.clicked(del) && ll.contains(u.in.mx, u.in.my) && r.contains(u.in.mx, u.in.my)) { dm_pending_album_ = al.first; mark(); }
            }
            iy += 89 * s;
        }
        if (albums.empty()) {
            card(ll, 10, th.bg_medium); gfx_->stroke_round(ll, 10 * s, 2 * s, {255, 255, 255, 40});
            const std::string m2 = "No albums cached";
            T(400, 14.4f, ll.x + (ll.w - gfx_->measure(F(400, 14.4f, true), m2)) / 2, ll.y, list_h, m2, th.sub, ll.w, true);
        }
        gfx_->clip(&r);
        // Cache Integrity
        Rect vc{mx, ll.y + ll.h + 20 * s, mw, ver_h * s};
        card(vc, 10, th.bg_medium); gfx_->stroke_round(vc, 10 * s, 2 * s, bd);
        T(600, 16, vc.x + 16 * s, vc.y + 14 * s, 24, "Cache Integrity", th.text);
        const std::string vl = dm_verifying_ ? "Verifying\xE2\x80\xA6" : "Verify Cache";
        const float vw = gfx_->measure(F(600, 15.2f), vl) + 60 * s;
        Rect vb{vc.x + 16 * s, vc.y + 52 * s, vw, 41 * s};
        const bool can_verify = !dm_verifying_ && cached_songs > 0;
        if (btn(vb, vl, rgb(0xffffff), can_verify, 15.2f, 44 * s)) {
            dm_verifying_ = true; dm_verify_before_ = cached_songs; dm_verify_t0_ = std::chrono::steady_clock::now();
            downloads_->start_verify();   // stats the files on worker threads; the page keeps drawing
            mark();
        }
        if (dm_verifying_) {
            if (downloads_->verify_running()) mark();
            else {
                const int dropped = downloads_->verify_dropped();
                dm_verified_ = std::max(0, dm_verify_before_ - dropped); dm_removed_ = dropped; dm_verified_done_ = true; dm_verifying_ = false;
                dm_verify_s_ = std::chrono::duration<double>(std::chrono::steady_clock::now() - dm_verify_t0_).count(); mark();
            }
        }
        fa(0xf3ed, vb.x + 20 * s + 6 * s, vb.y + 20.5f * s, 14, can_verify ? rgb(0xffffff) : alpha(rgb(0xffffff), 0.4f));
        if (dm_verified_done_) {
            std::string vr = std::to_string(dm_verified_) + " songs verified";
            if (dm_removed_ > 0) vr += ", " + std::to_string(dm_removed_) + " orphaned " + (dm_removed_ == 1 ? "entry" : "entries") + " removed";
            char dur[32] = {}; std::snprintf(dur, sizeof dur, " (%.1fs)", dm_verify_s_);
            fa(0xf058, vc.x + 16 * s + 7 * s, vc.y + 109 * s, 14, th.accent);
            T(400, 14, vc.x + 16 * s + 22 * s, vc.y + 99 * s, 20, vr + dur, th.accent, vc.w - 60 * s);
        }
        // Clear All Cache
        const float cy2 = vc.y + vc.h + 20 * s;
        if (dm_confirm_clear_) {
            const std::string q = "Delete all cached songs?";
            const float qw = gfx_->measure(F(600, 14), q);
            T(600, 14, mx, cy2, 41, q, th.sub);
            const float dw2 = gfx_->measure(F(600, 15.2f), "Delete all") + 40 * s;
            if (btn({mx + qw + 8 * s, cy2, dw2, 41 * s}, "Delete all", rgb(0xdc3545))) { downloads_->clear_all(); dm_cleared_.clear(); dm_confirm_clear_ = false; mark(); gfx_->clip(nullptr); return; }
            if (btn({mx + qw + 16 * s + dw2, cy2, 85 * s, 41 * s}, "Cancel", rgb(0xffffff))) { dm_confirm_clear_ = false; mark(); }
        } else {
            const float cw = gfx_->measure(F(600, 15.2f), "Clear All Cache") + 60 * s;
            if (btn({mx, cy2, cw, 41 * s}, "Clear All Cache", rgb(0xdc3545), cached_songs > 0, 15.2f, 44 * s)) { dm_confirm_clear_ = true; mark(); }
            fa(0xf51a, mx + 20 * s + 7 * s, cy2 + 20.5f * s, 14, cached_songs > 0 ? rgb(0xdc3545) : alpha(rgb(0xdc3545), 0.4f));
        }
    }
    gfx_->clip(nullptr);
}
