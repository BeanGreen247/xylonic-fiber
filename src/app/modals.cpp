// Theme chooser, custom theme editor and firewall setup dialogs (ThemeSelector.tsx, CustomThemeEditor.tsx, FirewallSetupDialog.tsx).
#include "app.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <unistd.h>

namespace {
Color alpha(Color c, float a) { c.a = static_cast<uint8_t>(std::lround(a * 255)); return c; }

struct FwSection { const char* id; const char* label; const char* cmds; };
const FwSection kLinux[4] = {
    {"ufw", "ufw (Ubuntu / Debian)", "sudo ufw allow 7766/udp comment 'Xylonic Remote Discovery'\nsudo ufw allow 7767/tcp comment 'Xylonic Remote Control'\nsudo ufw reload"},
    {"firewalld", "firewalld (Fedora / RHEL / Arch)", "sudo firewall-cmd --permanent --add-port=7766/udp\nsudo firewall-cmd --permanent --add-port=7767/tcp\nsudo firewall-cmd --reload"},
    {"nft", "nftables", "sudo nft add rule inet filter input udp dport 7766 accept\nsudo nft add rule inet filter input tcp dport 7767 accept"},
    {"iptables", "iptables", "sudo iptables -I INPUT -p udp --dport 7766 -j ACCEPT\nsudo iptables -I INPUT -p tcp --dport 7767 -j ACCEPT\n# Persist on Debian/Ubuntu:\nsudo iptables-save | sudo tee /etc/iptables/rules.v4\n# Persist on RHEL/Fedora:\nsudo service iptables save"},
};
const FwSection kWin[2] = {
    {"cmd", "Command Prompt (Admin)", "netsh advfirewall firewall add rule name=\"Xylonic Remote Discovery\" dir=in action=allow protocol=udp localport=7766\nnetsh advfirewall firewall add rule name=\"Xylonic Remote Control\" dir=in action=allow protocol=tcp localport=7767"},
    {"ps", "PowerShell (Admin)", "New-NetFirewallRule -DisplayName \"Xylonic Remote Discovery\" -Direction Inbound -Protocol UDP -LocalPort 7766 -Action Allow\nNew-NetFirewallRule -DisplayName \"Xylonic Remote Control\" -Direction Inbound -Protocol TCP -LocalPort 7767 -Action Allow"},
};

bool tool_installed(const char* id) {
    static const char* const dirs[] = {"/usr/sbin/", "/usr/bin/", "/sbin/", "/bin/", "/usr/local/sbin/", "/usr/local/bin/"};
    const char* bin = std::string(id) == "firewalld" ? "firewall-cmd" : std::string(id) == "nft" ? "nft" : id;
    for (const char* d : dirs) if (::access((std::string(d) + bin).c_str(), X_OK) == 0) return true;
    return false;
}

std::vector<std::string> split_lines(const std::string& s) {
    std::vector<std::string> v; size_t a = 0;
    for (;;) { size_t b = s.find('\n', a); v.push_back(s.substr(a, b == std::string::npos ? b : b - a)); if (b == std::string::npos) break; a = b + 1; }
    return v;
}
}  // namespace

int App::wrap_count(int weight, float px, const std::string& str, float max_w) {
    const int f = F(weight, px);
    int lines = 1;
    std::string line, word;
    for (size_t i = 0; i <= str.size(); ++i) {
        if (i == str.size() || str[i] == ' ') {
            if (!word.empty()) {
                const std::string cand = line.empty() ? word : line + " " + word;
                if (!line.empty() && gfx_->measure(f, cand) > max_w) { ++lines; line = word; } else line = cand;
                word.clear();
            }
        } else word += str[i];
    }
    return lines;
}

float App::TW(int weight, float px, float x, float y_top, float line_h, const std::string& str, Color c, float max_w) {
    const int f = F(weight, px);
    std::string line, word;
    float y = y_top;
    auto flush = [&]() { if (!line.empty()) { T(weight, px, x, y, line_h, line, c); y += line_h * S_; line.clear(); } };
    size_t i = 0;
    while (i <= str.size()) {
        if (i == str.size() || str[i] == ' ') {
            if (!word.empty()) {
                const std::string cand = line.empty() ? word : line + " " + word;
                if (!line.empty() && gfx_->measure(f, cand) > max_w) { flush(); line = word; } else line = cand;
                word.clear();
            }
        } else word += str[i];
        ++i;
    }
    flush();
    return y - y_top;
}

