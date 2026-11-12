#pragma once

// VChocolateLayer, VChocolate and VChocolateVendor (docs/re/greatcc-machines-and-chocolates.md 2): a chocolate is an empty sprite
// holding up to five layer sprites, one per slot; machines add layers; the vendor owns the prototypes the layers are copied from
// and the pools of what the machines can make. The rules (slots, comparison, cost) are in core/ChocolateSpec; these classes
// keep the sprites in step with it.

#include <array>
#include <functional>
#include <map>
#include <memory>
#include <string>

#include "games/greatcc/core/ChocolatePools.h"
#include "games/greatcc/core/DisplayInfluence.h"
#include "games/greatcc/core/ChocolateSpec.h"
#include "games/greatcc/scene/Effects.h"
#include "games/greatcc/shop/VCarriable.h"

namespace choco::games::greatcc {

class VChocolateVendor;

// A layer of a chocolate: a VObject with the three numbers of its slot. A leaf, so a click on it goes to the whole.
class VChocolateLayer : public VObject {
 public:
  // VChocolateLayer(layer, bar, option): the sprite layer follows the layer type.
  VChocolateLayer(script::ScriptContext& ctx, int layerType = 0, int barType = -1, int option = -1);
  static std::shared_ptr<VChocolateLayer> create(script::ScriptContext& ctx, int layerType = 0, int barType = -1, int option = -1) {
    return std::make_shared<VChocolateLayer>(ctx, layerType, barType, option);
  }
  const char* kind() const override { return "VChocolateLayer"; }

  const LayerSpec& spec() const { return spec_; }
  int layerType() const { return spec_.layerType; }
  int barType() const { return spec_.barType; }
  int option() const { return spec_.option; }
  // `layerType`, `barType` and `option` (any depth, default 0), the sprite layer from the type, then the VObject keys.
  void loadFromTable(const StyleChain& style) override;
  // The three numbers, then the VObject look.
  void copyTo(VSprite& dst) override;
  bool compare(const VChocolateLayer* other) const { return other && other->spec_ == spec_; }

 private:
  LayerSpec spec_;
};

class VChocolate : public VCarriable {
 public:
  // The type `chocolate`, the name `chocolate_<n>` (the vendor's counter), one hand, sprite layer 2, a leaf with no picture.
  VChocolate(script::ScriptContext& ctx, std::shared_ptr<VChocolateVendor> vendor, int serial);
  static std::shared_ptr<VChocolate> create(script::ScriptContext& ctx, std::shared_ptr<VChocolateVendor> vendor, int serial) {
    return std::make_shared<VChocolate>(ctx, std::move(vendor), serial);
  }
  const char* kind() const override { return "VChocolate"; }

  const ChocolateSpec& spec() const { return spec_; }
  const std::shared_ptr<VChocolateLayer>& layerAt(int slot) const { return layers_.at(static_cast<std::size_t>(slot)); }
  int top() const { return spec_.top(); }
  bool isBucket() const { return spec_.isBucket(); }
  bool canAddLayer(int l) const { return spec_.canAddLayer(l); }
  int numLayers() const { return spec_.numLayers(); }
  int barType() const { return spec_.barType(); }
  int mixOption() const { return spec_.mixOption(); }
  int ingredientId() const { return spec_.ingredientId(); }
  float decay() const { return spec_.decay(); }
  bool decayEnabled() const { return decayEnabled_; }
  void setDecayEnabled(bool e) { decayEnabled_ = e; }
  void setMixOption(int m) { spec_.setMixOption(m); }

  // AddLayer(layer): null is ignored; the slot's old sprite leaves, the new one is added as a child and `top` follows. A shape or
  // infusion layer removes the bucket, makes the filling layer from the vendor when the bucket held one, makes the shape layer an
  // infusion layer lacks (from the mix), and fixes the chocolate's shape.
  void addLayer(const std::shared_ptr<VChocolateLayer>& layer);
  // AddLayer(barType, layer, option): the vendor's clone of that prototype, initialised, added; null when there is none.
  std::shared_ptr<VChocolateLayer> addLayer(int barType, int layerType, int option);
  // AddIngredient: the filling is remembered and the barrel icon shows it.
  void addIngredient(int id);
  void removeLayer(int slot);

  bool compare(const VChocolate* other) const { return other && spec_.equals(other->spec_); }
  bool compareWithLayer(const VChocolate* held, int layer, int option) const { return held && spec_.compareWithLayer(held->spec_, layer, option); }
  // GetCost with the values `layer_price` and `layer_quality_modifier` and the quality upgrades the shop reports.
  float cost() const;
  std::string toString() const { return spec_.toString(); }

