// Decoder interface: a file or byte source -> interleaved float PCM (-1..1) at the file's own rate and channel count.
// Float keeps every bit of 24/32-bit sources; the player converts channels and rate and writes floats to the device.
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include "source.h"

class Decoder {
public:
    virtual ~Decoder() = default;
    virtual uint32_t sample_rate() const = 0;
    virtual uint32_t channels() const = 0;
    virtual uint64_t total_frames() const = 0;  // 0 if unknown
    // Returns frames decoded (0 at end of stream). out holds frames * channels() floats.
    virtual uint64_t read(float* out, uint64_t frames) = 0;
    virtual bool seek(uint64_t frame) = 0;
    virtual uint32_t bits() const { return 16; }        // source bit depth (0 = lossy / not applicable)
    virtual const char* codec() const { return ""; }    // short name for the UI: "FLAC", "AAC", ...
};

// The decoder family for a file suffix ("flac", "mp3", "wav", "aiff", "vorbis", "opus", "m4a", "aac"), or "" when none can read it.
// ogg/oga hold Vorbis or Opus; use sniff_audio_kind (source.h) on the file to tell them apart.
std::string decoder_kind_for_ext(const std::string& ext);
// True when a kind can be decoded from a growing download (the others need the whole file first).
bool decoder_kind_streams(const std::string& kind);

// kind from decoder_kind_for_ext / sniff_audio_kind; empty picks by file extension.
std::unique_ptr<Decoder> open_decoder(const std::string& path, const std::string& kind = "");

// Decode from a (possibly growing) byte source. Only kinds that stream ("mp3", "flac", "wav"); others return nullptr.
std::unique_ptr<Decoder> open_decoder_source(std::unique_ptr<ByteSource> src, const std::string& kind);

// Implemented in opus_decoder.cpp, aac_decoder.cpp and alac_decoder.cpp.
std::unique_ptr<Decoder> open_opus_file(const std::string& path);
std::unique_ptr<Decoder> open_aac_mp4_file(const std::string& path);    // AAC in an MP4/M4A container
std::unique_ptr<Decoder> open_alac_mp4_file(const std::string& path);   // ALAC in an MP4/M4A container
std::unique_ptr<Decoder> open_adts_file(const std::string& path);  // raw AAC (.aac)
