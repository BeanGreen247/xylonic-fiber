// Login screen (LoginForm.tsx): gradient page, 1100px max box, discovered servers, previous connections picker, offline
// toggle, server/user/password fields, Test Connection, Login. Login stays disabled until a test succeeds (online mode).
#include <algorithm>
#include <cmath>
#include "app.h"
#include "../net/discovery.h"
#include "../net/url.h"
#include "secrets.h"

namespace {
Color alpha(Color c, float a) { c.a = static_cast<uint8_t>(std::lround(a * 255)); return c; }
}  // namespace

void App::start_scan() {
    if (scan_cancel_) scan_cancel_->store(true);  // stop the previous scan
    discovered_.clear();
    scanning_ = true;
    uint32_t gen = ++scan_gen_;
    auto cancel = std::make_shared<std::atomic<bool>>(false);
    scan_cancel_ = cancel;
    // Detached on purpose: a scan can sit in a 2.5 s ping, and quitting must not wait for it (main() ends with _Exit).
    std::thread([this, gen, cancel] {
        discover_servers([this, gen](const DiscoveredServer& d) { ui_post([this, gen, d] { if (gen == scan_gen_) { discovered_.push_back(d); mark(); } }); }, *cancel);
        if (!cancel->load()) ui_post([this, gen] { if (gen == scan_gen_) { scanning_ = false; mark(); } });
    }).detach();
    mark();
}

void App::test_connection() {
    if (testing_) return;
    if (f_url_.empty() || f_user_.empty() || f_pass_.empty()) { test_ok_ = false; login_msg_ = "Please fill in all fields"; mark(); return; }
    testing_ = true;
    test_ok_ = false;
    cert_prompt_ = false;
    login_msg_.clear();
    Url u;
    std::string url = f_url_;
    if (url.find("://") == std::string::npos) url = "http://" + url;
    if (!parse_url(url, u)) { testing_ = false; login_msg_ = "Invalid server URL"; mark(); return; }
    ConnectOptions co;
    co.timeout_ms = 10000;
    co.pin_sha256 = login_pin_;
    uint32_t sess = ++session_;
    auto client = std::make_shared<SubsonicClient>(url, f_user_, f_pass_, co);
    worker_->post([this, client, sess, u, co] {
        std::string err;
        bool ok = client->ping(err);
        std::string fp;
        if (!ok && u.scheme == "https" && co.pin_sha256.empty() && err.find("TLS handshake") != std::string::npos) {
            std::string e2;
            if (!fetch_cert_fingerprint(u.host, u.port, 5000, fp, e2)) fp.clear();
        }
        ui_post([this, sess, ok, err, fp] {
            if (sess != session_) return;
            testing_ = false;
            test_ok_ = ok;
            if (ok) login_msg_ = "Connection successful! You can now login.";
            else if (!fp.empty()) { cert_fp_ = fp; cert_prompt_ = true; login_msg_ = "This server's certificate is not trusted."; }
            else login_msg_ = err.empty() ? "Connection failed" : err;
            mark();
        });
    });
    mark();
}

void App::select_connection(int idx) {
    sel_conn_ = idx;
    picker_open_ = false;
    test_ok_ = false; login_msg_.clear(); cert_prompt_ = false; login_pin_.clear();
    if (idx < 0) { f_url_.clear(); f_user_.clear(); f_pass_.clear(); return; }
    const Config::Conn& c = cfg_.conns[static_cast<size_t>(idx)];
    f_url_ = c.url; f_user_ = c.user;
    f_pass_.clear();
    if (c.url == cfg_.server_url) login_pin_ = cfg_.pin_sha256;
    std::string saved;
    if (offline_check_ && secret_lookup(c.url + "|" + c.user, saved)) { f_pass_ = saved; test_ok_ = true; login_msg_ = "Offline mode: Using cached data and stored credentials"; }
    else if (offline_check_) login_msg_ = secret_available() ? "Cache available but no stored credentials found. Please enter your password." : "Cache available but secure storage not available. Please enter your password.";
}