void App::draw_firewall_modal(float W, float H) {
    Ui& u = *ui_;
    const float s = S_;
    const Theme& th = u.th;
    gfx_->fill({0, 0, W, H}, {0, 0, 0, 170});
#if defined(_WIN32)
    const int plat = 2;
#elif defined(__linux__) && !defined(__ANDROID__)
    const int plat = 1;
#else
    const int plat = 0;   // the original only lists Linux and Windows commands
#endif
    const FwSection* tabs = plat == 2 ? kWin : kLinux;
    const int ntabs = plat == 2 ? 2 : plat == 1 ? 4 : 0;
    if (!fw_init_) {
        fw_init_ = true; fw_tab_ = 0;
        if (plat == 1) for (int i = 0; i < 4; ++i) if (tool_installed(kLinux[i].id)) { fw_tab_ = i; break; }
    }
    const float mw = std::min(560.0f, W / s - 24);
    // content height
    const std::string& cmds = ntabs ? tabs[fw_tab_].cmds : std::string();
    const auto lines = split_lines(cmds);
    const bool det = plat == 1 && tool_installed(tabs[fw_tab_ < 4 ? fw_tab_ : 0].id);
    const float code_h = ntabs ? 24 + 19.5f * lines.size() : 0;
    const float mh = std::min(H / s - 24, 64 + 20 + 66 + (ntabs ? 48 + (det ? 28 : 0) + code_h + 12 + 42 + 16 : 0) + 20 + 70);
    Rect b{(W - mw * s) / 2, (H - mh * s) / 2, mw * s, mh * s};
    gfx_->fill_round({b.x - 1, b.y - 1, b.w + 2, b.h + 2}, 13 * s, th.border);
    gfx_->fill_round(b, 12 * s, th.bg_light);
    fa(0xf3ed, b.x + 28 * s, b.y + 32 * s, 15, th.accent);
    T(700, 16, b.x + 44 * s, b.y + 20 * s, 24, "Firewall Setup", th.text);
    Rect cl{b.x + b.w - 52 * s, b.y + 16 * s, 32 * s, 32 * s};
    if (u.hover(cl)) gfx_->fill_round(cl, 16 * s, th.hover);
    fa(skin::Xmark, cl.x + 16 * s, cl.y + 16 * s, 14, th.sub);
    if (u.clicked(cl) || u.in.escape) { modal_ = Modal::None; fw_init_ = false; return; }
    gfx_->fill({b.x, b.y + 64 * s, b.w, 1 * s}, th.border_subtle);
    float y = b.y + 84 * s;
    const float x = b.x + 20 * s, cw = b.w - 40 * s;
    const char* l1 = "Remote mode uses UDP 7766 (LAN discovery) and TCP 7767 (command channel). Open both inbound ports on this machine so other devices on the same network can reach it.";
    TW(400, 13, x, y, 21 * 1.0f, l1, th.sub, cw);
    y += 66 * s;
    if (ntabs) {
        float tx = x, ty = y;
        for (int i = 0; i < ntabs; ++i) {
            const float tw = gfx_->measure(F(500, 12.5f), tabs[i].label) + 28 * s + (plat == 1 && tool_installed(tabs[i].id) ? 14 * s : 0);
            if (tx + tw > x + cw) { tx = x; ty += 36 * s; }
            Rect t{tx, ty, tw, 30 * s};
            const bool on = fw_tab_ == i;
            gfx_->fill_round(t, 15 * s, on ? alpha(th.accent, 0.18f) : (u.hover(t) ? th.hover : alpha(th.text, 0.05f)));
            gfx_->stroke_round(t, 15 * s, 1 * s, on ? th.accent : alpha(th.text, 0.12f));
            T(500, 12.5f, t.x + 14 * s, t.y, 30, tabs[i].label, on ? th.accent : th.sub);
            if (plat == 1 && tool_installed(tabs[i].id)) gfx_->circle(t.x + t.w - 12 * s, t.y + 15 * s, 3 * s, rgb(0x1db954));
            u.tip(t, plat == 1 && tool_installed(tabs[i].id) ? "Detected on this system" : "");
            if (u.clicked(t)) fw_tab_ = i;
            tx += tw + 8 * s;
        }
        y = ty + 42 * s;
        if (det) { fa(0xf058, x + 7 * s, y + 9 * s, 12, rgb(0x1db954)); T(500, 12, x + 22 * s, y, 18, "Detected on this system", rgb(0x1db954)); y += 28 * s; }
        Rect code{x, y, cw, code_h * s};
        gfx_->fill_round(code, 8 * s, th.bg);
        gfx_->stroke_round(code, 8 * s, 1 * s, th.border_subtle);
        gfx_->clip(&code);
        for (size_t i = 0; i < lines.size(); ++i) T(400, 12, code.x + 12 * s, code.y + (12 + 19.5f * i) * s, 19.5f, lines[i], rgb(0xdbdbdb));
        gfx_->clip(nullptr);
        y += code.h + 12 * s;
        const bool copied = fw_copied_until_ > SDL_GetTicks();
        const std::string cl_ = copied ? "Copied!" : "Copy commands";
        const float bw = gfx_->measure(F(500, 13), cl_) + 52 * s;
        Rect cb{x, y, bw, 34 * s};
        gfx_->fill_round(cb, 8 * s, u.hover(cb) ? alpha(th.text, 0.12f) : alpha(th.text, 0.06f));
        fa(copied ? 0xf00c : 0xf0c5, cb.x + 20 * s, cb.y + 17 * s, 13, copied ? rgb(0x1db954) : th.sub);
        T(500, 13, cb.x + 36 * s, cb.y, 34, cl_, th.text);
        if (u.clicked(cb)) { SDL_SetClipboardText(cmds.c_str()); fw_copied_until_ = SDL_GetTicks() + 2000; }
        if (copied) { u.wants_frames = true; }
        y += 52 * s;
    }
    const char* note = plat == 1 ? "Run the commands in a terminal. If none of the above tools are installed, your distribution may not have a firewall enabled by default \xE2\x80\x94 remote mode should already work."
                     : plat == 2 ? "Open an elevated prompt: press Win+R, type cmd or powershell, then Ctrl+Shift+Enter to run as Administrator. Alternatively, search Windows Defender Firewall with Advanced Security and add two inbound rules manually (UDP 7766 and TCP 7767)."
                                 : "Allow this app on your private network in the system firewall if remote devices cannot find it.";
    TW(400, 12, x, y, 18.5f, note, th.sub, cw);
}

