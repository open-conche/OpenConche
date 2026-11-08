#pragma once

#include <cstddef>
#include <vector>

#include "gfx/Types.h"

namespace choco::gfx {

// Intersecting clip stack shared by renderer implementations. With no clip pushed there is no restriction.
class ClipStack {
 public:
  // Pushes the intersection of `r` with the current clip and returns it.
  RectI push(RectI r) {
    RectI eff = stack_.empty() ? r : RectI::intersect(stack_.back(), r);
    if (eff.empty()) eff = {eff.x, eff.y, 0, 0};
    stack_.push_back(eff);
    return eff;
  }
  void pop() {
    if (!stack_.empty()) stack_.pop_back();
  }
  bool active() const { return !stack_.empty(); }
  // Valid only when active().
  RectI current() const { return stack_.back(); }
  // True when a clip is active and nothing can be drawn.
  bool blocksAll() const { return !stack_.empty() && stack_.back().empty(); }
  std::size_t depth() const { return stack_.size(); }

 private:
  std::vector<RectI> stack_;
};

}  // namespace choco::gfx
