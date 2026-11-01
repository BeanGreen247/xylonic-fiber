#pragma once
#include <string>

struct Url {
    std::string scheme;  // "http" or "https"
    std::string host;
    int port = 0;
    std::string target;  // path + query, always starts with '/'
};

bool parse_url(const std::string& s, Url& out);
std::string url_encode(const std::string& s);  // RFC 3986 unreserved kept
