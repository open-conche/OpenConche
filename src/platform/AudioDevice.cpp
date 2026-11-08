#include "platform/AudioDevice.h"

#include <SDL3/SDL.h>

#include <algorithm>

namespace choco::platform {

namespace {
constexpr int kChunkFrames = 2048;
}

void lowerCurrentThreadPriority() { SDL_SetCurrentThreadPriority(SDL_THREAD_PRIORITY_LOW); }

AudioDevice::AudioDevice(audio::Mixer& mixer) : mixer_(mixer) {
  if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
    error_ = SDL_GetError();
    return;
  }
  SDL_AudioSpec spec{};
  spec.format = SDL_AUDIO_F32;
  spec.channels = 2;
  spec.freq = audio::kSampleRate;
  buffer_.resize(static_cast<std::size_t>(kChunkFrames) * 2);
  stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, &AudioDevice::callback, this);
  if (!stream_) {
    error_ = SDL_GetError();
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    return;
  }
  mixer_.setLock(this);
  SDL_ResumeAudioStreamDevice(stream_);
}

AudioDevice::~AudioDevice() {
  if (!stream_) return;
  SDL_DestroyAudioStream(stream_);  // closes the device and stops the callback
  mixer_.setLock(nullptr);
  SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

void AudioDevice::lock() { SDL_LockAudioStream(stream_); }
void AudioDevice::unlock() { SDL_UnlockAudioStream(stream_); }

// Runs on the audio thread with the stream already locked by SDL: render, then queue the samples. No allocation here.
void AudioDevice::callback(void* self, SDL_AudioStream* stream, int additionalAmount, int /*totalAmount*/) {
  auto* d = static_cast<AudioDevice*>(self);
  constexpr int kFrameBytes = 2 * static_cast<int>(sizeof(float));
  int frames = additionalAmount / kFrameBytes;
  while (frames > 0) {
    const int n = std::min(frames, kChunkFrames);
    d->mixer_.render(d->buffer_.data(), static_cast<std::size_t>(n));
    SDL_PutAudioStreamData(stream, d->buffer_.data(), n * kFrameBytes);
    frames -= n;
  }
}

}  // namespace choco::platform
