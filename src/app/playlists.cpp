// Playlists (playlistService.ts + PlaylistsTab.tsx + AddToPlaylistDialog): local playlists per account, mirrored to the
// Subsonic server (create/update/delete) and synced from it when the tab opens.
#include <algorithm>
#include <cmath>
#include <ctime>
#include "app.h"
#include "../../third_party/yyjson.h"

namespace {
int64_t now_ms() { return static_cast<int64_t>(std::time(nullptr)) * 1000; }
Color alpha(Color c, float a) { c.a = static_cast<uint8_t>(std::lround(a * 255)); return c; }
}  // namespace

void App::pl_load() {
    playlists_.clear();
    std::string j;
    if (!store_ || !store_->get("playlists", j)) return;
    yyjson_doc* d = yyjson_read(j.data(), j.size(), 0);
    if (!d) return;
    size_t i = 0, n = 0; yyjson_val* o = nullptr;
    yyjson_arr_foreach(yyjson_doc_get_root(d), i, n, o) {
        Playlist p;
        auto str = [&](const char* k) { yyjson_val* v = yyjson_obj_get(o, k); return v && yyjson_is_str(v) ? std::string(yyjson_get_str(v)) : std::string(); };
        p.id = str("id"); p.name = str("name"); p.server_id = str("serverId"); p.owner = str("owner");
        yyjson_val* c = yyjson_obj_get(o, "created"); yyjson_val* u = yyjson_obj_get(o, "updated");
        p.created = c ? static_cast<int64_t>(yyjson_get_sint(c)) : 0; p.updated = u ? static_cast<int64_t>(yyjson_get_sint(u)) : 0;
        size_t j2 = 0, n2 = 0; yyjson_val* sv = nullptr;
        yyjson_arr_foreach(yyjson_obj_get(o, "songs"), j2, n2, sv) {
            char* js = yyjson_val_write(sv, 0, nullptr);
            Song s;
            if (js && from_json(js, s)) p.songs.push_back(std::move(s));
            free(js);
        }
        if (!p.id.empty()) playlists_.push_back(std::move(p));
    }
    yyjson_doc_free(d);
}

void App::pl_save() {
    if (!store_) return;
    yyjson_mut_doc* d = yyjson_mut_doc_new(nullptr);
    yyjson_mut_val* arr = yyjson_mut_arr(d);
    yyjson_mut_doc_set_root(d, arr);
    for (const Playlist& p : playlists_) {
        yyjson_mut_val* o = yyjson_mut_arr_add_obj(d, arr);
        yyjson_mut_obj_add_strcpy(d, o, "id", p.id.c_str());
        yyjson_mut_obj_add_strcpy(d, o, "name", p.name.c_str());
        yyjson_mut_obj_add_strcpy(d, o, "serverId", p.server_id.c_str());
        yyjson_mut_obj_add_strcpy(d, o, "owner", p.owner.c_str());
        yyjson_mut_obj_add_sint(d, o, "created", p.created);
        yyjson_mut_obj_add_sint(d, o, "updated", p.updated);
        yyjson_mut_val* ss = yyjson_mut_arr(d);
        for (const Song& s : p.songs) {
            std::string js = to_json(s);
            yyjson_doc* sd = yyjson_read(js.data(), js.size(), 0);
            if (sd) { yyjson_mut_arr_add_val(ss, yyjson_val_mut_copy(d, yyjson_doc_get_root(sd))); yyjson_doc_free(sd); }
        }
        yyjson_mut_obj_add_val(d, o, "songs", ss);
    }
    char* out = yyjson_mut_write(d, 0, nullptr);
    if (out) { store_->put("playlists", out); free(out); }
    for (const Playlist& p : playlists_) store_->set_playlist(p.id, p.songs);   // membership queries use the indexed copy
    yyjson_mut_doc_free(d);
}

App::Playlist* App::pl_find(const std::string& id) {
    for (auto& p : playlists_) if (p.id == id) return &p;
    return nullptr;
}

