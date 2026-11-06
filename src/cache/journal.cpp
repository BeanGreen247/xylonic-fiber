#include "journal.h"

#include <cstdio>
#include <fcntl.h>
#include <unistd.h>
#include <fstream>
#include <map>
#include <utility>

namespace xf::cache {
namespace {
constexpr uint32_t kMagic = 0x58464348u; // XFCH
constexpr uint16_t kVersion = 1;
constexpr uint16_t kData = 1;
constexpr uint16_t kCommit = 2;
constexpr size_t kHeader = 32;

void put16(std::string& b, uint16_t v) { b.push_back(static_cast<char>(v)); b.push_back(static_cast<char>(v >> 8)); }
void put32(std::string& b, uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back(static_cast<char>(v >> (i * 8))); }
void put64(std::string& b, uint64_t v) { for (int i = 0; i < 8; ++i) b.push_back(static_cast<char>(v >> (i * 8))); }
uint16_t get16(const std::string& b, size_t p) { return static_cast<uint16_t>(static_cast<unsigned char>(b[p]) | (static_cast<unsigned char>(b[p + 1]) << 8)); }
uint32_t get32(const std::string& b, size_t p) { uint32_t v = 0; for (int i = 0; i < 4; ++i) v |= static_cast<uint32_t>(static_cast<unsigned char>(b[p + i])) << (i * 8); return v; }
uint64_t get64(const std::string& b, size_t p) { uint64_t v = 0; for (int i = 0; i < 8; ++i) v |= static_cast<uint64_t>(static_cast<unsigned char>(b[p + i])) << (i * 8); return v; }

std::string record(uint16_t type, uint64_t tx, uint64_t generation, const std::string& payload) {
    std::string h; h.reserve(kHeader + payload.size());
    put32(h, kMagic); put16(h, kVersion); put16(h, type); put64(h, tx); put64(h, generation);
    put32(h, static_cast<uint32_t>(payload.size())); put32(h, cache_crc32(payload.data(), payload.size()));
    h += payload; return h;
}
}

namespace {
struct CrcTables {
    uint32_t t[8][256];
    CrcTables() {
        for (uint32_t i = 0; i < 256; ++i) { uint32_t c = i; for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1u))); t[0][i] = c; }
        for (uint32_t i = 0; i < 256; ++i) for (int s = 1; s < 8; ++s) t[s][i] = (t[s - 1][i] >> 8) ^ t[0][t[s - 1][i] & 0xff];
    }
};
bool read_file(const std::string& path, std::string& out, bool& exists) {
    std::FILE* f = std::fopen(path.c_str(), "rb"); exists = f != nullptr; if (!f) return false;
    out.clear(); char buf[1 << 16] = {}; size_t n = 0;
    if (std::fseek(f, 0, SEEK_END) == 0) { long size = std::ftell(f); if (size > 0) out.reserve(static_cast<size_t>(size)); std::fseek(f, 0, SEEK_SET); }
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
    std::fclose(f); return true;
}
}

uint32_t cache_crc32(const void* data, size_t size) { return ~cache_crc32_update(0xffffffffu, data, size); }

uint32_t cache_crc32_update(uint32_t crc, const void* data, size_t size) {
    static const CrcTables tables;
    const auto* p = static_cast<const unsigned char*>(data);
    while (size >= 8) {
        uint32_t a = crc ^ (static_cast<uint32_t>(p[0]) | static_cast<uint32_t>(p[1]) << 8 | static_cast<uint32_t>(p[2]) << 16 | static_cast<uint32_t>(p[3]) << 24);
        crc = tables.t[7][a & 0xff] ^ tables.t[6][(a >> 8) & 0xff] ^ tables.t[5][(a >> 16) & 0xff] ^ tables.t[4][a >> 24] ^
              tables.t[3][p[4]] ^ tables.t[2][p[5]] ^ tables.t[1][p[6]] ^ tables.t[0][p[7]];
        p += 8; size -= 8;
    }
    while (size--) crc = (crc >> 8) ^ tables.t[0][(crc ^ *p++) & 0xff];
    return crc;
}

