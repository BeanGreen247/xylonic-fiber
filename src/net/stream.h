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
};

// Returns nullptr and fills err on failure.
std::unique_ptr<Stream> connect_stream(const std::string& host, int port, bool tls, const ConnectOptions& opt, std::string& err);
