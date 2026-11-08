#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace choco::audio {

// The mixer's rate. Effects at another rate (the Macintosh build's are 22.05 kHz) are resampled to it when decoded.
inline constexpr int kSampleRate = 44100;

// Decoded audio: int16, interleaved, one or two channels.
struct Pcm {
  int channels = 2;
  int sampleRate = kSampleRate;
  std::vector<int16_t> samples;

  std::size_t frames() const { return channels > 0 ? samples.size() / static_cast<std::size_t>(channels) : 0; }
};

enum class DecodeError { None, Corrupt, UnsupportedChannels, UnsupportedRate, Empty };

// Decodes a whole Ogg Vorbis file from memory (stb_vorbis). Fails cleanly, never throws or crashes, on corrupt or
// truncated input, on a channel count other than 1 or 2, and on a rate outside 8000..96000 Hz; other rates are resampled
// to kSampleRate by linear interpolation.
std::optional<Pcm> decodeOgg(const uint8_t* bytes, std::size_t size, DecodeError* error = nullptr);
inline std::optional<Pcm> decodeOgg(const std::vector<uint8_t>& bytes, DecodeError* error = nullptr) {
  return decodeOgg(bytes.data(), bytes.size(), error);
}

}  // namespace choco::audio
