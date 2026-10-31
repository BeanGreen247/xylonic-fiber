#include "url.h"
#include <cstdlib>

bool parse_url(const std::string& s, Url& u) {
    size_t p = s.find("://");
    if (p == std::string::npos) return false;
    u.scheme = s.substr(0, p);
    for (auto& c : u.scheme) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
    if (u.scheme != "http" && u.scheme != "https") return false;
    size_t a = p + 3;
    size_t slash = s.find_first_of("/?#", a);
    std::string hp = s.substr(a, slash == std::string::npos ? std::string::npos : slash - a);
    u.target = slash == std::string::npos ? "/" : s.substr(slash);
    size_t hash = u.target.find('#');
    if (hash != std::string::npos) u.target.resize(hash);
    if (u.target[0] != '/') u.target.insert(0, "/");
    u.port = u.scheme == "https" ? 443 : 80;
    size_t at = hp.rfind('@');
    if (at != std::string::npos) hp.erase(0, at + 1);
    if (!hp.empty() && hp[0] == '[') {  // IPv6 literal
        size_t e = hp.find(']');
        if (e == std::string::npos) return false;
        u.host = hp.substr(1, e - 1);
        if (e + 1 < hp.size()) {
            if (hp[e + 1] != ':') return false;
            u.port = std::atoi(hp.c_str() + e + 2);
        }
    } else {
        size_t c = hp.rfind(':');
        if (c != std::string::npos) {
            u.host = hp.substr(0, c);
            u.port = std::atoi(hp.c_str() + c + 1);
        } else {
            u.host = hp;
        }
    }
    return !u.host.empty() && u.port > 0 && u.port < 65536;
}

std::string url_encode(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string o;
    o.reserve(s.size());
    for (unsigned char c : s) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_' || c == '~') {
            o += static_cast<char>(c);
        } else {
            o += '%';
            o += hex[c >> 4];
            o += hex[c & 15];
        }
    }
    return o;
}
