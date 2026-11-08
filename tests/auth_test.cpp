// Credential rejection detection: HTTP 401 and Subsonic errors 40/41/44 set auth_failed(); set_password() clears it.
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>
#include "../src/subsonic/client.h"
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)
static void serve(int lfd, std::vector<std::string> replies) {
    for (const auto& r : replies) {
        int c = accept(lfd, nullptr, nullptr); if (c < 0) return;
        char buf[4096]; (void)!read(c, buf, sizeof buf); (void)!write(c, r.data(), r.size()); close(c);
    }
}
static std::string http(int status, const char* text, const std::string& body) {
    return "HTTP/1.1 " + std::to_string(status) + " " + text + "\r\nContent-Type: application/json\r\nContent-Length: " + std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
}
int main() {
    int lfd = socket(AF_INET, SOCK_STREAM, 0); int on = 1; setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
    sockaddr_in a{}; a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); a.sin_port = 0; bind(lfd, reinterpret_cast<sockaddr*>(&a), sizeof a); listen(lfd, 8);
    socklen_t len = sizeof a; getsockname(lfd, reinterpret_cast<sockaddr*>(&a), &len);
    const std::string ok = "{\"subsonic-response\":{\"status\":\"ok\",\"version\":\"1.16.1\"}}";
    auto failed = [](int code) { return "{\"subsonic-response\":{\"status\":\"failed\",\"version\":\"1.16.1\",\"error\":{\"code\": " + std::to_string(code) + ",\"message\":\"nope\"}}}"; };
    std::thread t(serve, lfd, std::vector<std::string>{http(200, "OK", failed(70)), http(200, "OK", failed(40)), http(401, "Unauthorized", ""), http(200, "OK", failed(44)), http(200, "OK", ok)});
    SubsonicClient c("http://127.0.0.1:" + std::to_string(ntohs(a.sin_port)), "u", "old");
    std::string err;
    CHECK(!c.ping(err) && !c.auth_failed());          // 70 = data not found: not a credential problem
    CHECK(!c.ping(err) && c.auth_failed());           // 40 = wrong username or password
    c.set_password("new"); CHECK(!c.auth_failed());
    CHECK(!c.ping(err) && c.auth_failed());           // HTTP 401
    c.clear_auth_failed();
    CHECK(!c.ping(err) && c.auth_failed());           // 44 = invalid API key
    c.set_password("newer"); CHECK(c.ping(err) && !c.auth_failed());
    t.join(); close(lfd);
    std::printf(fails ? "FAILED\n" : "OK\n"); return fails ? 1 : 0;
}
