#include "config.h"
#include "app_icons.h"
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
    server_url = str("server_url"); username = str("username"); pin_sha256 = str("pin_sha256"); cache_root = str("cache_root"); downloads_root = str("downloads_root");
    volume_pct = num("volume_pct", 80); stream_quality = num("stream_quality", 0);
    theme = num("theme", 0); theme_mode = num("theme_mode", 0); icon_set = num("icon_set", 1); app_icon = num("app_icon", 0);
    download_quality = num("download_quality", 320); concurrent_downloads = std::min(8, std::max(1, num("concurrent_downloads", 3)));
    default_view = num("default_view", 0); car_minimal = num("car_minimal", 0); perf_mode = num("perf_mode", 0);
    offline_mode = num("offline_mode", 0) != 0;
    car_keep_on = num("car_keep_on", 1) != 0; car_start = num("car_start", 0) != 0; car_pulse = num("car_pulse", 0) != 0;
    debug_log = num("debug_log", 0) != 0; render_timer = num("render_timer", 0) != 0; software_render = num("software_render", 0) != 0;
    auto_offline_metered = num("auto_offline_metered", 0) != 0;
    be_controlled = num("be_controlled", 1) != 0; control_others = num("control_others", 1) != 0;
    if (theme < 0 || theme > 11) theme = 0;
    if (theme_mode < 0 || theme_mode > 2) theme_mode = 0;
    if (icon_set < 0 || icon_set > 3) icon_set = 1;
    if (app_icon < 0 || app_icon >= kAppIconCount) app_icon = 0;
    size_t ci = 0, cn = 0; yyjson_val* cc = nullptr;
    conns.clear();
    yyjson_arr_foreach(yyjson_obj_get(r, "connections"), ci, cn, cc) {
        Conn c;
        yyjson_val* u = yyjson_obj_get(cc, "url"); yyjson_val* us = yyjson_obj_get(cc, "user"); yyjson_val* lu = yyjson_obj_get(cc, "last_used");
        if (u && yyjson_is_str(u)) c.url = yyjson_get_str(u);
        if (us && yyjson_is_str(us)) c.user = yyjson_get_str(us);
        if (lu && yyjson_is_num(lu)) c.last_used = static_cast<int64_t>(yyjson_get_sint(lu));
        if (!c.url.empty() && !c.user.empty()) conns.push_back(std::move(c));
    }
    accounts.clear();
    size_t ai = 0, an = 0; yyjson_val* ac = nullptr;
    yyjson_arr_foreach(yyjson_obj_get(r, "accounts"), ai, an, ac) {
        Account a;
        auto sv = [&](yyjson_val* o, const char* k) { yyjson_val* v = yyjson_obj_get(o, k); return v && yyjson_is_str(v) ? std::string(yyjson_get_str(v)) : std::string(); };
        a.id = sv(ac, "id"); a.user = sv(ac, "user");
        size_t k = 0, kn = 0; yyjson_val* e = nullptr;
        yyjson_arr_foreach(yyjson_obj_get(ac, "urls"), k, kn, e) if (yyjson_is_str(e)) a.urls.push_back(yyjson_get_str(e));
        yyjson_arr_foreach(yyjson_obj_get(ac, "fp"), k, kn, e) if (yyjson_is_str(e)) a.fp.push_back(yyjson_get_str(e));
        if (!a.id.empty() && !a.user.empty()) accounts.push_back(std::move(a));
    }
    size_t i = 0, n = 0; yyjson_val* c = nullptr;
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
    yyjson_mut_obj_add_strcpy(d, o, "downloads_root", downloads_root.c_str());
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
    yyjson_mut_obj_add_int(d, o, "software_render", software_render);
    yyjson_mut_obj_add_int(d, o, "auto_offline_metered", auto_offline_metered);
    yyjson_mut_obj_add_int(d, o, "be_controlled", be_controlled);
    yyjson_mut_obj_add_int(d, o, "control_others", control_others);
    yyjson_mut_obj_add_int(d, o, "theme", theme);
    yyjson_mut_obj_add_int(d, o, "theme_mode", theme_mode);
    yyjson_mut_obj_add_int(d, o, "icon_set", icon_set);
    yyjson_mut_obj_add_int(d, o, "app_icon", app_icon);
    yyjson_mut_val* accs = yyjson_mut_arr(d);
    for (const Account& a : accounts) {
        yyjson_mut_val* ao = yyjson_mut_obj(d);
        yyjson_mut_obj_add_strcpy(d, ao, "id", a.id.c_str());
        yyjson_mut_obj_add_strcpy(d, ao, "user", a.user.c_str());
        yyjson_mut_val* us = yyjson_mut_arr(d); for (const auto& u : a.urls) yyjson_mut_arr_add_strcpy(d, us, u.c_str());
        yyjson_mut_val* fs = yyjson_mut_arr(d); for (const auto& f : a.fp) yyjson_mut_arr_add_strcpy(d, fs, f.c_str());
        yyjson_mut_obj_add_val(d, ao, "urls", us); yyjson_mut_obj_add_val(d, ao, "fp", fs);
        yyjson_mut_arr_add_val(accs, ao);
    }
    yyjson_mut_obj_add_val(d, o, "accounts", accs);
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

