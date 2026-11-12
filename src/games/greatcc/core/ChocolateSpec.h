#pragma once

// The data side of a chocolate (docs/re/greatcc-machines-and-chocolates.md 2.1, 2.2): up to five layers in fixed slots (bucket,
// ingredient, shape, infusion, topping), each a (layerType, barType, option) triple, plus the numbers that go with the whole: the
// highest occupied slot, the shape, the filling chosen in the bucket, the mix and the remaining life. No rendering and no Lua; the
// sprites (VChocolate, VChocolateLayer) keep one of these and delegate their rules to it.

#include <array>
#include <functional>
#include <optional>
#include <string>

#include "games/greatcc/core/LevelData.h"

namespace choco::games::greatcc {

// One layer: which slot it belongs to, which shape family it was made for and which of the three choices it is.
struct LayerSpec {
  int layerType = kLayerBucket;
  int barType = 0;
  int option = 0;
  // VChocolateLayer::Compare: all three equal.
  bool operator==(const LayerSpec& o) const { return layerType == o.layerType && barType == o.barType && option == o.option; }
};

// The sprite layer a chocolate layer draws on (VChocolateLayer's constructor and loader): shape 1, ingredient 2, infusion 3,
// topping 4, anything else (the bucket) 0.
int spriteLayerForLayerType(int layerType);

// What GetCost reads: the two values and whether a quality upgrade is owned (category `station`, the name as `kMixerQ1`).
struct CostEnv {
  double layerPrice = 0;        // value `layer_price`
  double qualityModifier = 1.0;  // value `layer_quality_modifier`
  std::function<bool(const std::string& upgradeName)> qualityActive;
};

class ChocolateSpec {
 public:
  static constexpr int kSlots = 5;

  const std::optional<LayerSpec>& slot(int i) const { return slots_.at(static_cast<std::size_t>(i)); }
  // The highest occupied slot (-1 for none).
  int top() const { return top_; }
  // -1 until a shape or infusion layer is added.
  int barType() const { return barType_; }
  // The bucket's filling (-1 for none) and the mix the bucket was made with (-1 for none).
  int ingredientId() const { return ingredientId_; }
  int mixOption() const { return mixOption_; }
  void setMixOption(int m) { mixOption_ = m; }
  void setBarType(int b) { barType_ = b; }
  float decay() const { return decay_; }
  void setDecay(float d) { decay_ = d; }

  // The number of occupied slots among 1..4 (the bucket is not counted).
  int numLayers() const;
  // `top < 2`: an empty chocolate, a bare bucket and a bucket with a filling are all buckets.
  bool isBucket() const { return top_ < 2; }
  // CanAddLayer: layers below 3 need `top < layer`; the infusion needs `top < 1`; the topping needs `top >= 2` and `top < layer`.
  bool canAddLayer(int layer) const;

  // Raw slot access for the sprite code (it also moves the sprites): the layer is stored in its own slot, `top` becomes the
  // larger of the two. A null layer type outside 0..4 is ignored.
  void store(const LayerSpec& l);
  // RemoveLayer's bookkeeping: the slot is emptied and `top` falls to the last occupied slot.
  void clear(int slotIndex);
  // AddIngredient: the filling is remembered and the highest slot becomes 1.
  void addIngredient(int id) {
    ingredientId_ = id;
    top_ = 1;
  }

  // VChocolate::Compare: every one of the five slots is empty in both or equal in both (nothing else is compared).
  bool equals(const ChocolateSpec& other) const;
  // VChocolate::CompareWithLayer, `this` being the order chocolate and `held` the chocolate in hand: false unless option >= 0
  // and layer is 1..4; slots below `layer` must match exactly and this chocolate's slot `layer` must exist with that option.
  bool compareWithLayer(const ChocolateSpec& held, int layer, int option) const;
  // VChocolate::GetCost, in single precision as the original.
  float cost(const CostEnv& env) const;
  // VChocolate::ToString.
  std::string toString() const;

 private:
  std::array<std::optional<LayerSpec>, kSlots> slots_{};
  int top_ = -1;
  int barType_ = -1;
  int ingredientId_ = -1;
  int mixOption_ = -1;
  float decay_ = 100.0f;
};

}  // namespace choco::games::greatcc
