// Every decoder against files made by ffmpeg (tests/fixtures/audio): rate, channels, length, level, pitch, channel order, seeking, sniffing.
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
#include "../src/audio/decoder.h"

static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

struct Case { const char* file; const char* kind; uint32_t rate, channels, bits; double seconds; bool lossless; };

// Dominant channel and, over its middle half, the amplitude and purity of the 440 Hz tone (Goertzel): robust to codec noise and
// level wobble, unlike zero crossings. purity 1 = a clean sine at exactly 440 Hz.
static void analyse(const std::vector<float>& pcm, uint32_t ch, uint32_t rate, uint32_t& loud, float& peak, double& amp, double& purity) {
    const size_t frames = pcm.size() / ch;
    std::vector<double> e(ch, 0.0);
    for (size_t i = 0; i < frames; ++i) for (uint32_t c = 0; c < ch; ++c) e[c] += static_cast<double>(pcm[i * ch + c]) * pcm[i * ch + c];
    loud = 0;
    for (uint32_t c = 1; c < ch; ++c) if (e[c] > e[loud]) loud = c;
    peak = 0;
    for (size_t i = 0; i < frames; ++i) peak = std::max(peak, std::fabs(pcm[i * ch + loud]));
    const size_t a = frames / 4, b = frames * 3 / 4, n = b - a;
    const double w = 2.0 * 3.14159265358979323846 * 440.0 / rate, coeff = 2.0 * std::cos(w);
    double s1 = 0, s2 = 0, ms = 0;
    for (size_t i = a; i < b; ++i) {
        const double v = pcm[i * ch + loud];
        const double s0 = v + coeff * s1 - s2; s2 = s1; s1 = s0;
        ms += v * v;
    }
    const double power = s1 * s1 + s2 * s2 - coeff * s1 * s2;
    amp = 2.0 * std::sqrt(std::max(0.0, power)) / static_cast<double>(n);
    purity = ms > 0 ? (amp * amp / 2.0) / (ms / static_cast<double>(n)) : 0.0;
}

static std::vector<float> decode_all(Decoder& d) {
    std::vector<float> out, buf(4096 * d.channels());
    for (;;) {
        const uint64_t n = d.read(buf.data(), 4096);
        if (n == 0) break;
        out.insert(out.end(), buf.begin(), buf.begin() + static_cast<long>(n * d.channels()));
    }
    return out;
}

