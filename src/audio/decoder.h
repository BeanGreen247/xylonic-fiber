// Decoder interface: file path -> interleaved int16 PCM. Format picked by extension.
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
    // Returns frames decoded (0 at end of stream).
    virtual uint64_t read(int16_t* out, uint64_t frames) = 0;
    virtual bool seek(uint64_t frame) = 0;
};

// kind "mp3"/"flac", or empty to pick by file extension.
std::unique_ptr<Decoder> open_decoder(const std::string& path, const std::string& kind = "");

// Decode from a (possibly growing) byte source. kind: "mp3" or "flac". Source ownership moves to the decoder.
std::unique_ptr<Decoder> open_decoder_source(std::unique_ptr<ByteSource> src, const std::string& kind);
