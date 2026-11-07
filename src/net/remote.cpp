#include "remote.h"
#include "portable.h"
#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <chrono>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include "../../third_party/yyjson.h"
#include "stream.h"

namespace {
int64_t now_ms() { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count(); }
const char* kMulticast = "239.255.85.89";

std::string esc(const std::string& s) {  // JSON string literal
    std::string o = "\"";
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') { o += '\\'; o += static_cast<char>(c); }
        else if (c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); o += b; }
        else o += static_cast<char>(c);
    }
    return o + "\"";
}
std::string str(yyjson_val* o, const char* k) { yyjson_val* v = yyjson_obj_get(o, k); return v && yyjson_is_str(v) ? std::string(yyjson_get_str(v)) : std::string(); }

bool virtual_iface(const std::string& n) {
    static const char* pre[] = {"docker", "veth", "br-", "vmnet", "virbr", "tun", "tap", "vpn", "wsl", "lo"};
    std::string l;
    for (char c : n) l += static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
    for (const char* p : pre) if (l.rfind(p, 0) == 0) return true;
    return false;
}
bool lan_ip(const std::string& a) {
    if (a.rfind("192.168.", 0) == 0 || a.rfind("10.", 0) == 0) return true;
    int x = 0, y = 0;
    return std::sscanf(a.c_str(), "%d.%d", &x, &y) == 2 && x == 172 && y >= 16 && y <= 31;
}
std::string local_ip() {
    ifaddrs* ifs = nullptr;
    std::string any;
    if (getifaddrs(&ifs) != 0) return "127.0.0.1";
    std::string best;
    for (ifaddrs* a = ifs; a && best.empty(); a = a->ifa_next) {
        if (!a->ifa_addr || a->ifa_addr->sa_family != AF_INET || (a->ifa_flags & IFF_LOOPBACK) || virtual_iface(a->ifa_name)) continue;
        char b[INET_ADDRSTRLEN] = {};
        inet_ntop(AF_INET, &reinterpret_cast<sockaddr_in*>(a->ifa_addr)->sin_addr, b, sizeof b);
        if (lan_ip(b)) best = b; else if (any.empty()) any = b;
    }
    freeifaddrs(ifs);
    return !best.empty() ? best : !any.empty() ? any : "127.0.0.1";
}
std::vector<std::string> broadcast_addrs() {
    std::vector<std::string> r{"255.255.255.255"};
    ifaddrs* ifs = nullptr;
    if (getifaddrs(&ifs) != 0) return r;
    for (ifaddrs* a = ifs; a; a = a->ifa_next) {
        if (!a->ifa_addr || a->ifa_addr->sa_family != AF_INET || !a->ifa_netmask || (a->ifa_flags & IFF_LOOPBACK)) continue;
        uint32_t ip = ntohl(reinterpret_cast<sockaddr_in*>(a->ifa_addr)->sin_addr.s_addr), mk = ntohl(reinterpret_cast<sockaddr_in*>(a->ifa_netmask)->sin_addr.s_addr);
        uint32_t bc = (ip & mk) | ~mk;
        in_addr x{htonl(bc)};
        char b[INET_ADDRSTRLEN] = {};
        inet_ntop(AF_INET, &x, b, sizeof b);
        bool dup = false;
        for (auto& e : r) if (e == b) dup = true;
        if (!dup) r.push_back(b);
    }
    freeifaddrs(ifs);
    return r;
}
}  // namespace

std::string local_device_name() {
    char h[256] = "device";
    gethostname(h, sizeof h - 1);
#if defined(__ANDROID__)
    return std::string(h) + ",Android";
#elif defined(__linux__)
    return std::string(h) + ",Linux";
#elif defined(__APPLE__) && TARGET_OS_IPHONE
    return std::string(h) + ",iOS";
#elif defined(__APPLE__)
    return std::string(h) + ",macOS";
#else
    return std::string(h) + ",Windows";
#endif
}

RemoteService::RemoteService(std::string id, std::string name, Options o) : id_(std::move(id)), name_(std::move(name)), opt_(o) {}
RemoteService::~RemoteService() { stop(); }

void RemoteService::set_handlers(CommandHandler c, StateProvider s, EventHandler e) { on_cmd_ = std::move(c); state_ = std::move(s); on_event_ = std::move(e); }

