// Minimal HTTP/1.1 GET client (Connection: close). Streams the body to a sink; follows up to 3 redirects.
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include "stream.h"

struct HttpResult {
    int status = 0;
    std::string content_type;
    int64_t content_length = -1;
    std::string error;  // non-empty on transport failure
};

// Sink returns false to abort (e.g. user cancelled). Called only for 2xx bodies.
using BodySink = std::function<bool(const uint8_t*, size_t)>;

// on_headers (optional) is called once per final response, before any body bytes, with status and Content-Length (-1 unknown).
using HeaderCb = std::function<void(int status, int64_t content_length)>;
HttpResult http_get(const std::string& url, const BodySink& sink, const ConnectOptions& opt = {},
                    const std::vector<std::string>& extra_headers = {}, const HeaderCb& on_headers = nullptr);
HttpResult http_get_string(const std::string& url, std::string& body, const ConnectOptions& opt = {},
                           size_t max_bytes = 64u << 20);
