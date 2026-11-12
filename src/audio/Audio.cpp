#include "audio/Audio.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "app/Diagnostics.h"

namespace choco::audio {

namespace {

std::string joinFiles(const std::vector<std::string>& files) {
  std::string s;
  for (const std::string& f : files) s += (s.empty() ? "" : ";") + f;
  return s;
}

constexpr int kFileAmbientTag = -2;       // voice tag of the bed started by setAmbientFile
constexpr float kAmbientCrossfadeMs = 300;  // replacing or stopping a bed (a decision: Chocolatier 2's fade is unknown)

std::string cadiPath(const std::string& file) { return "cadi/audio/" + file; }

// The exact resource of entry `i` of an event's files (or sweeteners); the basename under cadi/audio when the table has none.
Resource resourceAt(const EventDef& def, bool sweetener, std::size_t i) {
  const auto& res = sweetener ? def.sweetenerRes : def.fileRes;
  if (i < res.size()) return res[i];
  return {cadiPath((sweetener ? def.sweeteners : def.files)[i]), {}};
}

}  // namespace

uint64_t Audio::Rng::next() {
  s ^= s >> 12;
  s ^= s << 25;
  s ^= s >> 27;
  return s * 2685821657736338717ULL;
}
double Audio::Rng::uniform() { return static_cast<double>(next() >> 11) / 9007199254740992.0; }

Audio::Audio(SoundCache::Loader loader, const EventTable& table, LogSink log, uint32_t seed)
    : log_(log ? std::move(log) : LogSink([](const std::string& l) { diag::logLine(l); })),
      table_(table),
      cache_(std::move(loader), log_),
      rng_{(0x9E3779B97F4A7C15ULL ^ (static_cast<uint64_t>(seed) * 0xBF58476D1CE4E5B9ULL)) | 1ULL} {
  applyGroupGains();
}

Audio::~Audio() = default;

void Audio::log(const std::string& line) {
  if (log_) log_(line);
}

void Audio::applyGroupGains() {
  // AU s.10.6: a volume event writes the channel's Volume, so the group gain is the level itself, linear. (This replaces
  // the design's earlier "level x root gain x 0.985" guess.)
  const auto curve = [this](float v) { return squared_ ? v * v : v; };
  mixer_.setGroupGain(Group::Sfx, curve(sfx_));
  mixer_.setGroupGain(Group::Music, curve(music_));
  mixer_.setGroupGain(Group::Env, curve(env_));
  mixer_.setGroupGain(Group::Direct, curve(std::min(1.0f, sfx_)));  // TSound clamps to [0, 1]
  mixer_.setMaster(muted_ ? 0.0f : 1.0f);
}

void Audio::setSquaredGain(bool squared) {
  squared_ = squared;
  applyGroupGains();
}

Audio::VoiceHandle Audio::playVoice(const std::string& path, Group group, bool loop, float gain, bool cached) {
  std::unique_ptr<Pcm> owned;
  const Pcm* pcm = nullptr;
  if (cached) {
    pcm = cache_.pcm(path);
  } else {
    owned = cache_.decodeUncached(path);
    pcm = owned.get();
  }
  if (!pcm) return 0;
  VoiceParams p;
  p.group = group;
  p.loop = loop;
  p.gain = gain;
  const int slot = mixer_.play(pcm, p);
  if (slot < 0) {
    noteDrop();
    return 0;
  }
  static constexpr const char* kGroupNames[] = {"sfx", "env", "music", "direct"};
  log("[audio] voice " + path + " group=" + kGroupNames[static_cast<int>(group)] + " loop=" + (loop ? "1" : "0"));
  bumpFired(path);
  const VoiceHandle h = nextVoice_++;
  VoiceRec& r = voices_[h];
  r.slot = slot;
  r.serial = mixer_.voiceSerial(slot);
  r.pcm = pcm;
  r.owned = std::move(owned);
  return h;
}

bool Audio::voiceLive(const VoiceRec& r) const {
  return mixer_.voiceActive(r.slot, r.pcm) && mixer_.voiceSerial(r.slot) == r.serial;
}

bool Audio::voiceDone(VoiceHandle h) const {
  const auto it = voices_.find(h);
  return it == voices_.end() || !voiceLive(it->second);
}

void Audio::setVoiceGain(VoiceHandle h, float gain) {
  const auto it = voices_.find(h);
  if (it != voices_.end() && voiceLive(it->second)) mixer_.setVoiceGain(it->second.slot, gain);
}

void Audio::pauseVoice(VoiceHandle h, bool paused) {
  const auto it = voices_.find(h);
  if (it != voices_.end() && voiceLive(it->second)) mixer_.setVoicePaused(it->second.slot, paused);
}

void Audio::stopVoice(VoiceHandle h) {
  const auto it = voices_.find(h);
  if (it == voices_.end()) return;
  if (voiceLive(it->second)) mixer_.stop(it->second.slot);
  voices_.erase(it);  // the voice is stopped, so an owned decode can go
}

void Audio::setLevels(float sfx, float music, float env, bool muted) {
  sfx_ = sfx;
  music_ = music;
  env_ = env;
  muted_ = muted;
  applyGroupGains();
}

void Audio::setForeground(bool foreground) {
  focusPaused_ = !foreground;
  mixer_.setSystemPaused(focusPaused_);
}

void Audio::setGamePaused(bool paused) {
  gamePaused_ = paused;
  mixer_.setCadiPaused(paused);
}

void Audio::noteDrop() {
  if (capLogged_) return;
  capLogged_ = true;
  log("[audio] voice cap reached (" + std::to_string(kMaxVoices) + "): dropping sounds");
}

void Audio::playDirect(const std::string& path, Group group) {
  const Pcm* pcm = cache_.pcm(path);
  if (!pcm) return;
  VoiceParams p;
  p.group = group;
  if (mixer_.play(pcm, p) < 0) noteDrop();
}

void Audio::playFile(const std::string& fullPath) {
  std::string path = fullPath;
  Group group = Group::Direct;
  if (const auto q = path.find("?group="); q != std::string::npos) {
    if (std::atoi(path.c_str() + q + 7) == 1001) group = Group::Music;  // the other values map to Direct
    path.resize(q);
  }
  log("[audio] file " + path);
  bumpFired(path);
  playDirect(path, group);
}

void Audio::stopFile(const std::string& path) {
  log("[audio] stop file " + path);
  const Pcm* pcm = cache_.pcm(path);
  if (!pcm) return;  // never played (a failed load was logged once already)
  for (int slot = 0; slot < kMaxVoices; ++slot)
    if (mixer_.voiceActive(slot, pcm)) mixer_.stop(slot);  // at once: the original's stop is immediate too
}

int Audio::playCadi(const EventDef& def, const Resource& res, bool loop, float gain, float fadeInMs, int tag) {
  const Pcm* pcm = cache_.pcm(res.primary, res.fallback);
  if (!pcm) return -1;
  VoiceParams p;
  p.group = def.group;
  p.loop = loop;
  p.gain = gain;
  p.cadi = true;
  p.tag = tag;
  p.fadeInMs = fadeInMs;
  const int slot = mixer_.play(pcm, p);
  if (slot < 0) noteDrop();
  return slot;
}

std::unique_ptr<MusicTrack> Audio::buildTrack(const EventDef& def, Part part) {
  auto track = std::make_unique<MusicTrack>();
  track->name = def.name;
  track->gain = def.channelVolume;
  const MusicRoles& m = def.music;
  // Music streams: every source is an OggStream over the cached bytes, owned by the track and freed when it is reaped.
  // Stages that never overlap in time may share a stream (a stem's successive clips): `lane` names the shared stream.
  std::map<std::string, OggStream*> lanes;
  auto open = [&](const std::string& base, bool shared) -> OggStream* {
    if (shared) {
      if (auto it = lanes.find(base); it != lanes.end()) return it->second;
    }
    const Resource* r = def.resourceOf(base);
    const Resource res = r ? *r : Resource{cadiPath(base), {}};
    OggStream* raw = nullptr;
    if (auto stream = cache_.openStream(res.primary, res.fallback)) {
      stream->setSilent(skipDecode_);
      raw = stream.get();
      track->streams.push_back(std::move(stream));
    }
    if (shared) lanes[base] = raw;
    return raw;
  };
  // Adds a stage and returns the longest of its sources in frames (0 when nothing loaded).
  auto stage = [&](const std::vector<std::string>& files, bool loop, uint64_t start, uint64_t length, float gain = 1.0f,
                   bool shared = false) -> uint64_t {
    MusicStage st;
    st.loop = loop;
    st.startFrame = start;
    st.lengthFrames = length;
    st.gain = gain;
    uint64_t longest = 0;
    for (const std::string& f : files) {
      if (f.empty() || st.sources.size() >= static_cast<std::size_t>(kMaxStemsPerStage)) continue;
      if (OggStream* stream = open(f, shared)) {
        st.sources.push_back(stream);
        longest = std::max<uint64_t>(longest, stream->totalFrames());
      }
    }
    if (!st.sources.empty()) track->stages.push_back(std::move(st));
    return longest;
  };
  switch (part) {
    case Part::Start: {
      if (!m.clips.empty()) {
        // port_music<n> (docs/re/audio-port-music.md): each stem is a few clips on the cue's timeline, with its own start,
        // length (cut to the stem's loop region) and volume. The whole cue loops at its length, so every stream is
        // rewound as its clip begins. Hook for refinements (the arrangement's own fade, clip-level quantised starts):
        // MusicClip / MusicStage already carry everything the timeline needs.
        for (const MusicClip& c : m.clips) stage({c.file}, false, c.startFrame, c.lengthFrames, c.gain, true);
        track->loopFrames = m.lengthFrames;
        break;
      }
      // AU s.10.5: a port arrangement is a clip on a timeline. The sting plays once from 0 with the percussive layer under
      // it until the layers enter (`handover`, lima 276,102); then the layers play once, portmusicFADE follows them, and
      // the clip ends at its length. An arrangement without a sting (map_) loops its layers.
      // TODO: the other ports' layer repeats and fade length are read for lima only; the hand-over, and the per-clip
      // volumes (sting 1.1955, the second percussive 1.007) are assumed from lima.
      const bool hasSting = !m.sting.empty();
      uint64_t layersAt = 0;
      if (hasSting) {
        stage({m.sting}, false, 0, 0);
        layersAt = m.handoverFrames;
        if (layersAt && !m.under.empty()) stage({m.under}, false, 0, layersAt);
      }
      uint64_t layersLen = 0;
      if (!m.layers.empty()) {
        layersLen = stage(m.layers, !hasSting, layersAt, 0);
      } else if (!hasSting || !m.body.empty()) {
        stage({m.body}, true, layersAt, 0);
      }
      if (hasSting && !m.tail.empty() && layersLen) stage({m.tail}, true, layersAt + layersLen, 0);
      if (hasSting && !m.loop) track->endFrames = m.lengthFrames;
      if (!m.layers.empty() && m.loop) track->loopFrames = m.lengthFrames;
      break;
    }
    case Part::Intro: {
      // TODO: the body clip starts at 355,164, not at the intro file's end (AU s.10.5).
      const uint64_t introLen = stage({m.intro}, false, 0, 0);
      stage({m.body}, true, introLen, 0);
      break;
    }
    case Part::Outro:
      stage({m.ending}, false, 0, 0);
      break;
  }
  return track;
}

void Audio::startMusic(const Resolved& r, const std::string& name) {
  const EventDef& def = *r.def;
  const bool playing = musicTrack_ && musicTrack_ == mixer_.current() && musicIndex_ == r.index;
  // Retrigger rule (design 9, open RE question 2): an event equal to the one playing is ignored. A `start` while the same
  // arrangement is already playing any part (e.g. still in its intro) is ignored too.
  if (playing && (musicPart_ == r.part || r.part == Part::Start)) {
    log("[audio] " + name + " -> music " + def.name + " ignored (already playing)");
    return;
  }
  if (!def.music.present) {
    log("[audio] " + name + " -> music " + def.name + " has no roles in music.tsv: silent");
    return;
  }
  auto track = buildTrack(def, r.part);
  // Crossfade (AU s.10.6, a decision): the outgoing music fades out over the incoming clip's source fade and the incoming
  // fades in over its master fade, overlapping. A fade the clip does not state is 1000 ms; a stated 0 is a cut. The
  // arrangement's own fade pair is not used.
  const int inMs = r.masterFadeMs >= 0 ? r.masterFadeMs : 1000;
  const int outMs = r.sourceFadeMs >= 0 ? r.sourceFadeMs : 1000;
  MusicTrack* raw = track.get();
  if (!mixer_.startTrack(raw, static_cast<float>(inMs), static_cast<float>(outMs))) {
    log("[audio] " + name + " -> music " + def.name + " not started");
    return;
  }
  tracks_.push_back(std::move(track));
  musicIndex_ = r.index;
  musicFile_.clear();
  musicPart_ = r.part;
  musicTrack_ = raw;
  musicFadeMs_ = static_cast<float>(inMs);
  bumpFired(name);
  log("[audio] " + name + " -> music " + def.name + (r.alias ? " (alias)" : "") + " fade " + std::to_string(inMs) + "/" + std::to_string(outMs));
}

void Audio::stopEnvironment() {
  if (envIndex_ >= 0) {
    const EventDef& d = table_.events()[static_cast<std::size_t>(envIndex_)];
    mixer_.release(envIndex_, static_cast<float>(std::max(d.releaseMs, 0)));
  }
  envIndex_ = -1;
  if (!envFile_.empty()) {
    mixer_.release(kFileAmbientTag, kAmbientCrossfadeMs);
    envFile_.clear();
  }
  sweetenerAt_ = -1;
}

void Audio::startEnvironment(const EventDef& def, int index) {
  if (envIndex_ == index) {
    log("[audio] " + def.name + " -> environment ignored (already playing)");
    return;
  }
  stopEnvironment();
  envIndex_ = index;
  if (!def.files.empty()) playCadi(def, resourceAt(def, false, 0), true, def.channelVolume, static_cast<float>(std::max(def.attackMs, 0)), index);
  if (!def.sweeteners.empty() && def.intervalMinMs >= 0 && def.intervalMaxMs > 0) {  // the generator rejects 0-0
    sweetenerAt_ = audioMs_ + def.intervalMinMs + rng_.uniform() * (def.intervalMaxMs - def.intervalMinMs);
  }
  bumpFired(def.name);
  log("[audio] " + def.name + " -> environment " + joinFiles(def.files));
}

void Audio::event(const std::string& name) {
  const Resolved r = table_.resolve(name);
  if (!r.def) {
    if (unknownLogged_.insert(name).second) log("[audio] unknown event " + name);
    return;
  }
  const EventDef& def = *r.def;
  switch (def.kind) {
    case Kind::Direct:
      if (!def.files.empty()) {
        log("[audio] " + name + " -> direct " + def.files[0]);
        bumpFired(def.files[0]);
        playDirect(def.files[0], Group::Direct);
      }
      break;
    case Kind::Oneshot:
      if (!def.files.empty()) {
        bumpFired(name);
        log("[audio] " + name + " -> oneshot " + def.files[0]);
        if (def.restart) mixer_.release(r.index, 0);
        playCadi(def, resourceAt(def, false, 0), false, def.channelVolume, 0, def.restart ? r.index : -1);
      }
      break;
    case Kind::OneshotRandom: {
      // A RandomSelector (AU s.10.6): one random child per trigger, never the one picked last time, and a child whose
      // voice is still playing is skipped (the original declines that pick; we pick among the others instead).
      if (def.files.empty()) break;
      const std::size_t n = def.files.size();
      auto& state = children_[r.index];
      if (state.size() != n) state.assign(n, Child{});
      std::vector<std::size_t> open;
      for (std::size_t i = 0; i < n; ++i) {
        const Child& c = state[i];
        const bool playing = c.slot >= 0 && mixer_.voiceActive(c.slot, c.pcm);
        if (playing || (n > 1 && static_cast<int>(i) + 1 == lastPick_[r.index])) continue;
        open.push_back(i);
      }
      if (open.empty()) {
        log("[audio] " + name + " -> random: every child is still playing");
        break;
      }
      const std::size_t pick = open[static_cast<std::size_t>(rng_.uniform() * static_cast<double>(open.size()))];
      lastPick_[r.index] = static_cast<int>(pick) + 1;  // 0 = none yet
      bumpFired(name);
      log("[audio] " + name + " -> random " + def.files[pick]);
      const Resource res = resourceAt(def, false, pick);
      const int slot = playCadi(def, res, false, def.channelVolume, 0, -1);
      state[pick] = {slot, mixer_.voicePcm(slot)};
      break;
    }
    case Kind::Loop:
      if (!def.files.empty()) {
        // A loop that is already running restarts (AU s.7.3, open RE question 2).
        mixer_.release(r.index, 0);
        bumpFired(name);
        log("[audio] " + name + " -> loop " + def.files[0]);
        playCadi(def, resourceAt(def, false, 0), true, def.channelVolume, static_cast<float>(std::max(def.attackMs, 0)), r.index);
      }
      break;
    case Kind::AmbienceLoop:
      startEnvironment(def, r.index);
      break;
    case Kind::Music:
      startMusic(r, name);
      break;
    case Kind::MusicFile:
      // Every fire restarts the file from 0 with a cut: the Mac runs "stop music" and then plays.
      if (!def.files.empty()) {
        const Resource res = resourceAt(def, false, 0);
        playMusicFile(res.primary, def.loop, 0, 0);
      }
      break;
    case Kind::Stop:
      if (def.stopGroup == StopGroup::Music) {
        log("[audio] " + name + " -> stop music");
        // UNKNOWN (AU s.11): whether Stop_Music fades or cuts. Default (design 9): fade over the playing clip's fade.
        if (mixer_.stopTrack(musicTrack_ == mixer_.current() ? musicFadeMs_ : 0.0f)) {
          musicIndex_ = -1;
          musicFile_.clear();
          musicTrack_ = nullptr;
        } else if (mixer_.current()) {
          log("[audio] " + name + " -> stop music not done (too many tracks still fading)");
        }
      } else if (def.stopGroup == StopGroup::Environments) {
        log("[audio] " + name + " -> stop environments");
        stopEnvironment();
      } else if (def.stopTarget >= 0) {
        const EventDef& target = table_.events()[static_cast<std::size_t>(def.stopTarget)];
        log("[audio] " + name + " -> stop " + target.name);
        mixer_.release(def.stopTarget, static_cast<float>(std::max(target.releaseMs, 0)));
      }
      break;
    case Kind::Builtin:
      log("[audio] " + name + " -> " + (name == "Pause" ? "pause" : "unpause"));
      setGamePaused(name == "Pause");
      break;
    case Kind::Unknown:
      if (unknownLogged_.insert(name).second) log("[audio] " + name + " -> effect unknown (RE), silent");
      break;
    case Kind::Volume:
    case Kind::Silent:
      break;  // known-silent: no log (design 7)
  }
}

// ---- File-driven music and ambience (Chocolatier 2) ----

double Audio::uniform() { return rng_.uniform(); }

void Audio::stopMusicTrack(float fadeMs) {
  if (mixer_.stopTrack(fadeMs)) {
    musicIndex_ = -1;
    musicFile_.clear();
    musicTrack_ = nullptr;
  } else if (mixer_.current()) {
    log("[audio] stop music not done (too many tracks still fading)");
  }
}

bool Audio::playMusicFile(const std::string& path, bool loop, int fadeInMs, int fadeOutMs) {
  mixer_.setMusicPaused(false);  // a new track always plays
  auto track = std::make_unique<MusicTrack>();
  track->name = path;
  if (auto stream = cache_.openStream(path, {})) {
    stream->setSilent(skipDecode_);
    MusicStage st;
    st.loop = loop;
    st.sources.push_back(stream.get());
    track->stages.push_back(std::move(st));
    track->streams.push_back(std::move(stream));
  } else {
    // Missing or undecodable: the cache logged it once. The music that was playing is stopped, not left under a state
    // that no longer matches.
    stopMusicTrack(static_cast<float>(fadeOutMs));
    return false;
  }
  MusicTrack* raw = track.get();
  if (!mixer_.startTrack(raw, static_cast<float>(fadeInMs), static_cast<float>(fadeOutMs))) {
    log("[audio] music file " + path + " not started");
    return false;
  }
  tracks_.push_back(std::move(track));
  musicIndex_ = -1;
  musicFile_ = path;
  musicTrack_ = raw;
  musicFadeMs_ = static_cast<float>(fadeOutMs);
  bumpFired(path);
  log("[audio] music file " + path + (loop ? " loop" : " once") + " fade " + std::to_string(fadeInMs) + "/" + std::to_string(fadeOutMs));
  return true;
}

void Audio::stopMusic(int fadeMs) {
  log("[audio] music stop fade " + std::to_string(fadeMs));
  stopMusicTrack(static_cast<float>(fadeMs));
}

void Audio::setMusicPaused(bool paused) {
  if (mixer_.musicPaused() == paused) return;
  log(paused ? "[audio] music paused" : "[audio] music resumed");
  mixer_.setMusicPaused(paused);
}

void Audio::setAmbientFile(const std::string& path) {
  if (path.empty()) {
    if (envFile_.empty() && envIndex_ < 0) return;
    log("[audio] ambient stop");
    stopEnvironment();
    return;
  }
  if (path == envFile_ && mixer_.hasTag(kFileAmbientTag)) {
    log("[audio] ambient " + path + " ignored (already playing)");
    return;
  }
  stopEnvironment();
  const Pcm* pcm = cache_.pcm(path);
  if (!pcm) {
    if (unknownLogged_.insert("ambient:" + path).second) log("[audio] ambient " + path + " not started");
    return;
  }
  VoiceParams p;
  p.group = Group::Env;
  p.loop = true;
  p.tag = kFileAmbientTag;
  p.fadeInMs = kAmbientCrossfadeMs;
  if (mixer_.play(pcm, p) < 0) {
    noteDrop();
    return;
  }
  envFile_ = path;
  bumpFired(path);
  log("[audio] ambient " + path);
}

void Audio::reap() {
  MusicTrack* done[8];
  const std::size_t n = mixer_.reap(done, 8);
  for (std::size_t i = 0; i < n; ++i) {
    if (done[i] == musicTrack_) {
      musicTrack_ = nullptr;
      musicIndex_ = -1;
      musicFile_.clear();
    }
    tracks_.erase(std::remove_if(tracks_.begin(), tracks_.end(), [&](const auto& t) { return t.get() == done[i]; }), tracks_.end());
  }
}

std::vector<SoundCache::Request> Audio::prefetchList(const EventTable& table) {
  std::vector<SoundCache::Request> list;
  for (const EventDef& def : table.events()) {
    switch (def.kind) {
      case Kind::Oneshot:
      case Kind::OneshotRandom:
      case Kind::Loop:
      case Kind::AmbienceLoop:
        for (std::size_t i = 0; i < def.files.size(); ++i) {
          const Resource r = resourceAt(def, false, i);
          list.push_back({r.primary, r.fallback});
        }
        for (std::size_t i = 0; i < def.sweeteners.size(); ++i) {
          const Resource r = resourceAt(def, true, i);
          list.push_back({r.primary, r.fallback});
        }
        break;
      case Kind::Direct:  // a plain TSound file, played as listed
        if (!def.files.empty()) list.push_back({def.files[0], {}});
        break;
      case Kind::Silent:
        // Direct files that only appear as names: "cadi/ui_click.ogg", "sfx/main_rollover.ogg", and the typing clicks,
        // which the text box plays as sfx/<name>.ogg.
        if (def.group == Group::Direct) {
          if (def.name.size() > 4 && def.name.compare(def.name.size() - 4, 4, ".ogg") == 0) {
            list.push_back({def.name, {}});
          } else if (def.name.rfind("sfx_typing_", 0) == 0) {
            list.push_back({"sfx/" + def.name + ".ogg", {}});
          }
        }
        break;
      default:
        break;
    }
  }
  return list;
}

void Audio::startPrefetch(std::function<void()> onWorkerStart, std::vector<SoundCache::Request> extra) {
  auto list = prefetchList(table_);
  list.insert(list.end(), extra.begin(), extra.end());
  cache_.startPrefetch(std::move(list), std::move(onWorkerStart));
}

void Audio::update(double nowMs) {
  cache_.pollPrefetch();
  if (!started_) {
    started_ = true;
    lastMs_ = nowMs;
    pumpBase_ = static_cast<int64_t>(std::floor(nowMs * kSampleRate / 1000.0)) - static_cast<int64_t>(mixer_.framesRenderedLocked());
  }
  const double dt = std::max(0.0, nowMs - lastMs_);
  lastMs_ = nowMs;
  // Audio time stands still while CADI is paused, so sweeteners wait out a pause.
  if (!focusPaused_ && !gamePaused_) audioMs_ += dt;
  if (envIndex_ >= 0 && sweetenerAt_ >= 0) {
    const EventDef& d = table_.events()[static_cast<std::size_t>(envIndex_)];
    // Volume spread of the Randomizer is inferred (AU s.10.6): gain = channelVolume x (1 - volvary x u).
    while (audioMs_ >= sweetenerAt_) {
      const std::size_t k = static_cast<std::size_t>(rng_.uniform() * static_cast<double>(d.sweeteners.size()));
      const float g = d.channelVolume * static_cast<float>(1.0 - d.volVary * rng_.uniform());
      log("[audio] " + d.name + " -> sweetener " + d.sweeteners[k]);
      playCadi(d, resourceAt(d, true, k), false, g, 0, -1);
      sweetenerAt_ += std::max(1.0, d.intervalMinMs + rng_.uniform() * (d.intervalMaxMs - d.intervalMinMs));  // never a zero step
    }
  }
  if (pump_) {
    const int64_t target = static_cast<int64_t>(std::floor(nowMs * kSampleRate / 1000.0)) - pumpBase_;
    int64_t todo = target - static_cast<int64_t>(mixer_.framesRenderedLocked());
    constexpr std::size_t kChunk = 4096;
    if (scratch_.size() < kChunk * 2) scratch_.resize(kChunk * 2);
    while (todo > 0) {
      const std::size_t n = static_cast<std::size_t>(std::min<int64_t>(todo, kChunk));
      mixer_.render(scratch_.data(), n);
      todo -= static_cast<int64_t>(n);
    }
  }
  reap();
  // Voice records of voices that ended are dropped here (an uncached decode is freed once the audio thread is done with it).
  for (auto it = voices_.begin(); it != voices_.end();) it = voiceLive(it->second) ? std::next(it) : voices_.erase(it);
}

std::string Audio::dump() const {
  char buf[160];
  const char* pause = focusPaused_ && gamePaused_ ? "focus+game" : focusPaused_ ? "focus" : gamePaused_ ? "game" : "none";
  std::snprintf(buf, sizeof buf, "audio levels sfx=%.3f music=%.3f env=%.3f mute=%d pause=%s\n", static_cast<double>(sfx_),
                static_cast<double>(music_), static_cast<double>(env_), muted_ ? 1 : 0, pause);
  std::string out = buf;
  const MusicTrack* cur = mixer_.current();
  if (cur && cur == musicTrack_ && (musicIndex_ >= 0 || !musicFile_.empty())) {
    const auto ph = mixer_.trackPhase(cur);
    out += "audio music " + (musicIndex_ >= 0 ? table_.events()[static_cast<std::size_t>(musicIndex_)].name : musicFile_) +
           " stage=" + std::to_string(ph.first) + "/" + std::to_string(ph.second) + (mixer_.musicPaused() ? " paused" : "") + "\n";
  } else {
    out += "audio music none\n";
  }
  out += envIndex_ >= 0 ? "audio env " + table_.events()[static_cast<std::size_t>(envIndex_)].name + "\n"
         : !envFile_.empty() ? "audio env " + envFile_ + "\n"
                             : "audio env none\n";
  std::vector<std::string> loops;
  for (std::size_t i = 0; i < table_.events().size(); ++i) {
    if (table_.events()[i].kind == Kind::Loop && mixer_.hasTag(static_cast<int>(i))) loops.push_back(table_.events()[i].name);
  }
  std::sort(loops.begin(), loops.end());
  for (const std::string& l : loops) out += "audio loop " + l + "\n";
  out += "audio fired";
  if (fired_.empty()) out += " none";
  for (const auto& [name, n] : fired_) out += " " + name + "=" + std::to_string(n);
  out += "\n";
  return out;
}

}  // namespace choco::audio