bool RemoteService::start() {
    if (run_.load()) return true;
    // command server socket
    sfd_ = socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    setsockopt(sfd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in a{};
    a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_ANY); a.sin_port = htons(static_cast<uint16_t>(opt_.command_port));
    bool ok_srv = bind(sfd_, reinterpret_cast<sockaddr*>(&a), sizeof a) == 0 && listen(sfd_, 16) == 0;
    if (!ok_srv) { close(sfd_); sfd_ = -1; }
    // presence listener socket
    lfd_ = socket(AF_INET, SOCK_DGRAM, 0);
    setsockopt(lfd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
#ifdef SO_REUSEPORT
    setsockopt(lfd_, SOL_SOCKET, SO_REUSEPORT, &one, sizeof one);
#endif
    sockaddr_in la{};
    la.sin_family = AF_INET; la.sin_addr.s_addr = htonl(INADDR_ANY); la.sin_port = htons(static_cast<uint16_t>(opt_.broadcast_port));
    bool ok_l = bind(lfd_, reinterpret_cast<sockaddr*>(&la), sizeof la) == 0;
    if (!ok_l) { close(lfd_); lfd_ = -1; }
    else if (opt_.multicast) {
        ifaddrs* ifs = nullptr;
        if (getifaddrs(&ifs) == 0) {
            for (ifaddrs* i = ifs; i; i = i->ifa_next) {
                if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET || (i->ifa_flags & IFF_LOOPBACK)) continue;
                ip_mreq m{};
                inet_pton(AF_INET, kMulticast, &m.imr_multiaddr);
                m.imr_interface = reinterpret_cast<sockaddr_in*>(i->ifa_addr)->sin_addr;
                setsockopt(lfd_, IPPROTO_IP, IP_ADD_MEMBERSHIP, &m, sizeof m);
            }
            freeifaddrs(ifs);
        }
    }
    if (!ok_srv && !ok_l) return false;
    run_ = true;
    if (ok_srv) th_s_ = std::thread([this] { server_loop(); });
    if (ok_l) th_l_ = std::thread([this] { listen_loop(); });
    th_b_ = std::thread([this] { broadcast_loop(); });
    return true;
}

void RemoteService::stop() {
    if (!run_.exchange(false)) return;
    if (th_b_.joinable()) th_b_.join();
    if (th_l_.joinable()) th_l_.join();
    if (th_s_.joinable()) th_s_.join();
    if (lfd_ >= 0) { close(lfd_); lfd_ = -1; }
    if (sfd_ >= 0) { close(sfd_); sfd_ = -1; }
    for (int i = 0; i < 700 && clients_.load() > 0; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));   // handlers time out after 5 s
}

void RemoteService::set_enabled(bool on) {
    bool cleared = false;
    {
        std::lock_guard<std::mutex> lk(m_);
        enabled_ = on;
        if (!on && !paired_controller_.empty()) { paired_controller_.clear(); cleared = true; }
    }
    if (cleared && on_event_) on_event_("pairing-cleared", "");
}

std::vector<RemoteDevice> RemoteService::devices() {
    std::lock_guard<std::mutex> lk(m_);
    std::vector<RemoteDevice> v;
    for (auto& kv : devices_) v.push_back(kv.second);
    return v;
}

std::string RemoteService::paired_controller() { std::lock_guard<std::mutex> lk(m_); return paired_controller_; }

std::string RemoteService::presence_json() {
    std::string account, paired, controlling;
    { std::lock_guard<std::mutex> lk(m_); account = account_; paired = paired_controller_; controlling = controlling_; }
    std::string j = "{\"type\":\"XYLONIC_PRESENCE\",\"id\":" + esc(id_) + ",\"name\":" + esc(name_) + ",\"host\":" + esc(local_ip()) +
                    ",\"cmdPort\":" + std::to_string(opt_.command_port) + ",\"platform\":\"native\",\"pairedWith\":" + (paired.empty() ? "null" : esc(paired)) +
                    ",\"controllingId\":" + (controlling.empty() ? "null" : esc(controlling)) + ",\"accountId\":" + (account.empty() ? "null" : esc(account)) +
                    ",\"ts\":" + std::to_string(now_ms());
    if (state_) {
        RemotePlayerState ps = state_();
        if (ps.has_state) {
            char b[96] = {};
            std::snprintf(b, sizeof b, "%s,\"currentTime\":%.3f,\"duration\":%.3f", ps.playing ? "true" : "false", ps.current_time, ps.duration);
            j += ",\"playerState\":{\"isPlaying\":" + std::string(b) + ",\"song\":" + (ps.song_json.empty() ? "null" : ps.song_json) + "}";
        }
    }
    return j + "}";
}

