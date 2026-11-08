#include "games/choc2/TravelSounds.h"

namespace choco::game::choc2 {

TravelAudio travelAudio(const std::string& type, const std::string& previous) {
  TravelAudio out;
  if (type == "train") out.ambient = "sfx/saloon_loop.ogg";
  else if (type == "ship" || type == "raft") out.ambient = "sfx/seaside_loop.ogg";
  else if (type == "airplane") out.ambient = "";
  else if (type == "hiker" || type == "camel") out.ambient = "sfx/farm_loop.ogg";
  else if (type == "canoe") out.ambient = "sfx/remote_loop.ogg";
  const bool silentType = type == "raft" || type == "hiker" || type == "camel" || type == "canoe";
  if (!silentType && type != previous) out.oneShot = type;
  out.remember = type;  // TravelOneWeek always stores the last type; only the one-shot is suppressed for the silent types
  return out;
}

}  // namespace choco::game::choc2
