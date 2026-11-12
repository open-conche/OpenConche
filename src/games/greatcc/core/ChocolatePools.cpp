#include "games/greatcc/core/ChocolatePools.h"

namespace choco::games::greatcc {

void ChocolatePools::updatePool(int mode, bool active, int barType, int layer, int option) {
  const bool a = mode == 0 || mode == 2;
  const bool b = mode == 1 || mode == 2;
  for (int bar = 0; bar < 4; ++bar) {
    if (barType != kAllBars && bar != barType) continue;
    if (layer == kBarSet) {
      if (a) {
        if (active) barsA_.insert(bar);
        else barsA_.erase(bar);
      }
      if (b) {
        if (active) barsB_.insert(bar);
        else barsB_.erase(bar);
      }
      continue;
    }
    for (int l = 1; l < 5; ++l) {
      if (layer != l) continue;
      if (a) {
        if (active) optionsA_[bar][l].insert(option);
        else optionsA_[bar][l].erase(option);
      }
      if (b) {
        if (active) optionsB_[bar][l].insert(option);
        else optionsB_[bar][l].erase(option);
      }
    }
  }
}

void ChocolatePools::updateCapability(bool both, const std::function<bool(const char*)>& active) {
  const int mode = both ? 2 : 1;
  // mixes (the shape layer, slot 2), fillings (1) and toppings (4) for every shape; infusions (3) for the infusion shape only
  static const char* const kMixer[3] = {"kMixer1", "kMixer2", "kMixer3"};
  static const char* const kIngredient[3] = {"kIngredient1", "kIngredient2", "kIngredient3"};
  static const char* const kTopping[3] = {"kTopping1", "kTopping2", "kTopping3"};
  static const char* const kInfusion[3] = {"kInfusion1", "kInfusion2", "kInfusion3"};
  static const char* const kPress[3] = {"kPress1", "kPress2", "kPress3"};
  for (int i = 0; i < 3; ++i) updatePool(mode, active(kMixer[i]), kAllBars, kLayerChocolate, i);
  for (int i = 0; i < 3; ++i) updatePool(mode, active(kIngredient[i]), kAllBars, kLayerIngredient, i);
  for (int i = 0; i < 3; ++i) updatePool(mode, active(kTopping[i]), kAllBars, kLayerTopping, i);
  for (int i = 0; i < 3; ++i) updatePool(mode, active(kInfusion[i]), kShapeInfusion, kLayerInfusion, i);
  for (int i = 0; i < 3; ++i) updatePool(mode, active(kPress[i]), i, kBarSet, 0);
  // the first infusion also makes the infusion shape available
  updatePool(mode, active(kInfusion[0]), kShapeInfusion, kBarSet, 0);
}

bool ChocolatePools::canCreate(const ChocolateSpec& c) const {
  if (!barsB_.count(c.barType())) return false;
  for (int i = 1; i < ChocolateSpec::kSlots; ++i) {
    const auto& l = c.slot(i);
    if (!l) continue;
    if (!options(true, c.barType(), i).count(l->option)) return false;
  }
  return true;
}

const std::set<int>& ChocolatePools::options(bool poolB, int barType, int layer) const {
  static const std::set<int> none;
  const OptionMap& m = poolB ? optionsB_ : optionsA_;
  const auto bar = m.find(barType);
  if (bar == m.end()) return none;
  const auto l = bar->second.find(layer);
  return l == bar->second.end() ? none : l->second;
}

void ChocolatePools::clearA() {
  barsA_.clear();
  optionsA_.clear();
}

}  // namespace choco::games::greatcc