void RemoteService::broadcast_loop() {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &one, sizeof one);
    while (run_.load()) {
        bool enabled = false;
        { std::lock_guard<std::mutex> lk(m_); enabled = enabled_; }
        if (enabled) {
            std::string pkt = presence_json();
            auto send_to = [&](const std::string& addr) {
                sockaddr_in a{};
                a.sin_family = AF_INET; a.sin_port = htons(static_cast<uint16_t>(opt_.broadcast_port));
                inet_pton(AF_INET, addr.c_str(), &a.sin_addr);
                sendto(fd, pkt.data(), pkt.size(), 0, reinterpret_cast<sockaddr*>(&a), sizeof a);
            };
            if (opt_.broadcast) for (auto& a : broadcast_addrs()) send_to(a);
            if (opt_.multicast) send_to(kMulticast);
            send_to("127.0.0.1");  // lets two instances on one machine (and tests) see each other
        }
        for (int i = 0; i < 10 && run_.load(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    close(fd);
}

void RemoteService::listen_loop() {
    int64_t last_gc = now_ms();
    while (run_.load()) {
        pollfd pf{lfd_, POLLIN, 0};
        if (poll(&pf, 1, 250) > 0) {
            char buf[8192] = {};
            ssize_t n = recv(lfd_, buf, sizeof buf - 1, 0);
            if (n > 0) {
                yyjson_doc* d = yyjson_read(buf, static_cast<size_t>(n), 0);
                yyjson_val* r = d ? yyjson_doc_get_root(d) : nullptr;
                if (r && str(r, "type") == "XYLONIC_PRESENCE" && str(r, "id") != id_ && !str(r, "id").empty()) {
                    RemoteDevice dv;
                    dv.id = str(r, "id"); dv.name = str(r, "name"); dv.host = str(r, "host"); dv.platform = str(r, "platform");
                    dv.paired_with = str(r, "pairedWith"); dv.controlling_id = str(r, "controllingId"); dv.account_id = str(r, "accountId");
                    yyjson_val* cp = yyjson_obj_get(r, "cmdPort");
                    dv.cmd_port = cp && yyjson_is_num(cp) ? static_cast<int>(yyjson_get_sint(cp)) : 7767;
                    dv.last_seen_ms = now_ms();
                    if (yyjson_val* ps = yyjson_obj_get(r, "playerState")) {
                        yyjson_val* ip = yyjson_obj_get(ps, "isPlaying"); yyjson_val* ct = yyjson_obj_get(ps, "currentTime"); yyjson_val* du = yyjson_obj_get(ps, "duration");
                        dv.playing = ip && yyjson_get_bool(ip);
                        dv.current_time = ct && yyjson_is_num(ct) ? yyjson_get_num(ct) : 0;
                        dv.duration = du && yyjson_is_num(du) ? yyjson_get_num(du) : 0;
                        if (yyjson_val* sv = yyjson_obj_get(ps, "song")) { char* js = yyjson_val_write(sv, 0, nullptr); if (js) { if (std::strcmp(js, "null") != 0) dv.song_json = js; free(js); } }
                    }
                    bool is_new = false;
                    { std::lock_guard<std::mutex> lk(m_); is_new = !devices_.count(dv.id); devices_[dv.id] = dv; }
                    if (is_new && on_event_) on_event_("device-found", dv.id);
                }
                if (d) yyjson_doc_free(d);
            }
        }
        int64_t now = now_ms();
        if (now - last_gc > 5000) {
            last_gc = now;
            std::vector<std::string> lost;
            bool cleared = false;
            {
                std::lock_guard<std::mutex> lk(m_);
                for (auto it = devices_.begin(); it != devices_.end();) {
                    if (now - it->second.last_seen_ms > 15000) {
                        if (it->first == paired_controller_) { paired_controller_.clear(); cleared = true; }
                        lost.push_back(it->first);
                        it = devices_.erase(it);
                    } else ++it;
                }
            }
            if (on_event_) { for (auto& id : lost) on_event_("device-lost", id); if (cleared) on_event_("pairing-cleared", ""); }
        }
    }
}

void RemoteService::server_loop() {
    while (run_.load()) {
        pollfd pf{sfd_, POLLIN, 0};
        if (poll(&pf, 1, 250) <= 0) continue;
        int c = accept(sfd_, nullptr, nullptr);
        if (c < 0) continue;
        if (clients_.load() >= 16) { close(c); continue; }   // a LAN peer cannot exhaust threads by opening connections
        clients_.fetch_add(1);
        std::thread([this, c] { handle_client(c); clients_.fetch_sub(1); }).detach();
    }
}

void RemoteService::handle_client(int fd) {
    timeval tv{5, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    std::string req;
    char buf[4096] = {};
    size_t body_at = std::string::npos;
    size_t want = 0;
    for (;;) {
        ssize_t n = recv(fd, buf, sizeof buf, 0);
        if (n <= 0) break;
        req.append(buf, static_cast<size_t>(n));
        if (body_at == std::string::npos) {
            size_t h = req.find("\r\n\r\n");
            if (h != std::string::npos) {
                body_at = h + 4;
                size_t cl = req.find("Content-Length:");
                if (cl == std::string::npos) cl = req.find("content-length:");
                want = cl == std::string::npos ? 0 : static_cast<size_t>(std::atoll(req.c_str() + cl + 15));
            }
        }
        if (body_at != std::string::npos && req.size() >= body_at + want) break;
        if (req.size() > (1u << 20)) break;
    }
    auto reply = [&](int code, const std::string& body) {
        std::string r = std::string("HTTP/1.1 ") + (code == 200 ? "200 OK" : code == 400 ? "400 Bad Request" : "404 Not Found") +
                        "\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nContent-Length: " + std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
        send(fd, r.data(), r.size(), MSG_NOSIGNAL);
        close(fd);
    };
    if (req.compare(0, 10, "POST /cmd ") != 0 || body_at == std::string::npos) { reply(404, ""); return; }
    std::string body = req.substr(body_at, want ? want : std::string::npos);
    yyjson_doc* d = yyjson_read(body.data(), body.size(), 0);
    if (!d) { reply(400, "{\"ok\":false,\"reason\":\"parse_error\"}"); return; }
    yyjson_val* r = yyjson_doc_get_root(d);
    std::string action = str(r, "action"), controller = str(r, "controllerId");
    yyjson_val* data = yyjson_obj_get(r, "data");
    std::string data_json = "{}";
    if (data && yyjson_is_obj(data)) { char* js = yyjson_val_write(data, 0, nullptr); if (js) { data_json = js; free(js); } }
    std::string ctl_acc = data && yyjson_is_obj(data) ? str(data, "controllerAccountId") : "", ctl_name = data && yyjson_is_obj(data) ? str(data, "controllerName") : "";
    yyjson_doc_free(d);
    std::string out;
    bool established = false, cleared = false, forward = false;
    {
        std::lock_guard<std::mutex> lk(m_);
        if (action == "pair") {
            if (!enabled_) out = "{\"ok\":false,\"reason\":\"remote_control_disabled\"}";
            else if (controller.empty()) out = "{\"ok\":false,\"reason\":\"missing_id\"}";
            else if (account_.empty()) out = "{\"ok\":false,\"reason\":\"no_account\"}";
            else if (!ctl_acc.empty() && account_ != ctl_acc) out = "{\"ok\":false,\"reason\":\"account_mismatch\"}";
            else if (!paired_controller_.empty() && paired_controller_ != controller && devices_.count(paired_controller_)) out = "{\"ok\":false,\"reason\":\"already_paired\"}";
            else { paired_controller_ = controller; established = true; out = "{\"ok\":true}"; }
        } else if (action == "disconnect") {
            if (!controller.empty() && controller == paired_controller_) { paired_controller_.clear(); cleared = true; out = "{\"ok\":true}"; }
            else out = "{\"ok\":false,\"reason\":\"not_paired\"}";
        } else if (!paired_controller_.empty() && paired_controller_ != controller) out = "{\"ok\":false,\"reason\":\"not_paired\"}";
        else { forward = true; out = "{\"ok\":true}"; }
    }
    if (established && on_event_) on_event_("pairing-established", ctl_name);
    if (cleared && on_event_) on_event_("pairing-cleared", "");
    if (forward && on_cmd_) on_cmd_(action, data_json);
    reply(200, out);
}

bool RemoteService::send_command(const std::string& host, int port, const std::string& action, const std::string& data_json,
                                 const std::string& controller_id, std::string& reason) {
    std::string err;
    ConnectOptions o;
    o.timeout_ms = 5000;
    auto st = connect_stream(host, port, false, o, err);
    if (!st) { reason = "network_error"; return false; }
    std::string body = "{\"action\":" + esc(action) + ",\"data\":" + (data_json.empty() ? "{}" : data_json) + ",\"controllerId\":" + esc(controller_id) + "}";
    std::string req = "POST /cmd HTTP/1.1\r\nHost: " + host + ":" + std::to_string(port) + "\r\nContent-Type: application/json\r\nContent-Length: " + std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
    if (!st->write_all(req.data(), req.size())) { reason = "network_error"; return false; }
    std::string resp;
    char buf[2048] = {};
    long n = 0;
    while ((n = st->read(buf, sizeof buf)) > 0) resp.append(buf, static_cast<size_t>(n));
    size_t b = resp.find("\r\n\r\n");
    if (b == std::string::npos) { reason = "network_error"; return false; }
    yyjson_doc* d = yyjson_read(resp.data() + b + 4, resp.size() - b - 4, 0);
    if (!d) { reason = "parse_error"; return false; }
    yyjson_val* r = yyjson_doc_get_root(d);
    yyjson_val* ok = yyjson_obj_get(r, "ok");
    bool good = ok && yyjson_get_bool(ok);
    reason = str(r, "reason");
    yyjson_doc_free(d);
    return good;
}
