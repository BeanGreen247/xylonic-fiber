// Remote control integration (RemoteModeContext, RemoteDevicePicker): "Be Controlled" executes commands from paired
// controllers; "Control Others" lists devices on the same account and routes the player bar to the chosen device.
#include <algorithm>
#include <cmath>
#include <ctime>
#include <fstream>
#include <mbedtls/md5.h>
#include <psa/crypto.h>
#include "app.h"
#include "metered.h"
#include "../../third_party/yyjson.h"

namespace {
Color alpha(Color c, float a) { c.a = static_cast<uint8_t>(std::lround(a * 255)); return c; }
std::string md5hex(const std::string& s) {
    unsigned char d[16];
    mbedtls_md5(reinterpret_cast<const unsigned char*>(s.data()), s.size(), d);
    static const char* h = "0123456789abcdef";
    std::string o;
    for (unsigned char c : d) { o += h[c >> 4]; o += h[c & 15]; }
    return o;
}
std::string device_id(const std::string& path) {
    std::ifstream f(path);
    std::string id;
    if (f && std::getline(f, id) && id.size() >= 8) return id;
    unsigned char r[16];
    psa_crypto_init();
    psa_generate_random(r, sizeof r);
    static const char* h = "0123456789abcdef";
    id.clear();
    for (unsigned char c : r) { id += h[c >> 4]; id += h[c & 15]; }
    std::ofstream o(path);
    o << id;
    return id;
}
}  // namespace

void App::remote_start() {
    remote_.reset();
    if (cfg_.server_url.empty() || cfg_.username.empty()) return;
    RemoteService::Options o;
    remote_ = std::make_unique<RemoteService>(device_id(pref_ + "device_id.txt"), local_device_name(), o);
    remote_->set_account_id(md5hex(cfg_.username + ":" + cfg_.server_url));
    remote_->set_enabled(cfg_.be_controlled);
    remote_->set_handlers(
        [this](const std::string& action, const std::string& data) { ui_post([this, action, data] { remote_apply(action, data); }); },
        [this] { std::lock_guard<std::mutex> lk(remote_m_); return remote_state_; },
        [this](const std::string& ev, const std::string& arg) {
            ui_post([this, ev, arg] {
                if (ev == "pairing-established") remote_note_ = "Controlled by " + (arg.empty() ? std::string("another device") : arg);
                else if (ev == "pairing-cleared") remote_note_.clear();
                mark();
            });
        });
    remote_->start();
}

void App::remote_stop() {
    remote_mode_ = false;
    remote_target_.clear();
    remote_.reset();
}

void App::remote_push_state() {
    if (!remote_) return;
    const Song* cur = queue_.current();
    RemotePlayerState st;
    st.has_state = true;
    st.playing = player_.playing() && !track_loading_;
    st.current_time = player_.position_seconds();
    st.duration = player_.duration_seconds();
    if (cur) st.song_json = "{\"id\":" + std::string("\"") + cur->id + "\",\"title\":\"" + [&] { std::string o; for (char c : cur->title) { if (c == '"' || c == '\\') o += '\\'; if (static_cast<unsigned char>(c) >= 0x20) o += c; } return o; }() +
                              "\",\"artist\":\"" + [&] { std::string o; for (char c : cur->artist) { if (c == '"' || c == '\\') o += '\\'; if (static_cast<unsigned char>(c) >= 0x20) o += c; } return o; }() +
                              "\",\"album\":\"\",\"coverArt\":\"" + cur->cover_art + "\",\"duration\":" + std::to_string(cur->duration) + "}";
    std::lock_guard<std::mutex> lk(remote_m_);
    remote_state_ = st;
}

