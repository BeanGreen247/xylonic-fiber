// Minimal MP4/M4A reader shared by the AAC and ALAC decoders: the audio track's codec config, sample sizes/offsets and timing, edit list.
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace mp4 {
inline uint32_t rd32(const uint8_t* p) { return static_cast<uint32_t>(p[0]) << 24 | static_cast<uint32_t>(p[1]) << 16 | static_cast<uint32_t>(p[2]) << 8 | p[3]; }
inline uint64_t rd64(const uint8_t* p) { return static_cast<uint64_t>(rd32(p)) << 32 | rd32(p + 4); }
inline uint16_t rd16(const uint8_t* p) { return static_cast<uint16_t>(p[0] << 8 | p[1]); }

struct Box { size_t body = 0, end = 0; char type[5] = {}; };

// Children of [from, to) inside `d`; calls fn(Box) for each. Handles 64-bit sizes.
template <class F> void each_box(const std::vector<uint8_t>& d, size_t from, size_t to, F&& fn) {
    size_t p = from;
    while (p + 8 <= to && p + 8 <= d.size()) {
        uint64_t size = rd32(&d[p]);
        size_t hdr = 8;
        if (size == 1 && p + 16 <= d.size()) { size = rd64(&d[p + 8]); hdr = 16; }
        else if (size == 0) size = to - p;
        if (size < hdr || p + size > to) break;
        Box b;
        std::memcpy(b.type, &d[p + 4], 4);
        b.body = p + hdr; b.end = p + static_cast<size_t>(size);
        fn(b);
        p += static_cast<size_t>(size);
    }
}
inline bool is(const Box& b, const char* t) { return std::memcmp(b.type, t, 4) == 0; }

struct Mp4Track {
    bool alac = false, aac = false;
    std::vector<uint8_t> cfg;                  // AAC AudioSpecificConfig, or the ALAC magic cookie
    uint32_t timescale = 0, channels = 0, rate = 0, bits = 0;
    uint64_t duration = 0;                     // media timescale units
    int64_t edit_start = 0;                    // media units skipped at the start (encoder delay, from the edit list)
    uint64_t edit_len = 0;                     // media units played (0 = to the end)
    std::vector<uint64_t> offset;
    std::vector<uint32_t> size;
    std::vector<uint64_t> dts;                 // decode time of each sample, plus the end time at the back
};

// ES descriptor chain -> AudioSpecificConfig. Returns the object type (0x40 = AAC).
inline int parse_esds(const uint8_t* p, size_t n, std::vector<uint8_t>& asc) {
    size_t i = 4;   // version/flags
    int object_type = 0;
    auto len = [&](size_t& pos) { uint32_t l = 0; for (int k = 0; k < 4 && pos < n; ++k) { const uint8_t b = p[pos++]; l = (l << 7) | (b & 0x7f); if (!(b & 0x80)) break; } return l; };
    while (i < n) {
        const uint8_t tag = p[i++];
        const uint32_t l = len(i);
        if (tag == 0x03) { i += 2; if (i < n && (p[i] & 0x80)) i += 2; if (i < n && (p[i] & 0x40)) { i += 1 + p[i + 1]; } ++i; }   // ES_ID + flags (+ dependsOn / URL)
        else if (tag == 0x04) { if (i + 13 > n) break; object_type = p[i]; i += 13; }
        else if (tag == 0x05) { if (i + l > n) break; asc.assign(p + i, p + i + l); return object_type; }
        else i += l;
    }
    return object_type;
}

inline bool read_moov(std::FILE* f, std::vector<uint8_t>& moov) {
    std::fseek(f, 0, SEEK_END);
    const long fsize = std::ftell(f);
    long pos = 0;
    while (pos + 8 <= fsize) {
        uint8_t h[16];
        std::fseek(f, pos, SEEK_SET);
        if (std::fread(h, 1, 8, f) != 8) return false;
        uint64_t size = rd32(h);
        size_t hdr = 8;
        if (size == 1) { if (std::fread(h + 8, 1, 8, f) != 8) return false; size = rd64(h + 8); hdr = 16; }
        else if (size == 0) size = static_cast<uint64_t>(fsize - pos);
        if (size < hdr) return false;
        if (std::memcmp(h + 4, "moov", 4) == 0) {
            if (size > (64u << 20)) return false;
            moov.resize(static_cast<size_t>(size));
            std::fseek(f, pos, SEEK_SET);
            return std::fread(moov.data(), 1, moov.size(), f) == moov.size();
        }
        pos += static_cast<long>(size);
    }
    return false;
}

inline bool parse_track(const std::vector<uint8_t>& d, const Box& trak, uint32_t movie_ts, Mp4Track& t) {
    bool soun = false;
    Box stbl, edts_elst;
    bool have_stbl = false, have_elst = false;
    each_box(d, trak.body, trak.end, [&](const Box& b) {
        if (is(b, "edts")) each_box(d, b.body, b.end, [&](const Box& e) { if (is(e, "elst")) { edts_elst = e; have_elst = true; } });
        if (!is(b, "mdia")) return;
        each_box(d, b.body, b.end, [&](const Box& m) {
            if (is(m, "mdhd") && m.end - m.body >= 24) {
                const uint8_t* p = &d[m.body];
                if (p[0] == 1) { t.timescale = rd32(p + 20); t.duration = rd64(p + 24); }
                else { t.timescale = rd32(p + 12); t.duration = rd32(p + 16); }
            } else if (is(m, "hdlr") && m.end - m.body >= 12) {
                soun = std::memcmp(&d[m.body + 8], "soun", 4) == 0;
            } else if (is(m, "minf")) {
                each_box(d, m.body, m.end, [&](const Box& s) { if (is(s, "stbl")) { stbl = s; have_stbl = true; } });
            }
        });
    });
    if (!soun || !have_stbl || t.timescale == 0) return false;
    std::vector<std::pair<uint32_t, uint32_t>> stts;           // count, delta
    struct Stsc { uint32_t first, per; };
    std::vector<Stsc> stsc;
    std::vector<uint64_t> chunk_off;
    uint32_t fixed_size = 0, n_samples = 0;
    std::vector<uint32_t> sizes;
    bool have_stsd = false;
    each_box(d, stbl.body, stbl.end, [&](const Box& b) {
        const uint8_t* p = &d[b.body];
        const size_t n = b.end - b.body;
        if (is(b, "stsd") && n >= 16) {
            const uint8_t* e = p + 8;   // first entry: size, format, reserved(6), dref(2), then the audio sample entry
            const size_t esz = std::min<size_t>(rd32(e), n - 8);
            if (esz < 36) return;
            const uint32_t version = rd16(e + 16);
            t.channels = rd16(e + 24); t.bits = rd16(e + 26); t.rate = rd32(e + 32) >> 16;
            size_t child = 36 + (version == 1 ? 16 : version == 2 ? 36 : 0);
            const size_t base = b.body + 8;
            if (std::memcmp(e + 4, "mp4a", 4) == 0) {
                each_box(d, base + child, base + esz, [&](const Box& c) {
                    if (is(c, "esds")) {
                        std::vector<uint8_t> asc;
                        const int ot = parse_esds(&d[c.body], c.end - c.body, asc);
                        if ((ot == 0x40 || ot == 0x66 || ot == 0x67 || ot == 0x68) && !asc.empty()) { t.aac = true; t.cfg = asc; have_stsd = true; }
                    }
                });
            } else if (std::memcmp(e + 4, "alac", 4) == 0) {
                each_box(d, base + child, base + esz, [&](const Box& c) {
                    if (is(c, "alac")) { t.alac = true; t.cfg.assign(d.begin() + static_cast<long>(c.body - 8), d.begin() + static_cast<long>(c.end)); have_stsd = true; }   // the whole box: the decoder skips its header
                });
            }
        } else if (is(b, "stts") && n >= 8) {
            const uint32_t c = rd32(p + 4);
            for (uint32_t i = 0; i < c && 8 + (i + 1) * 8 <= n; ++i) stts.emplace_back(rd32(p + 8 + i * 8), rd32(p + 12 + i * 8));
        } else if (is(b, "stsc") && n >= 8) {
            const uint32_t c = rd32(p + 4);
            for (uint32_t i = 0; i < c && 8 + (i + 1) * 12 <= n; ++i) stsc.push_back({rd32(p + 8 + i * 12), rd32(p + 12 + i * 12)});
        } else if (is(b, "stsz") && n >= 12) {
            fixed_size = rd32(p + 4); n_samples = rd32(p + 8);
            if (fixed_size == 0) for (uint32_t i = 0; i < n_samples && 12 + (i + 1) * 4 <= n; ++i) sizes.push_back(rd32(p + 12 + i * 4));
        } else if (is(b, "stco") && n >= 8) {
            const uint32_t c = rd32(p + 4);
            for (uint32_t i = 0; i < c && 8 + (i + 1) * 4 <= n; ++i) chunk_off.push_back(rd32(p + 8 + i * 4));
        } else if (is(b, "co64") && n >= 8) {
            const uint32_t c = rd32(p + 4);
            for (uint32_t i = 0; i < c && 8 + (i + 1) * 8 <= n; ++i) chunk_off.push_back(rd64(p + 8 + i * 8));
        }
    });
    if (!have_stsd || chunk_off.empty() || stsc.empty() || n_samples == 0) return false;
    if (fixed_size) sizes.assign(n_samples, fixed_size);
    if (sizes.size() != n_samples) return false;
    // sample -> file offset
    t.size = sizes;
    t.offset.reserve(n_samples);
    size_t s = 0;
    for (size_t c = 0; c < chunk_off.size() && s < n_samples; ++c) {
        uint32_t per = stsc.front().per;
        for (const Stsc& e : stsc) if (e.first <= c + 1) per = e.per;
        uint64_t off = chunk_off[c];
        for (uint32_t k = 0; k < per && s < n_samples; ++k, ++s) { t.offset.push_back(off); off += sizes[s]; }
    }
    if (t.offset.size() != n_samples) return false;
    // sample -> decode time
    t.dts.reserve(n_samples + 1);
    uint64_t time = 0;
    for (const auto& e : stts) for (uint32_t k = 0; k < e.first && t.dts.size() < n_samples; ++k) { t.dts.push_back(time); time += e.second; }
    while (t.dts.size() < n_samples) t.dts.push_back(time);
    t.dts.push_back(time);
    if (t.duration == 0 || t.duration > time) t.duration = time;
    if (have_elst && edts_elst.end - edts_elst.body >= 8) {
        const uint8_t* p = &d[edts_elst.body];
        const uint32_t cnt = rd32(p + 4);
        if (cnt >= 1) {
            const bool v1 = p[0] == 1;
            const size_t ent = v1 ? 20 : 12;
            if (8 + ent <= edts_elst.end - edts_elst.body) {
                const uint64_t seg = v1 ? rd64(p + 8) : rd32(p + 8);
                const int64_t mt = v1 ? static_cast<int64_t>(rd64(p + 16)) : static_cast<int32_t>(rd32(p + 12));
                if (mt > 0) t.edit_start = mt;
                if (seg && movie_ts) t.edit_len = static_cast<uint64_t>(static_cast<double>(seg) * t.timescale / movie_ts);
            }
        }
    }
    return true;
}

// Shared by AAC and ALAC: demuxed sample access + gapless trimming + seeking by sample.
class Mp4Source {
public:
    ~Mp4Source() { if (f_) std::fclose(f_); }
    bool open(const std::string& path) {
        f_ = std::fopen(path.c_str(), "rb");
        if (!f_) return false;
        std::vector<uint8_t> moov;
        if (!read_moov(f_, moov)) return false;
        uint32_t movie_ts = 0;
        bool ok = false;
        each_box(moov, 8, moov.size(), [&](const Box& b) {
            if (is(b, "mvhd") && b.end - b.body >= 16) { const uint8_t* p = &moov[b.body]; movie_ts = p[0] == 1 ? rd32(p + 20) : rd32(p + 12); }
        });
        each_box(moov, 8, moov.size(), [&](const Box& b) {
            if (ok || !is(b, "trak")) return;
            Mp4Track t;
            if (parse_track(moov, b, movie_ts, t)) { trk_ = std::move(t); ok = true; }
        });
        return ok;
    }
    bool sample(size_t i, std::vector<uint8_t>& out) {
        if (i >= trk_.offset.size()) return false;
        out.resize(trk_.size[i]);
        if (std::fseek(f_, static_cast<long>(trk_.offset[i]), SEEK_SET) != 0) return false;
        return std::fread(out.data(), 1, out.size(), f_) == out.size();
    }
    Mp4Track trk_;
private:
    std::FILE* f_ = nullptr;
};

}  // namespace mp4
