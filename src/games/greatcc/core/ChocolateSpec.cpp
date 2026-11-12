#include "games/greatcc/core/ChocolateSpec.h"

#include <cstdio>

namespace choco::games::greatcc {

int spriteLayerForLayerType(int layerType) {
  switch (layerType) {
    case kLayerChocolate: return 1;
    case kLayerIngredient: return 2;
    case kLayerInfusion: return 3;
    case kLayerTopping: return 4;
    default: return 0;
  }
}

int ChocolateSpec::numLayers() const {
  int n = 0;
  for (int i = 1; i < kSlots; ++i)
    if (slots_[static_cast<std::size_t>(i)]) ++n;
  return n;
}

bool ChocolateSpec::canAddLayer(int layer) const {
  if (layer < 3) return top_ < layer;
  if (layer == 3) return top_ < 1;
  return top_ >= 2 && layer > top_;
}

void ChocolateSpec::store(const LayerSpec& l) {
  if (l.layerType < 0 || l.layerType >= kSlots) return;
  slots_[static_cast<std::size_t>(l.layerType)] = l;
  if (l.layerType > top_) top_ = l.layerType;
}

void ChocolateSpec::clear(int slotIndex) {
  if (slotIndex < 0 || slotIndex >= kSlots) return;
  if (!slots_[static_cast<std::size_t>(slotIndex)]) return;
  slots_[static_cast<std::size_t>(slotIndex)].reset();
  while (top_ >= 0 && !slots_[static_cast<std::size_t>(top_)]) --top_;
}

bool ChocolateSpec::equals(const ChocolateSpec& other) const {
  for (int i = 0; i < kSlots; ++i) {
    const auto& theirs = other.slots_[static_cast<std::size_t>(i)];
    const auto& mine = slots_[static_cast<std::size_t>(i)];
    if (!theirs) {
      if (mine) return false;
    } else if (!mine || !(*theirs == *mine)) {
      return false;
    }
  }
  return true;
}

bool ChocolateSpec::compareWithLayer(const ChocolateSpec& held, int layer, int option) const {
  if (option < 0 || layer <= 0 || layer >= kSlots) return false;
  for (int i = 0; i < layer; ++i) {
    const auto& theirs = held.slots_[static_cast<std::size_t>(i)];
    const auto& mine = slots_[static_cast<std::size_t>(i)];
    if (!theirs) {
      if (mine) return false;
    } else if (!mine || !(*theirs == *mine)) {
      return false;
    }
  }
  const auto& wanted = slots_[static_cast<std::size_t>(layer)];
  return wanted && wanted->option == option;
}

float ChocolateSpec::cost(const CostEnv& env) const {
  const float quality = static_cast<float>(env.qualityModifier);
  const float price = static_cast<float>(env.layerPrice);
  float total = 0.0f;
  for (int i = 1; i < kSlots; ++i) {
    const auto& l = slots_[static_cast<std::size_t>(i)];
    if (!l) continue;
    total += price;
    // the upgrade named after the slot: the mixer's follows the bucket's mix, the others the layer's own option
    int n = 0;
    const char* family = nullptr;
    switch (i) {
      case 1: family = "kIngredientQ"; n = l->option + 1; break;
      case 2: family = "kMixerQ"; n = mixOption_ + 1; break;
      case 3: family = "kInfusionQ"; n = l->option + 1; break;
      default: family = "kToppingQ"; n = l->option + 1; break;
    }
    char name[32];
    std::snprintf(name, sizeof name, "%s%d", family, n);
    if (env.qualityActive && env.qualityActive(name)) total += quality * price;
  }
  return total;
}

std::string ChocolateSpec::toString() const {
  std::string s = "(";
  if (mixOption_ == 1) s += "milk";
  else if (mixOption_ == 2) s += "white";
  else if (mixOption_ == 0) s += "dark";
  char buf[32];
  if (ingredientId_ > -1) {
    std::snprintf(buf, sizeof buf, ",ingredient:%d", ingredientId_);
    s += buf;
  }
  switch (barType_) {
    case kShapeTruffle: s += ",truffle"; break;
    case kShapeBar: s += ",bar"; break;
    case kShapeNovelty: s += ",novelty"; break;
    case kShapeInfusion: s += ",infusion"; break;
    default: break;
  }
  for (int i = 1; i < kSlots; ++i) {
    const auto& l = slots_[static_cast<std::size_t>(i)];
    if (!l) continue;
    if (i == 3) {
      std::snprintf(buf, sizeof buf, ",filling:%d", l->option);
      s += buf;
    } else if (i == 4) {
      std::snprintf(buf, sizeof buf, ",topping:%d", l->option);
      s += buf;
    }
  }
  return s + ")";
}

}  // namespace choco::games::greatcc
