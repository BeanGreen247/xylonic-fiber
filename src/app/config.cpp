#include "config.h"
#include <cstdio>
#include "../../third_party/yyjson.h"

bool Config::load(const std::string& path) {
    yyjson_doc* d = yyjson_read_file(path.c_str(), 0, nullptr, nullptr);
    if (!d) return false;
    yyjson_val* r = yyjson_doc_get_root(d);
    auto str = [&](const char* k) { yyjson_val* v = yyjson_obj_get(r, k); return v && yyjson_is_str(v) ? std::string(yyjson_get_str(v)) : std::string(); };
    auto num = [&](const char* k, int def) { yyjson_val* v = yyjson_obj_get(r, k); return v && yyjson_is_num(v) ? static_cast<int>(yyjson_get_sint(v)) : def; };
    server_url = str("server_url"); username = str("username"); pin_sha256 = str("pin_sha256");
    volume_pct = num("volume_pct", 80); stream_quality = num("stream_quality", 0);
    theme = num("theme", 0); theme_mode = num("theme_mode", 0);
    if (theme < 0 || theme > 11) theme = 0;
    if (theme_mode < 0 || theme_mode > 2) theme_mode = 0;
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
    yyjson_mut_obj_add_int(d, o, "volume_pct", volume_pct);
    yyjson_mut_obj_add_int(d, o, "stream_quality", stream_quality);
    yyjson_mut_obj_add_int(d, o, "theme", theme);
    yyjson_mut_obj_add_int(d, o, "theme_mode", theme_mode);
    yyjson_mut_val* ct = yyjson_mut_arr(d);
    for (const Custom& c : custom) {
        yyjson_mut_val* e = yyjson_mut_arr_add_obj(d, ct);
        yyjson_mut_obj_add_strcpy(d, e, "name", c.name.c_str());
        yyjson_mut_obj_add_uint(d, e, "primary", c.primary);
    }
    yyjson_mut_obj_add_val(d, o, "custom_themes", ct);
    std::string tmp = path + ".tmp";
    bool ok = yyjson_mut_write_file(tmp.c_str(), d, YYJSON_WRITE_PRETTY, nullptr, nullptr);
    yyjson_mut_doc_free(d);
    return ok && std::rename(tmp.c_str(), path.c_str()) == 0;
}
