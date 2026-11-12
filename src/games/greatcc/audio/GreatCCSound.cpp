#include "games/greatcc/audio/GreatCCSound.h"

#include <algorithm>

namespace choco::games::greatcc {

GreatCCSound::GreatCCSound(audio::Audio* audio) : audio_(audio) {}

// The world dies before the audio service: leave nothing of ours playing.
GreatCCSound::~GreatCCSound() {
  if (!audio_) return;
  for (const SoundPtr& s : active_) kill(s);
  kill(curMusic_);
  kill(curVO_);
  for (const Fade& f : fades_) kill(f.sound);
}

audio::Group GreatCCSound::audioGroup(int group) {
  if (group == kSfxGroup) return audio::Group::Sfx;
  if (group == kMusicGroup) return audio::Group::Music;
  return audio::Group::Direct;
}

bool GreatCCSound::usable(const std::string& path) {
  if (path.empty()) return false;
  if (path.find(".ogg") == std::string::npos) {
    // TSound::Get refuses a path without ".ogg" and logs nothing; the engine notes it once.
    if (audio_ && std::find(warned_.begin(), warned_.end(), path) == warned_.end()) {
      warned_.push_back(path);
      audio_->log("[greatcc-audio] '" + path + "' has no .ogg extension: silent");
    }
    return false;
  }
  return true;
}

bool GreatCCSound::preload(const std::string& path) {
  if (!audio_ || !usable(path)) return false;
  return audio_->cache().pcm(path) != nullptr;
}

void GreatCCSound::kill(const SoundPtr& s) {
  if (!s || !audio_ || s->voice == 0) return;
  audio_->stopVoice(s->voice);
}

GreatCCSound::SoundPtr GreatCCSound::start(const std::string& path, int group, bool loop, float gain, bool isVO) {
  if (!audio_ || !usable(path)) return nullptr;
  const audio::Audio::VoiceHandle h = audio_->playVoice(path, audioGroup(group), loop, gain, /*cached=*/group != kMusicGroup);
  if (h == 0) return nullptr;
  auto s = std::make_shared<Sound>();
  s->name = path;
  s->group = group;
  s->voice = h;
  s->gain = gain;
  s->isVO = isVO;
  started_.push_back(path);
  return s;
}

bool GreatCCSound::playSfx(const std::string& path, bool loop) {
  SoundPtr s = start(path, kSfxGroup, loop, 1.0f, false);
  if (!s) return false;
  active_.push_back(std::move(s));
  return true;
}

int GreatCCSound::playSfxVoice(const std::string& path, bool loop) {
  SoundPtr s = start(path, kSfxGroup, loop, 1.0f, false);
  if (!s) return 0;
  const int voice = s->voice;
  active_.push_back(std::move(s));
  return voice;
}

void GreatCCSound::stopVoice(int voice) {
  if (voice != 0 && audio_) audio_->stopVoice(voice);
}

bool GreatCCSound::playUi(const std::string& path) {
  if (!audio_ || !usable(path)) return false;
  audio_->playFile(path);
  started_.push_back(path);
  return true;
}

bool GreatCCSound::fadeSound(const SoundPtr& s, bool in) {
  if (!s) return false;
  if (audio_ && audio_->muted()) {
    s->done = !in;  // no fade while muted: a fade-out ends the sound at the next update
    return false;
  }
  fades_.push_back({s, in, -1});
  return true;
}

bool GreatCCSound::playMusic(const std::string& path, bool loop, bool fade) {
  if (curMusic_ && curMusic_->name == path) return true;  // the same track: nothing restarts
  const bool fadeIn = fade && audio_ && !audio_->muted();
  SoundPtr s = start(path, kMusicGroup, loop, fadeIn ? 0.0f : 1.0f, false);
  if (!s) return false;  // the old track keeps playing
  active_.push_back(s);
  if (!fade) {
    if (curMusic_) curMusic_->done = true;  // killed by the next update
  } else {
    fadeSound(curMusic_, false);
    fadeSound(s, true);
  }
  curMusic_ = s;
  return true;
}

void GreatCCSound::playVO(const std::string& path, bool interrupt, bool skipIfBusy) {
  if (!audio_ || !usable(path)) return;
  // GetSound must succeed now (the sound is loaded when it is queued; it plays when it reaches the front).
  if (!audio_->cache().pcm(path)) return;
  if (skipIfBusy && !voQueue_.empty()) return;
  if (interrupt && (!voQueue_.empty() || curVO_)) purgeVOQueue();
  auto s = std::make_shared<Sound>();
  s->name = path;
  s->group = kSfxGroup;
  s->isVO = true;
  voQueue_.push_back(std::move(s));
}

void GreatCCSound::purgeVOQueue() {
  if (curVO_) {
    kill(curVO_);  // it stays in ACTIVE until the next update sees it finished
    curVO_.reset();
  }
  voQueue_.clear();
}

void GreatCCSound::purgeSounds() { active_.clear(); }

void GreatCCSound::pauseSounds(bool pause, int group) {
  if (!audio_) return;
  for (const SoundPtr& s : active_)
    if (s && s->voice != 0 && (group < 0 || s->group == group)) audio_->pauseVoice(s->voice, pause);
}

float GreatCCSound::currentMusicGain() const { return curMusic_ ? curMusic_->gain : 1.0f; }

void GreatCCSound::update(double nowMs) {
  if (!audio_) return;
  // The stream-complete callbacks: a finished voice raises its done flag, and a finished VO frees the VO slot.
  auto complete = [this](const SoundPtr& s) {
    if (s && !s->done && s->voice != 0 && audio_->voiceDone(s->voice)) {
      s->done = true;
      if (s == curVO_) curVO_.reset();
    }
  };
  for (const SoundPtr& s : active_) complete(s);
  complete(curVO_);
  complete(curMusic_);
  // 1. VO start.
  if (!curVO_ && !voQueue_.empty()) {
    SoundPtr vo = voQueue_.front();
    voQueue_.pop_front();
    if (SoundPtr s = start(vo->name, kSfxGroup, false, 1.0f, true)) {
      active_.push_back(s);
      curVO_ = s;
    }
  }
  // 2. Reap: every done voice is killed and erased.
  for (auto it = active_.begin(); it != active_.end();) {
    if (*it && (*it)->done) {
      kill(*it);
      it = active_.erase(it);
    } else {
      ++it;
    }
  }
  // 3. Fades: linear in the volume value, which is squared into the gain; the voice gain is that square (the group level
  // carries the target). A fade ends when its sound is done or gone, or after the tick that reaches 1.
  for (std::size_t i = 0; i < fades_.size();) {
    Fade& f = fades_[i];
    Sound& s = *f.sound;
    if (s.done || s.voice == 0 || audio_->voiceDone(s.voice)) {
      fades_.erase(fades_.begin() + static_cast<std::ptrdiff_t>(i));
      continue;
    }
    if (f.startMs < 0) f.startMs = nowMs;
    const double t = std::min(1.0, std::max(0.0, nowMs - f.startMs) / kFadeMs);
    const double v = f.in ? t : 1.0 - t;
    s.gain = static_cast<float>(v * v);
    audio_->setVoiceGain(s.voice, s.gain);
    if (t >= 1.0) {
      if (!f.in) {
        s.done = true;
        kill(f.sound);  // a faded-out track ends here even if it was purged from ACTIVE
      }
      fades_.erase(fades_.begin() + static_cast<std::ptrdiff_t>(i));
      continue;
    }
    ++i;
  }
}

}  // namespace choco::games::greatcc