Config::Account* Config::find_account(const std::string& url, const std::string& user) {
    for (Account& a : accounts)
        if (a.user == user && std::find(a.urls.begin(), a.urls.end(), url) != a.urls.end()) return &a;
    return nullptr;
}

Config::Account* Config::match_account(const std::string& user, const std::vector<std::string>& fp) {
    Account* best = nullptr; size_t best_common = 0;
    for (Account& a : accounts) {
        if (a.user != user || !fingerprint_match(a.fp, fp)) continue;
        const size_t c = fingerprint_common(a.fp, fp);
        if (c > best_common) { best = &a; best_common = c; }
    }
    return best;
}

static std::string legacy_account_id(const std::string& k) {   // the id this app used before accounts existed: keeps old data reachable
    uint64_t h = 1469598103934665603ULL;
    for (unsigned char c : k) { h ^= c; h *= 1099511628211ULL; }
    char b[16] = {};
    std::snprintf(b, sizeof b, "%08x", static_cast<unsigned>(h ^ (h >> 32)));
    return b;
}

std::string Config::account_id(const std::string& url, const std::string& user) {
    if (const Account* a = find_account(url, user)) return a->id;
    return legacy_account_id(url + "|" + user);
}

static uint32_t id_hash(const std::string& k) {
    uint64_t h = 1469598103934665603ULL;
    for (unsigned char c : k) { h ^= c; h *= 1099511628211ULL; }
    return static_cast<uint32_t>((h ^ (h >> 29)) >> 3);
}

std::vector<std::string> library_fingerprint(const std::vector<std::string>& artist_ids) {
    std::vector<std::string> out;
    for (const std::string& id : artist_ids) if (!id.empty() && (id_hash(id) & 3u) == 0) out.push_back(id);
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    if (out.size() > 128) out.resize(128);
    return out;
}

size_t fingerprint_common(const std::vector<std::string>& a, const std::vector<std::string>& b) {
    size_t i = 0, j = 0, n = 0;
    while (i < a.size() && j < b.size()) {
        if (a[i] == b[j]) { ++n; ++i; ++j; }
        else if (a[i] < b[j]) ++i;
        else ++j;
    }
    return n;
}

bool fingerprint_match(const std::vector<std::string>& a, const std::vector<std::string>& b) {
    const size_t common = fingerprint_common(a, b), smaller = std::min(a.size(), b.size());
    return common >= 3 && common * 10 >= smaller * 6;   // at least 3 shared ids and 60% of the smaller sample (libraries of ~12+ artists)
}
