#pragma once

// VSoundManager (docs/re/greatcc-audio.md, docs/design/greatcc.md 3.6): the play lists, the music slot, the voice-over queue
// and the 1000 ms fades, over the voice handles of audio::Audio. Owned by GreatCCWorld and ticked once per frame from
// GameState::afterAudio. It touches no window.

#include <deque>
#include <list>
#include <memory>
#include <string>
#include <vector>

#include "audio/Audio.h"

namespace choco::games::greatcc {

class GreatCCSound {
 public:
  // The pflib group ids (scripts/style.lua kSFXSoundGroup / kMusicSoundGroup; 0 is what a TButton's own sounds use).
  static constexpr int kUiGroup = 0;
  static constexpr int kSfxGroup = 1000;
  static constexpr int kMusicGroup = 1001;
  static constexpr double kFadeMs = 1000;

  // `audio` may be null (no sound service: every call is silent). It must outlive this object.
  explicit GreatCCSound(audio::Audio* audio);
  ~GreatCCSound();
  GreatCCSound(const GreatCCSound&) = delete;
  GreatCCSound& operator=(const GreatCCSound&) = delete;

  // PlaySFX / the sprite script's playsound: no dedupe, group 1000. Returns whether a voice started. A path without ".ogg"
  // or an empty one is silent (TSound::Get returns null without a log), a missing file is logged once by the audio cache.
  bool playSfx(const std::string& path, bool loop);
  // PlaySFX that hands back the voice (0 when none started), for a caller that ends the sound itself (the zeppelin's engine loop):
  // TSoundInstance::Kill is stopVoice; a voice that has finished or been stopped already is ignored.
  int playSfxVoice(const std::string& path, bool loop);
  void stopVoice(int voice);
  // A TButton's own click or rollover sound (group 0, plays through TSound directly: not in ACTIVE, so PauseSounds and
  // PurgeSounds never reach it). Same path rules as PlaySFX.
  bool playUi(const std::string& path);
  // PlayMusic: the track already current (same name) is left alone; a file that fails to load leaves the current track
  // untouched; else the new one starts and replaces it, with a 1000 ms crossfade when `fade` (none while muted).
  // Returns whether CURMUSIC now holds a track of that name.
  bool playMusic(const std::string& path, bool loop, bool fade);
  // PlayVO: group 1000, queued, one at a time.
  void playVO(const std::string& path, bool interrupt, bool skipIfBusy);
  void purgeVOQueue();
  // A sound of assets.xml decoded at level load (VAssetManager::LoadAssetFile), so playing it later does not decode. A path that
  // is not a usable .ogg is skipped. Returns whether the file is now in the cache.
  bool preload(const std::string& path);
  // Drops the ACTIVE references only: nothing is killed (CURMUSIC keeps playing).
  void purgeSounds();
  // Pauses or resumes the ACTIVE voices of `group` (any group when negative); music (1001) is not touched by group 1000.
  void pauseSounds(bool pause, int group);
  // The per-frame task (every 16 ms in the original): completed voices raise their done flag, the next VO starts, done
  // voices are killed and erased (in that order), then the fades advance to `nowMs` (script clock).
  void update(double nowMs);

  // --- inspection (tests, logs) ---
  std::size_t activeCount() const { return active_.size(); }
  std::size_t voQueueLength() const { return voQueue_.size(); }
  bool voPlaying() const { return curVO_ != nullptr; }
  std::string currentMusic() const { return curMusic_ ? curMusic_->name : std::string(); }
  // The voice gain of the current music track (the fade's squared factor), 1 when no fade runs.
  float currentMusicGain() const;
  std::size_t fadeCount() const { return fades_.size(); }
  // The files of the voices started so far, in order (VO included, when it starts).
  const std::vector<std::string>& startedLog() const { return started_; }

 private:
  struct Sound {
    std::string name;
    int group = kSfxGroup;
    audio::Audio::VoiceHandle voice = 0;
    bool done = false;
    float gain = 1.0f;
    bool isVO = false;
  };
  using SoundPtr = std::shared_ptr<Sound>;
  struct Fade {
    SoundPtr sound;
    bool in = true;
    double startMs = -1;  // latched on the first update
  };

  static audio::Group audioGroup(int group);
  // GetSound + Play in one step (the original loads, then plays): null when the file is unusable.
  SoundPtr start(const std::string& path, int group, bool loop, float gain, bool isVO);
  bool usable(const std::string& path);
  void kill(const SoundPtr& s);
  bool fadeSound(const SoundPtr& s, bool in);

  audio::Audio* audio_;
  std::list<SoundPtr> active_;
  std::deque<SoundPtr> voQueue_;
  SoundPtr curMusic_, curVO_;
  std::vector<Fade> fades_;
  std::vector<std::string> started_;
  std::vector<std::string> warned_;
};

}  // namespace choco::games::greatcc
