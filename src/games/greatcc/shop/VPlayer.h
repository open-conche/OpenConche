#pragma once

// VPlayer (docs/re/greatcc-sprites-and-zones.md 5.2, 5.3): the avatar. A VCharacter whose walk speed is a managed value, whose
// scale follows its height on the screen, whose body parts (arms, torso) switch to the facing's art, and which holds up to two
// things in its hands. It tells its shop where it has arrived and whether the middle table is there.

#include <functional>
#include <memory>

#include "games/greatcc/shop/ShopHost.h"
#include "games/greatcc/shop/VCarriable.h"
#include "games/greatcc/shop/VCharacter.h"

namespace choco::games::greatcc {

class VPlayer : public VCharacter {
 public:
  // eHand for DetachObject: 0 the preferred hand then the other, 1 slot B, 2 and 3 slot A.
  enum Hand { kHandPreferred = 0, kHandB = 1, kHandA = 2, kHandA2 = 3 };

  VPlayer(script::ScriptContext& ctx, std::shared_ptr<ZoneGraph> zones);
  static std::shared_ptr<VPlayer> create(script::ScriptContext& ctx, std::shared_ptr<ZoneGraph> zones) {
    return std::make_shared<VPlayer>(ctx, std::move(zones));
  }
  const char* kind() const override { return "VPlayer"; }
  void setHost(ShopHost* host) { host_ = host; }
  ShopHost* host() const { return host_; }

  // Drops anything carried, reads `baseY`, `rangeY`, `scaleRange` (this table only), takes the walk speed from the value
  // `walkspeed`, the VCharacter keys, and sets every frame delay to 30 ms.
  void loadFromTable(const StyleChain& style) override;
  // The turn-around countdown, then the held-item check (a seam, see `setDropCheck`), then VCharacter.
  void update(bool softPaused) override;

  bool tutorialInEffect() const override { return host_ && host_->tutorialInEffect(); }
  // Straight to the point without the centre table, round it along the graph's route with it.
  void travelTo(ZonePoint* p) override;
  // The station of the destination point is told (VStation::OnArrival), then the next queued point.
  void atDestination() override;
  // The walk speed is read again, the step is taken, the perspective scale follows the height and the carried things follow.
  void updatePosition() override;
  void setFacing(int facing) override;
  void playRandomIdleAnim() override;
  void stopCurrentIdle() override;

  // ---- hands ----
  int numHandsFree() const { return (slotA_ ? 0 : 1) + (slotB_ ? 0 : 1); }
  // AttachObject: false when the item needs more hands than are free (or is null). A 2-hand item fills both slots and takes the
  // anchor `bucket`; a 1-hand item the first free slot. The facing is applied again so the arms show the carry art.
  bool attachObject(const std::shared_ptr<VCarriable>& item);
  // DetachObject (see Hand): the item leaves its arm, its scale goes back to 1, the slot is cleared and the facing applied
  // again. Null when nothing was there.
  std::shared_ptr<VCarriable> detachObject(int hand);
  // GetObject(hand, outHand): the item of that hand (0 = the preferred one, then the other; 2 and 3 slot A; 1 slot B), with the hand
  // id it was found in written to `outHand` when there is one. A hand without an item turns the preference (so a second ask
  // tries the other hand) and gives null.
  std::shared_ptr<VCarriable> getObject(int hand, int* outHand = nullptr);
  // The same for a chocolate: the item cast (null for a sauce or a drink, with `outHand` still set).
  std::shared_ptr<class VChocolate> getChocolate(int hand, int* outHand = nullptr);
  const std::shared_ptr<VCarriable>& handA() const { return slotA_; }
  const std::shared_ptr<VCarriable>& handB() const { return slotB_; }
  int preferredHand() const { return preferred_; }
  // PositionCarriables: each held item goes on the arm the facing puts it on, at the anchor of that hand, scaled against the
  // avatar's own perspective scale.
  void positionCarriables();
  // The chocolate slice's test for a held item that has decayed (VChocolate's check): true drops it. Empty until chocolates
  // exist.
  using DropCheck = std::function<bool(const VCarriable&)>;
  void setDropCheck(DropCheck f) { dropCheck_ = std::move(f); }

  float baseY() const { return baseY_; }
  float rangeY() const { return rangeY_; }
  float scaleRange() const { return scaleRange_; }
  float perspectiveScale() const { return scale_; }
  int turnAroundMs() const { return turnAround_; }

 private:
  ShopHost* host_ = nullptr;
  float baseY_ = 0, rangeY_ = 1, scaleRange_ = 0, scale_ = 1;
  int turnAround_ = 0;
  int preferred_ = 2;
  std::shared_ptr<VCarriable> slotA_, slotB_;
  DropCheck dropCheck_;
};

}  // namespace choco::games::greatcc