void App::draw_theme_modal(float W, float H) {
    Ui& u = *ui_;
    const float s = S_;
    const Theme& th = u.th;
    gfx_->fill({0, 0, W, H}, {0, 0, 0, 170});
    const float mw = std::min(680.0f, W / s * 0.92f), mhmax = H / s * 0.8f;
    const float inner = mw - 36;
    const int cols = std::max(1, static_cast<int>((inner + 16) / (140 + 16)));
    const float cardw = (inner - 16 * (cols - 1)) / cols, cardh = 153;
    const int prow = (kPresetCount + cols - 1) / cols, crow = (kCustomSlots + cols - 1) / cols;
    const float content = 18 + (21 + 16 + prow * cardh + (prow - 1) * 16 + 32) + (21 + 16 + crow * cardh + (crow - 1) * 16 + 32) + 64 + 18 - 12;
    const float mh = std::min(mhmax, 65 + content);
    Rect b{(W - mw * s) / 2, (H - mh * s) / 2, mw * s, mh * s};
    gfx_->fill_round({b.x - 1, b.y - 1, b.w + 2, b.h + 2}, 13 * s, th.border);
    gfx_->fill_round(b, 12 * s, th.bg_light);
    fa(0xf53f, b.x + 25 * s, b.y + 33 * s, 14, th.accent);
    T(700, 16, b.x + 40 * s, b.y + 20 * s, 26, "Choose Theme", th.text);
    Rect cl{b.x + b.w - 50 * s, b.y + 17 * s, 32 * s, 32 * s};
    fa(skin::Xmark, cl.x + 16 * s, cl.y + 16 * s, 14, u.hover(cl) ? th.text : th.sub);
    gfx_->fill({b.x, b.y + 64 * s, b.w, 1 * s}, th.border_subtle);
    Rect area{b.x, b.y + 65 * s, b.w, b.h - 65 * s};
    if (u.hover(area)) theme_scroll_.y -= u.in.wheel * 60 * s;
    theme_scroll_.clamp(content * s, area.h);
    gfx_->clip(&area);
    float y = area.y + 18 * s - theme_scroll_.y;
    const float x0 = b.x + 18 * s;
    const bool in_area = true;
    (void)in_area;
    auto grid = [&](const char* title, int first, int count, bool custom) {
        gfx_->set_letter_spacing(0.5f * s);
        T(600, 14, x0, y, 21, title, th.sub);
        gfx_->set_letter_spacing(0);
        y += 37 * s;
        for (int i = 0; i < count; ++i) {
            const int idx = first + i;
            const ThemeDef d = theme_def(idx);
            const int col = i % cols, row = i / cols;
            Rect c{x0 + col * (cardw + 16) * s, y + row * (cardh + 16) * s, cardw * s, cardh * s};
            const bool active = cfg_.theme == idx, hov = u.hover(c) && area.contains(u.in.mx, u.in.my);
            Rect cc = c;
            if (hov) cc.y -= 6 * s;
            gfx_->fill_round(cc, 12 * s, active ? alpha(th.accent, 0.12f) : hov ? th.bg_medium : th.bg_light);
            if (active) gfx_->fill_round(cc, 12 * s, alpha(th.accent, 0.10f));
            gfx_->stroke_round(cc, 12 * s, 2 * s, active || hov ? th.accent : alpha(th.text, 0.10f));
            Rect pv{cc.x + (cc.w - 80 * s) / 2, cc.y + 20 * s, 80 * s, 80 * s};
            gfx_->fill_round(pv, 8 * s, rgb(d.primary));
            if (active) fa(0xf00c, pv.x + pv.w / 2, pv.y + pv.h / 2, 32, rgb(0xffffff));
            const float nw = gfx_->measure(F(500, 14), d.name);
            T(500, 14, cc.x + std::max(8 * s, (cc.w - nw) / 2), cc.y + 112 * s, 21, d.name, th.text, cc.w - 16 * s);
            bool reset_hit = false;
            if (custom) {
                Rect rb{cc.x + cc.w - 30 * s, cc.y + 6 * s, 24 * s, 24 * s};
                u.tip(rb, "Reset to default");
                if (hov) {
                    gfx_->circle(rb.x + 12 * s, rb.y + 12 * s, 12 * s, u.hover(rb) ? alpha(th.danger, 0.9f) : alpha(th.text, 0.12f));
                    fa(skin::Xmark, rb.x + 12 * s, rb.y + 12 * s, 11, u.hover(rb) ? rgb(0xffffff) : th.sub);
                }
                if (hov && u.clicked(rb)) {
                    const int slot = idx - kPresetCount;
                    cfg_.custom[slot] = {"My Theme " + std::to_string(slot + 1), 0x00bcd4};
                    if (cfg_.theme == idx) cfg_.theme = 0;
                    edit_loaded_ = false; apply_theme(); cfg_.save(cfg_path_); reset_hit = true;
                }
            }
            if (!reset_hit && hov && u.clicked(c)) { cfg_.theme = idx; apply_theme(); cfg_.save(cfg_path_); }
        }
        const int rows = (count + cols - 1) / cols;
        y += (rows * cardh + (rows - 1) * 16 + 32) * s;
    };
    grid("PRESET THEMES", 0, kPresetCount, false);
    grid("CUSTOM THEMES", kPresetCount, kCustomSlots, true);
    Rect eb{x0, y - 12 * s, b.w - 36 * s, 64 * s};
    gfx_->quad_gradient(eb, th.primary_dark, th.accent, th.accent, th.primary_dark);
    if (u.hover(eb)) gfx_->fill(eb, {255, 255, 255, 18});
    const std::string el = "Edit Custom Themes";
    const float ew = gfx_->measure(F(700, 16), el) + 28 * s;
    fa(0xf53f, eb.x + (eb.w - ew) / 2 + 8 * s, eb.y + eb.h / 2, 16, rgb(0xffffff));
    T(700, 16, eb.x + (eb.w - ew) / 2 + 28 * s, eb.y, 64, el, rgb(0xffffff));
    if (u.clicked(eb) && area.contains(u.in.mx, u.in.my)) { modal_ = Modal::ThemeEdit; edit_loaded_ = false; gfx_->clip(nullptr); return; }
    gfx_->clip(nullptr);
    if (u.clicked(cl) || u.in.escape || (u.in.pressed && !b.contains(u.in.mx, u.in.my))) modal_ = Modal::None;
}

