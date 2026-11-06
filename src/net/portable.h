// Socket portability shims. Apple has no MSG_NOSIGNAL (SIGPIPE is ignored process-wide in main instead).
// Windows: Winsock2 behind the same POSIX-shaped helpers (xf_*), started once per process by xf_net_init().
#pragma once
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <cstdint>
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif
using xf_sock_t = SOCKET;
constexpr xf_sock_t kBadSock = INVALID_SOCKET;
inline void xf_net_init() { static const bool once = [] { WSADATA d; return WSAStartup(MAKEWORD(2, 2), &d) == 0; }(); (void)once; }
inline void xf_close(xf_sock_t s) { ::closesocket(s); }
inline int xf_poll(xf_sock_t s, short ev, int timeout_ms) { WSAPOLLFD p{s, ev, 0}; return ::WSAPoll(&p, 1, timeout_ms); }
inline void xf_set_nonblock(xf_sock_t s, bool on) { u_long m = on ? 1 : 0; ::ioctlsocket(s, FIONBIO, &m); }
inline bool xf_in_progress() { const int e = WSAGetLastError(); return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS; }
inline bool xf_interrupted() { return false; }
inline long xf_recv(xf_sock_t s, void* b, size_t n) { return ::recv(s, static_cast<char*>(b), static_cast<int>(n), 0); }
inline long xf_send(xf_sock_t s, const void* b, size_t n) { return ::send(s, static_cast<const char*>(b), static_cast<int>(n), 0); }
inline int xf_sockopt_error(xf_sock_t s) { int e = 0; int l = sizeof e; ::getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&e), &l); return e; }
inline void xf_nodelay(xf_sock_t s) { int one = 1; ::setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&one), sizeof one); }
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif
using xf_sock_t = int;
constexpr xf_sock_t kBadSock = -1;
inline void xf_net_init() {}
inline void xf_close(xf_sock_t s) { ::close(s); }
inline int xf_poll(xf_sock_t s, short ev, int timeout_ms) { pollfd p{s, ev, 0}; int r; do { r = ::poll(&p, 1, timeout_ms); } while (r < 0 && errno == EINTR); return r; }
inline void xf_set_nonblock(xf_sock_t s, bool on) { int fl = fcntl(s, F_GETFL, 0); fcntl(s, F_SETFL, on ? (fl | O_NONBLOCK) : (fl & ~O_NONBLOCK)); }
inline bool xf_in_progress() { return errno == EINPROGRESS; }
inline bool xf_interrupted() { return errno == EINTR; }
inline long xf_recv(xf_sock_t s, void* b, size_t n) { return ::recv(s, b, n, 0); }
inline long xf_send(xf_sock_t s, const void* b, size_t n) { return ::send(s, b, n, MSG_NOSIGNAL); }
inline int xf_sockopt_error(xf_sock_t s) { int e = 0; socklen_t l = sizeof e; ::getsockopt(s, SOL_SOCKET, SO_ERROR, &e, &l); return e; }
inline void xf_nodelay(xf_sock_t s) { int one = 1; ::setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one); }
#endif
#include "stream.h"
