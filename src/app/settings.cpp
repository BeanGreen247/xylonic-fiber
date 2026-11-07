// Settings page (SettingsView.tsx + sections), the Accent Theme dialog and the Sleep Timer picker.
// Measured at 1024x768: content column 680 wide, cards radius 16 with a 1px rgba(255,255,255,.06) border,
// rows 52/61/71 high, section titles 12px 600 accent uppercase.
#include <algorithm>
#include <cmath>
#include <ctime>
#include <set>
#include "log.h"
#include "app.h"
#include "log.h"
#include "secrets.h"
#include "android_bridge.h"

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
    char b[24] = {};
    if (rem >= 3600) std::snprintf(b, sizeof b, "%d:%02d:%02d", rem / 3600, (rem % 3600) / 60, rem % 60);
    else std::snprintf(b, sizeof b, "%d:%02d", rem / 60, rem % 60);
    return b;
}

void App::tick_sleep() {
    if (sleep_end_ms_ && SDL_GetTicks64() >= sleep_end_ms_) { sleep_end_ms_ = 0; if (player_.playing()) player_.pause(); mark(); }
}

// ---------------------------------------------------------------- modal helpers

void App::refresh_perf_stats() {
    PerfStats p;
    p.loaded = true;
    if (images_) { const auto st = images_->stats(); p.images = images_->disk_files(); p.image_bytes = images_->disk_bytes(); p.mem_hits = st.mem_hits; p.disk_hits = st.disk_hits; p.server_fetches = st.server_fetches; p.internet_fetches = st.internet_fetches; }
    p.api_calls = SubsonicClient::api_calls();
    if (store_) { const Store::Stats st = store_->stats(); p.artists = st.artists; p.albums = st.albums; p.songs = st.songs; p.index_bytes = st.index_bytes; }
    perf_stats_ = p;
}

