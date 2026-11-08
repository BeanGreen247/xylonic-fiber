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
    // durable: fsync every committed transaction. Otherwise call sync() at a batch boundary (group commit).
    explicit Journal(std::string path, bool durable = false) : path_(std::move(path)), durable_(durable) {}
    bool sync() const;
    bool append_transaction(uint64_t transaction, uint64_t generation, const std::vector<std::string>& payloads) const;
    bool recover(std::vector<JournalRecord>& records) const;
    bool compact() const;
private:
    std::string path_;
    bool durable_ = false;
};

bool write_snapshot(const std::string& path, const std::string& payload);
bool read_snapshot(const std::string& path, std::string& payload);
uint32_t cache_crc32(const void* data, size_t size);
// Incremental form: start with 0xffffffff, feed chunks, finish with ~state.
uint32_t cache_crc32_update(uint32_t state, const void* data, size_t size);
bool sync_path(const std::string& path);        // fsync a file by path
bool sync_directory(const std::string& path);   // fsync a directory so a rename survives power loss

} // namespace xf::cache
