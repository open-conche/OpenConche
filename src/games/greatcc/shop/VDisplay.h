#pragma once

// VDisplay (docs/re/greatcc-machines-and-chocolates.md 5.10): the centre table. It holds one thing (two with `kDisplay2`) of
// whatever the avatar carries, and gives it back to her. A chocolate on it tells the vendor which options are on display, so the
// random orders lean towards them. It is shown only while `kDisplay1` is on; while shown the avatar walks round it.
// Clicking the table sends her there; clicking an item on it sends her there to take that item. A chocolate that decays on the
// table is dropped.

#include <memory>
#include <vector>

#include "games/greatcc/shop/VCarriable.h"
#include "games/greatcc/shop/VStation.h"

namespace choco::games::greatcc {

class VDisplay : public VStation {
 public:
  explicit VDisplay(script::ScriptContext& ctx, int id = 0);
  ~VDisplay() override;
  const char* kind() const override { return "VDisplay"; }

  // The children and the slots go, the vendor forgets what was on display, then the station's own load.
  void loadFromTable(const StyleChain& style) override;
  // `kDisplay1` shows the table (its `base` asset) or hides it; the click listener for the items is made; the capacity is 2 with
  // `kDisplay2` and 1 without. VStation::Init is not run.
  void init() override;
  // With a single slot a click on the table picks what it holds (nothing when empty); then the avatar is sent.
  void onClick() override;
  // Unless softly paused every decayed chocolate on the table is taken off it; then VStation::Update.
  void update(bool softPaused) override;
  // The station's arrival first. Then, when the picked item (or, with none picked, nothing) fits her free hands, it goes to her;
  // otherwise, if there is room, what she carries (the preferred hand first) goes on the table. The pick is forgotten.
  void onArrival(VPlayer& player, const ZonePoint* point) override;

  // The listener's call when an item on the table is clicked: it becomes the pick and the station is clicked.
  void pickupCarriable(const std::shared_ptr<VCarriable>& item);
  // AttachCarriable: into the first free slot (nothing when full or null): a child at layer `capacity` under the anchor
  // `chocolate<slot+1>`, no longer a leaf and clickable, listened to, with a hover glow of 0.25; a chocolate registers with the
  // vendor; `++DisplayUsed`.
  void attachCarriable(const std::shared_ptr<VCarriable>& item);
  // DetachCarriable: a leaf again, not clickable, unlistened, its effects cancelled, unregistered from the vendor, out of its
  // slot and no longer a child.
  void detachCarriable(const std::shared_ptr<VCarriable>& item);

  int capacity() const { return capacity_; }
  // The items on the table (the slots that are not empty, in slot order).
  int count() const { return count_; }
  const std::vector<std::shared_ptr<VCarriable>>& slots() const { return slots_; }
  const std::shared_ptr<VCarriable>& picked() const { return picked_; }

 private:
  class Listener;
  std::vector<std::shared_ptr<VCarriable>> slots_;  // +0x2a0
  int count_ = 0;                                   // +0x2ac
  int capacity_ = 1;                                // +0x2b0
  std::shared_ptr<VCarriable> picked_;              // +0x2b4
  std::shared_ptr<SpriteListener> listener_;        // +0x2bc
};

}  // namespace choco::games::greatcc
