#include "journal.h"

#include <cstdio>
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

uint32_t cache_crc32(const void* data, size_t size) {
    const auto* p = static_cast<const unsigned char*>(data);
    uint32_t crc = 0xffffffffu;
    for (size_t i = 0; i < size; ++i) {
        crc ^= p[i];
        for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

bool Journal::append_transaction(uint64_t transaction, uint64_t generation, const std::vector<std::string>& payloads) const {
    std::ofstream out(path_, std::ios::binary | std::ios::app);
    if (!out) return false;
    for (const auto& payload : payloads) out.write(record(kData, transaction, generation, payload).data(), static_cast<std::streamsize>(kHeader + payload.size()));
    const std::string commit = record(kCommit, transaction, generation, std::string());
    out.write(commit.data(), static_cast<std::streamsize>(commit.size())); out.flush();
    return static_cast<bool>(out);
}

bool Journal::recover(std::vector<JournalRecord>& records) const {
    records.clear();
    std::ifstream in(path_, std::ios::binary);
    if (!in) return true; // An absent journal is an empty journal.
    std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
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
        const std::string payload = bytes.substr(pos + kHeader, length);
        if (cache_crc32(payload.data(), payload.size()) != checksum) break;
        pos += kHeader + length;
        if (type == kData) { auto& item = pending[tx]; item.generation = generation; item.payloads.push_back(payload); }
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
    return std::rename(temp.c_str(), path_.c_str()) == 0;
}

bool write_snapshot(const std::string& path, const std::string& payload) {
    const std::string temp = path + ".tmp";
    const std::string data = record(kData, 0, 0, payload);
    { std::ofstream out(temp, std::ios::binary | std::ios::trunc); if (!out) return false; out.write(data.data(), static_cast<std::streamsize>(data.size())); out.flush(); if (!out) { std::remove(temp.c_str()); return false; } }
    std::remove(path.c_str());
    return std::rename(temp.c_str(), path.c_str()) == 0;
}

bool read_snapshot(const std::string& path, std::string& payload) {
    std::ifstream in(path, std::ios::binary); if (!in) return false;
    std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (data.size() < kHeader || get32(data, 0) != kMagic || get16(data, 4) != kVersion || get16(data, 6) != kData) return false;
    const uint32_t length = get32(data, 24); if (length != data.size() - kHeader) return false;
    payload = data.substr(kHeader); return cache_crc32(payload.data(), payload.size()) == get32(data, 28);
}
} // namespace xf::cache
