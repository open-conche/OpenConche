#pragma once

// Chocolatier 1's background music queue (docs/re/choc1-natives.md section 6; the exe's TChocoSound state machine at +0x9c).
// StartMusic and QueueMusic play audio/music/<file> once (no loop): looping is the Lua re-queuing the same file. A playing
// track is faded out over 1000 ms before the next one starts (no overlap), and a queued entry can wait `seconds` of silence.
// This class is plain logic over a MusicSink and a caller-supplied clock, so it is unit-tested on a fake clock.

#include <cstdint>
#include <deque>
#include <string>

namespace choco::audio {
class Audio;
}

namespace choco::game::choc1 {

// What the queue needs from the audio service.
class MusicSink {
 public:
  virtual ~MusicSink() = default;
  // Starts `path` once. False when it could not start (missing or undecodable file).
  virtual bool play(const std::string& path) = 0;
  // Fades the current track out over `ms` and stops it. playing() turns false at once; the fade is the sink's business.
  virtual void fadeOutAndStop(int ms) = 0;
  // Whether a track is playing now (false once it ended on its own, or after fadeOutAndStop).
  virtual bool playing() const = 0;
};

// The MusicSink over the audio service.
class AudioMusicSink : public MusicSink {
 public:
  explicit AudioMusicSink(audio::Audio& a) : a_(a) {}
  bool play(const std::string& path) override;
  void fadeOutAndStop(int ms) override;
  bool playing() const override;

 private:
  audio::Audio& a_;
};

class Choc1Music {
 public:
  static constexpr int kFadeMs = 1000;  // the exe's TChocoMusicTicker: 100 ms ticks, volume falls linearly over 1000 ms
  enum class State : std::uint8_t { Idle, Playing, Fading, Waiting };

  // StartMusic(file): "" fades out and stops (and drops the queue); the file already playing is a no-op; otherwise the
  // queue is cleared, and the track starts at once when idle or waiting, or after the current one has faded out.
  void start(MusicSink& sink, const std::string& file, double nowMs);
  // QueueMusic(file, seconds): idle starts at once (the delay is ignored); otherwise appended behind the current track.
  void queue(MusicSink& sink, const std::string& file, double seconds, double nowMs);
  // Once per frame: a track that ended, a finished fade or a finished wait hands over to the next queue entry.
  void tick(MusicSink& sink, double nowMs);

  State state() const { return state_; }
  const std::string& playingFile() const { return file_; }
  std::size_t queued() const { return queue_.size(); }

 private:
  struct Entry {
    std::string file;
    double seconds = 0;
  };
  bool begin(MusicSink& sink, const std::string& file);
  void advance(MusicSink& sink, double nowMs);  // pops queue entries until one plays or waits

  State state_ = State::Idle;
  std::string file_;      // the track playing (Playing), or the entry waiting to start (Waiting)
  double untilMs_ = 0;    // end of the fade (Fading) or of the wait (Waiting), in the caller's clock
  std::deque<Entry> queue_;
};

}  // namespace choco::game::choc1
