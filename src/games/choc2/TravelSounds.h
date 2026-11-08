#pragma once

#include <optional>
#include <string>

namespace choco::game::choc2 {

// What TravelOneWeek(type) does to the sound (TMapTravel::TravelOneWeek, C2 0x24410; docs/re/choc2-map-binary.md 1.4, FACT).
struct TravelAudio {
  // The ambient bed to set ("" = silence); nullopt leaves the ambience unchanged (any type not listed, e.g. boat).
  std::optional<std::string> ambient;
  // The one-shot to play (PlaySound(name)); empty = none.
  std::string oneShot;
  // The "previous type" to remember for the next call: always the type given (only the one-shot is suppressed below).
  std::string remember;
};

// `previous` is the type remembered from the last call (empty at the start). The one-shot is the type's own name, played
// only when the type differs from `previous`, and never for raft, hiker, camel or canoe.
TravelAudio travelAudio(const std::string& type, const std::string& previous);

}  // namespace choco::game::choc2
