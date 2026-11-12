// The Great Chocolate Chase, slice 11 (docs/design/greatcc.md section 6): the data side of chocolates. Pure rules, no sprites: the
// slots and what may be added (CanAddLayer), the exact match (Compare, CompareWithLayer), the price (GetCost), the printed form,
// and the vendor's capability pools.
#include <doctest/doctest.h>

#include <set>
#include <string>

#include "games/greatcc/core/ChocolatePools.h"
#include "games/greatcc/core/ChocolateSpec.h"

using namespace choco::games::greatcc;

namespace {
LayerSpec L(int layerType, int barType, int option) { return LayerSpec{layerType, barType, option}; }
}  // namespace

TEST_CASE("ChocolateSpec::canAddLayer: the bucket, the filling, the shape, the infusion and the topping against every top") {
  // top: -1 empty, 0 bare bucket, 1 bucket with a filling, 2 shaped, 3 infusion, 4 topped
  struct Row { int top; bool filling, shape, infusion, topping; };
  const Row rows[] = {
      {-1, true, true, true, false},   // an empty chocolate: the filling needs top < 1, the shape top < 2 ...
      {0, true, true, true, false},    // a bare bucket takes the filling, the shape and the infusion
      {1, false, true, false, false},  // a filling in: the shape only
      {2, false, false, false, true},  // shaped: only a topping
      {3, false, false, false, true},  // an infusion bar: a topping
      {4, false, false, false, false},
  };
  for (const Row& r : rows) {
    ChocolateSpec c;
    // reach the top by storing layers of the right slots
    if (r.top >= 0) c.store(L(r.top, 0, 0));
    INFO("top " << r.top);
    CHECK(c.top() == r.top);
    CHECK(c.canAddLayer(kLayerIngredient) == r.filling);
    CHECK(c.canAddLayer(kLayerChocolate) == r.shape);
    CHECK(c.canAddLayer(kLayerInfusion) == r.infusion);
    CHECK(c.canAddLayer(kLayerTopping) == r.topping);
  }
}

TEST_CASE("ChocolateSpec: isBucket is top < 2, numLayers skips the bucket, store raises top, clear lowers it to the last occupied slot") {
  ChocolateSpec c;
  CHECK(c.isBucket());
  CHECK(c.numLayers() == 0);
  c.store(L(kLayerBucket, -1, 0));
  CHECK(c.isBucket());
  CHECK(c.numLayers() == 0);  // the bucket is not counted
  c.addIngredient(2);
  CHECK(c.top() == 1);
  CHECK(c.ingredientId() == 2);
  CHECK(c.isBucket());  // a bucket with a filling is still a bucket
  c.store(L(kLayerChocolate, 0, 1));
  c.store(L(kLayerTopping, 0, 0));
  CHECK(c.top() == 4);
  CHECK_FALSE(c.isBucket());
  CHECK(c.numLayers() == 2);
  c.clear(kLayerTopping);
  CHECK(c.top() == 2);
  c.clear(kLayerChocolate);
  CHECK(c.top() == 0);  // the bucket is still there
  c.clear(kLayerBucket);
  CHECK(c.top() == -1);
  c.store(L(9, 0, 0));  // an unknown slot is ignored
  CHECK(c.top() == -1);
}

