#include <cstring>
// Starts a fake Subsonic ping responder on 127.0.0.1:4747 and checks discovery finds it through the localhost probe.
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <atomic>
#include <cstdio>
#include <mutex>
#include <thread>
#include <vector>
#include "../src/net/discovery.h"

int main() {
    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    int one = 1; setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in a{}; a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); a.sin_port = htons(4747);
    if (bind(lfd, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0) { std::printf("SKIP port 4747 busy\n"); return 0; }
    listen(lfd, 8);
    std::atomic<bool> run{true};
    std::thread srv([&] {
        while (run) {
            pollfd pf{lfd, POLLIN, 0};
            if (poll(&pf, 1, 100) > 0) {
                int c = accept(lfd, nullptr, nullptr);
                if (c < 0) continue;
                char b[1024]; recv(c, b, sizeof b, 0);
                const char* body = "{\"subsonic-response\":{\"status\":\"ok\",\"version\":\"1.16.1\",\"type\":\"navidrome\",\"serverVersion\":\"0.99.0\"}}";
                std::string r = std::string("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: ") + std::to_string(strlen(body)) + "\r\nConnection: close\r\n\r\n" + body;
                send(c, r.data(), r.size(), MSG_NOSIGNAL);
                close(c);
            }
        }
    });
    int fails = 0;
    if (!tcp_probe("127.0.0.1", 4747, 500)) { std::printf("FAIL probe\n"); ++fails; }
    if (tcp_probe("127.0.0.1", 1, 300)) { std::printf("FAIL closed port reported open\n"); ++fails; }
    std::vector<DiscoveredServer> got;
    std::mutex m;
    std::atomic<bool> cancel{false};
    discover_servers([&](const DiscoveredServer& d) { std::lock_guard<std::mutex> lk(m); got.push_back(d); }, cancel);
    bool hit = false;
    for (auto& d : got) if (d.url == "http://localhost:4747" && d.type == "navidrome" && d.version == "0.99.0" && d.via == "local") hit = true;
    if (!hit) { std::printf("FAIL local server not discovered (%zu found)\n", got.size()); ++fails; }
    run = false; srv.join(); close(lfd);
    std::printf(fails ? "FAILED\n" : "OK\n");
    return fails ? 1 : 0;
}
