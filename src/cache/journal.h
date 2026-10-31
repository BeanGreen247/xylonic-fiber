#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace xf::cache {

struct JournalRecord {
    uint64_t transaction = 0;
    uint64_t generation = 0;
    std::string payload;
};

class Journal {
public:
    explicit Journal(std::string path) : path_(std::move(path)) {}
    bool append_transaction(uint64_t transaction, uint64_t generation, const std::vector<std::string>& payloads) const;
    bool recover(std::vector<JournalRecord>& records) const;
private:
    std::string path_;
};

bool write_snapshot(const std::string& path, const std::string& payload);
bool read_snapshot(const std::string& path, std::string& payload);
uint32_t cache_crc32(const void* data, size_t size);

} // namespace xf::cache
