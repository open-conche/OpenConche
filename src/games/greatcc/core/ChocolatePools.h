#pragma once

// The capability pools of the chocolate vendor (docs/re/greatcc-machines-and-chocolates.md 2.4): which shapes and which options
// of each layer the player's machines can currently make. Two parallel pools exist in the original (A and B); every shipped
// caller updates both together, so they are always equal, but both are kept because the vendor's reload clears only A.
// Pure data; the vendor sprite code asks the upgrades and feeds them in.

#include <functional>
#include <map>
#include <set>

#include "games/greatcc/core/ChocolateSpec.h"

namespace choco::games::greatcc {

class ChocolatePools {
 public:
  // The special arguments of UpdatePool.
  static constexpr int kAllBars = -3;
  static constexpr int kBarSet = -1;  // the layer argument that means "the bar type itself"

  // VChocolateVendor::UpdatePool(pool, active, barType, layer, option): mode 0 changes A, 1 changes B, 2 both. barType kAllBars
  // is every bar type 0..3. A `layer` of kBarSet adds or removes the bar type; 1..4 adds or removes the option of that layer.
  void updatePool(int mode, bool active, int barType, int layer, int option);

  // VChocolateVendor::UpdateCapability: the sixteen calls, each with whether its upgrade (category `station`) is active.
  // `both` is the flag (true: both pools, false: B only).
  void updateCapability(bool both, const std::function<bool(const char* upgradeName)>& active);

  // VChocolateVendor::CanCreate: pool B holds the chocolate's shape and, for every layer 1..4 present, its option.
  bool canCreate(const ChocolateSpec& c) const;

  const std::set<int>& barTypes(bool poolB) const { return poolB ? barsB_ : barsA_; }
  // The options of `layer` for a bar type (empty when none).
  const std::set<int>& options(bool poolB, int barType, int layer) const;

  // SetMaxLayers (clamped to 1..3, default 3).
  void setMaxLayers(int n) { maxLayers_ = n > 3 ? 3 : (n < 1 ? 1 : n); }
  int maxLayers() const { return maxLayers_; }

  // ClearVendor clears pool A (and not B).
  void clearA();

 private:
  using OptionMap = std::map<int, std::map<int, std::set<int>>>;  // barType -> layer -> options
  std::set<int> barsA_, barsB_;
  OptionMap optionsA_, optionsB_;
  int maxLayers_ = 3;
};

}  // namespace choco::games::greatcc
