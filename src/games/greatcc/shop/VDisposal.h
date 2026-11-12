#pragma once

// VDisposal (docs/re/greatcc-machines-and-chocolates.md 5.8): the trash station. Arriving there throws away whatever the avatar
// carries, a sauce or a drink as well as a chocolate, at no cost. It is a station, not a machine: it has no buttons, no state and
// no output. The conveyor calls Activate when an item falls off its end.

#include "games/greatcc/shop/VStation.h"

namespace choco::games::greatcc {

class VDisposal : public VStation {
 public:
  VDisposal(script::ScriptContext& ctx, int id = 0) : VStation(ctx, id, kStationDisposal) {}
  const char* kind() const override { return "VDisposal"; }

  // Enabled by `kDisposal`: then the `base` asset shows and the children are initialised (VStation::Init is not run: the tabletop
  // and the machines do not exist here); the station is shown only when enabled.
  void init() override;
  // Each hand, 2 then 1: what is in it is thrown away. A tutorial in effect keeps `num_disposed` still; `disposed_this_level`
  // always counts; the item's type (and, for a chocolate, its description) is appended to the game state `TrashUsedItemID`.
  // The animation and the sound run even when nothing was thrown, `++TrashUsed` is raised, and then the station's own arrival
  // (the avatar turns to the point's facing).
  void onArrival(VPlayer& player, const ZonePoint* point) override;
  // The `active` child plays once and `disposal.ogg` sounds.
  void activate();
};

}  // namespace choco::games::greatcc
