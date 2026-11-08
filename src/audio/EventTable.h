#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "audio/Mixer.h"

namespace choco::audio {

// What a SoundEvent name does (docs/design/audio.md s.4).
enum class Kind {
  Oneshot, OneshotRandom, Loop, AmbienceLoop, Music, Stop,
  MusicFile,  // one streamed file, restarted from 0 with a cut on every fire (the Mac cadilite.spec)
  Volume,   // the three "<group> Volume" events: native only, ignored from Lua
  Builtin,  // Pause / Unpause
  Direct,   // a plain TSound file (ui_click, fireworks, typing)
  Silent,   // known to match nothing: no log
  Unknown,  // effect Unknown in the RE: logged once, silent
};

// Which part of an arrangement a music event plays (design s.4 "Music parts").
enum class Part { Start, Intro, Outro };

// One audio resource (AU s.10.9): the loader tries `primary`, then `fallback` when that file is absent. Reforged ships
// `cadi/audio/<file>` for most, but the exact vanilla file is `cadi/cad/<authoring path>`, which only a CAD extraction has.
struct Resource {
  std::string primary, fallback;
};

// One stem clip of a port_music cue (docs/re/audio-port-music.md): the stem plays from its sample 0 for `lengthFrames`
// (the clip window cut to the resource's loop region), starting at `startFrame` on the cue's timeline.
struct MusicClip {
  std::string file;
  uint64_t startFrame = 0, lengthFrames = 0;
  float gain = 1.0f;  // track volume x clip volume
};

// docs/re/audio.md: the file roles and clip timing of an arrangement, spelt out in a game's music.tsv (AU s.10.5).
struct MusicRoles {
  bool present = false;
  std::string status;  // settled | inferred | unused
  std::string sting;
  std::vector<std::string> layers;
  std::string intro, body, ending;
  std::string under;  // layer that plays under the sting from 0 until `handoverFrames`
  std::string tail;   // plays after the layers (portmusicFADE)
  bool loop = false;  // the clip's TimeLineLoopEnabled
  uint32_t lengthFrames = 0;    // the clip's end, in samples at 44.1 kHz
  uint32_t handoverFrames = 0;  // where the layers enter (lima 276,102); 0 = with the sting
  std::vector<MusicClip> clips;  // non-empty: the arrangement is a clip timeline (port_music), not roles
};

enum class StopGroup { None, Music, Environments };

// The rows of a game's generated event table (a game's AudioEvents.inc expands to these four arrays).
struct StaticEvent {
  const char* name;
  Kind kind;
  Group group;
  float channelVolume;
  const char* files;
  const char* sweeteners;
  int intervalMin, intervalMax, attack, release;
  float volVary;
  int fade, srcFade;
  const char* stop;
  const char *fileRes, *sweetenerRes;
};
struct StaticMusic {
  const char *arrangement, *status, *sting, *layers, *intro, *body, *ending, *under, *tail;
  bool loop;
  uint32_t length, handover;
};
struct StaticClip {
  const char *arrangement, *file;
  uint64_t start, length;
  float gain;
};
struct StaticAlias {
  const char *event, *arrangement;
  Part part;
  int masterFade, sourceFade;
};

struct EventTableData {
  std::span<const StaticEvent> events;
  std::span<const StaticMusic> music;
  std::span<const StaticClip> clips;
  std::span<const StaticAlias> aliases;
};

struct EventDef {
  std::string name;
  Kind kind = Kind::Silent;
  Group group = Group::Sfx;
  float channelVolume = 1.0f;  // empty in the tsv = 1.0
  std::vector<std::string> files;       // basenames, as the tsv lists them
  std::vector<std::string> sweeteners;  // intermittent_files
  std::vector<Resource> fileRes;        // exact resource per entry of `files`
  std::vector<Resource> sweetenerRes;   // ... and of `sweeteners`
  int intervalMinMs = -1, intervalMaxMs = -1;
  int attackMs = -1, releaseMs = -1;  // -1 = no envelope
  float volVary = 0.0f;
  int fadeMs = -1, srcFadeMs = -1;  // music crossfade (-1 = none stated)
  StopGroup stopGroup = StopGroup::None;
  int stopTarget = -1;  // for Stop_<x>: the index of event x
  MusicRoles music;
  bool loop = false;     // MusicFile: the file repeats
  bool restart = false;  // Oneshot: releases its own voice before it plays (a spec event that stops its own sound first)

  // The exact resource of a file this event lists (by basename); a null result when the event doesn't list it.
  const Resource* resourceOf(const std::string& basename) const;
};

struct Resolved {
  const EventDef* def = nullptr;  // null = unknown name
  int index = -1;                 // into events()
  Part part = Part::Start;
  bool alias = false;  // reached through music-aliases.tsv (an exact ArrangementClip name, AU s.10.2)
  int masterFadeMs = -1, sourceFadeMs = -1;  // the clip's fades (alias only); -1 = none stated
};

// The event table of one game, built from its generated rows. Names match exactly and case-sensitively; there is
// no prefix matching (design decision 7).
class EventTable {
 public:
  explicit EventTable(const EventTableData& data);
  // A table built from ready definitions: no aliases; for a repeated name the first definition is the one found.
  explicit EventTable(std::vector<EventDef> events);
  static const EventTable& empty();  // no events at all: every name is unknown

  Resolved resolve(std::string_view name) const;
  const EventDef* lookup(std::string_view name) const { return resolve(name).def; }
  const EventDef* arrangement(std::string_view name) const;  // a music event by its own name; null otherwise
  const std::vector<EventDef>& events() const { return events_; }

  struct Alias {
    std::string event, arrangement;
    Part part;
    int masterFadeMs, sourceFadeMs;
  };
  const std::vector<Alias>& aliases() const { return aliases_; }

 private:
  std::vector<EventDef> events_;
  std::map<std::string, int, std::less<>> byName_;
  std::vector<Alias> aliases_;
  std::map<std::string, std::size_t, std::less<>> aliasByName_;
};

}  // namespace choco::audio