TEST_CASE("ChocolateSpec::equals and compareWithLayer: the exact match of all five slots, and the held-bucket quirk") {
  ChocolateSpec a, b;
  a.store(L(kLayerChocolate, 0, 1));
  a.store(L(kLayerIngredient, 0, 2));
  b.store(L(kLayerChocolate, 0, 1));
  CHECK_FALSE(a.equals(b));
  CHECK_FALSE(b.equals(a));
  b.store(L(kLayerIngredient, 0, 2));
  CHECK(a.equals(b));
  // the mix, the id of the filling and the decay are not compared
  a.setMixOption(0);
  b.setMixOption(2);
  a.setDecay(3);
  CHECK(a.equals(b));
  b.store(L(kLayerTopping, 0, 0));
  CHECK_FALSE(a.equals(b));
  // option or shape of one layer differs
  ChocolateSpec c;
  c.store(L(kLayerChocolate, 1, 1));
  c.store(L(kLayerIngredient, 0, 2));
  CHECK_FALSE(a.equals(c));

  // CompareWithLayer: `order` is the customer's chocolate, `held` the one in hand
  ChocolateSpec order, held;
  order.store(L(kLayerChocolate, 0, 0));
  order.store(L(kLayerIngredient, 0, 1));
  order.store(L(kLayerTopping, 0, 2));
  held.store(L(kLayerChocolate, 0, 0));
  CHECK(order.compareWithLayer(held, kLayerIngredient, 1));   // slot 0..0 match and the order has option 1 at slot 1
  CHECK_FALSE(order.compareWithLayer(held, kLayerIngredient, 2));  // another option
  CHECK_FALSE(order.compareWithLayer(held, kLayerIngredient, -1));  // option must be >= 0
  CHECK_FALSE(order.compareWithLayer(held, 0, 1));                  // layer must be > 0
  // the order has a null slot 0: a held chocolate that still has its bucket can never match
  ChocolateSpec bucketHeld;
  bucketHeld.store(L(kLayerBucket, -1, 0));
  CHECK_FALSE(order.compareWithLayer(bucketHeld, kLayerIngredient, 1));
  // the layer asked for must exist in the order
  CHECK_FALSE(order.compareWithLayer(held, kLayerInfusion, 0));
  // a longer match: the layers below must be equal
  ChocolateSpec heldFilled = held;
  heldFilled.store(L(kLayerIngredient, 0, 1));
  CHECK(order.compareWithLayer(heldFilled, kLayerTopping, 2));
  heldFilled.store(L(kLayerIngredient, 0, 0));
  CHECK_FALSE(order.compareWithLayer(heldFilled, kLayerTopping, 2));
}

TEST_CASE("ChocolateSpec::cost: the price of every layer but the bucket, plus the quality bonus of each matching upgrade") {
  CostEnv env;
  env.layerPrice = 10;
  env.qualityModifier = 0.1;
  // shape only
  ChocolateSpec bar;
  bar.store(L(kLayerChocolate, 0, 0));
  bar.setMixOption(0);
  CHECK(bar.cost(env) == doctest::Approx(10.0f));
  // shape + filling + topping
  ChocolateSpec full = bar;
  full.store(L(kLayerIngredient, 0, 1));
  full.store(L(kLayerTopping, 0, 2));
  CHECK(full.cost(env) == doctest::Approx(30.0f));
  // an infusion bar: shape + infusion
  ChocolateSpec inf;
  inf.store(L(kLayerChocolate, 3, 0));
  inf.store(L(kLayerInfusion, 3, 1));
  CHECK(inf.cost(env) == doctest::Approx(20.0f));
  // a bucket costs nothing
  ChocolateSpec bucket;
  bucket.store(L(kLayerBucket, -1, 0));
  CHECK(bucket.cost(env) == 0.0f);
  // each owned quality upgrade adds quality * price: the mixer's by the bucket's mix, the others by the layer's own option
  std::set<std::string> owned{"kMixerQ2", "kIngredientQ2", "kToppingQ3"};
  env.qualityActive = [&](const std::string& n) { return owned.count(n) != 0; };
  full.setMixOption(1);  // kMixerQ2
  CHECK(full.cost(env) == doctest::Approx(33.0f));
  full.setMixOption(0);  // kMixerQ1 is not owned
  CHECK(full.cost(env) == doctest::Approx(32.0f));
  // a chocolate built from an order has no mix (-1): the mixer term never applies
  full.setMixOption(-1);
  CHECK(full.cost(env) == doctest::Approx(32.0f));
  // a missing price is 0
  env.layerPrice = 0;
  CHECK(full.cost(env) == 0.0f);
}

TEST_CASE("ChocolateSpec::toString: the mix, the filling id, the shape, the infusion and the topping options") {
  ChocolateSpec c;
  CHECK(c.toString() == "()");
  c.setMixOption(1);
  c.addIngredient(2);
  c.store(L(kLayerChocolate, kShapeTruffle, 0));
  c.setBarType(kShapeTruffle);
  c.store(L(kLayerTopping, kShapeTruffle, 1));
  CHECK(c.toString() == "(milk,ingredient:2,truffle,topping:1)");
  ChocolateSpec inf;
  inf.setMixOption(0);
  inf.setBarType(kShapeInfusion);
  inf.store(L(kLayerInfusion, kShapeInfusion, 2));
  CHECK(inf.toString() == "(dark,infusion,filling:2)");
  ChocolateSpec w;
  w.setMixOption(2);
  w.setBarType(kShapeNovelty);
  CHECK(w.toString() == "(white,novelty)");
  ChocolateSpec bar;
  bar.setMixOption(0);
  bar.setBarType(kShapeBar);
  CHECK(bar.toString() == "(dark,bar)");
}

