#include "audio/Mixer.h"

#include <algorithm>

namespace choco::audio {

namespace {

constexpr float kInt16Scale = 1.0f / 32768.0f;

// Adds frame `p` of `pcm` times `g` to l/r. A mono source feeds both channels.
inline void addFrame(const Pcm& pcm, std::size_t p, float g, float& l, float& r) {
  if (pcm.channels == 1) {
    const float s = static_cast<float>(pcm.samples[p]) * kInt16Scale * g;
    l += s;
    r += s;
  } else {
    l += static_cast<float>(pcm.samples[p * 2]) * kInt16Scale * g;
    r += static_cast<float>(pcm.samples[p * 2 + 1]) * kInt16Scale * g;
  }
}

}  // namespace

int Mixer::play(const Pcm* pcm, const VoiceParams& params) {
  Guard lk(*this);
  if (!pcm || pcm->frames() == 0) return -1;
  for (int i = 0; i < kMaxVoices; ++i) {
    Voice& v = voices_[static_cast<std::size_t>(i)];
    if (v.active) continue;
    v = Voice{};
    v.pcm = pcm;
    v.loop = params.loop;
    v.gain = params.gain;
    v.group = params.group;
    v.cadi = params.cadi;
    v.tag = params.tag;
    v.active = true;
    v.serial = nextSerial_++;
    const uint32_t fade = msToFrames(params.fadeInMs);
    if (fade > 0) v.ramp.set(0.0f, 1.0f, fade);
    return i;
  }
  ++dropped_;
  return -1;
}

void Mixer::stop(int slot) {
  Guard lk(*this);
  if (slot >= 0 && slot < kMaxVoices) voices_[static_cast<std::size_t>(slot)].active = false;
}

void Mixer::release(int tag, float ms) {
  Guard lk(*this);
  const uint32_t frames = msToFrames(ms);
  for (Voice& v : voices_) {
    if (!v.active || v.tag != tag) continue;
    if (frames == 0) {
      v.active = false;
    } else {
      v.ramp.set(v.ramp.cur, 0.0f, frames, true);
    }
  }
}

bool Mixer::voiceActive(int slot, const Pcm* pcm) const {
  Guard lk(*this);
  if (slot < 0 || slot >= kMaxVoices) return false;
  const Voice& v = voices_[static_cast<std::size_t>(slot)];
  return v.active && v.pcm == pcm;
}

const Pcm* Mixer::voicePcm(int slot) const {
  Guard lk(*this);
  return slot >= 0 && slot < kMaxVoices ? voices_[static_cast<std::size_t>(slot)].pcm : nullptr;
}

uint32_t Mixer::voiceSerial(int slot) const {
  Guard lk(*this);
  return slot >= 0 && slot < kMaxVoices ? voices_[static_cast<std::size_t>(slot)].serial : 0;
}

void Mixer::setVoiceGain(int slot, float gain) {
  Guard lk(*this);
  if (slot >= 0 && slot < kMaxVoices) voices_[static_cast<std::size_t>(slot)].gain = gain;
}

void Mixer::setVoicePaused(int slot, bool paused) {
  Guard lk(*this);
  if (slot >= 0 && slot < kMaxVoices) voices_[static_cast<std::size_t>(slot)].paused = paused;
}

std::pair<std::size_t, std::size_t> Mixer::trackPhase(const MusicTrack* t) const {
  Guard lk(*this);
  return t->phase();
}

uint64_t Mixer::framesRenderedLocked() const {
  Guard lk(*this);
  return rendered_;
}

bool Mixer::hasTag(int tag) const {
  Guard lk(*this);
  return std::any_of(voices_.begin(), voices_.end(), [&](const Voice& v) { return v.active && v.tag == tag; });
}

int Mixer::activeVoices() const {
  Guard lk(*this);
  return static_cast<int>(std::count_if(voices_.begin(), voices_.end(), [](const Voice& v) { return v.active; }));
}

bool Mixer::retire(MusicTrack* t) {
  t->done = true;
  for (MusicTrack*& slot : retired_) {
    if (!slot) {
      slot = t;
      return true;
    }
  }
  return false;
}

std::size_t Mixer::freeRetiredSlots() const {
  return static_cast<std::size_t>(std::count(retired_.begin(), retired_.end(), nullptr));
}

std::pair<std::size_t, std::size_t> MusicTrack::phase() const {
  // Distinct start frames, counted without allocating (this runs under the audio lock): a stage counts when no earlier
  // stage has the same start.
  std::size_t distinct = 0, reached = 0;
  for (std::size_t i = 0; i < stages.size(); ++i) {
    bool seen = false;
    for (std::size_t j = 0; j < i && !seen; ++j) seen = stages[j].startFrame == stages[i].startFrame;
    if (seen) continue;
    ++distinct;
    if (stages[i].startFrame <= timeline) ++reached;
  }
  return {std::min(std::max<std::size_t>(reached, 1), distinct), distinct};
}

bool Mixer::startTrack(MusicTrack* track, float inMs, float outMs) {
  Guard lk(*this);
  if (!track || track == current_ || track == fading_ || track->stages.empty()) return false;
  uint64_t end = 0;
  bool endless = false;
  for (const MusicStage& st : track->stages) {
    if (st.sources.empty() || st.sources.size() > static_cast<std::size_t>(kMaxStemsPerStage)) return false;
    uint64_t longest = 0;
    for (const Source& src : st.sources) {
      if (!src.valid()) return false;
      longest = std::max<uint64_t>(longest, src.frames());
    }
    if (st.lengthFrames) longest = st.lengthFrames;
    else if (st.loop) endless = true;
    end = std::max(end, st.startFrame + longest);
  }
  if (track->loopFrames) track->finishAt = UINT64_MAX;
  else if (track->endFrames) track->finishAt = track->endFrames;
  else track->finishAt = endless ? UINT64_MAX : end;
  if (freeRetiredSlots() < 2) return false;  // make room first so a failure leaves everything as it was
  const uint32_t fadeIn = msToFrames(inMs), fadeOut = msToFrames(outMs);
  if (fading_) {  // a third switch during a fade cuts the one already fading out
    retire(fading_);
    fading_ = nullptr;
  }
  if (current_) {
    current_->ramp.set(current_->ramp.cur, 0.0f, fadeOut, true);
    if (fadeOut == 0) {
      retire(current_);
    } else {
      fading_ = current_;
    }
  }
  track->timeline = 0;
  track->done = false;
  track->ramp.set(0.0f, 1.0f, fadeIn);
  current_ = track;
  return true;
}

bool Mixer::stopTrack(float fadeMs) {
  Guard lk(*this);
  if (!current_ || freeRetiredSlots() < 2) return false;
  const uint32_t fade = msToFrames(fadeMs);
  if (fading_) {
    retire(fading_);
    fading_ = nullptr;
  }
  current_->ramp.set(current_->ramp.cur, 0.0f, fade, true);
  if (fade == 0) {
    retire(current_);
  } else {
    fading_ = current_;
  }
  current_ = nullptr;
  return true;
}

std::size_t Mixer::reap(MusicTrack** out, std::size_t max) {
  Guard lk(*this);
  std::size_t n = 0;
  auto take = [&](MusicTrack*& t) {
    if (t && n < max) out[n++] = t, t = nullptr;
  };
  if (current_ && current_->done) take(current_);
  if (fading_ && fading_->done) take(fading_);
  for (MusicTrack*& t : retired_) take(t);
  return n;
}

void Mixer::renderVoice(Voice& v, float* out, std::size_t frames) {
  const float base = v.gain * groupGain_[static_cast<std::size_t>(v.group)] * master_;
  const std::size_t n = v.pcm->frames();
  for (std::size_t f = 0; f < frames; ++f) {
    float l = 0, r = 0;
    addFrame(*v.pcm, v.pos, base * v.ramp.next(), l, r);
    out[f * 2] += l;
    out[f * 2 + 1] += r;
    ++v.pos;
    if (v.pos >= n) {
      if (v.loop) {
        v.pos = 0;
      } else {
        v.active = false;
        return;
      }
    }
    if (v.ramp.ended()) {
      v.active = false;
      return;
    }
  }
}

void Mixer::renderTrack(MusicTrack& t, float* out, std::size_t frames) {
  const float base = t.gain * groupGain_[static_cast<std::size_t>(Group::Music)] * master_;
  for (std::size_t f = 0; f < frames && !t.done; ++f) {
    const float g = base * t.ramp.next();
    float l = 0, r = 0;
    for (const MusicStage& stage : t.stages) {
      if (t.timeline < stage.startFrame) continue;
      const uint64_t rel = t.timeline - stage.startFrame;
      if (stage.lengthFrames && rel >= stage.lengthFrames) continue;
      const float sg = g * stage.gain;
      for (const Source& s : stage.sources) {
        if (s.pcm) {
          const uint64_t n = s.pcm->frames();
          const uint64_t p = stage.loop ? rel % n : rel;
          if (p < n) addFrame(*s.pcm, static_cast<std::size_t>(p), sg, l, r);
          continue;
        }
        OggStream& o = *s.stream;
        if (rel == 0) o.rewind();
        int16_t a = 0, b = 0;
        bool ok = o.nextFrame(a, b);
        if (!ok && stage.loop) {
          o.rewind();
          ok = o.nextFrame(a, b);
        }
        if (ok) {
          l += static_cast<float>(a) * kInt16Scale * sg;
          r += static_cast<float>(b) * kInt16Scale * sg;
        }
      }
    }
    out[f * 2] += l;
    out[f * 2 + 1] += r;
    ++t.timeline;
    if (t.loopFrames && t.timeline >= t.loopFrames) t.timeline = 0;
    if (t.timeline >= t.finishAt || t.ramp.ended()) t.done = true;
  }
}

void Mixer::render(float* out, std::size_t frames) {
  std::fill(out, out + frames * 2, 0.0f);
  rendered_ += frames;
  if (!systemPaused_) {
    for (Voice& v : voices_) {
      if (!v.active || v.paused || (cadiPaused_ && v.cadi)) continue;
      renderVoice(v, out, frames);
    }
    if (!cadiPaused_ && !musicPaused_) {
      if (fading_ && !fading_->done) renderTrack(*fading_, out, frames);
      if (current_ && !current_->done) renderTrack(*current_, out, frames);
    }
  }
  for (std::size_t i = 0; i < frames * 2; ++i) out[i] = std::clamp(out[i], -1.0f, 1.0f);
}

}  // namespace choco::audio
