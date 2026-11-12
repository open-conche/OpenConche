#include "games/greatcc/shop/VDisposal.h"

#include "games/greatcc/GreatCCWorld.h"
#include "games/greatcc/shop/VChocolate.h"
#include "games/greatcc/shop/VPlayer.h"

namespace choco::games::greatcc {

void VDisposal::init() {
  const bool active = host() && host()->upgradeActive("kDisposal", "station");
  if (active) {
    switchAsset("base", "default", "default");
    VObject::init();
  }
  setVisible(active);
}

void VDisposal::activate() {
  if (VObject* anim = childObject("active")) anim->play("", false, false, 0);
  if (GreatCCWorld* w = worldOf(context())) w->sound().playSfx("audio/sfx/disposal.ogg", false);
}

void VDisposal::onArrival(VPlayer& player, const ZonePoint* point) {
  const std::shared_ptr<VSprite> keep = shared_from_this();
  GreatCCWorld* w = worldOf(context());
  if (w) {
    std::string ids = w->gameStates().get("TrashUsedItemID");
    if (ids == "-1") ids.clear();
    for (const int hand : {VPlayer::kHandA, VPlayer::kHandB}) {
      const std::shared_ptr<VCarriable> item = player.detachObject(hand);
      if (!item) continue;
      if (!(host() && host()->tutorialInEffect())) w->values().addToValue("num_disposed", 1.0, 0, true);
      w->values().addToValue("disposed_this_level", 1.0, 0, false);
      if (!ids.empty()) ids += ",";
      ids += item->type();
      if (const auto* choc = dynamic_cast<const VChocolate*>(item.get())) ids += choc->toString();
    }
    activate();
    w->gameStates().set("++TrashUsed");
    w->gameStates().set("TrashUsedItemID", ids);
  }
  VStation::onArrival(player, point);
}

}  // namespace choco::games::greatcc
