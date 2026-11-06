#include "stream.h"
#include "portable.h"
#include <cerrno>
#include <mutex>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/sha256.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#include <psa/crypto.h>
#if defined(_WIN32)
#include <wincrypt.h>   // system root store (crypt32)
#endif

namespace {
class TcpStream : public Stream {
public:
    explicit TcpStream(xf_sock_t fd, int timeout_ms) : fd_(fd), timeout_(timeout_ms) {}
    ~TcpStream() override { if (fd_ != kBadSock) xf_close(fd_); }
    long read(void* buf, size_t n) override {
        if (!wait(POLLIN)) return -1;
        long r = xf_recv(fd_, buf, n);
        return r < 0 ? -1 : r;
    }
    bool write_all(const void* buf, size_t n) override {
        const char* p = static_cast<const char*>(buf);
        while (n) {
            if (!wait(POLLOUT)) return false;
            long w = xf_send(fd_, p, n);
            if (w <= 0) return false;
            p += w;
            n -= static_cast<size_t>(w);
        }
        return true;
    }
    xf_sock_t fd() const { return fd_; }
    bool wait(short ev) {
        return xf_poll(fd_, ev, timeout_) > 0;
    }
private:
    xf_sock_t fd_ = kBadSock;
    int timeout_ = 0;
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
    enum class Verify { Chain, Pin, None };
    std::string fingerprint;
    bool init(std::unique_ptr<TcpStream> tcp, const std::string& host, const std::string& ca_file, Verify mode, const std::string& pin, std::string& err) {
        tcp_ = std::move(tcp);
        mbedtls_ssl_init(&ssl_);
        mbedtls_ssl_config_init(&conf_);
        mbedtls_x509_crt_init(&ca_);
        mbedtls_ctr_drbg_init(&drbg_);
        mbedtls_entropy_init(&entropy_);
        if (psa_crypto_init() != PSA_SUCCESS) { err = "psa_crypto_init"; return false; }
        if (mbedtls_ctr_drbg_seed(&drbg_, mbedtls_entropy_func, &entropy_, nullptr, 0)) { err = "drbg seed"; return false; }
        if (mbedtls_ssl_config_defaults(&conf_, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT)) { err = "ssl config"; return false; }
        if (mode == Verify::Chain) {
            if (!load_roots(ca_file)) { err = "no CA certificates found"; return false; }
            mbedtls_ssl_conf_ca_chain(&conf_, shared_ ? const_cast<mbedtls_x509_crt*>(shared_) : &ca_, nullptr);
            mbedtls_ssl_conf_authmode(&conf_, MBEDTLS_SSL_VERIFY_REQUIRED);
        } else {
            mbedtls_ssl_conf_authmode(&conf_, MBEDTLS_SSL_VERIFY_NONE);
        }
        mbedtls_ssl_conf_rng(&conf_, mbedtls_ctr_drbg_random, &drbg_);
        if (mbedtls_ssl_setup(&ssl_, &conf_)) { err = "ssl setup"; return false; }
        if (mbedtls_ssl_set_hostname(&ssl_, host.c_str())) { err = "set hostname"; return false; }
        mbedtls_ssl_set_bio(&ssl_, this, &TlsStream::bio_send, &TlsStream::bio_recv, nullptr);
        int r = 0;
        while ((r = mbedtls_ssl_handshake(&ssl_)) != 0) {
            if (r != MBEDTLS_ERR_SSL_WANT_READ && r != MBEDTLS_ERR_SSL_WANT_WRITE) {
                char b[160] = {};
                mbedtls_strerror(r, b, sizeof b);
                err = std::string("TLS handshake: ") + b;
                return false;
            }
        }
        ready_ = true;
        if (mode != Verify::Chain) {
            const mbedtls_x509_crt* c = mbedtls_ssl_get_peer_cert(&ssl_);
            unsigned char h[32] = {};
            if (!c || mbedtls_sha256(c->raw.p, c->raw.len, h, 0)) { err = "no peer certificate"; return false; }
            static const char* d = "0123456789abcdef";
            fingerprint.clear();
            for (unsigned char b : h) { fingerprint += d[b >> 4]; fingerprint += d[b & 15]; }
            if (mode == Verify::Pin && fingerprint != pin) { err = "certificate does not match the trusted fingerprint"; return false; }
        }
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
    // The system roots are parsed once per process (~140 PEM files on Android: base64 + X.509 parsing on every connection was
    // a measurable share of CPU and of connect latency) and shared read-only by every stream.
    static const mbedtls_x509_crt* shared_roots() {
        static std::mutex mu;
        static mbedtls_x509_crt crt;
        static bool loaded = false;
        std::lock_guard<std::mutex> lk(mu);
        if (!loaded) {
            mbedtls_x509_crt_init(&crt);
            if (parse_system_roots(&crt)) loaded = true; else mbedtls_x509_crt_free(&crt);   // retried on the next connection
        }
        return loaded ? &crt : nullptr;
    }
    bool load_roots(const std::string& ca_file) {
        if (!ca_file.empty()) return mbedtls_x509_crt_parse_file(&ca_, ca_file.c_str()) >= 0 && ca_.version;
        shared_ = shared_roots();
        return shared_ != nullptr;
    }
    static bool parse_system_roots(mbedtls_x509_crt* ca_p) {
        mbedtls_x509_crt& ca_ = *ca_p;
#if defined(_WIN32)
        // Windows keeps its trusted roots in the system "ROOT" store; each certificate is handed to mbedTLS as DER.
        if (HCERTSTORE st = CertOpenSystemStoreA(0, "ROOT")) {
            for (PCCERT_CONTEXT c = CertEnumCertificatesInStore(st, nullptr); c; c = CertEnumCertificatesInStore(st, c))
                mbedtls_x509_crt_parse_der(&ca_, c->pbCertEncoded, c->cbCertEncoded);   // unparsable ones are skipped
            CertCloseStore(st, 0);
            if (ca_.version) return true;
        }
        return false;
#endif
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
        long w = xf_send(s->tcp_->fd(), b, n);
        return w < 0 ? MBEDTLS_ERR_SSL_INTERNAL_ERROR : static_cast<int>(w);
    }
    static int bio_recv(void* ctx, unsigned char* b, size_t n) {
        auto* s = static_cast<TlsStream*>(ctx);
        if (!s->tcp_->wait(POLLIN)) return MBEDTLS_ERR_SSL_TIMEOUT;
        long r = xf_recv(s->tcp_->fd(), b, n);
        return r < 0 ? MBEDTLS_ERR_SSL_INTERNAL_ERROR : static_cast<int>(r);
    }
    std::unique_ptr<TcpStream> tcp_;
    mbedtls_ssl_context ssl_;
    mbedtls_ssl_config conf_;
    mbedtls_x509_crt ca_;
    const mbedtls_x509_crt* shared_ = nullptr;   // process-wide system roots (when no explicit CA file)
    mbedtls_ctr_drbg_context drbg_;
    mbedtls_entropy_context entropy_;
    bool ready_ = false;
};

std::unique_ptr<TcpStream> tcp_connect(const std::string& host, int port, int timeout_ms, std::string& err) {
    xf_net_init();
    addrinfo hints{}, *res = nullptr;
    hints.ai_socktype = SOCK_STREAM;
    std::string ps = std::to_string(port);
    if (getaddrinfo(host.c_str(), ps.c_str(), &hints, &res) != 0 || !res) { err = "resolve " + host; return nullptr; }
    xf_sock_t fd = kBadSock;
    for (addrinfo* a = res; a; a = a->ai_next) {
        fd = ::socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (fd == kBadSock) continue;
        xf_set_nonblock(fd, true);
        int r = ::connect(fd, a->ai_addr, static_cast<int>(a->ai_addrlen));
        if (r < 0 && xf_in_progress()) {
            if (xf_poll(fd, POLLOUT, timeout_ms) > 0) r = xf_sockopt_error(fd) ? -1 : 0;
            else r = -1;
        }
        if (r == 0) { xf_set_nonblock(fd, false); break; }
        xf_close(fd);
        fd = kBadSock;
    }
    freeaddrinfo(res);
    if (fd == kBadSock) { err = "connect " + host + ":" + ps; return nullptr; }
    xf_nodelay(fd);
    return std::make_unique<TcpStream>(fd, timeout_ms);
}
}  // namespace

std::unique_ptr<Stream> connect_stream(const std::string& host, int port, bool tls, const ConnectOptions& opt, std::string& err) {
    auto tcp = tcp_connect(host, port, opt.timeout_ms, err);
    if (!tcp) return nullptr;
    if (!tls) return tcp;
    auto t = std::make_unique<TlsStream>();
    bool pin = !opt.pin_sha256.empty();
    if (!t->init(std::move(tcp), host, opt.ca_file, pin ? TlsStream::Verify::Pin : TlsStream::Verify::Chain, opt.pin_sha256, err)) return nullptr;
    return t;
}

bool fetch_cert_fingerprint(const std::string& host, int port, int timeout_ms, std::string& hex, std::string& err) {
    auto tcp = tcp_connect(host, port, timeout_ms, err);
    if (!tcp) return false;
    TlsStream t;
    if (!t.init(std::move(tcp), host, "", TlsStream::Verify::None, "", err)) return false;
    hex = t.fingerprint;
    return true;
}
