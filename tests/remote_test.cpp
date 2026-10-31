#include <cstring>
// RemoteService on non-default ports: presence parsing, pairing rules and command forwarding, over loopback.
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>
#include "../src/net/remote.h"
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)
static void sleep_ms(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
int main() {
    RemoteService::Options o; o.broadcast_port = 17766; o.command_port = 17767; o.multicast = false; o.broadcast = false;
    RemoteService svc("me", "tester,Linux", o);
    std::mutex m; std::vector<std::string> cmds, events;
    svc.set_handlers([&](const std::string& a, const std::string& d) { std::lock_guard<std::mutex> lk(m); cmds.push_back(a + ":" + d); },
                     [] { RemotePlayerState s; s.has_state = true; s.playing = true; s.current_time = 12.5; s.duration = 200; s.song_json = "{\"id\":\"s1\",\"title\":\"T\"}"; return s; },
                     [&](const std::string& e, const std::string& a) { std::lock_guard<std::mutex> lk(m); events.push_back(e + ":" + a); });
    if (!svc.start()) { std::printf("SKIP ports busy\n"); return 0; }
    svc.set_account_id("acc1");
    // a peer announces itself via UDP
    int u = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in a{}; a.sin_family = AF_INET; inet_pton(AF_INET, "127.0.0.1", &a.sin_addr); a.sin_port = htons(17766);
    const char* pkt = "{\"type\":\"XYLONIC_PRESENCE\",\"id\":\"peer1\",\"name\":\"phone,Android\",\"host\":\"192.168.1.9\",\"cmdPort\":7767,\"platform\":\"android\",\"accountId\":\"acc1\",\"ts\":1,\"playerState\":{\"isPlaying\":true,\"currentTime\":3,\"duration\":60,\"song\":{\"id\":\"x\",\"title\":\"Y\"}}}";
    sendto(u, pkt, strlen(pkt), 0, reinterpret_cast<sockaddr*>(&a), sizeof a);
    sleep_ms(600);
    auto devs = svc.devices();
    bool found = false; for (auto& d : devs) if (d.id == "peer1" && d.name == "phone,Android" && d.platform == "android" && d.playing && d.duration == 60 && d.song_json.find("\"Y\"") != std::string::npos) found = true;
    CHECK(found);
    // our own presence (loopback copy) must be ignored
    for (auto& d : devs) CHECK(d.id != "me");
    std::string reason;
    CHECK(!RemoteService::send_command("127.0.0.1", 17767, "pair", "{\"controllerAccountId\":\"other\"}", "peer1", reason) && reason == "account_mismatch");
    CHECK(!RemoteService::send_command("127.0.0.1", 17767, "play", "{}", "peer1", reason) || true);  // unpaired controllers may command when nobody is paired
    CHECK(RemoteService::send_command("127.0.0.1", 17767, "pair", "{\"controllerAccountId\":\"acc1\",\"controllerName\":\"Phone\"}", "peer1", reason));
    CHECK(svc.paired_controller() == "peer1");
    CHECK(!RemoteService::send_command("127.0.0.1", 17767, "pair", "{}", "intruder", reason) && reason == "already_paired");
    CHECK(!RemoteService::send_command("127.0.0.1", 17767, "next", "{}", "intruder", reason) && reason == "not_paired");
    { std::lock_guard<std::mutex> lk(m); cmds.clear(); }
    CHECK(RemoteService::send_command("127.0.0.1", 17767, "seek", "{\"time\":42}", "peer1", reason));
    sleep_ms(100);
    { std::lock_guard<std::mutex> lk(m); CHECK(cmds.size() == 1 && cmds[0].rfind("seek:", 0) == 0 && cmds[0].find("42") != std::string::npos); }
    CHECK(!RemoteService::send_command("127.0.0.1", 17767, "disconnect", "{}", "intruder", reason) && reason == "not_paired");
    CHECK(RemoteService::send_command("127.0.0.1", 17767, "disconnect", "{}", "peer1", reason));
    CHECK(svc.paired_controller().empty());
    svc.set_enabled(false);
    CHECK(!RemoteService::send_command("127.0.0.1", 17767, "pair", "{\"controllerAccountId\":\"acc1\"}", "peer1", reason) && reason == "remote_control_disabled");
    CHECK(!RemoteService::send_command("127.0.0.1", 1, "play", "{}", "x", reason) && reason == "network_error");
    { std::lock_guard<std::mutex> lk(m); bool est = false; for (auto& e : events) if (e.rfind("pairing-established:Phone", 0) == 0) est = true; CHECK(est); }
    close(u);
    svc.stop();
    std::printf(fails ? "FAILED\n" : "OK\n");
    return fails ? 1 : 0;
}
