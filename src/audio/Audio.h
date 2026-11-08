#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "audio/EventTable.h"
#include "audio/Mixer.h"
#include "audio/SoundCache.h"

namespace choco::audio {

// The audio service (docs/design/audio.md s.3 and s.4). No SDL: it owns a Mixer and drives it. Without a device (headless,
// or none could be opened) update() pumps the mixer into a discard buffer from the caller's clock, which keeps runs
// deterministic; with a platform::AudioDevice the app calls setPump(false) and the device pulls instead. Main thread only.
class Audio {
 public:
  using LogSink = SoundCache::LogSink;

  // `log` receives one line per event; null = diag::logLine (the --log file). `seed` seeds the audio-only RNG (design
  // decision 11; App seeds it with seed + 3).
  // `table` is the game's SoundEvent table (EventTable::empty() when the caller uses no event); it must outlive this.
  Audio(SoundCache::Loader loader, const EventTable& table, LogSink log = nullptr, uint32_t seed = 1);
  ~Audio();
  Audio(const Audio&) = delete;
  Audio& operator=(const Audio&) = delete;

  // SoundEvent(name): never errors, unknown names are silent.
  void event(const std::string& name);
  // A direct TSound file (button sound=, typing). A "?group=N" suffix maps 1001 to Music and anything else to Direct.
  void playFile(const std::string& path);
  // Stops every voice that plays `path` at once (a playFile voice has no event tag, so the event table's stop-by-tag
  // cannot reach it; the original natives that stop a sound hold its handle, e.g. Chocolatier 1's TDiceGame stops its
  // dice roll when the last die stops). Nothing plays it: nothing happens (the Pcm lookup fails silently).
  void stopFile(const std::string& path);

  // --- File-driven music and ambience (Chocolatier 2: no event table; docs/re/choc2-audio.md) ---
  // Streams `path` as the music track: the playing music fades out over fadeOutMs and the new one in over fadeInMs. A
  // missing or undecodable file logs once (the cache) and leaves the music stopped. Returns whether a track started.
  bool playMusicFile(const std::string& path, bool loop, int fadeInMs, int fadeOutMs);
  void stopMusic(int fadeMs);
  void setMusicPaused(bool paused);  // the music track only: ambience, sounds and the CADI pause are unaffected
  bool musicPaused() const { return mixer_.musicPaused(); }
  // Loops `path` in the environment group, replacing the current environment with a short crossfade; "" stops it; the
  // bed already playing is left alone.
  void setAmbientFile(const std::string& path);
  // Whether a music track is playing: the level the Chocolatier 2 music state machine (games/choc2) tests each frame.
  bool musicPlaying() const { return musicTrack_ != nullptr; }
  // Stops the music track with a fade; logs nothing itself (stopMusic adds the "music stop" line).
  void stopMusicTrack(float fadeMs);
  // One draw from the audio RNG stream, in [0, 1).
  double uniform();
  void log(const std::string& line);

  // Levels are 0..1 (pref per mille / 1000).
  void setLevels(float sfx, float music, float env, bool muted);
  void setForeground(bool foreground);  // false pauses everything
  void setGamePaused(bool paused);      // pauses CADI voices and music only

  float sfxLevel() const { return sfx_; }
  float musicLevel() const { return music_; }
  float envLevel() const { return env_; }
  bool muted() const { return muted_; }

  // Headless pump mode only: music streams yield silence and decode nothing, but keep their positions, so track timing is
  // unchanged. Affects tracks built after the call.
  void setSkipDecode(bool skip) { skipDecode_ = skip; }

  // False once a device drives the mixer: update() then no longer renders into the discard buffer.
  void setPump(bool pump) { pump_ = pump; }

  // The sounds the event table can reach, in table order: oneshot/loop/ambience files, sweeteners, and the direct files
  // (ui_click, rollover, fireworks, typing clicks). Music is streamed and is not listed. Derived from the table.
  static std::vector<SoundCache::Request> prefetchList(const EventTable& table);
  // Starts decoding prefetchList() on the cache's worker thread (see SoundCache::startPrefetch). The app calls it for
  // windowed runs; headless runs and tests decode on first use, which keeps them free of threads.
  // `extra` adds files to decode too (a game module's own files, which are not in the event table).
  void startPrefetch(std::function<void()> onWorkerStart = {}, std::vector<SoundCache::Request> extra = {});

  // Advances audio time and the sweetener timers, reaps finished tracks, and in pump mode renders the mixer up to nowMs.
  void update(double nowMs);

  // The `audio ...` lines for the script driver's dump (design s.6).
  std::string dump() const;

  Mixer& mixer() { return mixer_; }
  const Mixer& mixer() const { return mixer_; }
  SoundCache& cache() { return cache_; }
  const EventTable& table() const { return table_; }
  bool gamePaused() const { return gamePaused_; }
  bool focusPaused() const { return focusPaused_; }

 private:
  struct Rng {
    uint64_t s;
    uint64_t next();
    double uniform();  // [0, 1)
  };

  void bumpFired(const std::string& key) { ++fired_[key]; }
  void playDirect(const std::string& path, Group group);
  // Starts a CADI voice from the resource (primary, then fallback); returns its mixer slot or -1.
  int playCadi(const EventDef& def, const Resource& res, bool loop, float gain, float fadeInMs, int tag);
  void noteDrop();
  void startMusic(const Resolved& r, const std::string& name);
  void startEnvironment(const EventDef& def, int index);
  void stopEnvironment();
  void applyGroupGains();
  std::unique_ptr<MusicTrack> buildTrack(const EventDef& def, Part part);
  void reap();

  LogSink log_;
  const EventTable& table_;
  SoundCache cache_;
  Mixer mixer_;
  Rng rng_;

  float sfx_ = 0.75f, music_ = 0.65f, env_ = 0.5f;  // levels
  bool muted_ = false;
  bool focusPaused_ = false, gamePaused_ = false;

  std::set<std::string> unknownLogged_;
  std::map<std::string, int> fired_;
  bool capLogged_ = false;

  // Music: the arrangement and part now playing (its track is the mixer's current one).
  std::vector<std::unique_ptr<MusicTrack>> tracks_;
  int musicIndex_ = -1;
  Part musicPart_ = Part::Start;
  MusicTrack* musicTrack_ = nullptr;
  float musicFadeMs_ = 0;  // the playing clip's fade, used by Stop_Music
  std::string musicFile_;  // the file playing through playMusicFile (musicIndex_ is -1 then)

  struct Child {
    int slot = -1;
    const Pcm* pcm = nullptr;  // the voice at `slot` is this child's only while it still plays this sound
  };
  std::map<int, int> lastPick_;  // OneshotRandom: 1 + the child picked last time (0 = none), per event index
  std::map<int, std::vector<Child>> children_;

  // Environment: the bed's event index and the sweetener timer, in audio time (ms).
  int envIndex_ = -1;
  std::string envFile_;  // a bed started by setAmbientFile (envIndex_ is -1 then)
  double audioMs_ = 0;
  double sweetenerAt_ = -1;

  // Pump mode.
  bool pump_ = true;
  bool skipDecode_ = false;
  bool started_ = false;
  double lastMs_ = 0;
  int64_t pumpBase_ = 0;
  std::vector<float> scratch_;
};

}  // namespace choco::audio
