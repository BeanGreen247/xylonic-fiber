// URL parsing, HTTP client against an in-process server (content-length, chunked, redirect, 404), optional TLS check.
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include "../src/net/http.h"
#include "../src/net/url.h"

static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

static void serve(int lfd, int count) {
    for (int i = 0; i < count; ++i) {
        int c = accept(lfd, nullptr, nullptr);
        char b[2048];
        ssize_t n = recv(c, b, sizeof b - 1, 0);
        b[n > 0 ? n : 0] = 0;
        std::string req(b), resp;
        if (req.compare(0, 11, "GET /plain ") == 0) resp = "HTTP/1.1 200 OK\r\nContent-Length: 5\r\nContent-Type: text/plain\r\n\r\nhello";
        else if (req.compare(0, 13, "GET /chunked ") == 0) resp = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n3\r\nabc\r\n4\r\ndefg\r\n0\r\n\r\n";
        else if (req.compare(0, 10, "GET /redir") == 0) resp = "HTTP/1.1 302 Found\r\nLocation: /plain\r\nContent-Length: 0\r\n\r\n";
        else if (req.compare(0, 11, "GET /until ") == 0) resp = "HTTP/1.1 200 OK\r\n\r\nno-length-body";
        else resp = "HTTP/1.1 404 Not Found\r\nContent-Length: 3\r\n\r\nnop";
        send(c, resp.data(), resp.size(), 0);
        close(c);
    }
}

int main(int argc, char** argv) {
    Url u;
    CHECK(parse_url("https://Example.com:8443/a/b?x=1#f", u) && u.scheme == "https" && u.host == "Example.com" && u.port == 8443 && u.target == "/a/b?x=1");
    CHECK(parse_url("http://h", u) && u.port == 80 && u.target == "/");
    CHECK(parse_url("http://[::1]:4533/x", u) && u.host == "::1" && u.port == 4533);
    CHECK(!parse_url("ftp://h/", u) && !parse_url("http://", u) && !parse_url("nonsense", u));
    CHECK(url_encode("a b&c=é") == "a%20b%26c%3D%C3%A9");

    if (argc >= 4 && !std::strcmp(argv[1], "--tls")) {  // --tls <url> <ca.pem>
        ConnectOptions o; o.ca_file = argv[3];
        std::string body;
        auto r = http_get_string(argv[2], body, o);
        std::printf("tls status=%d err=%s bytes=%zu\n", r.status, r.error.c_str(), body.size());
        CHECK(r.status == 200 && r.error.empty() && !body.empty());
        ConnectOptions bad; bad.ca_file = "/dev/null";  // untrusted: must fail
        auto r2 = http_get_string(argv[2], body, bad);
        CHECK(!r2.error.empty());
        std::printf(fails ? "FAILED\n" : "OK\n");
        return fails ? 1 : 0;
    }

    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in a{};
    a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); a.sin_port = 0;
    bind(lfd, reinterpret_cast<sockaddr*>(&a), sizeof a); listen(lfd, 8);
    socklen_t l = sizeof a; getsockname(lfd, reinterpret_cast<sockaddr*>(&a), &l);
    std::string base = "http://127.0.0.1:" + std::to_string(ntohs(a.sin_port));
    std::thread t(serve, lfd, 6);
    std::string b;
    auto r = http_get_string(base + "/plain", b);
    CHECK(r.status == 200 && b == "hello" && r.content_length == 5 && r.content_type == "text/plain");
    r = http_get_string(base + "/chunked", b); CHECK(r.status == 200 && b == "abcdefg" && r.error.empty());
    r = http_get_string(base + "/redir", b); CHECK(r.status == 200 && b == "hello");  // 2 connections
    r = http_get_string(base + "/until", b); CHECK(r.status == 200 && b == "no-length-body");
    r = http_get_string(base + "/missing", b); CHECK(r.status == 404 && b.empty());  // body not delivered for non-2xx
    t.join(); close(lfd);
    r = http_get_string(base + "/plain", b);
    CHECK(!r.error.empty());  // connection refused
    std::printf(fails ? "FAILED\n" : "OK\n");
    return fails ? 1 : 0;
}
