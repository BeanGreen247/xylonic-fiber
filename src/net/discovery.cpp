#include "discovery.h"
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <set>
#include <thread>
#include <vector>
#include "../../third_party/yyjson.h"
#include "http.h"

namespace {
const int kPorts[] = {4533, 4747, 4040};  // Navidrome, Gonic, Airsonic / Subsonic
const char* kHostnames[] = {"localhost", "navidrome", "navidrome.local", "gonic", "gonic.local", "airsonic", "airsonic.local",
                            "subsonic", "subsonic.local", "music", "music.local"};
const int kMaxSweepHosts = 1024, kConcurrency = 48, kConnectTimeoutMs = 700, kPingTimeoutMs = 2500;

struct Task { std::string host; int port = 0; std::string name, via, scheme; };

std::string int_to_ip(uint32_t n) { in_addr a{htonl(n)}; char b[INET_ADDRSTRLEN] = {}; inet_ntop(AF_INET, &a, b, sizeof b); return b; }
bool in_cgnat(uint32_t n) { return (n & 0xffc00000u) == 0x64400000u; }
bool is_private(uint32_t n) {
    return in_cgnat(n) || (n & 0xff000000u) == 0x0a000000u || (n & 0xfff00000u) == 0xac100000u || (n & 0xffff0000u) == 0xc0a80000u ||
           (n & 0xffff0000u) == 0xa9fe0000u || (n & 0xff000000u) == 0x7f000000u;
}
bool vpn_iface(const std::string& n) {
    static const char* pre[] = {"tun", "tap", "wg", "ppp", "utun", "zt"};
    for (const char* p : pre) if (n.rfind(p, 0) == 0) return true;
    std::string l;
    for (char c : n) l += static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
    return l.find("wireguard") != std::string::npos || l.find("openvpn") != std::string::npos || l.find("zerotier") != std::string::npos || l.find("vpn") != std::string::npos;
}

std::vector<Task> subnet_targets() {
    std::vector<Task> out;
    int budget = kMaxSweepHosts;
    ifaddrs* ifs = nullptr;
    if (getifaddrs(&ifs) != 0) return out;
    for (ifaddrs* a = ifs; a; a = a->ifa_next) {
        if (!a->ifa_addr || a->ifa_addr->sa_family != AF_INET || !a->ifa_netmask || (a->ifa_flags & IFF_LOOPBACK)) continue;
        uint32_t self = ntohl(reinterpret_cast<sockaddr_in*>(a->ifa_addr)->sin_addr.s_addr);
        uint32_t nm = ntohl(reinterpret_cast<sockaddr_in*>(a->ifa_netmask)->sin_addr.s_addr);
        if (!is_private(self) || in_cgnat(self)) continue;
        int prefix = __builtin_popcount(nm);
        if (prefix < 22) prefix = 24;  // don't sweep a /16: stay on our own /24
        if (prefix >= 31) continue;
        uint32_t mask = 0xffffffffu << (32 - prefix), network = self & mask, last = network | ~mask;
        const char* via = vpn_iface(a->ifa_name) ? "vpn" : "lan";
        for (uint32_t n = network + 1; n < last && budget > 0; ++n, --budget)
            if (n != self) for (int p : kPorts) out.push_back({int_to_ip(n), p, int_to_ip(n), via, "http"});
    }
    freeifaddrs(ifs);
    return out;
}

struct Peer { std::string ip, label, dns; };
std::vector<Peer> tailscale_peers() {
    static const char* bins[] = {"tailscale", "/Applications/Tailscale.app/Contents/MacOS/Tailscale"};
    for (const char* b : bins) {
        std::string cmd = std::string(b) + " status --json 2>/dev/null";
        FILE* f = popen(cmd.c_str(), "r");
        if (!f) continue;
        std::string out;
        char buf[4096] = {};
        size_t n = 0;
        while ((n = fread(buf, 1, sizeof buf, f)) > 0 && out.size() < (4u << 20)) out.append(buf, n);
        int rc = pclose(f);
        if (rc != 0 || out.empty()) continue;
        yyjson_doc* d = yyjson_read(out.data(), out.size(), 0);
        if (!d) continue;
        std::vector<Peer> peers;
        yyjson_val* pr = yyjson_obj_get(yyjson_doc_get_root(d), "Peer");
        size_t i = 0, m = 0; yyjson_val *k = nullptr, *p = nullptr;
        yyjson_obj_foreach(pr, i, m, k, p) {
            yyjson_val* on = yyjson_obj_get(p, "Online");
            if (!on || !yyjson_get_bool(on)) continue;
            Peer pe;
            size_t j = 0, jm = 0; yyjson_val* a = nullptr;
            yyjson_arr_foreach(yyjson_obj_get(p, "TailscaleIPs"), j, jm, a) { const char* s = yyjson_get_str(a); if (s && std::strchr(s, '.')) { pe.ip = s; break; } }
            yyjson_val* hn = yyjson_obj_get(p, "HostName"); yyjson_val* dn = yyjson_obj_get(p, "DNSName");
            pe.dns = dn && yyjson_is_str(dn) ? yyjson_get_str(dn) : "";
            if (!pe.dns.empty() && pe.dns.back() == '.') pe.dns.pop_back();
            pe.label = hn && yyjson_is_str(hn) ? yyjson_get_str(hn) : pe.dns.substr(0, pe.dns.find('.'));
            if (!pe.ip.empty()) peers.push_back(pe);
        }
        yyjson_doc_free(d);
        return peers;
    }
    return {};
}

bool ping(const std::string& base, std::string& type, std::string& version) {
    std::string body;
    ConnectOptions o;
    o.timeout_ms = kPingTimeoutMs;
    HttpResult r = http_get_string(base + "/rest/ping.view?v=1.16.1&c=xylonic&f=json", body, o, 65536);
    if (!r.error.empty() || r.status < 200 || r.status >= 300) return false;
    yyjson_doc* d = yyjson_read(body.data(), body.size(), 0);
    bool ok = false;
    if (d) {
        yyjson_val* resp = yyjson_obj_get(yyjson_doc_get_root(d), "subsonic-response");
        if (resp) {
            yyjson_val* t = yyjson_obj_get(resp, "type"); yyjson_val* sv = yyjson_obj_get(resp, "serverVersion"); yyjson_val* v = yyjson_obj_get(resp, "version");
            type = t && yyjson_is_str(t) ? yyjson_get_str(t) : "subsonic";
            version = sv && yyjson_is_str(sv) ? yyjson_get_str(sv) : v && yyjson_is_str(v) ? yyjson_get_str(v) : "";
            ok = true;
        }
        yyjson_doc_free(d);
    } else if (body.find("<subsonic-response") != std::string::npos) { type = "subsonic"; ok = true; }
    return ok;
}
}  // namespace