bool sync_path(const std::string& path) {
    const int fd = ::open(path.c_str(), O_RDONLY); if (fd < 0) return false;
    const bool ok = ::fsync(fd) == 0; ::close(fd); return ok;
}
bool sync_directory(const std::string& path) {
    const int fd = ::open(path.c_str(), O_RDONLY | O_DIRECTORY); if (fd < 0) return false;
    const bool ok = ::fsync(fd) == 0; ::close(fd); return ok;
}
bool Journal::sync() const { return sync_path(path_); }

bool Journal::append_transaction(uint64_t transaction, uint64_t generation, const std::vector<std::string>& payloads) const {
    std::string buffer;
    for (const auto& payload : payloads) buffer += record(kData, transaction, generation, payload);
    buffer += record(kCommit, transaction, generation, std::string());
    std::FILE* out = std::fopen(path_.c_str(), "ab"); if (!out) return false;
    bool ok = std::fwrite(buffer.data(), 1, buffer.size(), out) == buffer.size() && std::fflush(out) == 0;
    if (ok && durable_) ok = ::fsync(::fileno(out)) == 0;
    return std::fclose(out) == 0 && ok;
}

bool Journal::recover(std::vector<JournalRecord>& records) const {
    records.clear();
    std::string bytes; bool exists = false;
    if (!read_file(path_, bytes, exists)) return !exists; // An absent journal is an empty journal.
    struct Pending { uint64_t generation = 0; std::vector<std::string> payloads; };
    std::map<uint64_t, Pending> pending;
    size_t pos = 0;
    while (bytes.size() - pos >= kHeader) {
        if (get32(bytes, pos) != kMagic || get16(bytes, pos + 4) != kVersion) break;
        const uint16_t type = get16(bytes, pos + 6);
        const uint64_t tx = get64(bytes, pos + 8);
        const uint64_t generation = get64(bytes, pos + 16);
        const uint32_t length = get32(bytes, pos + 24);
        const uint32_t checksum = get32(bytes, pos + 28);
        if (length > bytes.size() - pos - kHeader) break;
        if (cache_crc32(bytes.data() + pos + kHeader, length) != checksum) break;
        const size_t payload_pos = pos + kHeader;
        pos += kHeader + length;
        if (type == kData) { auto& item = pending[tx]; item.generation = generation; item.payloads.emplace_back(bytes, payload_pos, length); }
        else if (type == kCommit) {
            auto found = pending.find(tx);
            if (found != pending.end()) {
                for (const auto& item : found->second.payloads) records.push_back({tx, found->second.generation, item});
                pending.erase(found);
            }
        } else break;
    }
    return true;
}

bool Journal::compact() const {
    const std::string temp = path_ + ".tmp";
    { std::ofstream out(temp, std::ios::binary | std::ios::trunc); if (!out) return false; out.flush(); if (!out) { std::remove(temp.c_str()); return false; } }
    if (std::rename(temp.c_str(), path_.c_str()) != 0) return false;
    const size_t slash = path_.find_last_of('/'); sync_directory(slash == std::string::npos ? "." : path_.substr(0, slash));
    return true;
}

bool write_snapshot(const std::string& path, const std::string& payload) {
    const std::string temp = path + ".tmp";
    const std::string data = record(kData, 0, 0, payload);
    { std::FILE* out = std::fopen(temp.c_str(), "wb"); if (!out) return false; const bool ok = std::fwrite(data.data(), 1, data.size(), out) == data.size() && std::fflush(out) == 0 && ::fsync(::fileno(out)) == 0; if (std::fclose(out) != 0 || !ok) { std::remove(temp.c_str()); return false; } }
    if (std::rename(temp.c_str(), path.c_str()) != 0) return false;
    const size_t slash = path.find_last_of('/'); sync_directory(slash == std::string::npos ? "." : path.substr(0, slash));
    return true;
}

bool read_snapshot(const std::string& path, std::string& payload) {
    std::string data; bool exists = false; if (!read_file(path, data, exists)) return false;
    if (data.size() < kHeader || get32(data, 0) != kMagic || get16(data, 4) != kVersion || get16(data, 6) != kData) return false;
    const uint32_t length = get32(data, 24); if (length != data.size() - kHeader) return false;
    payload = data.substr(kHeader); return cache_crc32(payload.data(), payload.size()) == get32(data, 28);
}
} // namespace xf::cache