void App::remote_apply(const std::string& action, const std::string& data_json) {
    yyjson_doc* d = yyjson_read(data_json.data(), data_json.size(), 0);
    yyjson_val* data = d ? yyjson_doc_get_root(d) : nullptr;
    auto num = [&](const char* k, double def) { yyjson_val* v = data ? yyjson_obj_get(data, k) : nullptr; return v && yyjson_is_num(v) ? yyjson_get_num(v) : def; };
    if (action == "togglePlay") toggle_play();
    else if (action == "play") { if (!player_.playing()) toggle_play(); }
    else if (action == "pause") { if (player_.playing()) player_.pause(); }
    else if (action == "next") next_track();
    else if (action == "previous") prev_track();
    else if (action == "seek") player_.seek_seconds(num("time", player_.position_seconds()));
    else if (action == "setVolume") set_volume(static_cast<float>(num("volume", vol_)));
    else if (action == "toggleShuffle") queue_.set_shuffle(!queue_.shuffle());
    else if (action == "toggleRepeat") queue_.set_repeat(queue_.repeat() == Repeat::Off ? Repeat::All : queue_.repeat() == Repeat::All ? Repeat::One : Repeat::Off);
    else if (action == "playSong" && data) {
        char* js = yyjson_val_write(data, 0, nullptr);
        Song s;
        if (js && from_json(js, s) && !s.id.empty()) { std::vector<Song> one{s}; play_songs(one, 0, false); }
        free(js);
    } else if (action == "playPlaylist" && data) {
        std::vector<Song> songs;
        size_t i, n; yyjson_val* o;
        yyjson_arr_foreach(yyjson_obj_get(data, "songs"), i, n, o) {
            char* js = yyjson_val_write(o, 0, nullptr);
            Song s;
            if (js && from_json(js, s) && !s.id.empty()) songs.push_back(std::move(s));
            free(js);
        }
        if (!songs.empty()) play_songs(songs, static_cast<size_t>(num("startIndex", 0)), false);
    }
    if (d) yyjson_doc_free(d);
    mark();
}

void App::remote_send(const std::string& action, const std::string& data_json) {
    if (!remote_ || !remote_mode_) return;
    for (auto& d : remote_->devices()) {
        if (d.id != remote_target_) continue;
        worker_->post([this, host = d.host, port = d.cmd_port, action, data_json, id = remote_->device_id()] {
            std::string reason;
            bool ok = RemoteService::send_command(host, port, action, data_json, id, reason);
            if (!ok) ui_post([this, reason] { remote_error_ = reason == "not_paired" ? "This device is controlled by another remote." : "Remote command failed (" + reason + ")"; mark(); });
        });
        return;
    }
}

void App::remote_connect(const RemoteDevice& dev) {
    remote_error_.clear();
    if (!remote_) { remote_error_ = "Sign into a Navidrome account before using Remote Mode."; return; }
    std::string acc = md5hex(cfg_.username + ":" + cfg_.server_url);
    worker_->post([this, dev, acc, id = remote_->device_id(), name = local_device_name()] {
        std::string reason;
        std::string data = "{\"controllerAccountId\":\"" + acc + "\",\"controllerName\":\"" + name + "\"}";
        bool ok = RemoteService::send_command(dev.host, dev.cmd_port, "pair", data, id, reason);
        ui_post([this, dev, ok, reason] {
            if (ok && remote_) { remote_mode_ = true; remote_target_ = dev.id; remote_->set_controller_target(dev.id); remote_error_.clear(); modal_ = Modal::None; }
            else remote_error_ = reason == "already_paired" ? "That device is already controlled by someone else." : reason == "account_mismatch" ? "That device is signed into a different account." :
                                 reason == "remote_control_disabled" ? "Remote control is turned off on that device." : "Could not connect (" + reason + ")";
            mark();
        });
    });
}

void App::remote_disconnect() {
    if (!remote_ || !remote_mode_) return;
    for (auto& d : remote_->devices())
        if (d.id == remote_target_) worker_->post([host = d.host, port = d.cmd_port, id = remote_->device_id()] { std::string r; RemoteService::send_command(host, port, "disconnect", "{}", id, r); });
    remote_mode_ = false;
    remote_target_.clear();
    remote_->set_controller_target("");
    mark();
}

