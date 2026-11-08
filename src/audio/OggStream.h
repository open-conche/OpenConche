#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace choco::audio {

// A streaming Ogg Vorbis reader over bytes that outlive it (SoundCache::bytes never evicts), for music
// (docs/design/audio.md decision 3). open() runs on the main thread and allocates; nextFrame(), read() and rewind() run on
// the audio thread and never allocate, log or touch IO (stb_vorbis decodes with alloca when it was opened without an
// allocation buffer, and seek_start only re-reads the first page).
//
// Output is the file's own int16 samples (the same conversion decodeOgg uses), so a stream and a Pcm of the same file
// agree sample for sample. A mono file yields l == r from nextFrame(). A file at another rate (Chocolatier 1's menu music
// is 48 kHz) is resampled to 44100 by linear interpolation inside nextFrame(); totalFrames() is then the resampled length.
// read() always returns the file's own samples.
class OggStream {
 public:
  // Null on corrupt input, a channel count other than 1 or 2, a rate outside 8000..96000, or an empty stream.
  static std::unique_ptr<OggStream> open(const uint8_t* bytes, std::size_t size);
  ~OggStream();
  OggStream(const OggStream&) = delete;
  OggStream& operator=(const OggStream&) = delete;

  int channels() const { return channels_; }
  // The stream length from the last Ogg page's granule position (what decodeOgg returns, barring a damaged tail).
  uint64_t totalFrames() const { return total_; }

  // Reads up to `frames` frames of `channels()` interleaved int16 samples and returns how many it got (0 at the end).
  std::size_t read(int16_t* out, std::size_t frames);
  // Seeks back to the first sample. False if stb_vorbis could not.
  bool rewind();

  // One frame at a time through a small internal buffer (do not mix with read()), at 44100. False at the end of the stream.
  bool nextFrame(int16_t& l, int16_t& r);

  // Pump mode "skip decode" (design s.9): the stream still counts frames up to totalFrames() but yields silence and
  // decodes nothing. Set before the first read.
  void setSilent(bool silent) { silent_ = silent; }

  // True once a rewind failed or the first read after a rewind gave nothing: the stream then reads as ended for good, so a
  // looping stage cannot retry a seek for every output frame.
  bool dead() const { return dead_; }
  // Test hook: every read yields nothing.
  void forceEmptyReadsForTest() { forceEmpty_ = true; }

 private:
  struct Impl;
  explicit OggStream(std::unique_ptr<Impl> impl);

  static constexpr std::size_t kBufferFrames = 1024;
  bool nextSourceFrame(int16_t& l, int16_t& r);  // one frame at the file's own rate

  std::unique_ptr<Impl> impl_;
  int channels_ = 2;
  uint64_t total_ = 0;
  bool silent_ = false;
  uint64_t silentPos_ = 0;
  std::array<int16_t, kBufferFrames * 2> buf_{};
  std::size_t bufPos_ = 0, bufLen_ = 0;  // frames
  bool eof_ = false;
  bool dead_ = false;
  bool fresh_ = true;  // nothing read since open or the last rewind
  bool forceEmpty_ = false;
  // Resampling state (sourceRate_ != 44100): `a` and `b` are consecutive source frames, `frac` the output position between them.
  unsigned sourceRate_ = 44100;
  double step_ = 1;  // source frames per output frame
  struct Resample {
    int16_t al = 0, ar = 0, bl = 0, br = 0;
    double frac = 0;
    bool primed = false, aValid = false, bValid = false;
  } rs_;
};

}  // namespace choco::audio
