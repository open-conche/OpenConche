#include "audio/Pcm.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <vector>

// stb_vorbis.c is compiled as C (third_party, see CMakeLists.txt). Only the memory decoder is used here.
extern "C" int stb_vorbis_decode_memory(const unsigned char* mem, int len, int* channels, int* sample_rate, short** output);

namespace choco::audio {

std::optional<Pcm> decodeOgg(const uint8_t* bytes, std::size_t size, DecodeError* error) {
  auto fail = [&](DecodeError e) -> std::optional<Pcm> {
    if (error) *error = e;
    return std::nullopt;
  };
  if (error) *error = DecodeError::None;
  if (!bytes || size == 0 || size > static_cast<std::size_t>(std::numeric_limits<int>::max())) return fail(DecodeError::Corrupt);
  int channels = 0, rate = 0;
  short* out = nullptr;
  const int frames = stb_vorbis_decode_memory(bytes, static_cast<int>(size), &channels, &rate, &out);
  if (frames < 0 || !out) return fail(DecodeError::Corrupt);
  Pcm pcm;
  pcm.channels = channels;
  pcm.sampleRate = rate;
  if (channels == 1 || channels == 2) pcm.samples.assign(out, out + static_cast<std::size_t>(frames) * channels);
  std::free(out);
  if (channels != 1 && channels != 2) return fail(DecodeError::UnsupportedChannels);
  if (rate < 8000 || rate > 96000) return fail(DecodeError::UnsupportedRate);
  if (frames == 0) return fail(DecodeError::Empty);
  if (rate != kSampleRate) {
    // Linear interpolation to the mixer rate, as OggStream does for streamed music (the Macintosh build's effects are 22.05 kHz).
    const std::size_t inFrames = static_cast<std::size_t>(frames), ch = static_cast<std::size_t>(channels);
    const std::size_t outFrames = std::max<std::size_t>(1, static_cast<std::size_t>(static_cast<double>(inFrames) * kSampleRate / rate + 0.5));
    std::vector<int16_t> res(outFrames * ch);
    const double step = static_cast<double>(rate) / kSampleRate;
    for (std::size_t i = 0; i < outFrames; ++i) {
      const double pos = static_cast<double>(i) * step;
      const std::size_t i0 = std::min(static_cast<std::size_t>(pos), inFrames - 1), i1 = std::min(i0 + 1, inFrames - 1);
      const double t = pos - static_cast<double>(i0);
      for (std::size_t c = 0; c < ch; ++c) {
        const double a = pcm.samples[i0 * ch + c], b = pcm.samples[i1 * ch + c];
        res[i * ch + c] = static_cast<int16_t>(std::lround(a + (b - a) * t));
      }
    }
    pcm.samples = std::move(res);
    pcm.sampleRate = kSampleRate;
  }
  return pcm;
}

}  // namespace choco::audio
