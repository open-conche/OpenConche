#include "games/greatcc/core/DisplayInfluence.h"

namespace choco::games::greatcc {

void DisplayInfluence::apply(const ChocolateSpec& c, bool insert) {
  if (c.isBucket()) return;
  auto touch = [&](int index, int option) {
    if (insert) sets_[index].insert(option);
    else sets_[index].erase(option);
  };
  touch(0, c.mixOption());
  for (int slot = 1; slot < 5; ++slot) {
    const auto& layer = c.slot(slot);
    if (!layer) continue;
    touch(slot, slot == 2 ? c.barType() : layer->option);
  }
}

const std::set<int>& DisplayInfluence::options(int layerIndex) const {
  static const std::set<int> none;
  const auto it = sets_.find(layerIndex);
  return it == sets_.end() ? none : it->second;
}

const char* DisplayInfluence::valueName(int layerIndex) {
  switch (layerIndex) {
    case 0: return "display_influence_mix";
    case 1: return "display_influence_ingredient";
    case 2: return "display_influence_shape";
    case 3: return "display_influence_infusion";
    case 4: return "display_influence_topping";
    default: return nullptr;
  }
}

int DisplayInfluence::pick(int layerIndex, int fallback, double pct, const std::function<unsigned()>& draw) const {
  const std::set<int>& set = options(layerIndex);
  if (set.empty() || layerIndex < 0 || layerIndex > 4) return fallback;
  // (the comparison is in single precision, as the original's)
  const bool hit = static_cast<float>(draw() % 101u) <= static_cast<float>(pct);
  if (!hit) return fallback;
  unsigned index = 0;
  if (set.size() > 1) index = draw() % static_cast<unsigned>(set.size());
  auto it = set.begin();
  std::advance(it, index);
  return *it;
}

}  // namespace choco::games::greatcc
