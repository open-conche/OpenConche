#pragma once

// VThoughtBubble (docs/re/greatcc-shop-simulation.md 5.7): the bubble over a customer's head. It holds one bubble per order (a
// leaf of its own `bubble_single` asset carrying the order's picture at an anchor of that art); the one in front is the current
// one, the ones behind it are tinted and stepped back a diagonal offset each, and a bubble whose order is done fades out and goes.

#include <memory>
#include <vector>

#include "games/greatcc/scene/Effects.h"
#include "games/greatcc/scene/VObject.h"

namespace choco::games::greatcc {

class VThoughtBubble : public VObject {
 public:
  explicit VThoughtBubble(script::ScriptContext& ctx) : VObject(ctx, "", "") {}
  static std::shared_ptr<VThoughtBubble> create(script::ScriptContext& ctx) { return std::make_shared<VThoughtBubble>(ctx); }
  const char* kind() const override { return "VThoughtBubble"; }

  // AddOrder(item, anchor, enabled): a new bubble behind the others at (-offset * n, -offset * n), fading in over `transition_time`
  // plus half of it per bubble before it; a bubble that is not the first is tinted by `thought_tint` times the root of its place; one
  // that cannot be made (`enabled` false) is tinted red. `item` becomes a leaf child of the bubble, at the anchor named `anchor`.
  void addOrder(const std::shared_ptr<VObject>& item, const std::string& anchor, bool enabled);
  // SetMood(mood): every bubble shows the art of the mood (content for 2, neutral 3, annoyed 4, angry 5, happy otherwise).
  void setMood(int mood);
  // NextBubble: the one in front is done; true while another is left.
  bool nextBubble();
  // Clear: the children and their effects go, the count and the index start over.
  void clear();
  // The bubbles step toward their places, the ones that are done fade out and are removed; then VSprite::Update.
  void update(bool softPaused) override;

  int bubbleCount() const { return static_cast<int>(orders_.size()); }
  int currentBubble() const { return cur_; }
  // The bubbles still on show (not yet faded out).
  int liveBubbles() const { return live_; }
  // The bubble leaf of order `i` (null once it is gone).
  VObject* bubbleAt(int i) const { return i >= 0 && i < bubbleCount() ? orders_[static_cast<std::size_t>(i)].leaf.get() : nullptr; }
  bool bubbleDisabled(int i) const { return i >= 0 && i < bubbleCount() && orders_[static_cast<std::size_t>(i)].disabled; }

 private:
  struct Entry {
    bool disabled = false;
    std::shared_ptr<VObject> leaf;
  };
  std::vector<Entry> orders_;
  std::vector<std::shared_ptr<SpriteEffect>> fades_, tints_;  // by order index; null when none
  int cur_ = 0;
  int live_ = 0;
};

}  // namespace choco::games::greatcc
