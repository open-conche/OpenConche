#pragma once

#include <string>
#include <vector>

#include "audio/Mixer.h"

struct SDL_AudioStream;

namespace choco::platform {

// Called on a background worker (the sound prefetch) to put it below the main and audio threads.
void lowerCurrentThreadPriority();

// The one SDL3 playback stream (docs/design/audio.md decision 1): input f32 stereo 44.1 kHz, pulled by a callback that
// calls Mixer::render while SDL holds the stream lock. SDL converts to whatever the device wants. It is also the mixer's
// lock: while it lives, the Mixer's main-thread methods take SDL_LockAudioStream. Destroy it before the Mixer (the
// destructor stops the callback, then unhooks the lock).
class AudioDevice final : public audio::MixerLock {
 public:
  explicit AudioDevice(audio::Mixer& mixer);
  ~AudioDevice() override;
  AudioDevice(const AudioDevice&) = delete;
  AudioDevice& operator=(const AudioDevice&) = delete;

  bool ok() const { return stream_ != nullptr; }
  const std::string& error() const { return error_; }

  void lock() override;
  void unlock() override;

 private:
  static void callback(void* self, SDL_AudioStream* stream, int additionalAmount, int totalAmount);

  audio::Mixer& mixer_;
  SDL_AudioStream* stream_ = nullptr;
  std::vector<float> buffer_;  // sized once, so the callback never allocates
  std::string error_;
};

}  // namespace choco::platform