bool tcp_probe(const std::string& host, int port, int timeout_ms) {
    addrinfo hints{}, *res = nullptr;
    hints.ai_socktype = SOCK_STREAM;
    std::string ps = std::to_string(port);
    if (getaddrinfo(host.c_str(), ps.c_str(), &hints, &res) != 0 || !res) return false;
    bool ok = false;
    for (addrinfo* a = res; a && !ok; a = a->ai_next) {
        int fd = ::socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (fd < 0) continue;
        fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
        int r = ::connect(fd, a->ai_addr, a->ai_addrlen);
        if (r == 0) ok = true;
        else if (errno == EINPROGRESS) {
            pollfd pf{fd, POLLOUT, 0};
            if (::poll(&pf, 1, timeout_ms) > 0) { int e = 0; socklen_t l = sizeof e; getsockopt(fd, SOL_SOCKET, SO_ERROR, &e, &l); ok = e == 0; }
        }
        ::close(fd);
    }
    freeaddrinfo(res);
    return ok;
}

void discover_servers(const std::function<void(const DiscoveredServer&)>& found, const std::atomic<bool>& cancel) {
    std::vector<Task> tasks;
    auto add_host = [&](const std::string& host, const std::string& name, const std::string& via, bool with_https) {
        for (int p : kPorts) tasks.push_back({host, p, name, via, "http"});
        if (with_https) tasks.push_back({host, 443, name, via, "https"});
    };
    for (const char* h : kHostnames) add_host(h, h, std::strcmp(h, "localhost") == 0 ? "local" : "lan", std::strcmp(h, "localhost") != 0);
    for (const Peer& p : tailscale_peers()) {
        add_host(p.ip, p.label, "tailscale", false);
        if (!p.dns.empty()) add_host(p.dns, p.label, "tailscale", true);
    }
    for (Task& t : subnet_targets()) tasks.push_back(std::move(t));
    std::mutex m;
    std::set<std::string> seen;
    size_t next = 0;
    auto worker = [&] {
        for (;;) {
            Task t;
            {
                std::lock_guard<std::mutex> lk(m);
                if (cancel.load() || next >= tasks.size()) return;
                t = tasks[next++];
            }
            if (!tcp_probe(t.host, t.port, kConnectTimeoutMs)) continue;
            bool def = (t.scheme == "https" && t.port == 443) || (t.scheme == "http" && t.port == 80);
            std::string base = t.scheme + "://" + t.host + (def ? "" : ":" + std::to_string(t.port));
            std::string type, version;
            if (!ping(base, type, version)) continue;
            DiscoveredServer d{base, t.name, t.via, type, version};
            {
                std::lock_guard<std::mutex> lk(m);
                if (cancel.load() || !seen.insert(base).second) continue;
            }
            found(d);
        }
    };
    std::vector<std::thread> th;
    for (int i = 0; i < kConcurrency; ++i) th.emplace_back(worker);
    for (auto& t : th) t.join();
}
