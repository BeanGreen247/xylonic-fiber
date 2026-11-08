// Immutable metadata generations: one writer builds generation N+1 away from readers, journals it,
// then publishes it atomically. Readers hold a shared_ptr and never take a lock the writer holds.
#pragma once
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>
#include "journal.h"
#include "metadata.h"

namespace xf::cache {
struct UpdateBatch {
    bool reset = false;                      // replace every song (playlists are kept) instead of merging
    std::vector<SongInput> songs;
    std::vector<std::pair<std::string, bool>> likes;
    std::vector<std::pair<std::string, std::vector<std::string>>> playlists;
    bool empty() const { return !reset && songs.empty() && likes.empty() && playlists.empty(); }
};
// Operation payloads; one journal transaction carries every payload of a batch.
std::vector<std::string> encode_batch(const UpdateBatch& batch);
bool decode_payload(const std::string& payload, UpdateBatch& out);
bool apply_batch(MetadataSnapshot& snapshot, const UpdateBatch& batch, bool finalize = true);

class MetadataStore {
public:
    // dir empty: memory only (nothing persisted). Otherwise <dir>/metadata.{snapshot,journal,index}.
    explicit MetadataStore(std::string dir);
    bool open();                                   // snapshot + complete journal transactions, then publish
    // Validate, journal (fsync'd when durable), then publish. With no reader holding the live generation it is
    // updated in place; otherwise N+1 is built from a copy, so held generations never change.
    bool apply(const UpdateBatch& batch);
    bool compact();                                // snapshot the live generation, then drop the journal
    uint64_t disk_bytes() const;                   // snapshot + index + journal on disk
    bool sync() const;                             // fsync the journal (group commit when durable is off)
    bool needs_compact() const;                    // journal holds batches newer than the snapshot
    std::shared_ptr<const MetadataSnapshot> current() const;
    uint64_t generation() const;
private:
    std::string snapshot_path() const { return dir_ + "/metadata.snapshot"; }
    std::string index_path() const { return dir_ + "/metadata.index"; }
    std::string journal_path() const { return dir_ + "/metadata.journal"; }
    std::string dir_;
    mutable std::mutex writer_;
    mutable std::mutex publish_;
    std::shared_ptr<const MetadataSnapshot> live_;
    uint64_t generation_ = 0, transaction_ = 0, compacted_ = 0;
public:
    bool durable = true;   // fsync each batch; one batch is one response page, so this is not per-song
};
} // namespace xf::cache
