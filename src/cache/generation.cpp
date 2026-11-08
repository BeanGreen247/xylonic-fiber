#include "generation.h"
#include <cstdio>
#include <sys/stat.h>

namespace xf::cache {
namespace {
void put32(std::string& b, uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back(static_cast<char>(v >> (i * 8))); }
void put64(std::string& b, uint64_t v) { for (int i = 0; i < 8; ++i) b.push_back(static_cast<char>(v >> (i * 8))); }
void put_str(std::string& b, const std::string& s) { put32(b, static_cast<uint32_t>(s.size())); b += s; }
struct Reader {
    const std::string& b; size_t p = 0;
    bool u32(uint32_t& v) { if (b.size() - p < 4) return false; v = 0; for (int i = 0; i < 4; ++i) v |= static_cast<uint32_t>(static_cast<unsigned char>(b[p++])) << (i * 8); return true; }
    bool u64(uint64_t& v) { if (b.size() - p < 8) return false; v = 0; for (int i = 0; i < 8; ++i) v |= static_cast<uint64_t>(static_cast<unsigned char>(b[p++])) << (i * 8); return true; }
    bool str(std::string& s) { uint32_t n; if (!u32(n) || n > b.size() - p) return false; s.assign(b, p, n); p += n; return true; }
};
constexpr char kSong = 'S', kLike = 'L', kPlaylist = 'P', kReset = 'Z';

std::string encode_song(const SongInput& s) {
    std::string b(1, kSong);
    for (const std::string* v : {&s.id, &s.title, &s.artist_id, &s.artist_name, &s.album_id, &s.album_name, &s.album_artist_name, &s.format, &s.codec}) put_str(b, *v);
    for (uint32_t v : {s.track, s.disc, s.year, s.duration, s.bitrate, s.sample_rate}) put32(b, v);
    put32(b, static_cast<uint32_t>(s.channels) | static_cast<uint32_t>(s.bit_depth) << 16); put64(b, s.size); b.push_back(s.liked ? 1 : 0); put_str(b, s.cover_art);
    return b;
}
}

std::vector<std::string> encode_batch(const UpdateBatch& batch) {
    std::vector<std::string> out; out.reserve(batch.songs.size() + batch.likes.size() + batch.playlists.size() + 1);
    if (batch.reset) out.emplace_back(1, kReset);
    for (const auto& s : batch.songs) out.push_back(encode_song(s));
    for (const auto& l : batch.likes) { std::string b(1, kLike); put_str(b, l.first); b.push_back(l.second ? 1 : 0); out.push_back(std::move(b)); }
    for (const auto& p : batch.playlists) { std::string b(1, kPlaylist); put_str(b, p.first); put32(b, static_cast<uint32_t>(p.second.size())); for (const auto& id : p.second) put_str(b, id); out.push_back(std::move(b)); }
    return out;
}

bool decode_payload(const std::string& payload, UpdateBatch& out) {
    if (payload.empty()) return false;
    Reader r{payload, 1};
    if (payload[0] == kReset) { out.reset = true; return true; }
    if (payload[0] == kSong) {
        SongInput s; uint32_t cd = 0; uint64_t size = 0;
        for (std::string* v : {&s.id, &s.title, &s.artist_id, &s.artist_name, &s.album_id, &s.album_name, &s.album_artist_name, &s.format, &s.codec}) if (!r.str(*v)) return false;
        for (uint32_t* v : {&s.track, &s.disc, &s.year, &s.duration, &s.bitrate, &s.sample_rate}) if (!r.u32(*v)) return false;
        if (!r.u32(cd) || !r.u64(size) || r.p >= payload.size() || s.id.empty()) return false;
        s.channels = static_cast<uint16_t>(cd & 0xffff); s.bit_depth = static_cast<uint16_t>(cd >> 16); s.size = size; s.liked = payload[r.p++] != 0;
        if (!r.str(s.cover_art)) return false;
        out.songs.push_back(std::move(s)); return true;
    }
    if (payload[0] == kLike) {
        std::string id; if (!r.str(id) || r.p >= payload.size()) return false;
        out.likes.emplace_back(std::move(id), payload[r.p] != 0); return true;
    }
    if (payload[0] == kPlaylist) {
        std::string id; uint32_t n; if (!r.str(id) || !r.u32(n) || n > payload.size()) return false;
        std::vector<std::string> ids(n); for (auto& s : ids) if (!r.str(s)) return false;
        out.playlists.emplace_back(std::move(id), std::move(ids)); return true;
    }
    return false;
}

bool apply_batch(MetadataSnapshot& snapshot, const UpdateBatch& batch, bool finalize) {
    std::vector<std::pair<std::string, std::vector<std::string>>> keep;
    if (batch.reset) { keep = snapshot.export_playlists(); snapshot = MetadataSnapshot(); }
    for (const auto& s : batch.songs) if (!snapshot.upsert(s)) return false;
    for (const auto& p : keep) snapshot.set_playlist(p.first, p.second);
    for (const auto& l : batch.likes) snapshot.set_liked(l.first, l.second);
    for (const auto& p : batch.playlists) if (!snapshot.set_playlist(p.first, p.second)) return false;
    if (finalize) snapshot.finalize();
    return true;
}

MetadataStore::MetadataStore(std::string dir) : dir_(std::move(dir)), live_(std::make_shared<MetadataSnapshot>()) {}

std::shared_ptr<const MetadataSnapshot> MetadataStore::current() const { std::lock_guard<std::mutex> lk(publish_); return live_; }
uint64_t MetadataStore::generation() const { std::lock_guard<std::mutex> lk(writer_); return generation_; }

bool MetadataStore::open() {
    std::lock_guard<std::mutex> lk(writer_);
    if (dir_.empty()) return true;
    ::mkdir(dir_.c_str(), 0755);
    auto next = std::make_shared<MetadataSnapshot>(); uint64_t base = 0, tx = 0;
    std::string payload;
    if (read_snapshot(snapshot_path(), payload)) {      // A rejected snapshot falls back to journal-only recovery.
        Reader r{payload, 0}; uint64_t gen = 0, count = 0;
        if (r.u64(gen) && r.u64(tx) && r.u64(count)) {
            UpdateBatch batch; bool ok = true;
            for (uint64_t i = 0; i < count && ok; ++i) { std::string item; ok = r.str(item) && decode_payload(item, batch); }
            if (ok) { apply_batch(*next, batch, false); base = gen; }
            else { next = std::make_shared<MetadataSnapshot>(); tx = 0; }
        } else tx = 0;
    }
    std::vector<JournalRecord> records; Journal journal(journal_path());
    if (!journal.recover(records)) return false;
    uint64_t gen = base; uint64_t last_tx = 0; bool replayed = false;
    UpdateBatch pending;
    for (const auto& rec : records) {
        if (rec.generation <= base) continue;           // Already folded into the snapshot.
        replayed = true;
        if (rec.transaction != last_tx && !pending.empty()) { apply_batch(*next, pending, false); pending = UpdateBatch(); }
        last_tx = rec.transaction; gen = rec.generation; tx = rec.transaction > tx ? rec.transaction : tx;
        decode_payload(rec.payload, pending);           // Undecodable payloads are skipped, never fatal.
    }
    if (!pending.empty()) apply_batch(*next, pending, false);
    bool indexed = false;                                // A persisted index is only valid for exactly the snapshot it was built from.
    if (!replayed && base > 0) {
        std::string index;
        if (read_snapshot(index_path(), index) && index.size() > 8) {
            Reader r{index, 0}; uint64_t igen = 0;
            if (r.u64(igen) && igen == base) indexed = next->import_index(index.substr(8));
        }
    }
    if (!indexed) next->finalize();
    generation_ = gen; transaction_ = tx; compacted_ = base;
    std::lock_guard<std::mutex> pub(publish_); live_ = std::move(next);
    return true;
}

bool MetadataStore::apply(const UpdateBatch& batch) {
    if (batch.empty()) return true;
    for (const auto& s : batch.songs) if (s.id.empty()) return false;
    for (const auto& p : batch.playlists) if (p.first.empty()) return false;
    std::lock_guard<std::mutex> lk(writer_);
    if (!dir_.empty() && !Journal(journal_path(), durable).append_transaction(transaction_ + 1, generation_ + 1, encode_batch(batch))) return false;
    ++transaction_; ++generation_;
    {
        std::lock_guard<std::mutex> pub(publish_);
        if (live_.use_count() == 1) { apply_batch(const_cast<MetadataSnapshot&>(*live_), batch); return true; }   // nobody can hold it: update in place
    }
    std::shared_ptr<const MetadataSnapshot> base; { std::lock_guard<std::mutex> pub(publish_); base = live_; }
    auto next = std::make_shared<MetadataSnapshot>(*base); base.reset();
    apply_batch(*next, batch);
    std::lock_guard<std::mutex> pub(publish_); live_ = std::move(next);
    return true;
}

bool MetadataStore::compact() {
    std::lock_guard<std::mutex> lk(writer_);
    if (dir_.empty()) return true;
    std::shared_ptr<const MetadataSnapshot> base; { std::lock_guard<std::mutex> pub(publish_); base = live_; }
    UpdateBatch all; all.songs = base->export_songs(); all.playlists = base->export_playlists();
    const std::vector<std::string> items = encode_batch(all);
    std::string payload; put64(payload, generation_); put64(payload, transaction_); put64(payload, items.size());
    for (const auto& item : items) put_str(payload, item);
    if (!write_snapshot(snapshot_path(), payload)) return false;
    std::string index; put64(index, generation_); index += base->export_index();
    if (index.size() <= 8 || !write_snapshot(index_path(), index)) std::remove(index_path().c_str());   // a missing index only costs a rebuild
    compacted_ = generation_;
    return Journal(journal_path()).compact();
}
uint64_t MetadataStore::disk_bytes() const {
    if (dir_.empty()) return 0;
    uint64_t total = 0;
    for (const std::string& p : {snapshot_path(), index_path(), journal_path()}) { struct stat sb{}; if (::stat(p.c_str(), &sb) == 0) total += static_cast<uint64_t>(sb.st_size); }
    return total;
}
bool MetadataStore::sync() const { return dir_.empty() || Journal(journal_path()).sync(); }
bool MetadataStore::needs_compact() const { std::lock_guard<std::mutex> lk(writer_); return !dir_.empty() && generation_ > compacted_; }
} // namespace xf::cache
