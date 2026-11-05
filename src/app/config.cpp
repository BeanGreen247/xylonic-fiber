#include "config.h"
#include <algorithm>
#include <ctime>
#include <cstdio>
#include "../../third_party/yyjson.h"

bool Config::load(const std::string& path) {
    yyjson_doc* d = yyjson_read_file(path.c_str(), 0, nullptr, nullptr);
    if (!d) return false;
    yyjson_val* r = yyjson_doc_get_root(d);
    auto str = [&](const char* k) { yyjson_val* v = yyjson_obj_get(r, k); return v && yyjson_is_str(v) ? std::string(yyjson_get_str(v)) : std::string(); };
    auto num = [&](const char* k, int def) { yyjson_val* v = yyjson_obj_get(r, k); return v && yyjson_is_num(v) ? static_cast<int>(yyjson_get_sint(v)) : def; };
    server_url = str("server_url"); username = str("username"); pin_sha256 = str("pin_sha256"); cache_root = str("cache_root");
    volume_pct = num("volume_pct", 80); stream_quality = num("stream_quality", 0);
    theme = num("theme", 0); theme_mode = num("theme_mode", 0);
    download_quality = num("download_quality", 320); concurrent_downloads = std::min(8, std::max(1, num("concurrent_downloads", 3)));
    default_view = num("default_view", 0); car_minimal = num("car_minimal", 0); perf_mode = num("perf_mode", 0);
    offline_mode = num("offline_mode", 0) != 0;
    car_keep_on = num("car_keep_on", 1) != 0; car_start = num("car_start", 0) != 0; car_pulse = num("car_pulse", 0) != 0;
    debug_log = num("debug_log", 0) != 0; render_timer = num("render_timer", 0) != 0;
    auto_offline_metered = num("auto_offline_metered", 0) != 0;
    be_controlled = num("be_controlled", 1) != 0; control_others = num("control_others", 1) != 0;
    if (theme < 0 || theme > 11) theme = 0;
    if (theme_mode < 0 || theme_mode > 2) theme_mode = 0;
    size_t ci, cn; yyjson_val* cc;
    conns.clear();
    yyjson_arr_foreach(yyjson_obj_get(r, "connections"), ci, cn, cc) {
        Conn c;
        yyjson_val* u = yyjson_obj_get(cc, "url"); yyjson_val* us = yyjson_obj_get(cc, "user"); yyjson_val* lu = yyjson_obj_get(cc, "last_used");
        if (u && yyjson_is_str(u)) c.url = yyjson_get_str(u);
        if (us && yyjson_is_str(us)) c.user = yyjson_get_str(us);
        if (lu && yyjson_is_num(lu)) c.last_used = static_cast<int64_t>(yyjson_get_sint(lu));
        if (!c.url.empty() && !c.user.empty()) conns.push_back(std::move(c));
    }
    size_t i, n; yyjson_val* c;
    yyjson_arr_foreach(yyjson_obj_get(r, "custom_themes"), i, n, c) {
        if (i >= 4) break;
        yyjson_val* nm = yyjson_obj_get(c, "name"); yyjson_val* pc = yyjson_obj_get(c, "primary");
        if (nm && yyjson_is_str(nm)) custom[i].name = yyjson_get_str(nm);
        if (pc && yyjson_is_num(pc)) custom[i].primary = static_cast<uint32_t>(yyjson_get_uint(pc)) & 0xffffff;
    }
    yyjson_doc_free(d);
    return true;
}

bool Config::save(const std::string& path) const {
    yyjson_mut_doc* d = yyjson_mut_doc_new(nullptr);
    yyjson_mut_val* o = yyjson_mut_obj(d);
    yyjson_mut_doc_set_root(d, o);
    yyjson_mut_obj_add_strcpy(d, o, "server_url", server_url.c_str());
    yyjson_mut_obj_add_strcpy(d, o, "username", username.c_str());
    yyjson_mut_obj_add_strcpy(d, o, "pin_sha256", pin_sha256.c_str());
    yyjson_mut_obj_add_strcpy(d, o, "cache_root", cache_root.c_str());
    yyjson_mut_obj_add_int(d, o, "volume_pct", volume_pct);
    yyjson_mut_obj_add_int(d, o, "stream_quality", stream_quality);
    yyjson_mut_obj_add_int(d, o, "download_quality", download_quality);
    yyjson_mut_obj_add_int(d, o, "concurrent_downloads", concurrent_downloads);
    yyjson_mut_obj_add_int(d, o, "default_view", default_view);
    yyjson_mut_obj_add_int(d, o, "car_minimal", car_minimal);
    yyjson_mut_obj_add_int(d, o, "perf_mode", perf_mode);
    yyjson_mut_obj_add_int(d, o, "offline_mode", offline_mode);
    yyjson_mut_obj_add_int(d, o, "car_keep_on", car_keep_on);
    yyjson_mut_obj_add_int(d, o, "car_start", car_start);
    yyjson_mut_obj_add_int(d, o, "car_pulse", car_pulse);
    yyjson_mut_obj_add_int(d, o, "debug_log", debug_log);
    yyjson_mut_obj_add_int(d, o, "render_timer", render_timer);
    yyjson_mut_obj_add_int(d, o, "auto_offline_metered", auto_offline_metered);
    yyjson_mut_obj_add_int(d, o, "be_controlled", be_controlled);
    yyjson_mut_obj_add_int(d, o, "control_others", control_others);
    yyjson_mut_obj_add_int(d, o, "theme", theme);
    yyjson_mut_obj_add_int(d, o, "theme_mode", theme_mode);
    yyjson_mut_val* ct = yyjson_mut_arr(d);
    for (const Custom& c : custom) {
        yyjson_mut_val* e = yyjson_mut_arr_add_obj(d, ct);
        yyjson_mut_obj_add_strcpy(d, e, "name", c.name.c_str());
        yyjson_mut_obj_add_uint(d, e, "primary", c.primary);
    }
    yyjson_mut_obj_add_val(d, o, "custom_themes", ct);
    yyjson_mut_val* cs = yyjson_mut_arr(d);
    for (const Conn& c : conns) {
        yyjson_mut_val* e = yyjson_mut_arr_add_obj(d, cs);
        yyjson_mut_obj_add_strcpy(d, e, "url", c.url.c_str());
        yyjson_mut_obj_add_strcpy(d, e, "user", c.user.c_str());
        yyjson_mut_obj_add_sint(d, e, "last_used", c.last_used);
    }
    yyjson_mut_obj_add_val(d, o, "connections", cs);
    std::string tmp = path + ".tmp";
    bool ok = yyjson_mut_write_file(tmp.c_str(), d, YYJSON_WRITE_PRETTY, nullptr, nullptr);
    yyjson_mut_doc_free(d);
    return ok && std::rename(tmp.c_str(), path.c_str()) == 0;
}

void Config::remember_connection(const std::string& url, const std::string& user) {
    int64_t now = static_cast<int64_t>(std::time(nullptr)) * 1000;
    for (auto& c : conns) if (c.url == url && c.user == user) { c.last_used = now; goto sort; }
    conns.push_back({url, user, now});
sort:
    std::sort(conns.begin(), conns.end(), [](const Conn& a, const Conn& b) { return a.last_used > b.last_used; });
    if (conns.size() > 10) conns.resize(10);
}

void Config::forget_connection(const std::string& url, const std::string& user) {
    conns.erase(std::remove_if(conns.begin(), conns.end(), [&](const Conn& c) { return c.url == url && c.user == user; }), conns.end());
}