std::string App::pl_create(const std::string& name, const std::vector<Song>& songs) {
    Playlist p;
    p.id = "pl_" + std::to_string(now_ms()) + "_" + std::to_string(SDL_GetTicks());
    size_t b = name.find_first_not_of(" \t"), e = name.find_last_not_of(" \t");
    p.name = b == std::string::npos ? "Untitled Playlist" : name.substr(b, e - b + 1);
    p.songs = songs;
    p.created = p.updated = now_ms();
    std::string id = p.id;
    playlists_.push_back(std::move(p));
    pl_save();
    if (client_ && !offline_) {
        std::vector<std::string> ids;
        for (auto& s : songs) ids.push_back(s.id);
        pl_syncing_.insert(id);
        worker_->post([this, c = client_, s = session_, id, nm = pl_find(id)->name, ids] {
            std::string sid, e;
            bool ok = c->create_playlist(nm, ids, sid, e);
            ui_post([this, s, id, sid, ok] { if (s != session_) return; pl_syncing_.erase(id); if (ok) if (auto* p = pl_find(id)) { p->server_id = sid; pl_save(); } mark(); });
        });
    }
    mark();
    return id;
}

void App::pl_delete(const std::string& id) {
    Playlist* p = pl_find(id);
    if (!p) return;
    std::string sid = p->server_id;
    playlists_.erase(std::remove_if(playlists_.begin(), playlists_.end(), [&](const Playlist& x) { return x.id == id; }), playlists_.end());
    pl_save();
    if (!sid.empty() && client_ && !offline_) worker_->post([c = client_, sid] { std::string e; c->delete_playlist(sid, e); });
    mark();
}

void App::pl_rename(const std::string& id, const std::string& name) {
    Playlist* p = pl_find(id);
    if (!p || name.empty()) return;
    p->name = name; p->updated = now_ms();
    pl_save();
    if (!p->server_id.empty() && client_ && !offline_) worker_->post([c = client_, sid = p->server_id, name] { std::string e; c->update_playlist(sid, name, {}, {}, e); });
    mark();
}

bool App::pl_add_song(const std::string& id, const Song& song) {
    Playlist* p = pl_find(id);
    if (!p) return false;
    for (auto& s : p->songs) if (s.id == song.id) return false;
    p->songs.push_back(song); p->updated = now_ms();
    pl_save();
    if (!p->server_id.empty() && client_ && !offline_) worker_->post([c = client_, sid = p->server_id, sg = song.id] { std::string e; c->update_playlist(sid, "", {sg}, {}, e); });
    mark();
    return true;
}

void App::pl_remove_song(const std::string& id, int index) {
    Playlist* p = pl_find(id);
    if (!p || index < 0 || index >= static_cast<int>(p->songs.size())) return;
    p->songs.erase(p->songs.begin() + index); p->updated = now_ms();
    pl_save();
    if (!p->server_id.empty() && client_ && !offline_) worker_->post([c = client_, sid = p->server_id, index] { std::string e; c->update_playlist(sid, "", {}, {index}, e); });
    mark();
}

void App::pl_sync() {
    if (!client_ || offline_ || pl_sync_busy_) return;
    pl_sync_busy_ = true;
    worker_->post([this, c = client_, s = session_] {
        std::vector<PlaylistInfo> infos; std::string e;
        std::vector<std::pair<PlaylistInfo, std::vector<Song>>> full;
        if (c->playlists(infos, e))
            for (auto& i : infos) { PlaylistInfo pi; std::vector<Song> songs; if (c->playlist(i.id, pi, songs, e)) full.emplace_back(i, std::move(songs)); }
        ui_post([this, s, full = std::move(full)]() mutable {
            if (s != session_) return;
            pl_sync_busy_ = false;
            for (auto& f : full) {   // upsertServerPlaylist + updatePlaylistSongs
                Playlist* p = nullptr;
                for (auto& x : playlists_) if (x.server_id == f.first.id) { p = &x; break; }
                if (!p) { playlists_.emplace_back(); p = &playlists_.back(); p->id = "sv_" + f.first.id; p->server_id = f.first.id; p->created = now_ms(); }
                p->name = f.first.name; p->owner = f.first.owner; p->songs = std::move(f.second); p->updated = now_ms();
            }
            pl_save();
            mark();
        });
    });
    mark();
}

// ---------------------------------------------------------------- panel tab

