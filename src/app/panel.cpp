// Right panel (RightPanel.tsx): Queue, History and Playlists tabs. Measured at 1024x768: width clamp(260, 30vw, 360),
// top 74, bottom 130, radius 16 on the left corners, header 44, rows 53.
#include <algorithm>
#include <cmath>
#include "app.h"
#include "../../third_party/yyjson.h"

namespace {
Color alpha(Color c, float a) { c.a = static_cast<uint8_t>(std::lround(a * 255)); return c; }
std::string mmss(int s) {
    if (s < 0) s = 0;
    char b[16];
    std::snprintf(b, sizeof b, "%d:%02d", s / 60, s % 60);
    return b;
}
std::string time_ago(int64_t ts_ms, int64_t now_ms) {  // HistoryTab.timeAgo
    int64_t m = (now_ms - ts_ms) / 60000;
    if (m < 1) return "just now";
    if (m < 60) return std::to_string(m) + "m ago";
    int64_t h = m / 60;
    if (h < 24) return std::to_string(h) + "h ago";
    return std::to_string(h / 24) + "d ago";
}
const int kMaxHistory = 50;
}  // namespace

// ---------------------------------------------------------------- history (recentlyPlayedService)

void App::history_load() {
    history_.clear();
    std::string j;
    if (!store_ || !store_->get("history", j)) return;
    yyjson_doc* d = yyjson_read(j.data(), j.size(), 0);
    if (!d) return;
    size_t i, n; yyjson_val* o;
    yyjson_arr_foreach(yyjson_doc_get_root(d), i, n, o) {
        HistoryEntry e;
        yyjson_val* sv = yyjson_obj_get(o, "song");
        yyjson_val* ts = yyjson_obj_get(o, "at");
        if (!sv || !ts) continue;
        char* js = yyjson_val_write(sv, 0, nullptr);
        if (js) { from_json(js, e.song); free(js); }
        e.at = static_cast<int64_t>(yyjson_get_sint(ts));
        if (!e.song.id.empty()) history_.push_back(std::move(e));
    }
    yyjson_doc_free(d);
}

void App::history_save() {
    if (!store_) return;
    std::string out = "[";
    for (size_t i = 0; i < history_.size(); ++i) {
        if (i) out += ',';
        out += "{\"at\":" + std::to_string(history_[i].at) + ",\"song\":" + to_json(history_[i].song) + "}";
    }
    out += "]";
    store_->put("history", out);
}

void App::history_add(const Song& s) {
    history_.erase(std::remove_if(history_.begin(), history_.end(), [&](const HistoryEntry& e) { return e.song.id == s.id; }), history_.end());
    history_.insert(history_.begin(), HistoryEntry{s, static_cast<int64_t>(std::time(nullptr)) * 1000});
    if (history_.size() > static_cast<size_t>(kMaxHistory)) history_.resize(kMaxHistory);
    history_save();
}

// ---------------------------------------------------------------- panel

void App::toggle_panel(PanelTab tab) {
    if (panel_open_ && panel_tab_ == tab) panel_open_ = false;
    else { panel_tab_ = tab; panel_open_ = true; }
    panel_search_open_ = false;
    panel_search_.clear();
    mark();
}