void App::draw_login(float W, float H) {
    Ui& u = *ui_;
    const float s = S_;
    const Theme& th = u.th;
    // page: 135deg gradient primary-dark -> primary
    Color c0 = th.primary_dark, c1 = th.accent, cm{static_cast<uint8_t>((c0.r + c1.r) / 2), static_cast<uint8_t>((c0.g + c1.g) / 2), static_cast<uint8_t>((c0.b + c1.b) / 2), 255};
    gfx_->quad_gradient({0, 0, W, H}, c0, cm, c1, cm);
    bool two = W / s > 800;
    float bw = std::min(1100.0f * s, W - 40 * s);
    float pad = (two ? 28 : 24) * s;
    // content height (logical) to centre the box vertically
    float disc_h = 0;
    {
        size_t n = std::min<size_t>(discovered_.size(), 6);
        disc_h = 23 + 8 + (n ? n * 50 + (n - 1) * 8 : (scanning_ ? 0 : 0)) + 16;
    }
    float cols_h = two ? 259 : 520;
    float bh = (28 + 48 + 20 + disc_h + cols_h + 16 + 41 + 8 + 41 + 28) * s;
    if (u.in.wheel != 0) login_scroll_.y -= u.in.wheel * 60 * s;
    login_scroll_.clamp(bh + 40 * s, H);
    Rect box{(W - bw) / 2, std::max(20 * s, (H - bh) / 2) - login_scroll_.y, bw, bh};
    for (int i = 3; i >= 1; --i) gfx_->fill_round({box.x - i * 2 * s, box.y + i * 3 * s, box.w + 2 * i * 2 * s, box.h}, 24 * s + i * 2 * s, {0, 0, 0, 9});  // elevation-4
    gfx_->fill_round(box, 24 * s, th.panel);
    float x = box.x + pad, w = box.w - 2 * pad, y = box.y + 28 * s;
    // logo + title
    int ft = F(700, 28);
    std::string title = "Xylonic Fiber";
    T(700, 28, box.x + (box.w - gfx_->measure(ft, title)) / 2, y + 8 * s, 42, title, th.accent);
    y += 68 * s;
    // discovered servers
    {
        fa(skin::Search, x + 8 * s, y + 11.5f * s, 13, th.accent);
        std::string t = scanning_ ? "Looking for servers\xE2\x80\xA6" : !discovered_.empty() ? "Servers found" : "No servers found nearby";
        T(400, 13, x + 22 * s, y, 23, t, th.text);
        Rect rs{x + w - 70 * s, y, 70 * s, 23 * s};
        gfx_->stroke_round(rs, 11.5f * s, 1 * s, {255, 255, 255, 54});
        if (u.hover(rs) && !scanning_) gfx_->fill_round(rs, 11.5f * s, th.hover);
        T(400, 13, rs.x + (rs.w - gfx_->measure(F(400, 13), "Rescan")) / 2, rs.y, 23, "Rescan", scanning_ ? th.muted : th.sub);
        if (u.clicked(rs) && !scanning_) start_scan();
        y += 31 * s;
        size_t n = std::min<size_t>(discovered_.size(), 6);
        for (size_t i = 0; i < n; ++i) {
            const DiscoveredServer& d = discovered_[i];
            Rect it{x, y, w, 50 * s};
            bool active = f_url_ == d.url;
            gfx_->fill_round(it, 12 * s, th.bg_light);
            gfx_->stroke_round(it, 12 * s, 1 * s, active ? th.accent : alpha(th.text, 0.12f));
            if (u.hover(it)) gfx_->fill_round(it, 12 * s, th.hover);
            T(600, 13.33f, it.x + 15 * s, it.y + 9 * s, 15, d.name, th.text, it.w - 30 * s);
            std::string meta = d.type + (d.version.empty() ? "" : " " + d.version) + " \xC2\xB7 " + d.via + " \xC2\xB7 " + d.url;
            T(400, 13, it.x + 15 * s, it.y + 26 * s, 15, meta, rgb(0xb5b5b5), it.w - 30 * s);
            if (u.clicked(it)) { f_url_ = d.url; test_ok_ = false; login_msg_.clear(); login_pin_.clear(); }
            y += 58 * s;
        }
        y += (n ? 0 : 0) * s + 16 * s - (n ? 8 * s : 0);
    }
    // columns
    float colw = two ? (w - 24 * s) / 2 : w;
    float lx = x, rx = two ? x + colw + 24 * s : x;
    float ly = y, ry = two ? y : y + 160 * s + 20 * s;
    auto label = [&](float px, float py, uint32_t ic, const char* text) { fa(ic, px + 8 * s, py + 10 * s, 13, th.sub); T(600, 13, px + 22 * s, py, 20, text, th.sub); };
    auto small = [&](float px, float py, const std::string& text) { T(400, 11, px, py, 17, text, rgb(0xb5b5b5), colw); };
    Color edge = alpha(th.primary_light, 0.464f);
    // left: previous connections
    label(lx, ly, skin::History, "Previous Connections");
    Rect trig{lx, ly + 24 * s, colw, 41 * s};
    gfx_->fill_round(trig, 20.5f * s, th.panel);
    gfx_->stroke_round(trig, 20.5f * s, 1 * s, edge);
    if (u.hover(trig)) gfx_->fill_round(trig, 20.5f * s, th.hover);
    fa(0xf1e6, trig.x + 24 * s, trig.y + 20.5f * s, 15, th.accent);
    std::string cur = sel_conn_ >= 0 && sel_conn_ < static_cast<int>(cfg_.conns.size()) ? cfg_.conns[static_cast<size_t>(sel_conn_)].user + "@" + cfg_.conns[static_cast<size_t>(sel_conn_)].url.substr(cfg_.conns[static_cast<size_t>(sel_conn_)].url.find("://") + 3) : "New Connection";
    T(400, 15, trig.x + 40 * s, trig.y, 41, cur, th.text, trig.w - 80 * s);
    fa(skin::ChevDown, trig.x + trig.w - 22 * s, trig.y + 20.5f * s, 11, th.text);
    if (u.clicked(trig)) { picker_open_ = !picker_open_; mark(); }
    small(lx, ly + 69 * s, "Select a saved connection or create a new one");
    // offline toggle
    float oy = ly + 108 * s;
    Rect cb{lx, oy + 2 * s, 18 * s, 18 * s};
    gfx_->fill_round(cb, 4 * s, offline_check_ ? th.accent : th.bg_light);
    gfx_->stroke_round(cb, 4 * s, 1 * s, offline_check_ ? th.accent : alpha(th.text, 0.3f));
    if (offline_check_) fa(skin::Check, cb.x + 9 * s, cb.y + 9 * s, 12, th.accent_text);
    fa(skin::Wifi, lx + 36 * s + 10 * s, oy + 11.5f * s, 16, th.muted);
    T(600, 15, lx + 66 * s, oy, 23, "Enter Offline Mode", th.sub);
    if (u.clicked({lx, oy, colw, 23 * s})) { offline_check_ = !offline_check_; test_ok_ = false; login_msg_.clear(); }
    small(lx, oy + 27 * s, secret_available() ? "Use cached data and stored credentials without a network" : "Secure storage not available on this system");
    // right: server, user, password
    // Keep the focused field above the on-screen keyboard (phones shrink the window while it is shown): scroll once per focus or height change.
    if (u.focus != kUrl && u.focus != kUser && u.focus != kPass) login_revealed_ = 0;
    auto reveal = [&](int id, const Rect& f) {
        if (u.focus != id) return;
        if (login_revealed_ == id && login_last_h_ == H) return;
        login_revealed_ = id; login_last_h_ = H;
        const float over = f.y + f.h + 72 * s - H;
        if (over > 0) login_scroll_.y += over;
        else if (f.y < 8 * s) login_scroll_.y += f.y - 8 * s;
        mark();
    };
    label(rx, ry, 0xf233, "Server URL");
    std::string before = f_url_;
    {
        Rect f{rx, ry + 24 * s, colw, 41 * s};
        gfx_->fill_round(f, 20.5f * s, th.panel); gfx_->stroke_round(f, 20.5f * s, 1 * s, u.focus == kUrl ? th.accent : edge);
        reveal(kUrl, f);
        u.text_field(kUrl, f, f_url_, "http://192.168.1.100:4040", false, false, 15 * s, F(400, 15));
    }
    small(rx, ry + 69 * s, "Include http:// or https:// and port number");
    label(rx, ry + 100 * s, 0xf007, "Username");
    { Rect f{rx, ry + 124 * s, colw, 41 * s}; gfx_->fill_round(f, 20.5f * s, th.panel); gfx_->stroke_round(f, 20.5f * s, 1 * s, u.focus == kUser ? th.accent : edge);
      reveal(kUser, f);
      u.text_field(kUser, f, f_user_, "Your username", false, false, 15 * s, F(400, 15)); }
    label(rx, ry + 179 * s, skin::Lock, "Password");
    { Rect f{rx, ry + 204 * s, colw, 41 * s}; gfx_->fill_round(f, 20.5f * s, th.panel); gfx_->stroke_round(f, 20.5f * s, 1 * s, u.focus == kPass ? th.accent : edge);
      reveal(kPass, f);
      u.text_field(kPass, f, f_pass_, "Your password", !show_pass_, false, 15 * s, F(400, 15));
      Rect eye{f.x + f.w - 40 * s, f.y + 9 * s, 31 * s, 23 * s};
      fa(show_pass_ ? 0xf070 : 0xf06e, eye.x + 15 * s, eye.y + 11.5f * s, 15, th.sub);
      if (u.clicked(eye)) show_pass_ = !show_pass_; }
    if (f_url_ != before) { test_ok_ = false; }
    if (u.in.tab) u.focus = u.focus == kUrl ? kUser : u.focus == kUser ? kPass : kUrl;
    float by = (two ? std::max(ly + 160 * s, ry + 259 * s) : ry + 259 * s) + 16 * s;
    // result message
    if (!login_msg_.empty()) {
        Color mc = test_ok_ ? rgb(0x4caf50) : th.danger;
        T(500, 13, x, by - 6 * s, 20, login_msg_, mc, w);
        by += 18 * s;
    }
    if (cert_prompt_) {
        T(400, 11, x, by - 6 * s, 16, "SHA-256 " + cert_fp_, th.sub, w);
        if (u.button({x, by + 12 * s, 260 * s, 30 * s}, "Trust this certificate")) { login_pin_ = cert_fp_; cert_prompt_ = false; test_connection(); }
        by += 46 * s;
    }
    // buttons
    Rect tb{x, by, w, 41 * s};
    bool can_test = !testing_ && !offline_check_;
    gfx_->stroke_round(tb, 20.5f * s, 1 * s, alpha(th.text, can_test ? 0.35f : 0.18f));
    if (can_test && u.hover(tb)) gfx_->fill_round(tb, 20.5f * s, th.hover);
    std::string tl = testing_ ? "Testing..." : "Test Connection";
    float tw = gfx_->measure(F(600, 15), tl) + 34 * s;
    fa(0xf1e6, tb.x + (tb.w - tw) / 2 + 9 * s, tb.y + 20.5f * s, 15, can_test ? th.text : th.muted);
    T(600, 15, tb.x + (tb.w - tw) / 2 + 34 * s, tb.y, 41, tl, can_test ? th.text : th.muted);
    if (can_test && u.clicked(tb)) test_connection();
    Rect lb{x, by + 49 * s, w, 41 * s};
    bool can_login = (offline_check_ ? !f_url_.empty() && !f_user_.empty() : test_ok_) && !connecting_;
    gfx_->fill_round(lb, 20.5f * s, can_login ? (u.hover(lb) ? th.primary_light : th.accent) : alpha(th.accent, 0.45f));
    std::string ll = connecting_ ? "Logging in..." : "Login";
    float lw2 = gfx_->measure(F(600, 15), ll) + 34 * s;
    Color lc = can_login ? th.accent_text : alpha(th.accent_text, 0.6f);
    fa(0xf2f6, lb.x + (lb.w - lw2) / 2 + 9 * s, lb.y + 20.5f * s, 15, lc);
    T(600, 15, lb.x + (lb.w - lw2) / 2 + 34 * s, lb.y, 41, ll, lc);
    if ((u.clicked(lb) || (u.in.enter && u.focus != 0)) && !connecting_) {
        if (offline_check_) { cfg_.server_url = f_url_; cfg_.username = f_user_; enter_offline(); }
        else if (!test_ok_) { login_msg_ = "Please test connection first"; test_ok_ = false; }
        else connect(false);
    }
    // previous connections picker (modal-like popover)
    if (picker_open_) {
        float ph = 40 * s + (cfg_.conns.size() + 1) * 52 * s;
        Rect pb{trig.x, trig.y + trig.h + 6 * s, colw, std::min(ph, 340 * s)};
        gfx_->fill_round({pb.x - 1, pb.y - 1, pb.w + 2, pb.h + 2}, 13 * s, th.border);
        gfx_->fill_round(pb, 12 * s, th.bg_light);
        float iy = pb.y + 6 * s;
        auto item = [&](int idx, const std::string& title, const std::string& sub) {
            Rect ir{pb.x + 6 * s, iy, pb.w - 12 * s, 48 * s};
            if (u.hover(ir)) gfx_->fill_round(ir, 8 * s, th.hover);
            if (sel_conn_ == idx) gfx_->fill_round(ir, 8 * s, alpha(th.accent, 0.12f));
            fa(idx < 0 ? static_cast<uint32_t>(skin::CirclePlus) : 0xf1e6, ir.x + 22 * s, ir.y + 24 * s, 15, th.accent);
            T(500, 14, ir.x + 44 * s, ir.y + 5 * s, 20, title, th.text, ir.w - 120 * s);
            if (!sub.empty()) T(400, 11, ir.x + 44 * s, ir.y + 26 * s, 16, sub, rgb(0xb5b5b5), ir.w - 120 * s);
            bool clicked_item = u.clicked(ir);
            if (idx >= 0) {
                Rect del{ir.x + ir.w - 34 * s, ir.y + 12 * s, 24 * s, 24 * s};
                if (u.hover(del)) gfx_->fill_round(del, 12 * s, th.hover);
                fa(skin::Trash, del.x + 12 * s, del.y + 12 * s, 12, u.hover(del) ? th.danger : th.sub);
                if (u.clicked(del)) {
                    const Config::Conn c = cfg_.conns[static_cast<size_t>(idx)];
                    cfg_.forget_connection(c.url, c.user);
                    secret_clear(c.url + "|" + c.user);
                    cfg_.save(cfg_path_);
                    if (sel_conn_ == idx) sel_conn_ = -1;
                    iy += 52 * s;
                    return false;
                }
            }
            iy += 52 * s;
            return clicked_item;
        };
        if (item(-1, "New Connection", "")) select_connection(-1);
        for (size_t i = 0; i < cfg_.conns.size(); ++i) {
            const Config::Conn& c = cfg_.conns[i];
            if (item(static_cast<int>(i), c.user + "@" + c.url.substr(c.url.find("://") + 3), c.url)) { select_connection(static_cast<int>(i)); break; }
        }
        if (u.in.pressed && !pb.contains(u.in.mx, u.in.my) && !trig.contains(u.in.mx, u.in.my)) picker_open_ = false;
    }
}
