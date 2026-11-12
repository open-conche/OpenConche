#include "games/greatcc/shop/VChocolate.h"

#include <cstdio>
#include <iterator>
#include <set>

#include "games/greatcc/GreatCCWorld.h"

namespace choco::games::greatcc {

namespace {
std::string chocolateName(int serial) {
  char buf[32];
  std::snprintf(buf, sizeof buf, "chocolate_%d", serial);
  return buf;
}
std::string optionState(int n) {
  char buf[24];
  std::snprintf(buf, sizeof buf, "option_%d", n);
  return buf;
}
}  // namespace

// ---- VChocolateLayer ----

VChocolateLayer::VChocolateLayer(script::ScriptContext& ctx, int layerType, int barType, int option) : VObject(ctx, "", "") {
  spec_.layerType = layerType;
  spec_.barType = barType;
  spec_.option = option;
  setLayer(spriteLayerForLayerType(layerType));
  setLeaf(true);
}

void VChocolateLayer::loadFromTable(const StyleChain& style) {
  spec_.layerType = style.intOr("layerType", 0);
  spec_.barType = style.intOr("barType", 0);
  spec_.option = style.intOr("option", 0);
  setLayer(spriteLayerForLayerType(spec_.layerType));
  VObject::loadFromTable(style);
}

void VChocolateLayer::copyTo(VSprite& dst) {
  if (auto* d = dynamic_cast<VChocolateLayer*>(&dst)) d->spec_ = spec_;
  VObject::copyTo(dst);
}

// ---- VChocolate ----

VChocolate::VChocolate(script::ScriptContext& ctx, std::shared_ptr<VChocolateVendor> vendor, int serial)
    : VCarriable(ctx, 1, chocolateName(serial), "chocolate"), vendor_(std::move(vendor)) {
  setLeaf(true);
  setLayer(2);
}

void VChocolate::addLayer(const std::shared_ptr<VChocolateLayer>& layer) {
  if (!layer) return;
  const int slot = layer->layerType();
  if (slot < 0 || slot >= ChocolateSpec::kSlots) return;
  if (layers_[static_cast<std::size_t>(slot)]) removeChild(*layers_[static_cast<std::size_t>(slot)]);
  if (GreatCCWorld* w = worldOf(context()))
    if (const gfx::ColorF* tint = w->chocolateTint(slot)) layer->setTint(*tint);  // the console's `choctint`
  addChild(layer);
  layers_[static_cast<std::size_t>(slot)] = layer;
  spec_.store(layer->spec());
  if (slot == kLayerChocolate || slot == kLayerInfusion) {
    // shaping turns the bucket into real layers: the bucket goes, the filling it held becomes a layer, an infusion bar gets its
    // shape layer from the mix
    if (layers_[0]) removeChild(*layers_[0]);
    layers_[0].reset();
    spec_.clear(0);
    if (spec_.ingredientId() >= 0) addLayer(layer->barType(), kLayerIngredient, spec_.ingredientId());
    if (slot == kLayerInfusion && !layers_[kLayerChocolate]) addLayer(layer->barType(), kLayerChocolate, spec_.mixOption());
    spec_.setBarType(layer->barType());
  }
}

std::shared_ptr<VChocolateLayer> VChocolate::addLayer(int barType, int layerType, int option) {
  if (!vendor_) return nullptr;
  std::shared_ptr<VChocolateLayer> layer = vendor_->getLayer(barType, layerType, option);
  if (!layer) return nullptr;
  addLayer(layer);
  return layer;
}

void VChocolate::addIngredient(int id) {
  spec_.addIngredient(id);
  if (!layers_[0]) return;
  if (VObject* icon = layers_[0]->childObject("icon")) icon->addAssetLeaf("overlay", optionState(id), 0);
}

void VChocolate::removeLayer(int slot) {
  if (slot < 0 || slot >= ChocolateSpec::kSlots || !layers_[static_cast<std::size_t>(slot)]) return;
  removeChild(*layers_[static_cast<std::size_t>(slot)]);
  layers_[static_cast<std::size_t>(slot)].reset();
  spec_.clear(slot);
}

float VChocolate::cost() const {
  CostEnv env;
  if (GreatCCWorld* w = worldOf(context())) {
    env.qualityModifier = w->values().getReal("layer_quality_modifier", 1.0);
    env.layerPrice = w->values().getReal("layer_price", 0);
  }
  env.qualityActive = [this](const std::string& name) { return vendor_ && vendor_->upgradeActive(name, "station"); };
  return spec_.cost(env);
}

bool VChocolate::hasDecayed() const {
  if (0.0f < spec_.decay()) return false;
  return !(fade_ && fade_->inProgress());
}

void VChocolate::update(bool softPaused) {
  GreatCCWorld* w = worldOf(context());
  const double rate = w ? w->values().getReal("chocolate_decay_rate", 0) : 0;
  if (!softPaused && !isBucket() && spec_.decay() > 0.0f && rate > 0.0f && decayEnabled_) {
    const unsigned elapsed = static_cast<unsigned>(getElapsed());
    spec_.setDecay(spec_.decay() - static_cast<float>(elapsed) * static_cast<float>(rate));
    if (spec_.decay() <= 0.0f) {
      // the life is spent: the sprite fades out (once, for the life is no longer above 0) and the level counts it
      const unsigned fadeMs = w ? w->values().getUint("chocolate_fade_time", 1000) : 1000;
      fade_ = std::make_shared<AlphaEffect>(false, 1.0f, fadeMs, -1.0f, 0);
      addEffect(fade_);
      if (w) w->values().addToValue("decayed_this_level", 1.0, 0, false);
    }
  }
  VSprite::update(softPaused);
}

void VChocolate::cancelEffects() {
  if (fade_) removeEffect(fade_);
  VSprite::cancelEffects();
  if (fade_) addEffect(fade_);
}

// ---- VChocolateVendor ----

void VChocolateVendor::clear() {
  protos_.clear();
  pools_.clearA();
  display_.clear();  // (the original also clears a string-to-int map nothing else touches)
}

int VChocolateVendor::getDisplayInfluence(int layerIndex, int fallback) {
  const char* name = DisplayInfluence::valueName(layerIndex);
  if (!name || display_.options(layerIndex).empty()) return fallback;
  GreatCCWorld* w = worldOf(ctx_);
  const double pct = w ? w->values().getReal(name, 0) : 0;
  return display_.pick(layerIndex, fallback, pct, [this] { return ctx_.rng.next32(); });
}

void VChocolateVendor::loadFromTable(const StyleChain& table) {
  clear();
  table.own().forEachEntry([&](const std::string&, const ui::Style* t, const std::optional<std::string>&) {
    if (!t) return;  // a plain value (`baseDir`) is not a layer
    const StyleChain ec(*t, &table);
    const int barType = ec.intOr("barType", 0);
    const int layerType = ec.intOr("layerType", 0);
    const int option = ec.intOr("option", 0);
    auto proto = VChocolateLayer::create(ctx_, 0, -1, -1);
    proto->loadFromTable(ec);
    protos_[barType][layerType][option] = std::move(proto);
  });
}

std::shared_ptr<VChocolateLayer> VChocolateVendor::getLayer(int barType, int layerType, int option) const {
  const auto bar = protos_.find(barType);
  if (bar == protos_.end()) return nullptr;
  const auto layer = bar->second.find(layerType);
  if (layer == bar->second.end()) return nullptr;
  const auto proto = layer->second.find(option);
  if (proto == layer->second.end() || !proto->second) return nullptr;
  auto copy = VChocolateLayer::create(ctx_, 0, -1, -1);
  proto->second->copyTo(*copy);
  copy->init();
  return copy;
}

std::shared_ptr<VChocolate> VChocolateVendor::getEmptyChocolate(int mix, bool decayEnabled) {
  auto choc = VChocolate::create(ctx_, shared_from_this(), ++chocolateCount_);
  choc->setDecayEnabled(decayEnabled);
  if (std::shared_ptr<VChocolateLayer> bucket = getLayer(-1, kLayerBucket, 0)) {
    choc->addLayer(bucket);
    choc->setMixOption(mix);
    if (VObject* icon = bucket->childObject("icon")) {
      icon->setVisible(true);
      icon->switchAsset("choc", optionState(mix), "default");
    }
  }
  return choc;
}

std::shared_ptr<VChocolateLayer> VChocolateVendor::getRandomLayer(int barType, int layerType, bool poolB) {
  int option = getDisplayInfluence(layerType == 2 ? 0 : layerType, -1);
  if (option < 0) {
    const std::set<int>& options = pools_.options(poolB, barType, layerType);
    if (options.empty()) {
      option = -1;
    } else {
      const std::size_t pick = ctx_.rng.next32() % options.size();
      option = *std::next(options.begin(), static_cast<std::ptrdiff_t>(pick));
    }
  }
  return getLayer(barType, layerType, option);
}

std::shared_ptr<VChocolate> VChocolateVendor::getChocolate(const OrderInfo& info, bool poolB) {
  if (info.type != kOrderChocolate) return nullptr;
  auto choc = VChocolate::create(ctx_, shared_from_this(), ++chocolateCount_);
  // the layer budget: the order's, else 1 when one layer is all the vendor allows, else a draw up to the most
  int budget = info.layerBudget;
  if (budget < 1) {
    const int most = pools_.maxLayers();
    budget = most < 2 ? 1 : static_cast<int>(ctx_.rng.next32() % static_cast<unsigned>(most)) + 1;
  }
  // the shape: an infusion order keeps its own; any other can be replaced by one on display; an unspecified one is a draw from the
  // shapes the machines can make (the B pool's set whichever pool the layers come from)
  int barType = info.shape;
  if (barType != kShapeInfusion) barType = getDisplayInfluence(2, barType);
  if (barType == kShapeUnspecified) {
    const std::set<int>& bars = pools_.barTypes(true);
    if (bars.empty()) {
      barType = -1;
    } else {
      const std::size_t pick = ctx_.rng.next32() % bars.size();
      barType = *std::next(bars.begin(), static_cast<std::ptrdiff_t>(pick));
    }
  }
  // the shape layer: a random mix, or the order's (which a displayed mix can replace)
  if (info.mix < 0) choc->addLayer(getRandomLayer(barType, kLayerChocolate, poolB));
  else choc->addLayer(barType, kLayerChocolate, getDisplayInfluence(0, info.mix));
  budget -= 1;
  // the filling, the infusion and the topping, in that order
  for (const int slot : {kLayerIngredient, kLayerInfusion, kLayerTopping}) {
    const int wanted = info.layer[slot];
    if (wanted < -1) {
      // not asked for: an infusion bar always has its infusion
      if (slot == kLayerInfusion && barType == kShapeInfusion) {
        choc->addLayer(getRandomLayer(barType, kLayerInfusion, poolB));
        budget -= 1;
      }
      continue;
    }
    if (slot == kLayerInfusion && barType != kShapeInfusion) continue;
    if (slot == kLayerIngredient && barType == kShapeInfusion) continue;
    std::shared_ptr<VChocolateLayer> layer;
    if (wanted < 0) {
      layer = getRandomLayer(barType, slot, poolB);
      choc->addLayer(layer);
    } else {
      layer = choc->addLayer(barType, slot, getDisplayInfluence(slot, wanted));
    }
    budget += layer ? -1 : 0;
  }
  if (budget < 1) return choc;
  // what is left of the budget goes to a random filling and a random topping, on top of whatever those slots hold now
  if (info.layer[kLayerIngredient] < 0 && barType != kShapeInfusion) {
    bool skipFilling = false;
    if (budget < 2 && info.layer[kLayerTopping] < 0) skipFilling = (ctx_.rng.next32() & 1u) != 0;
    if (!skipFilling) {
      choc->addLayer(getRandomLayer(barType, kLayerIngredient, poolB));
      if (budget - 1 < 1) return choc;
    }
  }
  if (info.layer[kLayerTopping] >= 0) return choc;
  choc->addLayer(getRandomLayer(barType, kLayerTopping, poolB));
  return choc;
}

std::size_t VChocolateVendor::prototypeCount() const {
  std::size_t n = 0;
  for (const auto& [bar, layers] : protos_)
    for (const auto& [layer, options] : layers) n += options.size();
  return n;
}

}  // namespace choco::games::greatcc