void App::draw_theme_editor(float W, float H) {
    Ui& u = *ui_;
    const float s = S_;
    const Theme& th = u.th;
    gfx_->fill({0, 0, W, H}, {0, 0, 0, 170});
    const float mw = std::min(520.0f, W / s - 24), mh = std::min(H / s - 24, 560.0f);
    Rect b{(W - mw * s) / 2, (H - mh * s) / 2, mw * s, mh * s};
    gfx_->fill_round({b.x - 1, b.y - 1, b.w + 2, b.h + 2}, 13 * s, th.border);
    gfx_->fill_round(b, 12 * s, th.bg_light);
    fa(0xf1fc, b.x + 25 * s, b.y + 33 * s, 14, th.accent);
    T(700, 16, b.x + 40 * s, b.y + 20 * s, 26, "Create Custom Theme", th.text);
    Rect cl{b.x + b.w - 50 * s, b.y + 17 * s, 32 * s, 32 * s};
    fa(skin::Xmark, cl.x + 16 * s, cl.y + 16 * s, 14, u.hover(cl) ? th.text : th.sub);
    if (u.clicked(cl) || u.in.escape) { modal_ = Modal::None; u.focus = 0; return; }
    gfx_->fill({b.x, b.y + 64 * s, b.w, 1 * s}, th.border_subtle);
    if (!edit_loaded_) {
        edit_name_ = cfg_.custom[edit_slot_].name;
        char hx[16]; std::snprintf(hx, sizeof hx, "#%06x", cfg_.custom[edit_slot_].primary);
        edit_hex_ = hx; edit_loaded_ = true;
    }
    const float x = b.x + 24 * s, cw = b.w - 48 * s;
    float y = b.y + 84 * s;
    T(600, 13, x, y, 20, "Theme Slot:", th.text);
    y += 28 * s;
    {
        const float sw = (cw - 3 * 8 * s) / 4;
        for (int i = 0; i < kCustomSlots; ++i) {
            Rect sb{x + i * (sw + 8 * s), y, sw, 36 * s};
            const bool on = edit_slot_ == i;
            gfx_->fill_round(sb, 8 * s, on ? alpha(th.accent, 0.18f) : (u.hover(sb) ? th.hover : alpha(th.text, 0.05f)));
            gfx_->stroke_round(sb, 8 * s, 1 * s, on ? th.accent : alpha(th.text, 0.12f));
            const std::string& nm = cfg_.custom[i].name;
            const float nw = std::min(gfx_->measure(F(500, 12.5f), nm), sb.w - 12 * s);
            T(500, 12.5f, sb.x + (sb.w - nw) / 2, sb.y, 36, nm, on ? th.accent : th.sub, sb.w - 12 * s);
            if (u.clicked(sb)) { edit_slot_ = i; edit_loaded_ = false; }
        }
        y += 56 * s;
    }
    T(600, 13, x, y, 20, "Theme Name:", th.text);
    y += 26 * s;
    u.text_field(kEditName, {x, y, cw, 40 * s}, edit_name_, "My Awesome Theme");
    y += 58 * s;
    T(600, 13, x, y, 20, "Primary Color:", th.text);
    y += 26 * s;
    unsigned hexv = 0;
    const bool valid = edit_hex_.size() == 7 && edit_hex_[0] == '#' && std::sscanf(edit_hex_.c_str() + 1, "%6x", &hexv) == 1;
    u.text_field(kEditHex, {x, y, 160 * s, 40 * s}, edit_hex_, "#00bcd4");
    gfx_->fill_round({x + 172 * s, y, 40 * s, 40 * s}, 8 * s, valid ? rgb(hexv) : alpha(th.text, 0.1f));
    y += 48 * s;
    T(400, 11.5f, x, y, 17, "Other colors will be generated automatically", th.sub);
    y += 34 * s;
    T(600, 14, x, y, 22, "Preview", th.text);
    y += 32 * s;
    Rect sw{x, y, 120 * s, 56 * s};
    gfx_->fill_round(sw, 8 * s, valid ? rgb(hexv) : alpha(th.text, 0.1f));
    T(600, 13, sw.x + (sw.w - gfx_->measure(F(600, 13), "Primary")) / 2, sw.y, 56, "Primary", rgb(0xffffff));
    const float by = b.y + b.h - 62 * s;
    gfx_->fill({b.x, by - 12 * s, b.w, 1 * s}, th.border_subtle);
    auto apply = [&]() {
        cfg_.custom[edit_slot_] = {edit_name_.empty() ? "My Theme " + std::to_string(edit_slot_ + 1) : edit_name_, hexv};
        cfg_.theme = kPresetCount + edit_slot_;
        apply_theme(); cfg_.save(cfg_path_);
    };
    Rect pb{x, by, 150 * s, 42 * s}, sb{b.x + b.w - 24 * s - 190 * s, by, 190 * s, 42 * s};
    gfx_->fill_round(pb, 8 * s, u.hover(pb) ? alpha(th.text, 0.12f) : alpha(th.text, 0.06f));
    fa(0xf06e, pb.x + 28 * s, pb.y + 21 * s, 14, th.sub);
    T(600, 14, pb.x + 46 * s, pb.y, 42, "Preview", th.text);
    gfx_->fill_round(sb, 8 * s, u.hover(sb) ? th.primary_light : th.accent);
    fa(0xf0c7, sb.x + 28 * s, sb.y + 21 * s, 14, rgb(0xffffff));
    T(700, 14, sb.x + 48 * s, sb.y, 42, "Save & Apply", rgb(0xffffff));
    if (valid && u.clicked(pb)) apply();
    if (valid && u.clicked(sb)) { apply(); modal_ = Modal::None; u.focus = 0; }
}

