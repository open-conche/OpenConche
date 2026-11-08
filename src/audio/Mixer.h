#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "audio/OggStream.h"
#include "audio/Pcm.h"

namespace choco::audio {

// docs/design/audio.md s.3. `Direct` is the TSound path (UI clicks, fireworks, typing); the rest are CADI groups.
enum class Group : uint8_t { Sfx, Env, Music, Direct };
inline constexpr int kGroupCount = 4;

inline constexpr int kMaxVoices = 64;
inline constexpr int kMaxStemsPerStage = 9;  // the C3 Gameplay arrangements have nine stems

inline constexpr uint32_t msToFrames(float ms) {
  return ms <= 0 ? 0u : static_cast<uint32_t>(static_cast<double>(ms) * kSampleRate / 1000.0 + 0.5);
}

// A linear gain ramp counted in frames, so a fade of N frames takes exactly N frames: next() returns the current value
// and then advances, and `cur` equals `target` once `left` reaches zero.
struct Ramp {
  float cur = 1.0f;
  float target = 1.0f;
  float step = 0.0f;
  uint32_t left = 0;
  bool freeAtEnd = false;  // the owner is finished once this ramp has run out

  void set(float from, float to, uint32_t frames, bool freeWhenDone = false) {
    cur = from;
    target = to;
    freeAtEnd = freeWhenDone;
    left = frames;
    if (frames == 0) {
      cur = to;
      step = 0;
    } else {
      step = (to - from) / static_cast<float>(frames);
    }
  }
  float next() {
    const float v = cur;
    if (left > 0) {
      --left;
      cur = left == 0 ? target : cur + step;
    }
    return v;
  }
  bool ended() const { return freeAtEnd && left == 0; }
};

struct Voice {
  const Pcm* pcm = nullptr;
  std::size_t pos = 0;  // in frames
  bool loop = false;
  float gain = 1.0f;  // voiceGain: channel volume x sweetener spread
  Ramp ramp;
  Group group = Group::Sfx;
  bool cadi = false;  // frozen by the CADI pause (factory PauseGame) as well as by the system pause
  int tag = -1;       // the event-table index of the event that started it, or -1
  bool active = false;
};

struct VoiceParams {
  Group group = Group::Sfx;
  bool loop = false;
  float gain = 1.0f;
  bool cadi = false;
  int tag = -1;
  float fadeInMs = 0;  // attack ramp from silence
};

// What a stage source reads: decoded Pcm (random access) or an OggStream (read in order on the audio thread; the
// stage rewinds it when it starts, and a looping stage rewinds it at its end). Exactly one is set.
struct Source {
  const Pcm* pcm = nullptr;
  OggStream* stream = nullptr;
  Source() = default;
  Source(const Pcm* p) : pcm(p) {}  // NOLINT: lets a stage list its sources as plain pointers
  Source(OggStream* s) : stream(s) {}  // NOLINT
  bool valid() const { return (pcm != nullptr) != (stream != nullptr) && frames() > 0; }
  uint64_t frames() const { return pcm ? pcm->frames() : stream ? stream->totalFrames() : 0; }
};

// One stage of a music track, placed on the track's timeline: all its sources start together at `startFrame`, which
// is counted from the track's start and may be shared with other stages (a sting with a layer under it) or fall inside an
// earlier stage. A looping stage repeats until the track ends (each source wraps at its own length). A non-looping stage
// plays each source once. `lengthFrames` > 0 cuts the stage that many frames after its start. `gain` scales the stage (a
// clip's volume times its track's). Two stages may share one OggStream only if they never overlap in time.
struct MusicStage {
  std::vector<Source> sources;  // at most kMaxStemsPerStage, none empty
  bool loop = false;
  uint64_t startFrame = 0;
  uint64_t lengthFrames = 0;
  float gain = 1.0f;
};

// Built on the main thread, then handed to the Mixer, which runs it on the audio thread. `Audio` owns it, and with it
// the streams the stages point into (and any decoded `owned` sound): they are freed when the track is reaped.
struct MusicTrack {
  std::string name;
  std::vector<MusicStage> stages;
  std::vector<std::unique_ptr<Pcm>> owned;
  std::vector<std::unique_ptr<OggStream>> streams;
  float gain = 1.0f;  // channel volume
  // The clip's end. For a non-looping clip (`loopFrames` == 0) the track is done once the timeline reaches `endFrames`
  // (0 = when every stage has finished). With `loopFrames` > 0 the timeline wraps to 0 there and the track never ends.
  uint64_t endFrames = 0;
  uint64_t loopFrames = 0;

  // Mixer state.
  uint64_t timeline = 0;  // frames played since the track started (or last wrapped)
  uint64_t finishAt = 0;  // computed by startTrack; UINT64_MAX = never
  Ramp ramp;
  bool done = false;

