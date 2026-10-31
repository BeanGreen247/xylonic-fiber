#include "stream.h"
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#include <psa/crypto.h>

namespace {
class TcpStream : public Stream {
public:
    explicit TcpStream(int fd, int timeout_ms) : fd_(fd), timeout_(timeout_ms) {}
    ~TcpStream() override { if (fd_ >= 0) ::close(fd_); }
    long read(void* buf, size_t n) override {
        if (!wait(POLLIN)) return -1;
        ssize_t r = ::recv(fd_, buf, n, 0);
        return r < 0 ? -1 : r;
    }
    bool write_all(const void* buf, size_t n) override {
        const char* p = static_cast<const char*>(buf);
        while (n) {
            if (!wait(POLLOUT)) return false;
            ssize_t w = ::send(fd_, p, n, MSG_NOSIGNAL);
            if (w <= 0) return false;
            p += w;
            n -= static_cast<size_t>(w);
        }
        return true;
    }
    int fd() const { return fd_; }
    bool wait(short ev) {
        pollfd pf{fd_, ev, 0};
        int r;
        do { r = ::poll(&pf, 1, timeout_); } while (r < 0 && errno == EINTR);
        return r > 0;
    }
private:
    int fd_;
    int timeout_;
};

class TlsStream : public Stream {
public:
    ~TlsStream() override {
        if (ready_) mbedtls_ssl_close_notify(&ssl_);
        mbedtls_ssl_free(&ssl_);
        mbedtls_ssl_config_free(&conf_);
        mbedtls_x509_crt_free(&ca_);
        mbedtls_ctr_drbg_free(&drbg_);
        mbedtls_entropy_free(&entropy_);
    }
    bool init(std::unique_ptr<TcpStream> tcp, const std::string& host, const std::string& ca_file, std::string& err) {
        tcp_ = std::move(tcp);
        mbedtls_ssl_init(&ssl_);
        mbedtls_ssl_config_init(&conf_);
        mbedtls_x509_crt_init(&ca_);
        mbedtls_ctr_drbg_init(&drbg_);
        mbedtls_entropy_init(&entropy_);
        if (psa_crypto_init() != PSA_SUCCESS) { err = "psa_crypto_init"; return false; }
        if (mbedtls_ctr_drbg_seed(&drbg_, mbedtls_entropy_func, &entropy_, nullptr, 0)) { err = "drbg seed"; return false; }
        if (mbedtls_ssl_config_defaults(&conf_, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT)) { err = "ssl config"; return false; }
        if (!load_roots(ca_file)) { err = "no CA certificates found"; return false; }
        mbedtls_ssl_conf_ca_chain(&conf_, &ca_, nullptr);
        mbedtls_ssl_conf_authmode(&conf_, MBEDTLS_SSL_VERIFY_REQUIRED);
        mbedtls_ssl_conf_rng(&conf_, mbedtls_ctr_drbg_random, &drbg_);
        if (mbedtls_ssl_setup(&ssl_, &conf_)) { err = "ssl setup"; return false; }
        if (mbedtls_ssl_set_hostname(&ssl_, host.c_str())) { err = "set hostname"; return false; }
        mbedtls_ssl_set_bio(&ssl_, this, &TlsStream::bio_send, &TlsStream::bio_recv, nullptr);
        int r;
        while ((r = mbedtls_ssl_handshake(&ssl_)) != 0) {
            if (r != MBEDTLS_ERR_SSL_WANT_READ && r != MBEDTLS_ERR_SSL_WANT_WRITE) {
                char b[160];
                mbedtls_strerror(r, b, sizeof b);
                err = std::string("TLS handshake: ") + b;
                return false;
            }
        }
        ready_ = true;
        return true;
    }
    long read(void* buf, size_t n) override {
        for (;;) {
            int r = mbedtls_ssl_read(&ssl_, static_cast<unsigned char*>(buf), n);
            if (r > 0) return r;
            if (r == 0 || r == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) return 0;
            if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
            return -1;
        }
    }
    bool write_all(const void* buf, size_t n) override {
        const unsigned char* p = static_cast<const unsigned char*>(buf);
        while (n) {
            int w = mbedtls_ssl_write(&ssl_, p, n);
            if (w == MBEDTLS_ERR_SSL_WANT_READ || w == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
            if (w <= 0) return false;
            p += w;
            n -= static_cast<size_t>(w);
        }
        return true;
    }
private:
    bool load_roots(const std::string& ca_file) {
        if (!ca_file.empty()) return mbedtls_x509_crt_parse_file(&ca_, ca_file.c_str()) >= 0 && ca_.version;
        static const char* files[] = {"/etc/ssl/certs/ca-certificates.crt", "/etc/pki/tls/certs/ca-bundle.crt",
                                      "/etc/ssl/cert.pem", "/etc/ssl/ca-bundle.pem"};
        for (const char* f : files)
            if (mbedtls_x509_crt_parse_file(&ca_, f) >= 0 && ca_.version) return true;
        // Android keeps one PEM per file in this directory.
        return mbedtls_x509_crt_parse_path(&ca_, "/system/etc/security/cacerts") >= 0 && ca_.version;
    }
    static int bio_send(void* ctx, const unsigned char* b, size_t n) {
        auto* s = static_cast<TlsStream*>(ctx);
        if (!s->tcp_->wait(POLLOUT)) return MBEDTLS_ERR_SSL_TIMEOUT;
        ssize_t w = ::send(s->tcp_->fd(), b, n, MSG_NOSIGNAL);
        return w < 0 ? MBEDTLS_ERR_SSL_INTERNAL_ERROR : static_cast<int>(w);
    }
    static int bio_recv(void* ctx, unsigned char* b, size_t n) {
        auto* s = static_cast<TlsStream*>(ctx);
        if (!s->tcp_->wait(POLLIN)) return MBEDTLS_ERR_SSL_TIMEOUT;
        ssize_t r = ::recv(s->tcp_->fd(), b, n, 0);
        return r < 0 ? MBEDTLS_ERR_SSL_INTERNAL_ERROR : static_cast<int>(r);
    }
    std::unique_ptr<TcpStream> tcp_;
    mbedtls_ssl_context ssl_;
    mbedtls_ssl_config conf_;
    mbedtls_x509_crt ca_;
    mbedtls_ctr_drbg_context drbg_;
    mbedtls_entropy_context entropy_;
    bool ready_ = false;
};

std::unique_ptr<TcpStream> tcp_connect(const std::string& host, int port, int timeout_ms, std::string& err) {
    addrinfo hints{}, *res = nullptr;
    hints.ai_socktype = SOCK_STREAM;
    std::string ps = std::to_string(port);
    if (getaddrinfo(host.c_str(), ps.c_str(), &hints, &res) != 0 || !res) { err = "resolve " + host; return nullptr; }
    int fd = -1;
    for (addrinfo* a = res; a; a = a->ai_next) {
        fd = ::socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (fd < 0) continue;
        int fl = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, fl | O_NONBLOCK);
        int r = ::connect(fd, a->ai_addr, a->ai_addrlen);
        if (r < 0 && errno == EINPROGRESS) {
            pollfd pf{fd, POLLOUT, 0};
            if (::poll(&pf, 1, timeout_ms) > 0) {
                int e = 0;
                socklen_t l = sizeof e;
                getsockopt(fd, SOL_SOCKET, SO_ERROR, &e, &l);
                r = e ? -1 : 0;
            } else {
                r = -1;
            }
        }
        if (r == 0) { fcntl(fd, F_SETFL, fl); break; }
        ::close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    if (fd < 0) { err = "connect " + host + ":" + ps; return nullptr; }
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    return std::make_unique<TcpStream>(fd, timeout_ms);
}
}  // namespace

std::unique_ptr<Stream> connect_stream(const std::string& host, int port, bool tls, const ConnectOptions& opt, std::string& err) {
    auto tcp = tcp_connect(host, port, opt.timeout_ms, err);
    if (!tcp) return nullptr;
    if (!tls) return tcp;
    auto t = std::make_unique<TlsStream>();
    if (!t->init(std::move(tcp), host, opt.ca_file, err)) return nullptr;
    return t;
}