TEST_CASE("spriteLayerForLayerType: shape 1, filling 2, infusion 3, topping 4, anything else 0") {
  CHECK(spriteLayerForLayerType(kLayerChocolate) == 1);
  CHECK(spriteLayerForLayerType(kLayerIngredient) == 2);
  CHECK(spriteLayerForLayerType(kLayerInfusion) == 3);
  CHECK(spriteLayerForLayerType(kLayerTopping) == 4);
  CHECK(spriteLayerForLayerType(kLayerBucket) == 0);
  CHECK(spriteLayerForLayerType(-1) == 0);
}

// ================================================================ pools

TEST_CASE("ChocolatePools: updateCapability puts mixes, fillings, toppings, shapes and infusions in the pools by upgrade") {
  ChocolatePools p;
  const std::set<std::string> on{"kMixer1", "kPress1", "kIngredient1", "kIngredient2", "kInfusion1", "kTopping3"};
  p.updateCapability(true, [&](const char* n) { return on.count(n) != 0; });
  for (const bool poolB : {false, true}) {
    CHECK(p.barTypes(poolB) == std::set<int>{0, 3});  // the bar from kPress1, the infusion bar from kInfusion1
    for (int bar = 0; bar < 4; ++bar) {
      CHECK(p.options(poolB, bar, kLayerChocolate) == std::set<int>{0});
      CHECK(p.options(poolB, bar, kLayerIngredient) == std::set<int>{0, 1});
      CHECK(p.options(poolB, bar, kLayerTopping) == std::set<int>{2});
    }
    // the infusion options are for the infusion shape only
    CHECK(p.options(poolB, kShapeInfusion, kLayerInfusion) == std::set<int>{0});
    CHECK(p.options(poolB, kShapeBar, kLayerInfusion).empty());
  }
  // the pools follow the upgrades: an upgrade that is off removes its option again
  p.updateCapability(true, [&](const char* n) { return std::string(n) == "kMixer2"; });
  CHECK(p.options(true, 0, kLayerChocolate) == std::set<int>{1});
  CHECK(p.barTypes(true).empty());
}

TEST_CASE("ChocolatePools: the flag false changes pool B only, and ClearVendor clears A only") {
  ChocolatePools p;
  p.updateCapability(false, [](const char* n) { return std::string(n) == "kPress2"; });
  CHECK(p.barTypes(true) == std::set<int>{1});
  CHECK(p.barTypes(false).empty());
  p.updateCapability(true, [](const char* n) { return std::string(n) == "kPress1"; });
  CHECK(p.barTypes(true) == std::set<int>{0});
  CHECK(p.barTypes(false) == std::set<int>{0});
  p.clearA();
  CHECK(p.barTypes(false).empty());
  CHECK(p.barTypes(true) == std::set<int>{0});
  p.updatePool(0, true, 2, ChocolatePools::kBarSet, 0);  // mode 0: A alone
  CHECK(p.barTypes(false) == std::set<int>{2});
  CHECK(p.barTypes(true) == std::set<int>{0});
}

TEST_CASE("ChocolatePools::canCreate: the shape and every layer's option must be in pool B") {
  ChocolatePools p;
  const std::set<std::string> on{"kMixer1", "kPress1", "kIngredient1", "kTopping1"};
  p.updateCapability(true, [&](const char* n) { return on.count(n) != 0; });
  ChocolateSpec c;
  c.setBarType(kShapeBar);
  c.store(L(kLayerChocolate, kShapeBar, 0));
  CHECK(p.canCreate(c));
  c.store(L(kLayerIngredient, kShapeBar, 0));
  c.store(L(kLayerTopping, kShapeBar, 0));
  CHECK(p.canCreate(c));
  c.store(L(kLayerTopping, kShapeBar, 1));  // topping 2 is not available
  CHECK_FALSE(p.canCreate(c));
  ChocolateSpec truffle;
  truffle.setBarType(kShapeTruffle);
  truffle.store(L(kLayerChocolate, kShapeTruffle, 0));
  CHECK_FALSE(p.canCreate(truffle));  // no truffle press
  ChocolateSpec none;  // barType -1
  CHECK_FALSE(p.canCreate(none));
}

TEST_CASE("ChocolatePools::setMaxLayers clamps to 1..3") {
  ChocolatePools p;
  CHECK(p.maxLayers() == 3);
  p.setMaxLayers(0);
  CHECK(p.maxLayers() == 1);
  p.setMaxLayers(2);
  CHECK(p.maxLayers() == 2);
  p.setMaxLayers(9);
  CHECK(p.maxLayers() == 3);
  p.setMaxLayers(-4);
  CHECK(p.maxLayers() == 1);
}

