#pragma once

// What the display case tells the vendor (docs/re/greatcc-machines-and-chocolates.md 2.4): for each layer index the set of options
// the displayed chocolates carry, so a random or even a pinned order option can be replaced by one on display. Index 0 is the mix,
// 1 the filling, 2 the shape (the bar type), 3 the infusion and 4 the topping. These are sets, not counts: taking one chocolate
// out erases its options even when another displayed chocolate has the same one (the original's quirk, kept). Pure data; the
// vendor feeds the rolls.

#include <functional>
#include <map>
#include <set>

#include "games/greatcc/core/ChocolateSpec.h"

namespace choco::games::greatcc {

class DisplayInfluence {
 public:
  // AddDisplayChocolate: a bucket adds nothing; any other chocolate adds its mix to index 0, its bar type to index 2 and the
  // option of each filled slot 1, 3 and 4 to that slot's index.
  void add(const ChocolateSpec& c) { apply(c, true); }
  // RemoveDisplayChocolate: the same options are erased.
  void remove(const ChocolateSpec& c) { apply(c, false); }
  void clear() { sets_.clear(); }

  // The options on display for a layer index (empty when none).
  const std::set<int>& options(int layerIndex) const;

  // The index into the layer's value names: display_influence_mix, _ingredient, _shape, _infusion, _topping.
  static const char* valueName(int layerIndex);

  // GetDisplayInfluence(layerIndex, fallback): with nothing on display for the index, `fallback` and no draw. Otherwise one draw
  // `roll % 101` is compared (<=) with the percentage `pct`; a hit picks a member of the set (no draw for a one-member set,
  // otherwise one more draw `% size`, taken in ascending order), a miss gives `fallback`. An index outside 0..4 gives `fallback`.
  int pick(int layerIndex, int fallback, double pct, const std::function<unsigned()>& draw) const;

 private:
  void apply(const ChocolateSpec& c, bool insert);
  std::map<int, std::set<int>> sets_;
};

}  // namespace choco::games::greatcc