// SplashScreen.tsx: logo and name on the dark background while the stored login is checked, then a 450 ms fade.
void App::draw_splash(float W, float H) {
    if (splash_gone_) return;
    const uint32_t now = SDL_GetTicks();
    if (!splash_start_) splash_start_ = now;
    if (!connecting_ && !splash_end_ && now - splash_start_ >= 300) splash_end_ = now;
    float a = 1.0f;
    if (splash_end_) { a = 1.0f - (now - splash_end_) / 450.0f; if (a <= 0) { splash_gone_ = true; mark(); return; } }
    const float s = S_;
    const float enter = std::min(1.0f, (now - splash_start_) / 300.0f);
    const float sc = 0.92f + 0.08f * enter, ca = a * enter;
    const Color bg = ui_->th.bg;
    gfx_->fill({0, 0, W, H}, {bg.r, bg.g, bg.b, static_cast<uint8_t>(a * 255)});
    const float logo = 96 * s * sc, name_h = 44 * s * sc, gap = 24 * s * sc;
    const float top = (H - (logo + gap + name_h)) / 2;
    svg(0, {(W - logo) / 2, top, logo, logo}, {255, 255, 255, static_cast<uint8_t>(ca * 255)});
    const int f = F(700, 36 * sc);
    gfx_->set_letter_spacing(3 * s);
    const std::string nm = "XYLONIC";
    const float nw = gfx_->measure(f, nm);
    Color ac = ui_->th.accent; ac.a = static_cast<uint8_t>(ca * 255);
    T(700, 36 * sc, (W - nw) / 2, top + logo + gap, 44 * sc, nm, ac);
    gfx_->set_letter_spacing(0);
    mark();
}
