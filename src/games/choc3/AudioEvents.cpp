#include "games/choc3/AudioEvents.h"

#include <set>
#include <string>

#include "app/Diagnostics.h"
#include "audio/CadiLite.h"

namespace choco::games::choc3 {
namespace {

using namespace choco::audio;

const StaticEvent kEvents[] = {
#define EVENT(...) {__VA_ARGS__},
#define MUSIC(...) 
#define CLIP(...) 
#define ALIAS(...) 
#include "games/choc3/audio/generated/AudioEvents.inc"
#undef EVENT
#undef MUSIC
#undef CLIP
#undef ALIAS
};
const StaticMusic kMusic[] = {
#define EVENT(...) 
#define MUSIC(...) {__VA_ARGS__},
#define CLIP(...) 
#define ALIAS(...) 
#include "games/choc3/audio/generated/AudioEvents.inc"
#undef EVENT
#undef MUSIC
#undef CLIP
#undef ALIAS
};
const StaticClip kClips[] = {
#define EVENT(...) 
#define MUSIC(...) 
#define CLIP(...) {__VA_ARGS__},
#define ALIAS(...) 
#include "games/choc3/audio/generated/AudioEvents.inc"
#undef EVENT
#undef MUSIC
#undef CLIP
#undef ALIAS
};
const StaticAlias kAliases[] = {
#define EVENT(...) 
#define MUSIC(...) 
#define CLIP(...) 
#define ALIAS(...) {__VA_ARGS__},
#include "games/choc3/audio/generated/AudioEvents.inc"
#undef EVENT
#undef MUSIC
#undef CLIP
#undef ALIAS
};

}  // namespace

const audio::EventTable& audioEvents() {
  static const audio::EventTable table{audio::EventTableData{kEvents, kMusic, kClips, kAliases}};
  return table;
}

audio::EventTable macAudioEvents(std::string_view specText, std::vector<std::string>* messages) {
  using namespace choco::audio;
  CadiLiteCompiled c = compileCadiLite(specText);
  if (messages) {
    *messages = c.messages;
    messages->push_back("[audio] cadilite.spec: " + std::to_string(c.events.size()) + " events, " + std::to_string(c.errors) +
                        " errors, " + std::to_string(c.duplicates) + " duplicates dropped, " + std::to_string(c.unknown) +
                        " without an engine equivalent");
  }
  std::set<std::string> have;
  for (const EventDef& d : c.events) have.insert(d.name);
  std::vector<EventDef> rows = std::move(c.events);
  // The rows BSGSoundEvent and the hard-coded events supply: direct files, group volumes, Pause and Unpause, and the
  // names that match nothing. Pause/Unpause stay built-ins, so the factory pause silences audio.
  for (const EventDef& d : audioEvents().events()) {
    const bool native = d.kind == Kind::Direct || d.kind == Kind::Volume || d.kind == Kind::Builtin ||
                        (d.kind == Kind::Silent && d.group == Group::Direct);
    if (native && !have.count(d.name)) rows.push_back(d);
  }
  if (!have.count("Stop_Music")) {  // the Mac hard-codes it: it kills the whole Music group
    EventDef stop;
    stop.name = "Stop_Music";
    stop.kind = Kind::Stop;
    stop.group = Group::Music;
    stop.stopGroup = StopGroup::Music;
    rows.push_back(std::move(stop));
  }
  return EventTable(std::move(rows));
}

std::unique_ptr<const audio::EventTable> makeAudioEvents(const assets::AssetFS& fs) {
  if (auto text = fs.readText("cadi/cadilite.spec")) {
    std::vector<std::string> messages;
    auto table = std::make_unique<const audio::EventTable>(macAudioEvents(*text, &messages));
    for (const std::string& m : messages) diag::logLine(m);
    return table;
  }
  return std::make_unique<const audio::EventTable>(audioEvents());
}

}  // namespace choco::games::choc3