  // How many distinct stage start times the timeline has reached, out of how many there are ("stage 2/3" in the dump).
  std::pair<std::size_t, std::size_t> phase() const;
};

// What guards the Mixer against the audio thread (platform::AudioDevice implements it with the SDL stream lock).
class MixerLock {
 public:
  virtual ~MixerLock() = default;
  virtual void lock() = 0;
  virtual void unlock() = 0;
};

class Mixer {
 public:
  // Every main-thread method below that edits or reads voices, tracks or levels holds the lock for its own duration
  // (pointer-sized work only: nothing here decodes, allocates or logs). render() never locks: the device holds the lock
  // while it calls it. Null (the default, and the pump mode) = no locking. The audio thread also writes voice `active`
  // and `pos`, track `timeline` and `done`, and the frame counter, so the main thread reads those only through the
  // locked accessors voiceActive(), voicePcm(), trackPhase() and framesRenderedLocked(). voice(), current() and fading()
  // return references or pointers whose plain fields (not those) only the main thread writes.
  void setLock(MixerLock* lock) { lock_ = lock; }
  // --- voices ---
  // Starts a voice. Returns its slot, or -1 when all 64 are busy (the sound is dropped, and dropped() counts it).
  int play(const Pcm* pcm, const VoiceParams& params);
  void stop(int slot);
  // Ramps every voice of `tag` to silence over `ms` and frees it then; at once when `ms` <= 0.
  void release(int tag, float ms);
  bool hasTag(int tag) const;
  int activeVoices() const;
  int dropped() const { return dropped_; }
  // Locked reads of state the audio thread writes.
  bool voiceActive(int slot, const Pcm* pcm) const;  // the slot still plays `pcm`
  const Pcm* voicePcm(int slot) const;
  std::pair<std::size_t, std::size_t> trackPhase(const MusicTrack* t) const;
  uint64_t framesRenderedLocked() const;
  const Voice& voice(int slot) const { return voices_[static_cast<std::size_t>(slot)]; }

  // --- music tracks (two slots: the current one and the one fading out) ---
  // The old current track fades to silence over `outMs` (a track still fading out is cut), and `track` fades in from
  // silence over `inMs`; the two overlap. Returns false when it cannot be started (nothing to play, a stage with no source, a null
  // source or more than kMaxStemsPerStage of them, or too many unreaped tracks).
  bool startTrack(MusicTrack* track, float inMs, float outMs);
  bool startTrack(MusicTrack* track, float fadeMs) { return startTrack(track, fadeMs, fadeMs); }
  // Fades the current track out; at once when `fadeMs` <= 0.
  // False (and nothing changes) when there is no current track or the retired slots are full.
  bool stopTrack(float fadeMs);
  MusicTrack* current() const { return current_; }
  MusicTrack* fading() const { return fading_; }
  // Unlinks every finished or cut track and returns how many were written to `out` (at most `max`). The caller frees
  // them outside the audio lock.
  std::size_t reap(MusicTrack** out, std::size_t max);

  // --- levels and pause (all linear) ---
  void setGroupGain(Group g, float gain) { Guard lk(*this); groupGain_[static_cast<std::size_t>(g)] = gain; }
  float groupGain(Group g) const { return groupGain_[static_cast<std::size_t>(g)]; }
  void setMaster(float m) { Guard lk(*this); master_ = m; }
  float master() const { return master_; }
  void setSystemPaused(bool p) { Guard lk(*this); systemPaused_ = p; }
  void setCadiPaused(bool p) { Guard lk(*this); cadiPaused_ = p; }
  // Freezes the music tracks only (Chocolatier 2's PauseMusic); voices keep playing.
  void setMusicPaused(bool p) { Guard lk(*this); musicPaused_ = p; }
  bool musicPaused() const { return musicPaused_; }
  bool systemPaused() const { return systemPaused_; }
  bool cadiPaused() const { return cadiPaused_; }

  // Overwrites `out` (interleaved f32 stereo, `frames` frames) with the mix, hard-clipped to [-1, 1]. No SDL, no
  // allocation, no I/O.
  void render(float* out, std::size_t frames);
  uint64_t framesRendered() const { return rendered_; }

 private:
  // Re-entrant on the main thread: a public method may call another.
  struct Guard {
    explicit Guard(const Mixer& m) : m_(m) {
      if (m_.lock_ && m_.depth_++ == 0) m_.lock_->lock();
    }
    ~Guard() {
      if (m_.lock_ && --m_.depth_ == 0) m_.lock_->unlock();
    }
    const Mixer& m_;
  };
  bool retire(MusicTrack* t);
  std::size_t freeRetiredSlots() const;
  void renderVoice(Voice& v, float* out, std::size_t frames);
  void renderTrack(MusicTrack& t, float* out, std::size_t frames);

  std::array<Voice, kMaxVoices> voices_{};
  std::array<float, kGroupCount> groupGain_{1, 1, 1, 1};
  float master_ = 1.0f;
  bool systemPaused_ = false;
  bool cadiPaused_ = false;
  bool musicPaused_ = false;
  int dropped_ = 0;
  uint64_t rendered_ = 0;
  MusicTrack* current_ = nullptr;
  MusicTrack* fading_ = nullptr;
  std::array<MusicTrack*, 8> retired_{};
  MixerLock* lock_ = nullptr;
  mutable int depth_ = 0;  // main thread only  // cut or finished, waiting for reap()
};

}  // namespace choco::audio
