// Byte stream over TCP, optionally TLS. Blocking with timeouts. POSIX sockets; a Winsock shim comes with the Windows port.
#pragma once
#include <cstddef>
#include <memory>
#include <string>

class Stream {
public:
    virtual ~Stream() = default;
    // Returns bytes read (>0), 0 on clean EOF, -1 on error/timeout.
    virtual long read(void* buf, size_t n) = 0;
    // Writes all n bytes; false on error.
    virtual bool write_all(const void* buf, size_t n) = 0;
};

struct ConnectOptions {
    int timeout_ms = 10000;
    std::string ca_file;  // PEM bundle; empty = system locations
    // Trust a self-signed server by pinning its leaf certificate (lowercase hex SHA-256 of the DER).
    // When set, chain validation is skipped and only the pin must match.
    std::string pin_sha256;
};

// Handshakes without validating and returns the leaf certificate SHA-256 so a UI can ask the user to trust it.
bool fetch_cert_fingerprint(const std::string& host, int port, int timeout_ms, std::string& hex, std::string& err);

// Returns nullptr and fills err on failure.
std::unique_ptr<Stream> connect_stream(const std::string& host, int port, bool tls, const ConnectOptions& opt, std::string& err);