void App::draw_modal(float W, float H) {
    if (modal_ == Modal::None) return;
    Ui& u = *ui_;
    const float s = S_;
    const Theme& th = u.th;
    gfx_->fill({0, 0, W, H}, {0, 0, 0, 170});  // the old modal blurs the page; a dark scrim keeps the cost at zero
    auto frame = [&](float w, float h, uint32_t icon, const char* title) {
        w = std::min(w, W / s - 24);   // dialogs never wider than the screen (the G88 is 320 logical px wide)
        h = std::min(h, H / s - 24);
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
    if (modal_ == Modal::MoveCache || modal_ == Modal::StoragePick) { draw_storage_modal(W, H); return; }
    if (modal_ == Modal::Firewall) { draw_firewall_modal(W, H); return; }
    if (modal_ == Modal::Theme) { draw_theme_modal(W, H); return; }
    if (modal_ == Modal::ThemeEdit) { draw_theme_editor(W, H); return; }
    if (modal_ == Modal::AppIcon) { draw_app_icon_modal(W, H); return; }
    // wrapped paragraphs: height first (so the dialog fits its text on a narrow screen), then drawing
    auto para_h = [&](float w, const std::vector<std::string>& ls) {
        const float inner = (std::min(w, W / s - 24) - 40) * s;
        float h = 0;
        for (const auto& l : ls) h += (l.empty() ? 1 : wrap_count(400, 13, l, inner)) * 22.0f;
        return h;
    };
    auto para_draw = [&](const Rect& b, float y0, const std::vector<std::string>& ls) {
        float y = b.y + y0 * s;
        for (const auto& l : ls) { if (l.empty()) { y += 22 * s; continue; } y += TW(400, 13, b.x + 20 * s, y, 20, l, th.sub, b.w - 40 * s) + 2 * s; }
    };
    // Cancel + primary action side by side, sharing the width when it is small
    auto buttons = [&](const Rect& b, float go_w) {
        const float avail = b.w - 40 * s - 10 * s;
        float gw = std::min(go_w * s, avail * 0.62f);
        return std::make_pair(Rect{b.x + 20 * s, b.y + b.h - 56 * s, avail - gw, 36 * s}, Rect{b.x + b.w - 20 * s - gw, b.y + b.h - 56 * s, gw, 36 * s});
    };
    if (modal_ == Modal::ConfirmWipe) {
        const std::vector<std::string> ls{"This removes downloaded songs, cover art, credentials and all settings.", "It cannot be undone."};
        Rect b = frame(420, 64 + para_h(420, ls) + 70, skin::Warning, "Clear all app data?");
        para_draw(b, 64, ls);
        auto [cancel_r, go] = buttons(b, 190);
        if (u.button(cancel_r, "Cancel")) modal_ = Modal::None;
        gfx_->fill_round(go, 8 * s, u.hover(go) ? alpha(th.danger, 0.9f) : th.danger);
        T(600, 14, go.x + (go.w - gfx_->measure(F(600, 14), "Clear everything")) / 2, go.y, 36, "Clear everything", rgb(0xffffff));
        if (u.clicked(go)) { wipe_all_data(); modal_ = Modal::None; }
    } else if (modal_ == Modal::TechStack) {
        Rect b = frame(520, 330, skin::BookOpen, "Tech Stack");
        static const char* lines[] = {"Native C++17 application", "SDL2 window, input and rendering", "Custom audio pipeline with dr_mp3", "Own HTTP client with mbedTLS", "yyjson metadata parsing", "Roboto and Font Awesome subset"};
        for (int i = 0; i < 6; ++i) { fa(skin::Check, b.x + 28 * s, b.y + (70 + i * 32) * s, 11, th.accent); T(400, 13, b.x + 48 * s, b.y + (60 + i * 32) * s, 24, lines[i], th.text); }
        if (u.button({b.x + b.w - 130 * s, b.y + b.h - 48 * s, 110 * s, 32 * s}, "Close")) modal_ = Modal::None;
    } else if (modal_ == Modal::Licenses) {
        Rect b = frame(560, 500, 0xf24e, "Licenses");
        T(400, 13, b.x + 20 * s, b.y + 64 * s, 20, "Xylonic Fiber and its native dependencies use these licenses:", th.sub, b.w - 40 * s);
        static const char* lines[] = {"Xylonic Fiber — MIT", "SDL2 — zlib", "mbedTLS — Apache-2.0", "yyjson — MIT", "dr_mp3, dr_flac, dr_wav — public domain / MIT-0", "libopus — BSD-3-Clause", "libvorbis, libogg — BSD-3-Clause", "FDK AAC (Fraunhofer) — FDK AAC license", "Apple ALAC decoder — Apache-2.0", "Roboto — Apache-2.0", "Font Awesome Free — OFL-1.1 / CC BY 4.0"};
        for (int i = 0; i < 12; ++i) { T(400, 13, b.x + 28 * s, b.y + (94 + i * 27) * s, 20, lines[i], th.text); }
        T(400, 11, b.x + 20 * s, b.y + 444 * s, 18, "Full attribution is included with the distributed assets.", th.sub);
        if (u.button({b.x + b.w - 130 * s, b.y + b.h - 48 * s, 110 * s, 32 * s}, "Close")) modal_ = Modal::None;
    } else if (modal_ == Modal::ConfirmCarMode) {   // window.confirm('Start Car Mode?...')
        const std::vector<std::string> ls{"Car Mode opens a distraction-free widescreen player with large driving-friendly controls."};
        Rect b = frame(460, 62 + para_h(460, ls) + 70, skin::Car, "Start Car Mode?");
        para_draw(b, 62, ls);
        auto [cancel_r, go] = buttons(b, 190);
        if (u.button(cancel_r, "Cancel")) modal_ = Modal::None;
        gfx_->fill_round(go, 8 * s, u.hover(go) ? alpha(th.accent, 0.9f) : th.accent);
        T(600, 14, go.x + (go.w - gfx_->measure(F(600, 14), "Start Car Mode")) / 2, go.y, 36, "Start Car Mode", rgb(0x121212));
        if (u.clicked(go)) { modal_ = Modal::None; set_car_mode(true); }
    } else if (modal_ == Modal::ConfirmDeletePlaylist) {
        Rect b = frame(380, 170, skin::Warning, "Delete this playlist?");
        if (u.button({b.x + 20 * s, b.y + b.h - 56 * s, 150 * s, 36 * s}, "Cancel")) { modal_ = Modal::None; pl_delete_pending_.clear(); }
        Rect go{b.x + b.w - 20 * s - 150 * s, b.y + b.h - 56 * s, 150 * s, 36 * s};
        gfx_->fill_round(go, 8 * s, u.hover(go) ? alpha(th.danger, 0.9f) : th.danger);
        T(600, 14, go.x + (go.w - gfx_->measure(F(600, 14), "Delete")) / 2, go.y, 36, "Delete", rgb(0xffffff));
        if (u.clicked(go)) { const std::string id = pl_delete_pending_; modal_ = Modal::None; pl_delete_pending_.clear(); pl_delete(id); }
    } else if (modal_ == Modal::ConfirmSignOut) {
        const std::vector<std::string> lines = {
            "You will be signed out and the saved password forgotten.",
            "Your downloaded songs and library stay on this device.",
            "Sign in to the same server again, even from another address (another house, a VPN), and they are reused."};
        Rect b = frame(460, 62 + para_h(460, lines) + 70, 0xf2f5, "Sign out?");
        para_draw(b, 62, lines);
        auto [cancel_r, go] = buttons(b, 150);
        if (u.button(cancel_r, "Cancel")) modal_ = Modal::None;
        gfx_->fill_round(go, 8 * s, u.hover(go) ? alpha(th.danger, 0.9f) : th.danger);
        T(600, 14, go.x + (go.w - gfx_->measure(F(600, 14), "Sign out")) / 2, go.y, 36, "Sign out", rgb(0xffffff));
        if (u.clicked(go)) { modal_ = Modal::None; logout(); }
    } else if (modal_ == Modal::SwitchServer) {
        const float ih = 62.0f;
        Rect lst = qp_frame(W, H, (static_cast<float>(cfg_.conns.size()) + 1) * ih + 8, "Switch Server", 0xf362, "Select a saved connection to switch to.");
        for (size_t i = 0; i < cfg_.conns.size() && modal_ == Modal::SwitchServer; ++i) {
            const Config::Conn& cn = cfg_.conns[i];
            Rect ir{lst.x, lst.y + i * ih * s, lst.w, ih * s};
            if (i) gfx_->fill({ir.x, ir.y, ir.w, 1 * s}, {255, 255, 255, 15});
            if (u.hover(ir)) gfx_->fill(ir, {255, 255, 255, 31});
            Rect ico{ir.x + 18 * s, ir.y + (ir.h - 36 * s) / 2, 36 * s, 36 * s};
            gfx_->fill_round(ico, 10 * s, alpha(th.accent, 0.16f));
            fa(0xf233, ico.x + 18 * s, ico.y + 18 * s, 15, th.accent);
            std::string host = cn.url; const size_t sc = host.find("://"); if (sc != std::string::npos) host = host.substr(sc + 3);
            T(600, 15, ico.x + 50 * s, ir.y + 11 * s, 22, host, th.text, ir.w - 100 * s);
            T(400, 12, ico.x + 50 * s, ir.y + 35 * s, 17, cn.user + " \xC2\xB7 " + cn.url, th.sub, ir.w - 100 * s);
            if (u.clicked(ir)) { modal_ = Modal::None; switch_to(static_cast<int>(i), ""); }
        }
        if (modal_ == Modal::SwitchServer) {   // last entry: sign in to a server that is not in the list yet
            const size_t n = cfg_.conns.size();
            Rect ir{lst.x, lst.y + n * ih * s, lst.w, ih * s};
            if (n) gfx_->fill({ir.x, ir.y, ir.w, 1 * s}, {255, 255, 255, 15});
            if (u.hover(ir)) gfx_->fill(ir, {255, 255, 255, 31});
            Rect ico{ir.x + 18 * s, ir.y + (ir.h - 36 * s) / 2, 36 * s, 36 * s};
            gfx_->fill_round(ico, 10 * s, alpha(th.accent, 0.16f));
            fa(skin::CirclePlus, ico.x + 18 * s, ico.y + 18 * s, 15, th.accent);
            T(600, 15, ico.x + 50 * s, ir.y + 11 * s, 22, "Add server", th.text, ir.w - 100 * s);
            T(400, 12, ico.x + 50 * s, ir.y + 35 * s, 17, "Sign in to another server or address", th.sub, ir.w - 100 * s);
            if (u.clicked(ir)) { modal_ = Modal::None; add_server(); }
        }
    } else if (modal_ == Modal::SwitchPass) {
        const bool have = switch_idx_ >= 0 && switch_idx_ < static_cast<int>(cfg_.conns.size());
        std::string host = have ? cfg_.conns[static_cast<size_t>(switch_idx_)].url : "";
        const size_t sc = host.find("://"); if (sc != std::string::npos) host = host.substr(sc + 3);
        Rect c = qp_frame(W, H, 170, "Enter Password", skin::Lock, ("Switching to " + host).c_str());
        Rect pf{c.x + 18 * s, c.y + 8 * s, c.w - 36 * s, 42 * s};
        gfx_->fill_round(pf, 8 * s, th.bg); gfx_->stroke_round(pf, 8 * s, 1 * s, u.active == kReauth ? th.accent : th.border, th.bg);
        const bool enter = u.text_field(kReauth, pf, switch_pass_, "Password", true, false, 15 * s, F(400, 15)) && u.in.enter;
        if (!switch_err_.empty()) T(400, 12, c.x + 18 * s, pf.y + 52 * s, 18, switch_err_, th.danger, c.w - 36 * s);
        Rect go{c.x + 18 * s, pf.y + 76 * s, c.w - 36 * s, 42 * s};
        const bool ready = !switching_ && !switch_pass_.empty();
        gfx_->fill_round(go, 8 * s, ready ? (u.hover(go) ? alpha(th.accent, 0.88f) : th.accent) : alpha(th.accent, 0.35f));
        const char* gl = switching_ ? "Connecting\xE2\x80\xA6" : "Connect";
        T(700, 14, go.x + (go.w - gfx_->measure(F(700, 14), gl)) / 2, go.y, 42, gl, th.bg);
        if (ready && (u.clicked(go) || enter)) switch_to(switch_idx_, switch_pass_);
    } else if (modal_ == Modal::ConfirmRebuild || modal_ == Modal::ConfirmClearCaches) {
        const bool rebuild = modal_ == Modal::ConfirmRebuild;
        const std::vector<std::string> lines = rebuild
            ? std::vector<std::string>{"This will clear the image and search caches and re-run the initial precaching process on reload.", "Downloaded audio files will not be affected."}
            : std::vector<std::string>{"This will:", "\xE2\x80\xA2 Delete all cached album artwork", "\xE2\x80\xA2 Delete all offline cache data", "\xE2\x80\xA2 Reset all precache flags", "", "The cache will rebuild automatically on next launch."};
        Rect b = frame(460, 62 + para_h(460, lines) + 70, rebuild ? skin::Sync : skin::Warning, rebuild ? "Rebuild Cache?" : "Clear All Caches?");
        para_draw(b, 62, lines);
        auto [cancel_r, go] = buttons(b, 190);
        if (u.button(cancel_r, "Cancel")) modal_ = Modal::None;
        const Color gc = rebuild ? th.accent : th.danger;
        gfx_->fill_round(go, 8 * s, u.hover(go) ? alpha(gc, 0.9f) : gc);
        const char* gl = rebuild ? "Rebuild Cache" : "Clear All Caches";
        T(600, 14, go.x + (go.w - gfx_->measure(F(600, 14), gl)) / 2, go.y, 36, gl, rgb(rebuild ? 0x121212 : 0xffffff));
        if (u.clicked(go)) {
            modal_ = Modal::None;
            if (rebuild) { images_->clear_memory(); images_->clear_disk(); if (store_) { store_->clear_library(); store_->erase_prefix("pre:"); } }
            else {   // offlineCacheService.clearAllCache + imageCacheService.clearAllCacheAndReset + search cache + flags
                if (downloads_) downloads_->clear_all();
                images_->clear_memory(); images_->clear_disk();
                if (store_) { store_->clear_library(); store_->erase_prefix("pre:"); store_->erase_prefix("home:"); }
                album_cache_.clear(); artist_cache_.clear();
            }
            start_cache_preload(false);   // the original reloads, which shows the first-run build again
        }
    } else if (modal_ == Modal::DownloadQuality) { draw_download_picker(W, H);
    } else if (modal_ == Modal::Reauth) {
        Rect b = frame(440, 280, skin::Lock, "Sign in again");
        T(400, 13, b.x + 20 * s, b.y + 56 * s, 20, "The server rejected the saved password for " + cfg_.username + ".", th.sub, b.w - 40 * s);
        Rect pf{b.x + 20 * s, b.y + 108 * s, b.w - 40 * s, 42 * s};
        gfx_->fill_round(pf, 8 * s, th.bg); gfx_->stroke_round(pf, 8 * s, 1 * s, u.active == kReauth ? th.accent : th.border, th.bg);
        u.text_field(kReauth, pf, reauth_pass_, "Password", true, false, 15 * s, F(400, 15));
        if (!reauth_msg_.empty()) T(400, 12, b.x + 20 * s, b.y + 160 * s, 18, reauth_msg_, th.danger, b.w - 40 * s);
        if (u.button({b.x + 20 * s, b.y + b.h - 52 * s, 150 * s, 36 * s}, "Sign out")) { modal_ = Modal::None; logout(); }
        Rect go{b.x + b.w - 20 * s - 150 * s, b.y + b.h - 52 * s, 150 * s, 36 * s};
        gfx_->fill_round(go, 8 * s, u.hover(go) ? alpha(th.accent, 0.85f) : th.accent);
        T(600, 14, go.x + (go.w - gfx_->measure(F(600, 14), reauth_busy_ ? "Checking..." : "Sign in")) / 2, go.y, 36, reauth_busy_ ? "Checking..." : "Sign in", rgb(0x121212));
        if (u.clicked(go) && !reauth_busy_ && !reauth_pass_.empty() && client_) {
            reauth_busy_ = true; reauth_msg_.clear();
            auto fresh = std::make_shared<SubsonicClient>(cfg_.server_url, cfg_.username, reauth_pass_, copt_);
            worker_->post([this, fresh, old = client_, s = session_, pass = reauth_pass_] {
                std::string err; const bool ok = fresh->ping(err);
                ui_post([this, ok, err, old, s, pass] {
                    if (s != session_) return;
                    reauth_busy_ = false;
                    if (!ok) { reauth_msg_ = err.empty() ? "Sign in failed" : err; mark(); return; }
                    old->set_password(pass);    // same client object: queued downloads and workers pick it up
                    store_secret_async(cfg_.server_url + "|" + cfg_.username, pass);
                    reauth_pass_.clear(); modal_ = Modal::None; load_home(); mark();
                });
            });
        }
    } else if (modal_ == Modal::Remote) { draw_remote_picker(W, H);    } else if (modal_ == Modal::Remote) { draw_remote_picker(W, H);

    } else if (modal_ == Modal::Sleep) {
        Rect b = frame(420, 336, 0xf186, "Sleep Timer");
        {
            const std::string full = "Playback stops automatically after the selected time.";
            T(400, 12, b.x + 18 * s, b.y + 52 * s, 18, gfx_->measure(F(400, 12), full) > b.w - 36 * s ? "Playback stops after the selected time." : full, th.sub, b.w - 36 * s);
        }
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
    if (pop.y + pop.h > dd_bottom_ && anchor.y - 4 * s - pop.h >= dd_top_) pop.y = anchor.y - 4 * s - pop.h;   // opens upward near the bottom of the page
    else if (pop.y + pop.h > dd_bottom_) pop.y = std::max(dd_top_, dd_bottom_ - pop.h);
    dd_block_ = pop;
    dropdown_draw_ = [this, pop, opts, current, ih, id]() mutable {
        Ui& uu = *ui_;
        const Theme& t = uu.th;
        gfx_->fill_round({pop.x - 1, pop.y - 1, pop.w + 2, pop.h + 2}, 9 * S_, t.border);
        gfx_->fill_round(pop, 8 * S_, t.bg_light);
        for (size_t i = 0; i < opts.size(); ++i) {
            Rect r{pop.x + 4 * S_, pop.y + 4 * S_ + i * ih, pop.w - 8 * S_, ih};
            if (uu.hover(r)) gfx_->fill_round(r, 6 * S_, t.hover);
            T(static_cast<int>(i) == current ? 600 : 400, 13, r.x + 10 * S_, r.y, ih / S_, opts[i], static_cast<int>(i) == current ? t.accent : t.text);
            if (uu.clicked(r)) { dropdown_choice_ = {id, static_cast<int>(i)}; mark(); }
        }
        if (uu.in.pressed && !pop.contains(uu.in.mx, uu.in.my)) { dropdown_choice_ = {id, -2}; mark(); }
    };
    return -1;
}

void App::draw_settings(Rect r) {
    Ui& u = *ui_;
    const float s = S_;
    const Theme& th = u.th;
    const bool cp = mobile_;   // compact: 12 px side padding, full width, 24 px title
    // desktop: the column grows with the window (680 at 1024 wide, up to 980) and sits in the middle of the page
    const float w = cp ? r.w - 24 * s : std::min(std::min(980.0f, std::max(680.0f, r.w / s * 0.6f)) * s, r.w - 48 * s - 12 * s);
    const float x = cp ? r.x + 12 * s : r.x + std::max(24 * s, (r.w - 12 * s - w) / 2);
    float total = 3260 * s;
#if defined(XF_APP_STORE)
    total += 222 * s;
#endif
#if defined(__ANDROID__) || defined(__IPHONEOS__)
    total += 183 * s;
#endif
#if defined(__ANDROID__)
    total += 62 * s;
#endif
    for (float e : card_extra_prev_) total += e * s;
    if (u.hover(r)) settings_scroll_.y -= u.in.wheel * 60 * s;
    if (settings_total_ > 0) total = settings_total_;   // measured last frame; the estimate only covers the first one
    settings_scroll_.clamp(total, r.h);
    float y = r.y + (cp ? 12.0f : 24.0f) * s - settings_scroll_.y;
    gfx_->clip(&r);
    dropdown_draw_ = nullptr;
    dd_top_ = r.y; dd_bottom_ = r.y + r.h;
    const Input in_saved = u.in;
    if (dd_block_.w > 0 && dd_block_.contains(u.in.mx, u.in.my)) { u.in.pressed = u.in.released = u.in.rclick = false; u.in.down = false; }
    dd_block_ = {0, 0, 0, 0};
    // pending dropdown selection from the previous frame's popup
    int pick_id = dropdown_choice_.first, pick = dropdown_choice_.second;
    dropdown_choice_ = {0, -1};
    if (pick_id && pick == -2) { dropdown_open_ = 0; }
    // header
    if (cp) {
        fa(skin::Gear, x + 13 * s, y + 18 * s, 24, th.accent);
        T(600, 24, x + 42 * s, y, 36, "Settings", th.text);
        y += 52 * s;
    } else {
        fa(skin::Gear, x + 20 * s, y + 24 * s, 32, th.accent);
        gfx_->set_letter_spacing(-0.32f * s);
        T(700, 32, x + 52 * s, y, 48, "Settings", th.text);
        gfx_->set_letter_spacing(0);
        y += 80 * s;
    }

    auto section = [&](const char* title) {
        gfx_->set_letter_spacing(0.72f * s);
        T(600, 12, x, y, 18, title, th.accent);
        gfx_->set_letter_spacing(0);
        y += 26 * s;
    };
    int row_i = 0;
    const Color* icon_ov = nullptr;   // icon colour override for the row being drawn
    float reserve_ov = -1;   // >= 0: right-hand space (logical px) the next row keeps free for its own controls
    float last_extra_ = 0;   // extra height (logical px) the last row gained from wrapping: dropdowns centre on it
    // Row primitive. kind: 0 info, 1 button, 2 danger. Returns true when clicked (kind != info).
    auto row = [&](uint32_t icon, const std::string& title, const std::string& sub, int kind, bool chevron, const std::string& pill, bool pill_on, float h_logical) {
        if (row_i++) { gfx_->fill({x + 17 * s, y, w - 34 * s, 1 * s}, {255, 255, 255, 15}); y += 1 * s; }
        int sub_lines = 1;
        const bool dd_row = cp && kind == 0 && pill.empty() && !chevron && !sub.empty();   // a row with a dropdown on its right: narrower text column
        float reserve = cp ? (dd_row ? 160.0f : pill.empty() ? (chevron ? 46.0f : 30.0f) : 90.0f) : 140.0f;
        if (reserve_ov >= 0) reserve = reserve_ov;
        if (cp && !pill.empty()) reserve = std::max(reserve, (gfx_->measure(F(600, 12), pill) / s + 24 + (chevron ? 30.0f : 0.0f) + 32));   // the description stops before the pill
        last_extra_ = 0;
        int title_lines = 1;
        if (cp && !sub.empty()) {   // a long title wraps to a second line rather than truncating
            title_lines = std::min(2, wrap_count(400, 15, title, w - 2 * s - 54 * s - (dd_row ? 140.0f : reserve) * s));
            if (title_lines > 1) { h_logical += 22.5f; card_extra_now_[cur_card_] += 22.5f; last_extra_ += 22.5f; }
        }
        if (cp && !sub.empty()) {   // compact: descriptions wrap instead of truncating; the row (and its card) grow
            sub_lines = wrap_count(400, 12, sub, w - 2 * s - 54 * s - (dd_row ? 24.0f : reserve) * s);
            if (sub_lines > 1) { h_logical += (sub_lines - 1) * 18; card_extra_now_[cur_card_] += (sub_lines - 1) * 18; last_extra_ += (sub_lines - 1) * 18; }
        }
        Rect rr{x + 1 * s, y, w - 2 * s, h_logical * s};
        if (rr.y + rr.h < r.y - 2 * s || rr.y > r.y + r.h + 2 * s) { y += h_logical * s; return false; }   // off screen: height is known, nothing to draw or click
        bool hov = kind != 0 && u.hover(rr) && r.contains(u.in.mx, u.in.my);
        if (hov) gfx_->fill(rr, th.hover);
        Color ic = kind == 2 ? th.danger : icon_ov ? *icon_ov : th.sub;
        bool has_sub = !sub.empty();
        fa(icon, rr.x + 28 * s, rr.y + (has_sub ? std::min(rr.h / 2, 30.5f * s) : rr.h / 2), 18, ic);   // beside the title block, not the middle of a tall wrapped row
        Color tc = kind == 2 ? th.danger : th.text;
        if (has_sub) {
            const float tw = rr.w - 54 * s - reserve * s, sw = dd_row ? rr.w - 54 * s - 24 * s : tw;
            const float sy = rr.y + (33 + (title_lines - 1) * 22.5f) * s;
            if (title_lines > 1) TW(400, 15, rr.x + 54 * s, rr.y + 10 * s, 22.5f, title, tc, dd_row ? rr.w - 54 * s - 140 * s : tw);
            else T(400, 15, rr.x + 54 * s, rr.y + 10 * s, 22.5f, title, tc, tw);
            if (sub_lines > 1) TW(400, 12, rr.x + 54 * s, sy, 18, sub, rgb(0xb5b5b5), sw);
            else T(400, 12, rr.x + 54 * s, sy, 18, sub, rgb(0xb5b5b5), sw);
        } else {
            T(400, 15, rr.x + 54 * s, rr.y, h_logical, title, tc, rr.w - 54 * s - ((cp && kind == 0 && pill.empty() && !chevron) ? 160.0f : 100.0f) * s);   // a title-only row with a dropdown on its right must stop before it
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
    // Dropdown anchor of the row just drawn. Compact: on the title line (the description wraps under it at full width); wide: centred.
    auto dd_rect = [&]() { return Rect{x + w - 16 * s - 120 * s, cp ? y - (71 + last_extra_) * s + 9 * s : y - (71 + last_extra_ * 0.5f) * s + 21 * s, 120 * s, 29 * s}; };
    int card_n = 0;
    for (float& e : card_extra_now_) e = 0;
    auto card_fill = [&](float top, float h) {
        cur_card_ = std::min(card_n, 31);
        h += card_extra_prev_[cur_card_] * s;
        ++card_n;
        gfx_->fill_round({x, top, w, h}, 16 * s, {255, 255, 255, 15});
        gfx_->fill_round({x + 1 * s, top + 1 * s, w - 2 * s, h - 2 * s}, 15 * s, th.panel);
    };

#if defined(XF_APP_STORE)
    {   // Thank-you banner (app store build only)
        const float ch = 190 * s;
        gfx_->fill_round({x, y, w, ch}, 16 * s, alpha(th.accent, 0.25f));
        gfx_->fill_round({x + 1 * s, y + 1 * s, w - 2 * s, ch - 2 * s}, 15 * s, th.panel);
        gfx_->fill_round({x + 1 * s, y + 1 * s, w - 2 * s, ch - 2 * s}, 15 * s, alpha(th.accent, 0.05f));
        fa(0xf004, x + w / 2, y + 44 * s, 26, th.accent);
        const std::string t1 = "Thank you for supporting Xylonic!";
        T(600, 16, x + (w - gfx_->measure(F(600, 16), t1)) / 2, y + 66 * s, 24, t1, th.text);
        T(400, 13, x + (w - std::min(380.0f * s, w - 48 * s)) / 2, y + 94 * s, 20, "Your purchase helps keep this project alive and growing. You're awesome \xE2\x80\x94 thank you for being part of this journey.", th.sub, std::min(380.0f * s, w - 48 * s));
        const std::string gl = "View source on GitHub";
        const float gw = gfx_->measure(F(500, 13), gl) + 56 * s;
        Rect gb{x + (w - gw) / 2, y + ch - 52 * s, gw, 34 * s};
        gfx_->fill_round(gb, 17 * s, u.hover(gb) ? alpha(th.text, 0.12f) : alpha(th.text, 0.06f));
        fa(0xf09b, gb.x + 22 * s, gb.y + gb.h / 2, 14, th.sub);
        T(500, 13, gb.x + 40 * s, gb.y, 34, gl, th.sub);
        if (u.clicked(gb) && r.contains(u.in.mx, u.in.my)) SDL_OpenURL("https://github.com/BeanGreen247/xylonic-fiber");
        y += ch + 32 * s;
    }
#endif
    // ---- APPEARANCE
    section("APPEARANCE");
    card_fill(y, 250 * s);
    {
        row(0xf186, "Mode", "Light / dark \xE2\x80\x94 follows your system by default", 0, false, "", false, 71);
        Rect sel = dd_rect();
        int cur = cfg_.theme_mode;
        int p = dropdown(1, sel, {"System", "Light", "Dark"}, cur);
        (void)p;
        if (pick_id == 1 && pick >= 0) { cfg_.theme_mode = pick; apply_theme(); cfg_.save(cfg_path_); dropdown_open_ = 0; }
        if (row(0xf53f, "Accent Theme", "", 1, true, "", false, 52)) modal_ = Modal::Theme;
        row(0xf302, "Icon Style", "GitHub, Phosphor, Material or the classic solid icons", 0, false, "", false, 71);
        Rect isel = dd_rect();
        dropdown(9, isel, {"GitHub", "Classic", "Phosphor", "Material"}, cfg_.icon_set == 1 ? 0 : cfg_.icon_set == 0 ? 1 : cfg_.icon_set);
        if (pick_id == 9 && pick >= 0) { static const int map[4] = {1, 0, 2, 3}; cfg_.icon_set = map[pick]; cfg_.save(cfg_path_); dropdown_open_ = 0; mark(); }
        if (row(0xf302, "App Icon", "", 1, true, kAppIcons[cfg_.app_icon].label, false, 52)) modal_ = Modal::AppIcon;
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
    if (row(skin::Car, "Car Mode", "A high-contrast widescreen player for your dashboard", 1, true, "Launch", true, 61)) modal_ = Modal::ConfirmCarMode;   // asks first, as the original does
    { static const char* m[3] = {"Auto", "Always", "Never"}; if (row(0xf422, "Minimal layout", cfg_.car_minimal == 0 ? "Automatic: no top and bottom bars on very small screens" : cfg_.car_minimal == 1 ? "Always: seek bar on top, art as large as possible" : "Never: always show the full layout with its bars", 1, false, m[cfg_.car_minimal], cfg_.car_minimal != 2, 61)) { cfg_.car_minimal = (cfg_.car_minimal + 1) % 3; cfg_.save(cfg_path_); } }
    if (row(0xf0eb, "Keep screen on", "Stop the screen from dimming, sleeping or locking while Car Mode is open", 1, false, cfg_.car_keep_on ? "On" : "Off", cfg_.car_keep_on, 61)) { cfg_.car_keep_on = !cfg_.car_keep_on; cfg_.save(cfg_path_); }
    if (row(0xf011, "Start in Car Mode", "Open the driving display automatically when the app starts", 1, false, cfg_.car_start ? "On" : "Off", cfg_.car_start, 61)) { cfg_.car_start = !cfg_.car_start; cfg_.save(cfg_path_); }
    if (row(0xf080, "Start in Pulse Deck", "Open the animated spectrum visualizer when the app starts", 1, false, cfg_.car_pulse ? "On" : "Off", cfg_.car_pulse, 61)) { cfg_.car_pulse = !cfg_.car_pulse; cfg_.save(cfg_path_); }
    y += (1 + 32) * s; row_i = 0;
    // ---- ACCOUNT
    section("ACCOUNT");
    card_fill(y, 258 * s);
    row(0xf0c1, "Last.fm", "Scrobbling is handled by your server \xE2\x80\x94 enable Last.fm in your server's admin panel", 0, false, "Server", false, 71);
    if (row(skin::CirclePlus, "Add server", "", 1, true, "", false, 52)) add_server();
    if (row(0xf362, switching_ ? "Switching\xE2\x80\xA6" : "Switch Server", "", 1, true, "", false, 52) && !switching_) modal_ = Modal::SwitchServer;
    if (row(0xf2f5, "Sign out", "Forget the saved password and return to sign-in. Downloads stay; the same server reached another way shares them.", 2, false, "", false, 78)) modal_ = Modal::ConfirmSignOut;
    y += (1 + 32) * s; row_i = 0;
    // ---- PERFORMANCE CACHE (components/common/settings/PerformanceCacheSection.tsx)
    if (!perf_stats_.loaded) refresh_perf_stats();
    {
        gfx_->set_letter_spacing(0.72f * s);
        T(600, 12, x, y, 18, "PERFORMANCE CACHE", th.accent);
        const float tw = gfx_->measure(F(600, 12), "PERFORMANCE CACHE") + 17.0f * 0.72f * s;
        gfx_->set_letter_spacing(0);
        Rect rf{x + tw + 4 * s, y - 2 * s, 22 * s, 22 * s};
        fa(0xf2f1, rf.x + 11 * s, rf.y + 11 * s, 10, alpha(th.accent, u.hover(rf) ? 1.0f : 0.6f));
        if (u.clicked(rf) && r.contains(u.in.mx, u.in.my)) { perf_stats_.loaded = false; refresh_perf_stats(); }
        y += 26 * s;
    }
    {
        const PerfStats& ps = perf_stats_;
        auto grouped = [](uint64_t v) { std::string t = std::to_string(v), o; int n = 0; for (int i = static_cast<int>(t.size()) - 1; i >= 0; --i) { o.insert(o.begin(), t[static_cast<size_t>(i)]); if (++n % 3 == 0 && i > 0) o.insert(o.begin(), ','); } return o; };
        auto bytes_str = [](uint64_t b) { char t[32] = {}; if (b < 1024) std::snprintf(t, sizeof t, "%llu B", static_cast<unsigned long long>(b)); else if (b < 1024 * 1024) std::snprintf(t, sizeof t, "%.1f KB", b / 1024.0); else if (b < 1024ull * 1024 * 1024) std::snprintf(t, sizeof t, "%.1f MB", b / 1048576.0); else std::snprintf(t, sizeof t, "%.2f GB", b / 1073741824.0); return std::string(t); };
        const uint64_t cached = ps.mem_hits + ps.disk_hits, internet = ps.server_fetches + ps.internet_fetches + ps.api_calls, total = cached + internet;
        const int cpct = total == 0 ? 50 : static_cast<int>(std::lround(100.0 * cached / total)), ipct = 100 - cpct;
        const float card_top = y, card_h = 451 * s;
        card_fill(card_top, card_h);
        // overview
        const Color amber{0xf0, 0xa5, 0x00, 255};
        float oy = y + 16 * s;
        fa(0xf058, x + 20 * s + 7 * s, oy + 12 * s, 14, th.accent);
        T(700, 15, x + 20 * s + 22 * s, oy, 24, grouped(cached) + " cached", th.accent);
        const std::string it = grouped(internet) + " internet";
        const float iw = gfx_->measure(F(700, 15), it);
        fa(0xf1eb, x + w - 20 * s - 7 * s, oy + 12 * s, 14, amber);
        T(700, 15, x + w - 20 * s - 22 * s - iw, oy, 24, it, amber);
        oy += 32 * s;
        Rect bar{x + 20 * s, oy, w - 40 * s, 8 * s};
        gfx_->fill_round(bar, 4 * s, {255, 255, 255, 15});
        const float cw = (bar.w - 2 * s) * cpct / 100.0f;
        if (cpct > 0) gfx_->fill_round({bar.x, bar.y, std::max(cw, 8 * s), bar.h}, 4 * s, th.accent);
        if (ipct > 0) gfx_->fill_round({bar.x + cw + 2 * s, bar.y, (bar.w - 2 * s) * ipct / 100.0f, bar.h}, 4 * s, amber);
        oy += 14 * s;
        const std::string lab = total == 0 ? "No requests yet this session" : std::to_string(cpct) + "% served from cache \xC2\xB7 " + std::to_string(ipct) + "% used the network";
        T(400, 11, x + w / 2 - gfx_->measure(F(400, 11), lab) / 2, oy, 17, lab, {255, 255, 255, 153});
        y += 92 * s;
        row_i = 1;   // the divider above the first stat row
        struct Stat { std::string value, label; };
        auto stat_row = [&](uint32_t icon, Color icon_col, const std::string& title, const std::string& sub, std::vector<Stat> stats, const std::string& badge, bool badge_on, bool badge_danger) {
            const float row_top = y + 1 * s;
            const float stat_line = cp ? 22.0f : 0.0f;   // compact: the numbers sit on their own line under the description
            if (cp) card_extra_now_[cur_card_] += stat_line;
            icon_ov = &icon_col;
            reserve_ov = cp ? (badge.empty() ? 30.0f : 120.0f) : -1.0f;
            row(icon, title, sub, 0, false, "", false, 71 + stat_line);
            icon_ov = nullptr; reserve_ov = -1;
            float rx = x + w - 17 * s;
            if (cp) {
                const float top = y - (71 + stat_line + last_extra_) * s;   // row top after the row() advance
                float sx = x + 1 * s + 54 * s;
                for (const Stat& st : stats) {
                    std::string lbl = st.label; for (auto& ch : lbl) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
                    gfx_->set_letter_spacing(0.4f * s);
                    const float lw = gfx_->measure(F(400, 10), lbl);
                    gfx_->set_letter_spacing(0);
                    const float vw = gfx_->measure(F(600, 14), st.value), colw = std::max(lw, vw);
                    T(600, 14, sx, top + (55 + last_extra_) * s, 18, st.value, th.text);
                    gfx_->set_letter_spacing(0.4f * s);
                    T(400, 10, sx, top + (73 + last_extra_) * s, 14, lbl, {255, 255, 255, 153});
                    gfx_->set_letter_spacing(0);
                    sx += colw + 20 * s;
                }
                if (!badge.empty()) {
                    const int fb = F(600, 12); const float bw = gfx_->measure(fb, badge) + 20 * s;
                    Rect br{rx - bw, top + 10 * s, bw, 24 * s};
                    gfx_->fill_round(br, 12 * s, badge_danger ? Color{255, 69, 58, 46} : badge_on ? alpha(th.accent, 0.2f) : Color{0, 0, 0, 0});
                    if (!badge_danger && !badge_on) gfx_->stroke_round(br, 12 * s, 1 * s, {255, 255, 255, 61}, th.panel);
                    T(600, 12, br.x + 10 * s, br.y, 24, badge, badge_danger ? Color{255, 69, 58, 255} : badge_on ? th.accent : th.sub);
                }
                return;
            }
            if (!badge.empty()) {
                const int fb = F(600, 12); const float bw = gfx_->measure(fb, badge) + 20 * s;
                Rect br{rx - bw, row_top + 35.5f * s - 12 * s, bw, 24 * s};
                gfx_->fill_round(br, 12 * s, badge_danger ? Color{255, 69, 58, 46} : badge_on ? alpha(th.accent, 0.2f) : Color{0, 0, 0, 0});
                if (!badge_danger && !badge_on) gfx_->stroke_round(br, 12 * s, 1 * s, {255, 255, 255, 61}, th.panel);
                T(600, 12, br.x + 10 * s, br.y, 24, badge, badge_danger ? Color{255, 69, 58, 255} : badge_on ? th.accent : th.sub);
                rx -= bw + 12 * s;
            }
            for (auto it2 = stats.rbegin(); it2 != stats.rend(); ++it2) {
                std::string lbl = it2->label; for (auto& ch : lbl) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
                gfx_->set_letter_spacing(0.4f * s);
                const float lw = gfx_->measure(F(400, 10), lbl);
                gfx_->set_letter_spacing(0);
                const float vw = gfx_->measure(F(600, 14), it2->value), colw = std::max(lw, vw);
                T(600, 14, rx - vw, row_top + 22.5f * s, 14, it2->value, th.text);
                gfx_->set_letter_spacing(0.4f * s);
                T(400, 10, rx - lw, row_top + 37.5f * s, 10, lbl, {255, 255, 255, 153});
                gfx_->set_letter_spacing(0);
                rx -= colw + 12 * s;
            }
        };
        stat_row(0xf302, th.sub, "Image Cache (IndexedDB)", "Cover art blobs persisted across sessions", {{grouped(ps.images), "images"}, {bytes_str(ps.image_bytes), "on disk"}}, "", false, false);
        stat_row(0xf022, th.sub, "Search Index (IndexedDB)", ps.songs > 0 ? grouped(ps.artists) + " artists \xC2\xB7 " + grouped(ps.albums) + " albums \xC2\xB7 " + grouped(ps.songs) + " songs" : "Library metadata for instant search",
                 ps.index_bytes > 0 ? std::vector<Stat>{{bytes_str(ps.index_bytes), "on disk"}} : std::vector<Stat>{{"\xE2\x80\x94", "not built"}}, "", false, false);
        stat_row(0xf058, th.accent, "Images \xE2\x80\x94 No Network", grouped(ps.mem_hits) + " from RAM \xC2\xB7 " + grouped(ps.disk_hits) + " from disk", {{grouped(ps.mem_hits + ps.disk_hits), "served"}}, "", false, false);
        {
            const uint64_t tot = ps.mem_hits + ps.disk_hits + ps.server_fetches + ps.internet_fetches, hit = ps.mem_hits + ps.disk_hits;
            const int pct = tot == 0 ? -1 : static_cast<int>(std::lround(100.0 * hit / tot));
            stat_row(0xf1eb, amber, "Images \xE2\x80\x94 Internet Used", grouped(ps.server_fetches) + " from Subsonic server \xC2\xB7 " + grouped(ps.internet_fetches) + " from art service", {{grouped(ps.server_fetches + ps.internet_fetches), "fetched"}},
                     pct < 0 ? "" : std::to_string(pct) + "% cached", pct >= 80, pct >= 0 && pct < 50);
        }
        stat_row(0xf233, amber, "Metadata \xE2\x80\x94 Internet Used", "Artist lists, album details, song lists, search \xE2\x80\x94 always from server", {{grouped(ps.api_calls), "API calls"}}, "", false, false);
        y = card_top + (card_h + card_extra_prev_[cur_card_] * s);
    }
    y += 32 * s; row_i = 0;
    // ---- OFFLINE & CACHE
    section("OFFLINE & CACHE");
    card_fill(y, 302 * s);
    if (row(offline_ ? 0xf072u : static_cast<uint32_t>(skin::Cloud), "Offline Mode", offline_ ? "No network requests will be made" : "Streaming from server", 1, false, offline_ ? "On" : "Off", offline_, 61)) {
        toggle_offline();
    }
    {
        const bool can = songs_downloaded() > 0;
        if (row(0xf012, "Auto-offline on mobile data", can ? "Switch to offline automatically on cellular" : "Download songs first to enable", 1, false, cfg_.auto_offline_metered ? "On" : "Off", cfg_.auto_offline_metered, 61) && (can || cfg_.auto_offline_metered)) {
            cfg_.auto_offline_metered = !cfg_.auto_offline_metered; cfg_.save(cfg_path_);
            if (!cfg_.auto_offline_metered && auto_offline_active_) apply_metered(false);
            check_metered();
        }
    }
    if (row(skin::Download, "Downloads", "", 1, true, "", false, 52)) set_nav(Nav::Downloads);
    if (row(0xf2f1, "Rebuild Cache", "Re-fetches library index; keeps downloaded files", 1, false, "", false, 61)) modal_ = Modal::ConfirmRebuild;
    if (row(0xf1f8, "Clear All Caches", "Deletes artwork, offline data, and precache flags", 2, false, "", false, 61)) modal_ = Modal::ConfirmClearCaches;
    y += (1 + 32) * s; row_i = 0;
    // ---- REMOTE
    section("REMOTE");
    card_fill(y, 249 * s);
    if (row(skin::Satellite, "Be Controlled", "Let other Xylonic devices on your network control playback here", 1, false, cfg_.be_controlled ? "On" : "Off", cfg_.be_controlled, 61)) { cfg_.be_controlled = !cfg_.be_controlled; cfg_.save(cfg_path_); if (remote_) remote_->set_enabled(cfg_.be_controlled); }
    if (row(0xf11b, "Control Others", "Discover and control other Xylonic devices on your network", 1, false, cfg_.control_others ? "On" : "Off", cfg_.control_others, 61)) { cfg_.control_others = !cfg_.control_others; cfg_.save(cfg_path_); }
    if (row(0xf6ff, "Remote Devices", remote_ && !remote_->devices().empty() ? std::to_string(remote_->devices().size()) + " device(s) found" : "No devices found yet", 1, true, "", false, 61)) modal_ = Modal::Remote;
    if (row(0xf7e4, "Firewall Setup", "Open ports 7766 (UDP) and 7767 (TCP) for remote discovery", 1, true, "", false, 61)) modal_ = Modal::Firewall;
    y += (1 + 32) * s; row_i = 0;
    // ---- STREAMING
    section("STREAMING");
    card_fill(y, 73 * s);
    {
        row(0xf012, "Streaming Quality", "Applies to the next track loaded", 0, false, "", false, 71);
        static const int q[] = {0, 320, 256, 192, 128, 64};
        std::vector<std::string> o = {"Original", "320 kbps", "256 kbps", "192 kbps", "128 kbps", "64 kbps"};
        int cur = 0; for (int i = 0; i < 6; ++i) if (q[i] == cfg_.stream_quality) cur = i;
        dropdown(2, dd_rect(), o, cur);
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
        dropdown(3, dd_rect(), o, cur);
        if (pick_id == 3 && pick >= 0) { cfg_.download_quality = q[pick]; cfg_.save(cfg_path_); if (downloads_) downloads_->set_quality_label(q[pick] ? std::to_string(q[pick]) : "original"); dropdown_open_ = 0; }
        row(0xf5fd, "Concurrent Downloads", "Up to " + std::to_string(cfg_.concurrent_downloads) + " songs at once", 0, false, "", false, 71);
        std::vector<std::string> n = {"1", "2", "3", "4", "5", "6", "7", "8"};
        dropdown(4, dd_rect(), n, cfg_.concurrent_downloads - 1);
        if (pick_id == 4 && pick >= 0) { cfg_.concurrent_downloads = pick + 1; cfg_.save(cfg_path_); dropdown_open_ = 0; }
    }
    y += (1 + 32) * s; row_i = 0;
    // ---- LIBRARY
    section("LIBRARY");
    card_fill(y, 73 * s);
    {
        row(skin::BookOpen, "Default View", "", 0, false, "", false, 71);
        std::vector<std::string> o = {"Artists", "Albums", "Songs", "Liked"};
        dropdown(5, dd_rect(), o, cfg_.default_view);
        if (pick_id == 5 && pick >= 0) { cfg_.default_view = pick; lib_mode_ = pick; cfg_.save(cfg_path_); dropdown_open_ = 0; }
    }
    y += (1 + 32) * s; row_i = 0;
    // ---- ADVANCED (performance tiers)
    section("ADVANCED");
#if defined(__ANDROID__)
    card_fill(y, (335 + 61 + 1 + 62) * s);   // + the Background activity and Software rendering rows (the Open Log Folder row does not exist on Android)
#else
    card_fill(y, (335 + 62 + (cfg_.debug_log ? 53 : 0)) * s);
#endif
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
        if (row(cfg_.debug_log ? 0xf15cu : 0xf15bu, "Debug Logging", "Writes detailed logs to disk", 1, false, cfg_.debug_log ? "On" : "Off", cfg_.debug_log, 61)) { cfg_.debug_log = !cfg_.debug_log; cfg_.save(cfg_path_); xf_log_configure(cfg_.debug_log, pref_ + "logs"); xf_log("debug logging %s", cfg_.debug_log ? "enabled" : "disabled"); }
#if !defined(__ANDROID__)
        if (cfg_.debug_log && row(0xf07c, "Open Log Folder", "", 1, true, "", false, 52)) SDL_OpenURL(("file://" + pref_ + "logs").c_str());
#else
        {   // Android: let the OS keep playback and downloads alive with the screen off
            static uint32_t exempt_checked = 0; static bool exempt = true;
            if (SDL_GetTicks() - exempt_checked > 1500) { exempt = android_bridge::battery_exempt(); exempt_checked = SDL_GetTicks(); }
            if (row(0xf240, "Background activity", "Keep playing and downloading with the screen off: exempt from battery optimisation", 1, false, exempt ? "Allowed" : "Restricted", exempt, 61) && !exempt) android_bridge::request_battery_exemption();
        }
#endif
        if (row(0xf201, "Performance Overlay", "Shows FPS, CPU load, and OS RAM usage \xE2\x80\x94 green <16 ms, yellow <50 ms, red \xE2\x89\xA5 50 ms", 1, false, cfg_.render_timer ? "On" : "Off", cfg_.render_timer, 61)) { cfg_.render_timer = !cfg_.render_timer; cfg_.save(cfg_path_); mark(); }
        if (row(0xf201, "Software Rendering", software_ ? "Drawing on the CPU. Turn off if the GPU renderer works for you; applies on the next start" : "Draw on the CPU instead of the GPU \xE2\x80\x94 use it when the window is blank or glitchy; applies on the next start", 1, false, cfg_.software_render ? "On" : "Off", cfg_.software_render, 61)) { cfg_.software_render = !cfg_.software_render; cfg_.save(cfg_path_); toast_ = "Restart the app to apply"; toast_until_ = SDL_GetTicks() + 3000; mark(); }
    }
    y += (1 + 32) * s; row_i = 0;
#if defined(__ANDROID__) || defined(__IPHONEOS__)
    // ---- STORAGE (mobile: the original shows this on Android only)
    section("STORAGE");
    card_fill(y, 185 * s);
    {
        if (SDL_GetTicks() - settings_vols_ms_ > 2500 || settings_vols_.empty()) { settings_vols_ = storage_volumes(); settings_vols_ms_ = SDL_GetTicks(); }   // a JNI + StorageManager query: not every frame
        const auto& vols = settings_vols_;
        auto where = [&](const std::string& base) {
            for (auto& v : vols) if (base.compare(0, v.path.size(), v.path) == 0) {
                char t[160] = {}; std::snprintf(t, sizeof t, "%s \xC2\xB7 %.1f GB free", v.label.c_str(), v.free_bytes / 1073741824.0); return std::string(t);
            }
            return base;
        };
        if (row(0xf019, "Downloads location", where(downloads_base()), 1, true, "", false, 61)) choose_cache_folder(0);
        if (row(0xf0a0, "Streaming cache location", where(cache_base()), 1, true, "", false, 61)) choose_cache_folder(1);
#if defined(__ANDROID__)
        if (row(0xf013, "App install location", "Android can move the app itself to an SD card or adopted storage: Storage \xE2\x80\xBA Change", 1, false, "Open", true, 61)) SDL_OpenURL("package:settings");
#else
        row(0xf013, "App install location", "Managed by iOS", 0, false, "", false, 61);
#endif
    }
    y += (1 + 32) * s; row_i = 0;
#endif
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
    if (row(0xf5fd, "Tech Stack", "", 1, true, "", false, 52)) modal_ = Modal::TechStack;
    if (row(0xf24e, "Licenses", "", 1, true, "", false, 52)) modal_ = Modal::Licenses;
    gfx_->clip(nullptr);
    {
        const float measured = (y + settings_scroll_.y - r.y) + 32 * s;   // bottom of the About card + breathing room
        if (std::fabs(measured - settings_total_) > 1.0f) { settings_total_ = measured; mark(); }
    }
    {
        bool changed = false;
        for (int k = 0; k < 32; ++k) if (card_extra_now_[k] != card_extra_prev_[k]) { card_extra_prev_[k] = card_extra_now_[k]; changed = true; }
        if (changed) mark();
    }
    u.in = in_saved;
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
    if (images_) images_->set_disk_budget(cfg_.perf_mode == 0 ? 64ull << 20 : cfg_.perf_mode == 1 ? 32ull << 20 : 16ull << 20);
    if (images_) images_->set_max_textures(tex);
    if (images_) images_->set_concurrency(cfg_.perf_mode == 0 ? 4 : 1);   // IMAGE_CACHE_TUNING: balanced 4 fetches, gaming and eco 1
    mark();
}