// Picker dialog (rdp-modal).
void App::draw_remote_picker(float W, float H) {
    Ui& u = *ui_;
    const float s = S_;
    const Theme& th = u.th;
    gfx_->fill({0, 0, W, H}, {0, 0, 0, 170});
    auto devs = remote_ ? remote_->devices() : std::vector<RemoteDevice>{};
    float bw = std::min(440 * s, W - 24 * s), bh = std::min(H - 80 * s, (150 + 62 * std::max<size_t>(1, devs.size())) * s);
    Rect b{(W - bw) / 2, (H - bh) / 2, bw, bh};
    gfx_->fill_round({b.x - 1, b.y - 1, b.w + 2, b.h + 2}, 3 * s, th.border);
    gfx_->fill_round(b, 2 * s, th.bg_light);
    fa(skin::Satellite, b.x + 28 * s, b.y + 32 * s, 15, th.accent);
    T(600, 16, b.x + 44 * s, b.y + 20 * s, 24, "Remote Mode", th.text);
    Rect cl{b.x + b.w - 52 * s, b.y + 16 * s, 32 * s, 32 * s};
    if (u.hover(cl)) gfx_->fill_round(cl, 16 * s, th.hover);
    fa(skin::Xmark, cl.x + 16 * s, cl.y + 16 * s, 16, th.sub);
    if (u.clicked(cl) || u.in.escape) { modal_ = Modal::None; return; }
    float y = b.y + 64 * s;
    if (!remote_error_.empty()) { T(500, 12, b.x + 20 * s, y, 18, remote_error_, th.danger, b.w - 40 * s); y += 24 * s; }
    if (!cfg_.control_others) { T(400, 13, b.x + 20 * s, y, 20, "Control Others is turned off in Settings.", th.sub); y += 24 * s; }
    // RemoteDevicePicker: Remote Mode needs Wi-Fi; a metered (mobile data) link shows the warning. Polled off the UI thread every 5 s.
    if (!remote_net_busy_ && SDL_GetTicks() - remote_net_at_ > 5000) {
        remote_net_busy_ = true; remote_net_at_ = SDL_GetTicks();
        worker_->post([this] { const bool m = network_metered(); ui_post([this, m] { remote_net_busy_ = false; if (m != remote_metered_) { remote_metered_ = m; mark(); } }); });
    }
    if (remote_metered_) { fa(0xf1eb, b.x + 28 * s, y + 10 * s, 13, th.danger); T(500, 12, b.x + 44 * s, y, 20, "Remote Mode requires a WiFi connection (not mobile data).", th.danger, b.w - 64 * s); y += 24 * s; }
    if (remote_mode_) {
        for (auto& d : devs) if (d.id == remote_target_) {
            fa(skin::Satellite, b.x + 30 * s, y + 14 * s, 13, th.accent);
            T(400, 13, b.x + 48 * s, y, 28, "Controlling: ", th.sub);
            T(600, 13, b.x + 48 * s + gfx_->measure(F(400, 13), "Controlling: "), y, 28, d.name.substr(0, d.name.find(',')), th.text);
            Rect db{b.x + b.w - 130 * s, y, 110 * s, 28 * s};
            if (u.button(db, "Disconnect")) remote_disconnect();
        }
        y += 40 * s;
    }
    if (cfg_.username.empty()) T(400, 13, b.x + 20 * s, y + 8 * s, 20, "Sign into a Navidrome account to use Remote Mode.", th.danger, b.w - 40 * s);
    else if (!devs.empty()) T(400, 13, b.x + 20 * s, y + 4 * s, 20, "Select an available device to control it remotely.", th.sub, b.w - 40 * s);
    if (!devs.empty()) y += 28 * s;
    if (devs.empty() && !cfg_.username.empty()) T(400, 13, b.x + 20 * s, y + 8 * s, 20, "Searching for Xylonic devices on your network\xE2\x80\xA6", th.sub, b.w - 40 * s);
    std::string myacc = md5hex(cfg_.username + ":" + cfg_.server_url);
    for (auto& d : devs) {
        Rect row{b.x + 8 * s, y, b.w - 16 * s, 56 * s};
        bool wrong = !d.account_id.empty() && d.account_id != myacc;
        bool busy = !d.paired_with.empty() && d.paired_with != (remote_ ? remote_->device_id() : std::string());
        bool active = remote_mode_ && d.id == remote_target_;
        if (u.hover(row) && !wrong && !active) gfx_->fill_round(row, 10 * s, th.hover);
        fa(d.platform == "android" ? 0xf3cd : 0xf390, row.x + 28 * s, row.y + 28 * s, 18, wrong ? th.muted : th.accent);
        std::string nm = d.name.substr(0, d.name.find(','));
        T(500, 14, row.x + 56 * s, row.y + 8 * s, 20, nm, wrong ? th.muted : th.text, row.w - 190 * s);
        T(400, 11, row.x + 56 * s, row.y + 30 * s, 16, d.host + " \xC2\xB7 " + (d.platform == "android" ? "Android" : "Desktop"), th.sub, row.w - 190 * s);
        const char* badge = active ? "Active" : wrong ? "Different account" : busy ? "Busy" : "";
        if (*badge) { float bw2 = gfx_->measure(F(600, 11), badge) + 20 * s; Rect pr{row.x + row.w - bw2 - 12 * s, row.y + 17 * s, bw2, 22 * s}; gfx_->fill_round(pr, 11 * s, alpha(active ? th.accent : th.text, 0.12f)); T(600, 11, pr.x + 10 * s, pr.y, 22, badge, active ? th.accent : th.sub); }
        u.tip(row, wrong ? "Signed into a different Navidrome account" : active ? "Currently connected" : busy ? "Another Xylonic may be using this device \xE2\x80\x94 tap to try anyway" : ("Control " + nm).c_str());
        if (u.clicked(row) && !wrong && !active && cfg_.control_others) remote_connect(d);
        y += 62 * s;
    }
}