void App::draw_right_panel(float W, float H) {
    if (!panel_open_) return;
    Ui& u = *ui_;
    const float s = S_;
    const Theme& th = u.th;
    // compact: the panel fills the space between the header and the mini player + bottom nav (top 56, bottom 114)
    float pw = mobile_ ? W : std::min(360.0f, std::max(260.0f, 0.30f * W / s)) * s;
    Rect p = mobile_ ? Rect{0, 56 * s + banner_h_, W, H - 56 * s - banner_h_ - 114 * s} : Rect{W - pw, 74 * s, pw, H - 74 * s - 130 * s};
    const float prad = mobile_ ? 0.0f : 16 * s;
    // shadow: -8px 0 40px rgba(0,0,0,.55) approximated by fading strips
    for (int i = 0; i < (mobile_ ? 0 : 6); ++i) gfx_->fill({p.x - (i + 1) * 6 * s, p.y + 8 * s, 6 * s, p.h - 8 * s}, {0, 0, 0, static_cast<uint8_t>(34 - i * 5)});
    // left-rounded panel: rounded rect extended past the right edge so only the left corners show
    gfx_->fill_round({p.x - 1 * s, p.y - 1 * s, p.w + 40 * s, p.h + 2 * s}, prad, th.border);
    gfx_->fill_round({p.x, p.y, p.w + 40 * s, p.h}, prad > 0 ? 15 * s : 0, th.bg_medium);
    Rect clipr{p.x, p.y, p.w, p.h};
    gfx_->clip(&clipr);
    static const char* titles[3] = {"QUEUE", "RECENTLY PLAYED", "PLAYLISTS"};
    static const uint32_t icons[3] = {skin::ListUl, skin::History, skin::Music};
    int tab = static_cast<int>(panel_tab_);
    // header
    float hx = p.x + 12 * s;
    fa(icons[tab], hx + 8 * s, p.y + 22 * s, 13, th.accent);
    gfx_->set_letter_spacing(1.3f * s);
    T(700, 13, hx + 26 * s, p.y + 11 * s, 19.5f, titles[tab], th.text);
    gfx_->set_letter_spacing(0);
    auto hbtn = [&](float bx, uint32_t ic) {
        Rect b{bx, p.y + 10 * s, 32 * s, 23 * s};
        if (u.hover(b)) gfx_->fill_round(b, 8 * s, th.hover);
        fa(ic, b.x + 16 * s, b.y + 11.5f * s, 13, th.sub);
        return u.clicked(b);
    };
    if (hbtn(p.x + p.w - 78 * s, skin::Search)) { panel_search_open_ = !panel_search_open_; if (panel_search_open_) u.focus = kPanelSearch; else panel_search_.clear(); }
    if (hbtn(p.x + p.w - 36 * s, skin::Xmark)) { panel_open_ = false; gfx_->clip(nullptr); return; }
    gfx_->fill({p.x, p.y + 43 * s, p.w, 2 * s}, th.accent);
    float cy = p.y + 45 * s;
    if (panel_search_open_) {
        Rect sf{p.x + 12 * s, cy + 6 * s, p.w - 24 * s, 30 * s};
        gfx_->fill_round(sf, 15 * s, th.panel2);
        fa(skin::Search, sf.x + 16 * s, sf.y + 15 * s, 12, th.sub);
        u.text_field(kPanelSearch, sf, panel_search_, "Filter\xE2\x80\xA6", false, false, 32 * s, F(400, 13));
        cy += 42 * s;
    }
    std::string flt = panel_search_;
    for (auto& c : flt) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
    auto match = [&](const Song& sg) {
        if (flt.empty()) return true;
        std::string t = sg.title + " " + sg.artist + " " + sg.album;
        for (auto& c : t) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
        return t.find(flt) != std::string::npos;
    };
    std::vector<int> idx;  // visible entries
    int total = 0;
    if (panel_tab_ == PanelTab::Queue) { for (size_t i = 0; i < queue_.size(); ++i) if (match(queue_.songs()[i])) idx.push_back(static_cast<int>(i)); total = static_cast<int>(queue_.size()); }
    else if (panel_tab_ == PanelTab::History) { for (size_t i = 0; i < history_.size(); ++i) if (match(history_[i].song)) idx.push_back(static_cast<int>(i)); total = static_cast<int>(history_.size()); }
    if (panel_tab_ == PanelTab::Playlists) {
        draw_playlists_tab(p, cy);
        gfx_->clip(nullptr);
        return;
    }
    if (total > 0 && idx.empty()) {   // filter matched nothing
        const std::string l = "No results for \"" + panel_search_ + "\"";
        fa(skin::Search, p.x + p.w / 2, cy + 70 * s, 28, th.muted);
        T(400, 13, p.x + (p.w - gfx_->measure(F(400, 13), l)) / 2, cy + 100 * s, 20, l, th.sub, p.w - 24 * s);
        gfx_->clip(nullptr);
        return;
    }
    if (total == 0) {
        std::string l1 = panel_tab_ == PanelTab::Queue ? "Queue is empty." : "No recently played songs yet.";
        std::string l2 = panel_tab_ == PanelTab::Queue ? "Play a song to get started." : "Start playing to build your history.";
        fa(icons[tab], p.x + p.w / 2, cy + 70 * s, 28, th.muted);
        T(400, 13, p.x + (p.w - gfx_->measure(F(400, 13), l1)) / 2, cy + 100 * s, 20, l1, th.sub);
        T(400, 13, p.x + (p.w - gfx_->measure(F(400, 13), l2)) / 2, cy + 120 * s, 20, l2, th.sub);
        gfx_->clip(nullptr);
        return;
    }
    // section header: count + Clear
    {
        std::string lbl = std::to_string(idx.size()) + (idx.size() == 1 ? " SONG" : " SONGS");
        gfx_->set_letter_spacing(0.88f * s);
        T(600, 11, p.x + 12 * s, cy + 11 * s, 16.5f, lbl, rgb(0xb5b5b5));
        gfx_->set_letter_spacing(0);
        Rect cb{p.x + p.w - 6 * s - 63 * s, cy + 8 * s, 63 * s, 22 * s};
        if (u.hover(cb)) gfx_->fill_round(cb, 8 * s, th.hover);
        fa(0xf2ed, cb.x + 16 * s, cb.y + 11 * s, 12, u.hover(cb) ? th.danger : th.sub);
        T(400, 12, cb.x + 28 * s, cb.y, 22, "Clear", u.hover(cb) ? th.danger : th.sub);
        if (u.clicked(cb)) {
            if (panel_tab_ == PanelTab::Queue) { queue_.clear(); player_.stop(); ++play_gen_; }
            else { history_.clear(); history_save(); }
            gfx_->clip(nullptr);
            return;
        }
    }
    float ly = cy + 39 * s, rh = 53 * s;
    Rect list{p.x, ly, p.w, p.y + p.h - ly};
    if (u.hover(list)) panel_scroll_.y -= u.in.wheel * 53 * s;
    panel_scroll_.clamp(rh * idx.size(), list.h);
    gfx_->clip(&list);
    int64_t now_ms = static_cast<int64_t>(std::time(nullptr)) * 1000;
    // drag-reorder (queue only): grab the grip, drop on the target row
    int drop_to = -1;
    if (panel_drag_ >= 0 && u.in.down) drop_to = std::min<int>(static_cast<int>(idx.size()) - 1, std::max(0, static_cast<int>((u.in.my - ly + panel_scroll_.y) / rh)));
    for (size_t k = 0; k < idx.size(); ++k) {
        int i = idx[k];
        const Song& sg = panel_tab_ == PanelTab::Queue ? queue_.songs()[i] : history_[i].song;
        Rect row{p.x, ly + k * rh - panel_scroll_.y, p.w - 6 * s, rh};
        if (row.y + row.h < list.y || row.y > list.y + list.h) continue;
        bool is_cur = panel_tab_ == PanelTab::Queue && static_cast<size_t>(i) == queue_.index();
        bool hov = u.hover(row) && list.contains(u.in.mx, u.in.my);
        if (is_cur) { gfx_->fill(row, alpha(th.accent, 0.10f)); gfx_->fill({row.x, row.y, 2 * s, row.h}, th.accent); }
        else if (hov) gfx_->fill(row, th.hover);
        gfx_->fill({row.x, row.y + row.h - 1 * s, row.w, 1 * s}, th.border_subtle);
        if (panel_drag_ >= 0 && static_cast<int>(k) == drop_to) gfx_->fill({row.x, row.y - 1 * s, row.w, 2 * s}, th.accent);
        float tx = row.x + 45 * s;
        if (panel_tab_ == PanelTab::Queue) {
            Rect g{row.x + 12 * s, row.y + 15 * s, 23 * s, 22 * s};
            fa(skin::Grip, g.x + 11 * s, g.y + 11 * s, 12, rgb(0xb5b5b5));
            u.tip(g, "Drag to reorder");
            if (u.in.pressed && g.contains(u.in.mx, u.in.my)) { panel_drag_ = static_cast<int>(k); }
        }
        float tw = row.w - 45 * s - 6 * s - (panel_tab_ == PanelTab::Queue ? 112 : 112) * s;
        float xo = tx;
        if (is_cur) { fa(skin::VolumeHigh, tx + 6 * s, row.y + 18 * s, 10, th.accent); xo += 14 * s; }
        T(500, 13, xo, row.y + 8 * s, 19.5f, sg.title, is_cur ? th.accent : th.text, tw - (xo - tx));
        std::string sub = panel_tab_ == PanelTab::History ? sg.artist + " \xC2\xB7 " + time_ago(history_[i].at, now_ms) : sg.artist;
        T(400, 11, tx, row.y + 28 * s, 16.5f, sub, th.sub, tw);
        if (panel_tab_ == PanelTab::Queue) {
            std::string d = mmss(sg.duration);
            T(400, 11, row.x + row.w - 106 * s, row.y + 18 * s, 16.5f, d, rgb(0xb5b5b5));
        }
        Rect a1{row.x + row.w - 76 * s, row.y + 15 * s, 29 * s, 22 * s}, a2{row.x + row.w - 46 * s, row.y + 15 * s, 29 * s, 22 * s};
        if (hov || is_cur) {
            auto ab = [&](Rect b, uint32_t ic, bool danger, const char* tip) {
                u.tip(b, tip);
                if (u.hover(b)) gfx_->fill_round(b, 8 * s, th.hover);
                fa(ic, b.x + b.w / 2, b.y + b.h / 2, 12, u.hover(b) ? (danger ? th.danger : th.accent) : th.sub);
                return u.clicked(b);
            };
            if (panel_tab_ == PanelTab::Queue) {
                if (ab(a1, skin::CirclePlus, false, "Add to playlist")) add_song_ = sg;
                if (ab(a2, skin::Xmark, true, "Remove from queue")) {
                    bool was_cur = is_cur;
                    queue_.remove(static_cast<size_t>(i));
                    if (was_cur) { if (queue_.empty()) { player_.stop(); ++play_gen_; } else start_current(); }
                    gfx_->clip(nullptr);
                    return;
                }
            } else {
                // HistoryTab: Add to queue, Add to playlist, Play (there is no per-entry remove; Clear empties the list)
                Rect a0{row.x + row.w - 106 * s, a1.y, 29 * s, 22 * s};
                if (ab(a0, skin::ListUl, false, "Add to queue")) queue_.add(sg);
                if (ab(a1, skin::CirclePlus, false, "Add to playlist")) add_song_ = sg;
                if (ab(a2, skin::Play, false, "Play")) { std::vector<Song> one{sg}; play_songs(one, 0, false); gfx_->clip(nullptr); return; }
            }
        }
        ctx_hit(row, sg);
        Rect info{tx, row.y, row.w - 45 * s - 112 * s, row.h};
        if (u.clicked(info) && panel_drag_ < 0 && list.contains(u.in.mx, u.in.my)) {
            if (panel_tab_ == PanelTab::Queue) { queue_.jump(static_cast<size_t>(i)); start_current(); }
            else { std::vector<Song> one{sg}; play_songs(one, 0, false); }
            gfx_->clip(nullptr);
            return;
        }
    }
    if (panel_drag_ >= 0 && u.in.released) {
        if (panel_tab_ == PanelTab::Queue && drop_to >= 0 && drop_to < static_cast<int>(idx.size()) && drop_to != panel_drag_) queue_.move(static_cast<size_t>(idx[panel_drag_]), static_cast<size_t>(idx[drop_to]));
        panel_drag_ = -1;
    }
    gfx_->clip(nullptr);
    (void)H;
}
