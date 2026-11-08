// Ogg Opus: a small Ogg page reader (page index for seeking, lacing across pages) feeding libopus' multistream decoder.
// Output is 48 kHz float, pre-skip and end-of-stream trimming applied, the stream's output gain honoured, channels in WAVE order.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
#include <opus_multistream.h>
#include "decoder.h"

namespace {
struct Page { long offset = 0; int64_t granule = 0; uint8_t flags = 0; };

class OggOpusDecoder final : public Decoder {
public:
    ~OggOpusDecoder() override {
        if (dec_) opus_multistream_decoder_destroy(dec_);
        if (f_) std::fclose(f_);
    }

    bool open(const std::string& path) {
        f_ = std::fopen(path.c_str(), "rb");
        if (!f_) return false;
        if (!index_pages()) return false;
        std::fseek(f_, 0, SEEK_SET);
        std::vector<uint8_t> head;
        if (!next_packet(head) || head.size() < 19 || std::memcmp(head.data(), "OpusHead", 8) != 0) return false;
        ch_ = head[9];
        pre_skip_ = static_cast<uint32_t>(head[10] | head[11] << 8);
        const int16_t gain_q8 = static_cast<int16_t>(head[16] | head[17] << 8);
        gain_ = std::pow(10.0f, static_cast<float>(gain_q8) / (20.0f * 256.0f));
        const uint8_t family = head[18];
        int streams = 1, coupled = ch_ == 2 ? 1 : 0;
        unsigned char map[8] = {0, 1, 2, 3, 4, 5, 6, 7};
        if (family == 1) {
            if (head.size() < 21u + ch_) return false;
            streams = head[19]; coupled = head[20];
            for (uint32_t i = 0; i < ch_ && i < 8; ++i) map[i] = head[21 + i];
        } else if (family != 0 || ch_ == 0 || ch_ > 2) return false;
        if (ch_ == 0 || ch_ > 8) return false;
        int err = 0;
        dec_ = opus_multistream_decoder_create(48000, static_cast<int>(ch_), streams, coupled, map, &err);
        if (!dec_ || err != OPUS_OK) return false;
        std::vector<uint8_t> tags;
        if (!next_packet(tags)) return false;   // OpusTags: skipped
        audio_start_page_ = cur_page_;          // the first page after the headers
        const int64_t last = pages_.empty() ? 0 : pages_.back().granule;
        total_ = last > static_cast<int64_t>(pre_skip_) ? static_cast<uint64_t>(last) - pre_skip_ : 0;
        to_skip_ = pre_skip_;
        pcm_.resize(5760 * static_cast<size_t>(ch_));
        return true;
    }

    uint32_t sample_rate() const override { return 48000; }
    uint32_t channels() const override { return ch_; }
    uint64_t total_frames() const override { return total_; }
    uint32_t bits() const override { return 0; }
    const char* codec() const override { return "Opus"; }

    uint64_t read(float* out, uint64_t frames) override {
        uint64_t done = 0;
        while (done < frames) {
            if (avail_ == 0) {
                if (!decode_packet()) break;
                continue;
            }
            const uint64_t take = std::min<uint64_t>(frames - done, avail_);
            std::memcpy(out + done * ch_, pcm_.data() + off_ * ch_, static_cast<size_t>(take) * ch_ * sizeof(float));
            off_ += take; avail_ -= take; done += take; played_ += take;
        }
        if (total_ && played_ > total_) done -= std::min<uint64_t>(done, played_ - total_);   // trim the padding after the last real sample
        return done;
    }

    bool seek(uint64_t frame) override {
        if (pages_.empty()) return false;
        if (total_) frame = std::min(frame, total_);
        const int64_t target = static_cast<int64_t>(frame) + pre_skip_;
        const int64_t want = target - 3840;   // 80 ms of preroll so the decoder state converges before the target
        size_t k = 0;   // first audio page whose end granule reaches `want`; decoding starts at the page before it
        while (k < pages_.size() && pages_[k].granule < want) ++k;
        size_t start = k > audio_first_ ? k - 1 : audio_first_;
        while (start > audio_first_ && (pages_[start].flags & 1)) --start;   // begin on a page that starts a packet: a continued one cannot be decoded alone
        // decoding resumes at pages_[start]; the granule it begins at is the end of the page before it
        int64_t begin = start > audio_first_ ? pages_[start - 1].granule : 0;
        cur_page_ = start;
        cont_drop_ = false;
        opus_multistream_decoder_ctl(dec_, OPUS_RESET_STATE);
        avail_ = 0; off_ = 0;
        to_skip_ = static_cast<uint64_t>(std::max<int64_t>(0, target - begin));
        played_ = frame;
        pending_.clear(); pkts_.clear(); pkt_i_ = 0; page_loaded_ = false;
        return true;
    }

private:
    // Reads every page header once: offset + granule. Headers (granule -1/0 on the first two pages) are recorded too; audio starts after them.
    bool index_pages() {
        std::fseek(f_, 0, SEEK_SET);
        long pos = 0;
        uint32_t serial = 0;
        bool have_serial = false;
        for (;;) {
            unsigned char h[27];
            if (std::fread(h, 1, 27, f_) != 27) break;
            if (std::memcmp(h, "OggS", 4) != 0) break;
            const uint8_t nseg = h[26];
            unsigned char segs[255];
            if (std::fread(segs, 1, nseg, f_) != nseg) break;
            long payload = 0;
            for (int i = 0; i < nseg; ++i) payload += segs[i];
            const uint32_t sn = static_cast<uint32_t>(h[14] | h[15] << 8 | h[16] << 16 | static_cast<uint32_t>(h[17]) << 24);
            if (!have_serial) { serial = sn; have_serial = true; }
            if (sn == serial) {
                Page p;
                p.offset = pos; p.flags = h[5];
                uint64_t g = 0;
                for (int i = 7; i >= 0; --i) g = (g << 8) | h[6 + i];
                p.granule = static_cast<int64_t>(g);
                pages_.push_back(p);
            }
            pos += 27 + nseg + payload;
            if (std::fseek(f_, pos, SEEK_SET) != 0) break;
        }
        // the first pages carry OpusHead/OpusTags (granule 0); audio pages follow
        audio_first_ = 0;
        while (audio_first_ < pages_.size() && pages_[audio_first_].granule <= 0) ++audio_first_;
        // drop pages with an unset granule (-1) inside the audio (a packet spanning pages): they cannot be seek targets
        return pages_.size() > audio_first_;
    }