  // HasDecayed: no life left and the fade has run out (or never started).
  bool hasDecayed() const;
  // The decay of `chocolate_decay_rate` per ms (value 0: none), the fade-out and the counter `decayed_this_level`, once; then the
  // sprite. Only a decay-enabled, shaped chocolate that is not paused decays.
  void update(bool softPaused) override;
  // The fade is taken off with the other effects and put back (it must survive).
  void cancelEffects() override;

 private:
  void placeLayer(const std::shared_ptr<VChocolateLayer>& layer);
  std::shared_ptr<VChocolateVendor> vendor_;
  ChocolateSpec spec_;
  std::array<std::shared_ptr<VChocolateLayer>, ChocolateSpec::kSlots> layers_;
  std::shared_ptr<SpriteEffect> fade_;
  bool decayEnabled_ = false;
};

class VChocolateVendor : public std::enable_shared_from_this<VChocolateVendor> {
 public:
  explicit VChocolateVendor(script::ScriptContext& ctx) : ctx_(ctx) {}
  static std::shared_ptr<VChocolateVendor> create(script::ScriptContext& ctx) { return std::make_shared<VChocolateVendor>(ctx); }

  // LoadFromTable: clears the prototypes (and pool A), then every table entry becomes the prototype at
  // [barType][layerType][option] (keys default 0).
  void loadFromTable(const StyleChain& table);
  void clear();
  // GetLayer: a fresh copy of the prototype, initialised; null when there is none.
  std::shared_ptr<VChocolateLayer> getLayer(int barType, int layerType, int option) const;
  // GetEmptyChocolate: a chocolate holding the bucket, its mix remembered, the barrel icon showing that mix.
  std::shared_ptr<VChocolate> getEmptyChocolate(int mix, bool decayEnabled);
  // The count that names chocolates; the shop resets it at each level load.
  void resetChocolateCount() { chocolateCount_ = 0; }
  int chocolateCount() const { return chocolateCount_; }

  ChocolatePools& pools() { return pools_; }
  const ChocolatePools& pools() const { return pools_; }
  // UpdateCapability(flag) with the stations' upgrades from `active`.
  void updateCapability(bool both, const std::function<bool(const char*)>& active) { pools_.updateCapability(both, active); }
  bool canCreate(const VChocolate& c) const { return pools_.canCreate(c.spec()); }
  // The display case's registrations (AddDisplayChocolate and its two siblings): a chocolate that is not a bucket adds or erases its
  // options in the sets GetDisplayInfluence draws from. Loading the vendor clears them.
  void addDisplayChocolate(const VChocolate& c) { display_.add(c.spec()); }
  void removeDisplayChocolate(const VChocolate& c) { display_.remove(c.spec()); }
  void clearDisplayChocolates() { display_.clear(); }
  const DisplayInfluence& displayInfluence() const { return display_; }
  // GetDisplayInfluence(layerIndex, fallback): the option an order should ask for in that slot, the fallback unless something
  // of that index is on display and the roll (`display_influence_*` percent) lets it replace the fallback.
  int getDisplayInfluence(int layerIndex, int fallback);
  void setMaxLayers(int n) { pools_.setMaxLayers(n); }
  // GetRandomLayer(barType, layerType, poolB): the option is the display case's pick for the slot's index (the mix's for the shape
  // layer) when it replaces -1, else a draw `% size` from the pool's options for that shape and layer (the A pool when `poolB` is
  // false); a copy of that prototype, initialised, or null when there is none.
  std::shared_ptr<VChocolateLayer> getRandomLayer(int barType, int layerType, bool poolB);
  // GetChocolate(order, poolB) (docs/re/greatcc-machines-and-chocolates.md 2.6): a chocolate that matches an order: null for an order
  // that is not a chocolate; else the shape, the mix and the requested layers, then random extra layers while the layer budget lasts.
  std::shared_ptr<VChocolate> getChocolate(const OrderInfo& info, bool poolB);
  std::size_t prototypeCount() const;
  script::ScriptContext& context() const { return ctx_; }
  // VUpgradeManager::IsActive for the quality upgrades GetCost asks about (the shop supplies it; none is active without one).
  using UpgradeProbe = std::function<bool(const std::string& name, const std::string& category)>;
  void setUpgradeProbe(UpgradeProbe p) { upgradeProbe_ = std::move(p); }
  bool upgradeActive(const std::string& name, const std::string& category) const { return upgradeProbe_ && upgradeProbe_(name, category); }

 private:
  script::ScriptContext& ctx_;
  // barType -> layerType -> option -> prototype
  std::map<int, std::map<int, std::map<int, std::shared_ptr<VChocolateLayer>>>> protos_;
  ChocolatePools pools_;
  DisplayInfluence display_;
  UpgradeProbe upgradeProbe_;
  int chocolateCount_ = 0;
};

}  // namespace choco::games::greatcc