void App::draw_playlists_tab(Rect p, float cy) {
    Ui& u = *ui_;
    const float s = S_;
    const Theme& th = u.th;
    if (!pl_synced_once_) { pl_synced_once_ = true; pl_sync(); }
    std::string flt = panel_search_;
    for (auto& c : flt) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
    // create form
    Rect inp{p.x, cy, p.w - 68 * s, 35 * s}, btn{p.x + p.w - 68 * s, cy, 68 * s, 35 * s};
    gfx_->fill_round(inp, 8 * s, th.bg);
    gfx_->fill({inp.x + inp.w - 8 * s, inp.y, 8 * s, inp.h}, th.bg);
    u.text_field(kPlNew, inp, pl_new_name_, "New playlist name\xE2\x80\xA6", false, false, 12 * s, F(400, 13));
    gfx_->fill_round(btn, 8 * s, u.hover(btn) ? th.primary_light : th.accent);
    gfx_->fill({btn.x, btn.y, 8 * s, btn.h}, u.hover(btn) ? th.primary_light : th.accent);
    T(700, 12, btn.x + (btn.w - gfx_->measure(F(700, 12), "Create")) / 2, btn.y, 35, "Create", th.bg);
    if ((u.clicked(btn) || (u.in.enter && u.focus == kPlNew)) && !pl_new_name_.empty()) { pl_create(pl_new_name_, {}); pl_new_name_.clear(); }
    gfx_->fill({p.x, cy + 35 * s, p.w, 1 * s}, th.border_subtle);
    cy += 36 * s;
    // section header
    if (!queue_.empty()) {
        T(600, 11, p.x + 12 * s, cy + 11 * s, 16.5f, "FROM QUEUE", rgb(0xb5b5b5));
        Rect sq{p.x + 100 * s, cy + 8 * s, 96 * s, 22 * s};
        if (u.hover(sq)) gfx_->fill_round(sq, 8 * s, th.hover);
        fa(0xf0c7, sq.x + 14 * s, sq.y + 11 * s, 12, th.sub);
        T(400, 12, sq.x + 26 * s, sq.y, 22, "Save Queue", th.sub);
        u.tip(sq, "Save current queue as playlist");
        if (u.clicked(sq)) pl_create("Queue " + std::to_string(playlists_.size() + 1), queue_.songs());
    }
    Rect sy{p.x + p.w - 6 * s - 61 * s, cy + 8 * s, 61 * s, 22 * s};
    if (u.hover(sy)) gfx_->fill_round(sy, 8 * s, th.hover);
    fa(skin::Sync, sy.x + 16 * s, sy.y + 11 * s, 12, th.sub);
    T(400, 12, sy.x + 28 * s, sy.y, 22, "Sync", th.sub);
    u.tip(sy, "Sync playlists from server");
    if (u.clicked(sy)) pl_sync();
    cy += 39 * s;
    std::vector<size_t> vis;
    for (size_t i = 0; i < playlists_.size(); ++i) {
        std::string n = playlists_[i].name;
        for (auto& c : n) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
        if (flt.empty() || n.find(flt) != std::string::npos) vis.push_back(i);
    }
    if (vis.empty()) {
        fa(skin::Music, p.x + p.w / 2, cy + 56 * s, 32, th.muted);
        std::string l1 = flt.empty() ? "No playlists yet." : "No results for \"" + panel_search_ + "\"";
        T(400, 13, p.x + (p.w - gfx_->measure(F(400, 13), l1)) / 2, cy + 92 * s, 20, l1, th.sub);
        if (flt.empty()) { std::string l2 = "Create one above to get started."; T(400, 13, p.x + (p.w - gfx_->measure(F(400, 13), l2)) / 2, cy + 112 * s, 20, l2, th.sub); }
        return;
    }
    Rect list{p.x, cy, p.w, p.y + p.h - cy};
    float total = 0;
    for (size_t i : vis) total += 40 * s + (pl_expanded_ == playlists_[i].id ? 53 * s * playlists_[i].songs.size() : 0);
    if (u.hover(list)) panel_scroll_.y -= u.in.wheel * 53 * s;
    panel_scroll_.clamp(total, list.h);
    gfx_->clip(&list);
    float y = cy - panel_scroll_.y;
    for (size_t vi : vis) {
        Playlist& pl = playlists_[vi];
        bool exp = pl_expanded_ == pl.id;
        Rect hd{p.x, y, p.w - 6 * s, 40 * s};
        bool hov = u.hover(hd) && list.contains(u.in.mx, u.in.my);
        if (hov) gfx_->fill(hd, th.border_subtle);
        fa(skin::ChevRight, hd.x + 18 * s, hd.y + 20 * s, 10, th.muted);
        bool renaming = pl_rename_id_ == pl.id;
        if (renaming) {
            Rect ri{hd.x + 30 * s, hd.y + 6 * s, hd.w - 140 * s, 28 * s};
            gfx_->fill_round(ri, 6 * s, th.bg); gfx_->stroke_round(ri, 6 * s, 1 * s, th.accent);
            u.focus = u.focus ? u.focus : kPlRename;
            u.text_field(kPlRename, ri, pl_rename_text_, "", false, false, 8 * s, F(400, 14));
            if (u.in.enter) { pl_rename(pl.id, pl_rename_text_); pl_rename_id_.clear(); u.focus = 0; }
            if (u.in.escape) { pl_rename_id_.clear(); u.focus = 0; }
        } else {
            float nw = T(400, 14, hd.x + 32 * s, hd.y, 40, pl.name, th.text, hd.w - 170 * s);
            if (!pl.server_id.empty()) fa(pl_syncing_.count(pl.id) ? skin::Spinner : skin::Cloud, hd.x + 32 * s + nw + 14 * s, hd.y + 20 * s, 10, th.muted);
        }
        std::string cnt = std::to_string(pl.songs.size());
        T(400, 11, hd.x + hd.w - 112 * s - gfx_->measure(F(400, 11), cnt), hd.y + 11 * s, 16.5f, cnt, th.muted);
        float ax = hd.x + hd.w - 108 * s;
        auto ab = [&](float bx, uint32_t ic, bool danger, const char* tip) {
            Rect b{bx, hd.y + 9 * s, 24 * s, 22 * s};
            if (hov) u.tip(b, tip);
            if (hov && u.hover(b)) gfx_->fill_round(b, 8 * s, th.hover);
            if (hov) fa(ic, b.x + 12 * s, b.y + 11 * s, 11, u.hover(b) ? (danger ? th.danger : th.accent) : th.sub);
            return hov && u.clicked(b);
        };
        bool acted = false;
        if (ab(ax, skin::ListUl, false, "Add all to queue")) { for (auto& sg : pl.songs) queue_.add(sg); acted = true; }
        else if (ab(ax + 26 * s, skin::Play, false, "Play playlist")) { if (!pl.songs.empty()) play_songs(pl.songs, 0, false); acted = true; }
        else if (ab(ax + 52 * s, skin::Pen, false, "Rename")) { pl_rename_id_ = pl.id; pl_rename_text_ = pl.name; u.focus = kPlRename; acted = true; }
        else if (ab(ax + 78 * s, 0xf1f8, true, "Delete playlist")) { pl_delete_pending_ = pl.id; modal_ = Modal::ConfirmDeletePlaylist; gfx_->clip(nullptr); return; }   // window.confirm("Delete this playlist?")
        if (u.clicked(hd) && !acted && !renaming && list.contains(u.in.mx, u.in.my)) pl_expanded_ = exp ? "" : pl.id;
        y += 40 * s;
        gfx_->fill({p.x, y - 1 * s, p.w, 1 * s}, th.border_subtle);
        if (exp) {
            gfx_->fill({p.x, y, p.w, 53 * s * pl.songs.size()}, {0, 0, 0, 38});
            for (size_t k = 0; k < pl.songs.size(); ++k) {
                const Song& sg = pl.songs[k];
                Rect row{p.x, y + k * 53 * s, p.w - 6 * s, 53 * s};
                bool rh = u.hover(row) && list.contains(u.in.mx, u.in.my);
                if (rh) gfx_->fill(row, th.hover);
                fa(skin::Grip, row.x + 24 * s, row.y + 26 * s, 12, rgb(0xb5b5b5));
                T(500, 13, row.x + 45 * s, row.y + 8 * s, 19.5f, sg.title, th.text, row.w - 45 * s - 80 * s);
                T(400, 11, row.x + 45 * s, row.y + 28 * s, 16.5f, sg.artist, th.sub, row.w - 45 * s - 80 * s);
                if (rh) {
                    Rect a1{row.x + row.w - 66 * s, row.y + 15 * s, 29 * s, 22 * s}, a2{row.x + row.w - 36 * s, row.y + 15 * s, 29 * s, 22 * s};
                    if (u.hover(a1)) gfx_->fill_round(a1, 8 * s, th.hover);
                    fa(skin::ListUl, a1.x + 14 * s, a1.y + 11 * s, 12, u.hover(a1) ? th.accent : th.sub);
                    if (u.hover(a2)) gfx_->fill_round(a2, 8 * s, th.hover);
                    fa(skin::Xmark, a2.x + 14 * s, a2.y + 11 * s, 12, u.hover(a2) ? th.danger : th.sub);
                    u.tip(a1, "Add to queue"); u.tip(a2, "Remove from playlist");
                    if (u.clicked(a1)) queue_.add(sg);
                    else if (u.clicked(a2)) { std::string id = pl.id; pl_remove_song(id, static_cast<int>(k)); gfx_->clip(nullptr); return; }
                    else if (u.clicked(row)) { play_songs(pl.songs, k, false); gfx_->clip(nullptr); return; }
                }
                gfx_->fill({row.x, row.y + row.h - 1 * s, row.w, 1 * s}, th.border_subtle);
            }
            y += 53 * s * pl.songs.size();
        }
    }
    gfx_->clip(nullptr);
}

