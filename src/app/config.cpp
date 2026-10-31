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
    std::string tmp = path + ".tmp";
    bool ok = yyjson_mut_write_file(tmp.c_str(), d, YYJSON_WRITE_PRETTY, nullptr, nullptr);
    yyjson_mut_doc_free(d);
    return ok && std::rename(tmp.c_str(), path.c_str()) == 0;
}
