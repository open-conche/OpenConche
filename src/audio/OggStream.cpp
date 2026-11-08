#include "audio/OggStream.h"

#include <limits>
#include <utility>

#include "audio/Pcm.h"

// stb_vorbis.c is compiled as C (third_party, see CMakeLists.txt). The pull API is declared by hand, as in Pcm.cpp.
struct stb_vorbis;
extern "C" {
struct StbVorbisInfo {  // stb_vorbis_info
  unsigned int sample_rate;
  int channels;
  unsigned int setup_memory_required;
  unsigned int setup_temp_memory_required;
  unsigned int temp_memory_required;
  int max_frame_size;
};
stb_vorbis* stb_vorbis_open_memory(const unsigned char* data, int len, int* error, const void* alloc);
void stb_vorbis_close(stb_vorbis* f);
int stb_vorbis_seek_start(stb_vorbis* f);
unsigned int stb_vorbis_stream_length_in_samples(stb_vorbis* f);
int stb_vorbis_get_samples_short_interleaved(stb_vorbis* f, int channels, short* buffer, int num_shorts);
StbVorbisInfo stb_vorbis_get_info(stb_vorbis* f);
}

namespace choco::audio {

struct OggStream::Impl {
  stb_vorbis* v = nullptr;
  ~Impl() {
    if (v) stb_vorbis_close(v);
  }
};

OggStream::OggStream(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
OggStream::~OggStream() = default;

std::unique_ptr<OggStream> OggStream::open(const uint8_t* bytes, std::size_t size) {
  if (!bytes || size == 0 || size > static_cast<std::size_t>(std::numeric_limits<int>::max())) return nullptr;
  auto impl = std::make_unique<Impl>();
  int err = 0;
  impl->v = stb_vorbis_open_memory(bytes, static_cast<int>(size), &err, nullptr);
  if (!impl->v) return nullptr;
  const StbVorbisInfo info = stb_vorbis_get_info(impl->v);
  if ((info.channels != 1 && info.channels != 2) || info.sample_rate < 8000 || info.sample_rate > 96000) return nullptr;
  const unsigned int total = stb_vorbis_stream_length_in_samples(impl->v);
  if (total == 0 || total == 0xffffffffu) return nullptr;
  if (!stb_vorbis_seek_start(impl->v)) return nullptr;  // the length scan moved the read position
  std::unique_ptr<OggStream> s(new OggStream(std::move(impl)));
  s->channels_ = info.channels;
  s->sourceRate_ = info.sample_rate;
  s->step_ = static_cast<double>(info.sample_rate) / kSampleRate;
  s->total_ = info.sample_rate == static_cast<unsigned>(kSampleRate)
                  ? total
                  : static_cast<uint64_t>(static_cast<double>(total) * kSampleRate / info.sample_rate);
  return s;
}

std::size_t OggStream::read(int16_t* out, std::size_t frames) {
  if (forceEmpty_) return 0;
  const int n = stb_vorbis_get_samples_short_interleaved(impl_->v, channels_, out, static_cast<int>(frames) * channels_);
  return n > 0 ? static_cast<std::size_t>(n) : 0;
}

bool OggStream::rewind() {
  if (dead_) return false;
  bufPos_ = bufLen_ = 0;
  eof_ = false;
  fresh_ = true;
  silentPos_ = 0;
  rs_ = Resample{};
  if (silent_) return true;
  if (stb_vorbis_seek_start(impl_->v) == 0) {
    dead_ = true;
    return false;
  }
  return true;
}

bool OggStream::nextFrame(int16_t& l, int16_t& r) {
  if (dead_) return false;
  if (silent_) {
    if (silentPos_ >= total_) return false;
    ++silentPos_;
    l = r = 0;
    return true;
  }
  if (sourceRate_ == static_cast<unsigned>(kSampleRate)) return nextSourceFrame(l, r);
  // Linear interpolation between the two source frames around the output position.
  Resample& q = rs_;
  if (!q.primed) {
    q.aValid = nextSourceFrame(q.al, q.ar);
    q.bValid = q.aValid && nextSourceFrame(q.bl, q.br);
    q.frac = 0;
    q.primed = true;
  }
  if (!q.aValid) return false;
  if (q.bValid) {
    const double f = q.frac;
    l = static_cast<int16_t>(q.al + (q.bl - q.al) * f);
    r = static_cast<int16_t>(q.ar + (q.br - q.ar) * f);
  } else {
    l = q.al;
    r = q.ar;
  }
  q.frac += step_;
  while (q.frac >= 1) {
    q.frac -= 1;
    q.al = q.bl, q.ar = q.br, q.aValid = q.bValid;
    q.bValid = q.aValid && nextSourceFrame(q.bl, q.br);
  }
  return true;
}

bool OggStream::nextSourceFrame(int16_t& l, int16_t& r) {
  if (dead_) return false;
  if (bufPos_ >= bufLen_) {
    if (eof_) return false;
    bufLen_ = read(buf_.data(), kBufferFrames);
    bufPos_ = 0;
    if (bufLen_ == 0) {
      eof_ = true;
      if (fresh_) dead_ = true;  // nothing at all after a rewind: do not let a loop retry it every frame
      return false;
    }
    fresh_ = false;
  }
  const std::size_t i = bufPos_++ * static_cast<std::size_t>(channels_);
  l = buf_[i];
  r = channels_ == 2 ? buf_[i + 1] : buf_[i];
  return true;
}

}  // namespace choco::audio
