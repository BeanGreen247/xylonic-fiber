#include "http.h"
#include <cstdlib>
#include <cstring>
#include "url.h"

namespace {
std::string lower(std::string s) {
    for (auto& c : s) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
    return s;
}

// Reads from the stream through a small buffer so header parsing and chunked decoding can consume byte-wise.
struct Reader {
    explicit Reader(Stream& st) : s(st) {}
    Stream& s;
    uint8_t buf[16384];
    size_t pos = 0, len = 0;
    bool fill() {
        long r = s.read(buf, sizeof buf);
        if (r <= 0) return false;
        pos = 0;
        len = static_cast<size_t>(r);
        return true;
    }
    int get() {
        if (pos == len && !fill()) return -1;
        return buf[pos++];
    }
    bool line(std::string& out) {
        out.clear();
        int c = 0;
        while ((c = get()) >= 0) {
            if (c == '\n') {
                if (!out.empty() && out.back() == '\r') out.pop_back();
                return true;
            }
            out += static_cast<char>(c);
            if (out.size() > 65536) return false;
        }
        return false;
    }
};

bool pump(Reader& r, int64_t n, const BodySink& sink, bool deliver, bool& aborted) {  // n<0: until EOF
    while (n != 0) {
        if (r.pos == r.len && !r.fill()) return n < 0;
        size_t take = r.len - r.pos;
        if (n > 0 && static_cast<int64_t>(take) > n) take = static_cast<size_t>(n);
        if (deliver && !sink(r.buf + r.pos, take)) { aborted = true; return true; }
        r.pos += take;
        if (n > 0) n -= static_cast<int64_t>(take);
    }
    return true;
}
}  // namespace

HttpResult http_get(const std::string& url0, const BodySink& sink, const ConnectOptions& opt,
                    const std::vector<std::string>& extra, const HeaderCb& on_headers) {
    HttpResult res;
    std::string url = url0;
    for (int hop = 0; hop <= 3; ++hop) {
        Url u;
        if (!parse_url(url, u)) { res.error = "bad url"; return res; }
        auto st = connect_stream(u.host, u.port, u.scheme == "https", opt, res.error);
        if (!st) return res;
        std::string req = "GET " + u.target + " HTTP/1.1\r\nHost: " + u.host;
        if ((u.scheme == "http" && u.port != 80) || (u.scheme == "https" && u.port != 443)) req += ":" + std::to_string(u.port);
        req += "\r\nUser-Agent: xylonic-fiber\r\nAccept: */*\r\nConnection: close\r\n";
        for (auto& h : extra) req += h + "\r\n";
        req += "\r\n";
        if (!st->write_all(req.data(), req.size())) { res.error = "send failed"; return res; }

        auto rd = std::make_unique<Reader>(*st);
        std::string line;
        if (!rd->line(line) || line.compare(0, 5, "HTTP/") != 0 || line.size() < 12) { res.error = "bad status line"; return res; }
        res.status = std::atoi(line.c_str() + 9);
        bool chunked = false;
        std::string location;
        res.content_length = -1;
        res.content_type.clear();
        while (rd->line(line) && !line.empty()) {
            size_t c = line.find(':');
            if (c == std::string::npos) continue;
            std::string k = lower(line.substr(0, c)), v = line.substr(c + 1);
            v.erase(0, v.find_first_not_of(" \t"));
            if (k == "content-length") res.content_length = std::atoll(v.c_str());
            else if (k == "content-type") res.content_type = v;
            else if (k == "location") location = v;
            else if (k == "transfer-encoding" && lower(v).find("chunked") != std::string::npos) chunked = true;
        }
        if (res.status >= 300 && res.status < 400 && !location.empty()) {
            if (location[0] == '/') location = u.scheme + "://" + u.host + ":" + std::to_string(u.port) + location;
            url = location;
            continue;
        }
        if (on_headers) on_headers(res.status, chunked ? -1 : res.content_length);
        bool deliver = res.status >= 200 && res.status < 300, aborted = false;
        bool ok = false;
        if (chunked) {
            ok = true;
            for (;;) {
                if (!rd->line(line)) { ok = false; break; }
                int64_t sz = std::strtoll(line.c_str(), nullptr, 16);
                if (sz < 0) { ok = false; break; }   // garbage size line
                if (sz == 0) break;
                if (!pump(*rd, sz, sink, deliver, aborted)) { ok = false; break; }
                if (aborted) break;
                rd->line(line);  // CRLF after chunk
            }
        } else {
            ok = pump(*rd, res.content_length, sink, deliver, aborted);
        }
        if (!ok) res.error = "truncated body";
        return res;
    }
    res.error = "too many redirects";
    return res;
}

HttpResult http_get_string(const std::string& url, std::string& body, const ConnectOptions& opt, size_t max_bytes) {
    body.clear();
    return http_get(url, [&](const uint8_t* p, size_t n) {
        if (body.size() + n > max_bytes) return false;
        body.append(reinterpret_cast<const char*>(p), n);
        return true;
    }, opt);
}