    // Packets of the page at pages_[cur_page_] in order (laced packets that continue onto the next page are held back in pending_).
    bool load_page() {
        if (cur_page_ >= pages_.size()) return false;
        const Page& pg = pages_[cur_page_];
        std::fseek(f_, pg.offset, SEEK_SET);
        unsigned char h[27];
        if (std::fread(h, 1, 27, f_) != 27) return false;
        const uint8_t nseg = h[26];
        unsigned char segs[255];
        if (std::fread(segs, 1, nseg, f_) != nseg) return false;
        pkts_.clear(); pkt_i_ = 0;
        std::vector<uint8_t> cur = std::move(pending_);
        pending_.clear();
        bool first_continued = (pg.flags & 1) != 0;
        bool dropping = cont_drop_ && first_continued;   // after a seek the leading continuation fragment is useless
        cont_drop_ = false;
        bool open_packet = !cur.empty();
        for (int i = 0; i < nseg; ++i) {
            const size_t old = cur.size();
            cur.resize(old + segs[i]);
            if (segs[i] && std::fread(cur.data() + old, 1, segs[i], f_) != segs[i]) return false;
            open_packet = true;
            if (segs[i] < 255) {
                if (!dropping) pkts_.push_back(std::move(cur));
                dropping = false;
                cur.clear();
                open_packet = false;
            }
        }
        if (open_packet && !dropping) pending_ = std::move(cur);
        ++cur_page_;
        page_loaded_ = true;
        return true;
    }

    bool next_packet(std::vector<uint8_t>& out) {
        for (;;) {
            if (page_loaded_ && pkt_i_ < pkts_.size()) { out = std::move(pkts_[pkt_i_++]); return true; }
            if (!load_page()) return false;
        }
    }

    bool decode_packet() {
        std::vector<uint8_t> pk;
        if (!next_packet(pk)) return false;
        if (pk.empty()) return true;
        const int n = opus_multistream_decode_float(dec_, pk.data(), static_cast<opus_int32>(pk.size()), pcm_.data(), 5760, 0);
        if (n <= 0) return true;   // a damaged packet: skip it, the stream goes on
        if (gain_ != 1.0f) for (int i = 0; i < n * static_cast<int>(ch_); ++i) pcm_[static_cast<size_t>(i)] *= gain_;
        if (ch_ > 2) reorder(pcm_.data(), static_cast<size_t>(n));
        uint64_t skip = std::min<uint64_t>(to_skip_, static_cast<uint64_t>(n));
        to_skip_ -= skip;
        off_ = skip;
        avail_ = static_cast<uint64_t>(n) - skip;
        return true;
    }

    // Opus/Vorbis order (L C R ... ) to the WAVE order the player expects.
    void reorder(float* p, size_t frames) const {
        for (size_t i = 0; i < frames; ++i, p += ch_) {
            float t[8];
            for (uint32_t c = 0; c < ch_; ++c) t[c] = p[c];
            switch (ch_) {
            case 3: p[0] = t[0]; p[1] = t[2]; p[2] = t[1]; break;
            case 5: p[0] = t[0]; p[1] = t[2]; p[2] = t[1]; p[3] = t[3]; p[4] = t[4]; break;
            case 6: p[0] = t[0]; p[1] = t[2]; p[2] = t[1]; p[3] = t[5]; p[4] = t[3]; p[5] = t[4]; break;
            case 7: p[0] = t[0]; p[1] = t[2]; p[2] = t[1]; p[3] = t[6]; p[4] = t[5]; p[5] = t[3]; p[6] = t[4]; break;
            case 8: p[0] = t[0]; p[1] = t[2]; p[2] = t[1]; p[3] = t[7]; p[4] = t[5]; p[5] = t[6]; p[6] = t[3]; p[7] = t[4]; break;
            default: break;
            }
        }
    }

    std::FILE* f_ = nullptr;
    ::OpusMSDecoder* dec_ = nullptr;
    uint32_t ch_ = 0, pre_skip_ = 0;
    float gain_ = 1.0f;
    std::vector<Page> pages_;
    size_t audio_first_ = 0, audio_start_page_ = 0, cur_page_ = 0, pkt_i_ = 0;
    bool page_loaded_ = false, cont_drop_ = false;
    std::vector<std::vector<uint8_t>> pkts_;
    std::vector<uint8_t> pending_;
    std::vector<float> pcm_;
    uint64_t avail_ = 0, off_ = 0, to_skip_ = 0, played_ = 0, total_ = 0;
};
}  // namespace

std::unique_ptr<Decoder> open_opus_file(const std::string& path) {
    auto d = std::make_unique<OggOpusDecoder>();
    if (!d->open(path)) return nullptr;
    return d;
}