int main(int argc, char** argv) {
    const std::string dir = (argc > 1 ? argv[1] : "tests/fixtures/audio") + std::string("/");
    const Case cases[] = {
        {"wav24_96k.wav", "wav", 96000, 2, 24, 0.2, true},   {"wav8_8k.wav", "wav", 8000, 1, 8, 0.3, true},
        {"wavf32_48k.wav", "wav", 48000, 2, 32, 0.2, true},  {"wav16_5p1.wav", "wav", 48000, 6, 16, 0.2, true},
        {"aiff16_44k.aiff", "aiff", 44100, 2, 16, 0.2, true}, {"aiff24_96k.aiff", "aiff", 96000, 2, 24, 0.1, true},
        {"flac24_192k.flac", "flac", 192000, 2, 24, 0.1, true}, {"flac16_5p1.flac", "flac", 48000, 6, 16, 0.2, true},
        {"vorbis_44k.ogg", "vorbis", 44100, 2, 0, 0.3, false}, {"vorbis_5p1.ogg", "vorbis", 48000, 6, 0, 0.3, false},
        {"opus_48k.opus", "opus", 48000, 2, 0, 0.4, false},  {"opus_5p1.opus", "opus", 48000, 6, 0, 0.4, false},
        {"aac_44k.m4a", "m4a", 44100, 2, 0, 0.4, false},     {"aac_48k_mono.m4a", "m4a", 48000, 1, 0, 0.4, false},
        {"alac16_44k.m4a", "m4a", 44100, 2, 16, 0.2, true},  {"alac24_96k.m4a", "m4a", 96000, 2, 24, 0.1, true},
        {"oggflac24_96k.oga", "oggflac", 96000, 2, 24, 0.2, true},
        {"aac_adts.aac", "aac", 44100, 2, 0, 0.4, false},    {"mp3_44k.mp3", "mp3", 44100, 2, 0, 0.4, false},
    };
    for (const Case& c : cases) {
        const std::string path = dir + c.file;
        CHECK(sniff_audio_kind(path) == c.kind);
        auto d = open_decoder(path);
        if (!d) { std::printf("FAIL cannot open %s\n", c.file); ++fails; continue; }
        CHECK(d->sample_rate() == c.rate);
        CHECK(d->channels() == c.channels);
        if (c.bits) CHECK(d->bits() == c.bits);
        const std::vector<float> pcm = decode_all(*d);
        const uint64_t frames = pcm.size() / c.channels, want = static_cast<uint64_t>(c.seconds * c.rate + 0.5);
        const uint64_t slack = c.lossless ? 1 : std::max<uint64_t>(2300, want / 20);   // lossy encoders pad to a frame boundary
        CHECK(frames + slack >= want && frames <= want + slack);
        if (d->total_frames()) CHECK(d->total_frames() + slack >= want && d->total_frames() <= want + slack);
        uint32_t loud = 0; float peak = 0; double amp = 0, purity = 0;
        analyse(pcm, c.channels, c.rate, loud, peak, amp, purity);
        if (c.channels == 6) CHECK(loud == 2);   // a mono source upmixed to 5.1 sits on the centre channel, WAVE order index 2
        const float want_peak = c.channels == 2 ? 0.354f : 0.5f;   // the mono test tone is spread over a stereo pair at -3 dB
        CHECK(peak > want_peak * 0.88f && peak < want_peak * 1.12f);
        CHECK(purity > 0.75);   // a clean 440 Hz tone, not noise or a wrong rate
        CHECK(amp > want_peak * 0.8 && amp < want_peak * 1.2);
        // seek to the middle: still a 440 Hz tone and the remaining length matches
        CHECK(d->seek(frames / 2));
        std::vector<float> tail(static_cast<size_t>(c.channels) * 2048);
        uint64_t got = 0, n = 0;
        std::vector<float> rest;
        while ((n = d->read(tail.data(), 2048)) > 0) { got += n; rest.insert(rest.end(), tail.begin(), tail.begin() + static_cast<long>(n * c.channels)); }
        const uint64_t expect_rest = frames - frames / 2;
        CHECK(got + slack + 64 >= expect_rest && got <= expect_rest + slack + 64);
        if (rest.size() > c.channels * 512u) {
            uint32_t l2 = 0; float p2 = 0; double amp2 = 0, pur2 = 0;
            analyse(rest, c.channels, c.rate, l2, p2, amp2, pur2);
            if (!(pur2 > 0.7) || !(amp2 > want_peak * 0.75)) std::printf("  after seek: peak %.2f amp %.2f purity %.2f (%zu frames)\n", p2, amp2, pur2, rest.size() / c.channels);
            CHECK(pur2 > 0.7);
            CHECK(amp2 > want_peak * 0.75);
        }
        // seek back to the start and compare the first block with the first decode (lossless must match exactly)
        CHECK(d->seek(0));
        std::vector<float> head(static_cast<size_t>(c.channels) * 1024);
        const uint64_t hn = d->read(head.data(), 1024);
        CHECK(hn == 1024 || hn == frames);
        if (c.lossless) { bool same = true; for (size_t i = 0; i < hn * c.channels; ++i) if (std::fabs(head[i] - pcm[i]) > 1e-6f) { same = false; break; } CHECK(same); }
        std::printf("ok %-18s %-6s %6u Hz %u ch %2u bit  %7llu frames  amp %.2f  purity %.2f\n", c.file, d->codec(), c.rate, c.channels, d->bits(), static_cast<unsigned long long>(frames), amp, purity);
    }
    CHECK(decoder_kind_for_ext("M4A") == "m4a" && decoder_kind_for_ext("oga") == "ogg" && decoder_kind_for_ext("wma").empty());
    std::printf(fails ? "FAILED\n" : "OK\n");
    return fails ? 1 : 0;
}