// ---------------------------------------------------------------- add to playlist dialog

void App::draw_add_to_playlist(float W, float H) {
    Ui& u = *ui_;
    const float s = S_;
    const Theme& th = u.th;
    auto close = [&] { add_song_.reset(); add_sel_.clear(); add_new_.clear(); add_done_until_ = 0; };
    gfx_->fill({0, 0, W, H}, {0, 0, 0, 170});
    const float row_h = 52 * s, head = 108 * s, foot = 120 * s;
    const float list_want = row_h * std::max<size_t>(1, playlists_.size());
    float bw = std::min(380 * s, W - 24 * s), bh = std::min(H - 40 * s, head + list_want + foot);
    Rect b{(W - bw) / 2, (H - bh) / 2, bw, bh};
    gfx_->fill_round({b.x - 1, b.y - 1, b.w + 2, b.h + 2}, 3 * s, th.border);
    gfx_->fill_round(b, 2 * s, th.bg_light);
    T(600, 16, b.x + 20 * s, b.y + 18 * s, 24, "Add to Playlist", th.text);
    Rect cl{b.x + b.w - 52 * s, b.y + 14 * s, 32 * s, 32 * s};
    if (u.hover(cl)) gfx_->fill_round(cl, 16 * s, th.hover);
    fa(skin::Xmark, cl.x + 16 * s, cl.y + 16 * s, 16, th.sub);
    if (u.clicked(cl) || u.in.escape) { close(); return; }
    fa(skin::Music, b.x + 28 * s, b.y + 62 * s, 12, th.accent);
    T(400, 12, b.x + 44 * s, b.y + 52 * s, 20, add_song_->title, th.sub, b.w - 64 * s);
    if (!pl_syncing_.empty()) T(400, 11, b.x + 20 * s, b.y + 78 * s, 16, "Syncing from server\xE2\x80\xA6", th.muted);
    gfx_->fill({b.x, b.y + 98 * s, b.w, 1 * s}, th.border);
    Rect list{b.x, b.y + head, b.w, b.h - head - foot};
    add_scroll_.clamp(list_want, list.h);
    if (u.hover(list)) add_scroll_.y -= u.in.wheel * 60 * s;
    add_scroll_.clamp(list_want, list.h);
    gfx_->clip(&list);
    float y = list.y - add_scroll_.y;
    if (playlists_.empty()) T(400, 13, b.x + 20 * s, y + 8 * s, 20, "No playlists yet. Create one below.", th.sub, b.w - 40 * s);
    for (auto& pl : playlists_) {
        Rect row{b.x, y, b.w, row_h};
        bool has = false;
        if (store_) has = store_->playlist_contains(pl.id, add_song_->id);
        else for (auto& sg : pl.songs) if (sg.id == add_song_->id) has = true;
        const bool on = add_sel_.count(pl.id) > 0;
        if (!has && u.hover(row) && list.contains(u.in.mx, u.in.my)) gfx_->fill(row, th.hover);
        Rect cb{row.x + 20 * s, row.y + 17 * s, 18 * s, 18 * s};
        gfx_->fill_round(cb, 4 * s, on ? th.accent : th.bg);
        gfx_->stroke_round(cb, 4 * s, 1 * s, on ? th.accent : alpha(th.text, has ? 0.15f : 0.35f));
        if (on || has) fa(skin::Check, cb.x + 9 * s, cb.y + 9 * s, 11, on ? th.accent_text : th.muted);
        const float nw = row.w - 60 * s - (has ? 96 * s : 0);
        T(500, 14, row.x + 52 * s, row.y + 8 * s, 20, pl.name, has ? th.muted : th.text, nw);
        const std::string meta = std::to_string(pl.songs.size()) + (pl.songs.size() == 1 ? " song" : " songs");
        T(400, 11, row.x + 52 * s, row.y + 29 * s, 16, meta, th.sub);
        if (!pl.server_id.empty()) fa(skin::Cloud, row.x + 52 * s + gfx_->measure(F(400, 11), meta) + 14 * s, row.y + 37 * s, 10, th.muted);
        if (has) T(500, 11, row.x + row.w - 96 * s, row.y + 17 * s, 18, "Already added", th.accent);
        if (!has && u.clicked(row) && list.contains(u.in.mx, u.in.my)) { if (on) add_sel_.erase(pl.id); else add_sel_.insert(pl.id); mark(); }
        y += row_h;
    }
    gfx_->clip(nullptr);
    // new playlist + confirm
    const float fy = b.y + b.h - foot + 12 * s;
    gfx_->fill({b.x, fy - 6 * s, b.w, 1 * s}, th.border);
    fa(skin::CirclePlus, b.x + 28 * s, fy + 24 * s, 15, th.accent);
    Rect nf{b.x + 48 * s, fy + 6 * s, b.w - 68 * s, 36 * s};
    gfx_->fill_round(nf, 18 * s, th.panel); gfx_->stroke_round(nf, 18 * s, 1 * s, u.focus == kAddNew ? th.accent : th.border);
    const bool enter = u.in.enter && u.focus == kAddNew;
    u.text_field(kAddNew, nf, add_new_, "New playlist name\xE2\x80\xA6", false, false, 14 * s, F(400, 14));
    const size_t trimmed = add_new_.find_first_not_of(" \t");
    const int count = static_cast<int>(add_sel_.size()) + (trimmed != std::string::npos ? 1 : 0);
    Rect ok{b.x + 20 * s, fy + 54 * s, b.w - 40 * s, 40 * s};
    const bool done = add_done_until_ != 0;
    if (done) {
        T(600, 14, ok.x + (ok.w - gfx_->measure(F(600, 14), add_toast_)) / 2, ok.y, 40, add_toast_, th.accent);
        if (SDL_GetTicks() >= add_done_until_) { close(); mark(); return; }
    } else {
        const bool can = count > 0;
        gfx_->fill_round(ok, 20 * s, can ? (u.hover(ok) ? th.primary_light : th.accent) : alpha(th.accent, 0.35f));
        const std::string lab = count > 0 ? "Add to " + std::to_string(count) + (count == 1 ? " playlist" : " playlists") : "Select a playlist";
        T(600, 14, ok.x + (ok.w - gfx_->measure(F(600, 14), lab)) / 2, ok.y, 40, lab, can ? th.accent_text : alpha(th.accent_text, 0.6f));
        if (can && (u.clicked(ok) || enter)) {
            for (const std::string& id : add_sel_) pl_add_song(id, *add_song_);
            if (trimmed != std::string::npos) pl_create(add_new_, {*add_song_});
            add_toast_ = "Added to " + std::to_string(count) + (count == 1 ? " playlist" : " playlists");
            add_done_until_ = SDL_GetTicks() + 900;
            u.focus = 0;
        }
        else if (!can && enter) mark();
    }
    if (done) mark();
}
