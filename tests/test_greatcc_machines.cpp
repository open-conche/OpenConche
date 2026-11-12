// The Great Chocolate Chase, slices 11 and 12 (docs/design/greatcc.md section 6): chocolate sprites, the vendor, the machine base, the
// mixer, the press, the sauce and drink machines, the display case and the disposal. Synthetic fixtures only: the sheets (scripts with the original's frame counts and delays), the tables and the zone
// file are built here; a fake shop answers the machines' questions.
#include "GreatccMachineFixture.h"

// ================================================================ chocolates and the vendor

TEST_CASE("VChocolateVendor: the table's entries become prototypes by (barType, layerType, option); a layer is a fresh initialised copy") {
  Mx m;
  CHECK(m.vendor->prototypeCount() == 1 + 4 * 3 + 3 * 3 + 4 * 3 + 3);
  auto a = m.vendor->getLayer(kShapeBar, kLayerChocolate, 1);
  REQUIRE(a);
  CHECK(a->layerType() == kLayerChocolate);
  CHECK(a->barType() == kShapeBar);
  CHECK(a->option() == 1);
  CHECK(a->layer() == 1);  // the shape draws on sprite layer 1
  CHECK(a->leaf());
  auto b = m.vendor->getLayer(kShapeBar, kLayerChocolate, 1);
  CHECK(a != b);  // a copy each time
  CHECK(m.vendor->getLayer(kShapeBar, kLayerChocolate, 7) == nullptr);
  CHECK(m.vendor->getLayer(kShapeInfusion, kLayerIngredient, 0) == nullptr);  // infusion bars have no filling
  CHECK(m.vendor->getLayer(9, kLayerChocolate, 0) == nullptr);
  // the layer type sets the sprite layer
  CHECK(m.vendor->getLayer(kShapeBar, kLayerIngredient, 0)->layer() == 2);
  CHECK(m.vendor->getLayer(kShapeInfusion, kLayerInfusion, 0)->layer() == 3);
  CHECK(m.vendor->getLayer(kShapeBar, kLayerTopping, 0)->layer() == 4);
  CHECK(m.vendor->getLayer(-1, kLayerBucket, 0)->layer() == 0);
  // reloading clears pool A and the prototypes (pool B is kept)
  m.vendor->updateCapability(true, [](const char* n) { return std::string(n) == "kPress1"; });
  CHECK(m.vendor->pools().barTypes(false) == std::set<int>{0});
  const StyleChain chain(*m.protos, nullptr);
  m.vendor->loadFromTable(chain);
  CHECK(m.vendor->pools().barTypes(false).empty());
  CHECK(m.vendor->pools().barTypes(true) == std::set<int>{0});
  CHECK(m.clean());
}

TEST_CASE("VChocolateVendor::getEmptyChocolate: a bucket of that mix, its icon showing the mix, named by the counter") {
  Mx m;
  auto c = m.vendor->getEmptyChocolate(1, true);
  REQUIRE(c);
  CHECK(c->name() == "chocolate_1");
  CHECK(c->type() == "chocolate");
  CHECK(c->hands() == 1);
  CHECK(c->leaf());
  CHECK(c->layer() == 2);
  CHECK(c->decayEnabled());
  CHECK(c->mixOption() == 1);
  CHECK(c->top() == 0);
  CHECK(c->isBucket());
  REQUIRE(c->layerAt(kLayerBucket));
  VObject* icon = c->layerAt(kLayerBucket)->childObject("icon");
  REQUIRE(icon);
  CHECK(icon->visible());
  CHECK(icon->currentAsset() == "choc");
  CHECK(icon->currentState() == "option_1");
  auto d = m.vendor->getEmptyChocolate(0, false);
  CHECK(d->name() == "chocolate_2");
  CHECK_FALSE(d->decayEnabled());
  m.vendor->resetChocolateCount();
  CHECK(m.vendor->getEmptyChocolate(2, true)->name() == "chocolate_1");
  // a vendor without a bucket prototype makes a chocolate with nothing in it
  auto empty = VChocolateVendor::create(m.ctx);
  auto bare = empty->getEmptyChocolate(0, true);
  CHECK(bare->top() == -1);
  CHECK(bare->mixOption() == -1);
}

TEST_CASE("VChocolate: a filling shows on the barrel icon, shaping turns the bucket into layers and fixes the shape") {
  Mx m;
  auto c = m.vendor->getEmptyChocolate(2, true);
  CHECK(c->canAddLayer(kLayerIngredient));
  c->addIngredient(1);
  CHECK(c->ingredientId() == 1);
  CHECK(c->top() == 1);
  CHECK(c->isBucket());
  VObject* icon = c->layerAt(kLayerBucket)->childObject("icon");
  REQUIRE(icon);
  VObject* overlay = icon->childObject("overlay");  // the leaf AddAssetLeaf made, named after the asset
  REQUIRE(overlay);
  CHECK(overlay->currentAsset() == "overlay");
  CHECK(overlay->currentState() == "option_1");
  CHECK(overlay->leaf());
  CHECK(overlay->layer() == 0);
  // shaping with a bar of the white mix: the bucket goes, the filling the bucket held becomes its own layer
  auto shape = c->addLayer(kShapeBar, kLayerChocolate, c->mixOption());
  REQUIRE(shape);
  CHECK(c->layerAt(kLayerBucket) == nullptr);
  REQUIRE(c->layerAt(kLayerIngredient));
  CHECK(c->layerAt(kLayerIngredient)->option() == 1);
  CHECK(c->layerAt(kLayerIngredient)->barType() == kShapeBar);
  CHECK(c->layerAt(kLayerChocolate) == shape);
  CHECK(c->barType() == kShapeBar);
  CHECK(c->top() == 2);
  CHECK_FALSE(c->isBucket());
  CHECK(c->numLayers() == 2);
  CHECK(c->toString() == "(white,ingredient:1,bar)");
  // a topping on a shaped chocolate
  CHECK(c->canAddLayer(kLayerTopping));
  c->addLayer(kShapeBar, kLayerTopping, 2);
  CHECK(c->top() == 4);
  CHECK(c->numLayers() == 3);
  // a layer without a prototype gives null and changes nothing
  CHECK(c->addLayer(kShapeBar, kLayerTopping, 9) == nullptr);
  CHECK(c->numLayers() == 3);
  // children are added in order and sorted by their sprite layer: shape 1, filling 2, topping 4
  std::vector<int> layers;
  for (const auto& ch : c->children()) layers.push_back(ch->layer());
  CHECK(layers == std::vector<int>{1, 2, 4});
}

TEST_CASE("VChocolate: an infusion layer on a bucket makes the shape layer from the mix and removes the bucket") {
  Mx m;
  auto c = m.vendor->getEmptyChocolate(1, true);
  CHECK(c->canAddLayer(kLayerInfusion));
  auto inf = c->addLayer(kShapeInfusion, kLayerInfusion, 2);
  REQUIRE(inf);
  CHECK(c->layerAt(kLayerBucket) == nullptr);
  REQUIRE(c->layerAt(kLayerChocolate));  // made automatically, from the mix
  CHECK(c->layerAt(kLayerChocolate)->barType() == kShapeInfusion);
  CHECK(c->layerAt(kLayerChocolate)->option() == 1);
  CHECK(c->layerAt(kLayerIngredient) == nullptr);
  CHECK(c->barType() == kShapeInfusion);
  CHECK(c->top() == 3);
  CHECK(c->toString() == "(milk,infusion,filling:2)");
  // a bucket that already has a filling cannot take the infusion
  auto filled = m.vendor->getEmptyChocolate(0, true);
  filled->addIngredient(0);
  CHECK_FALSE(filled->canAddLayer(kLayerInfusion));
  // replacing a slot's layer removes the old sprite
  auto bar = m.vendor->getEmptyChocolate(0, true);
  auto first = bar->addLayer(kShapeBar, kLayerChocolate, 0);
  const std::size_t before = bar->children().size();
  auto second = bar->addLayer(kShapeBar, kLayerChocolate, 1);
  CHECK(bar->children().size() == before);
  CHECK(bar->layerAt(kLayerChocolate) == second);
  CHECK(first->parent() == nullptr);
  bar->removeLayer(kLayerChocolate);
  CHECK(bar->top() == -1);
  CHECK(bar->children().empty());
}

TEST_CASE("VChocolate::compare, compareWithLayer and cost go through the spec, with the values and quality upgrades of the shop") {
  Mx m;
  m.shop.upgrades.insert("kmixerq1");
  m.vendor->setUpgradeProbe([&](const std::string& n, const std::string& cat) { return cat == "station" && m.shop.upgradeActive(n, cat); });
  auto a = m.vendor->getEmptyChocolate(0, true);
  a->addLayer(kShapeBar, kLayerChocolate, 0);
  auto b = m.vendor->getEmptyChocolate(0, true);
  b->addLayer(kShapeBar, kLayerChocolate, 0);
  CHECK(a->compare(b.get()));
  b->addLayer(kShapeBar, kLayerTopping, 0);
  CHECK_FALSE(a->compare(b.get()));
  CHECK_FALSE(a->compare(nullptr));
  CHECK(b->compareWithLayer(a.get(), kLayerTopping, 0));
  CHECK_FALSE(b->compareWithLayer(nullptr, kLayerTopping, 0));
  CHECK(a->cost() == doctest::Approx(11.0f));  // 10 for the shape + 0.1 x 10 for the owned kMixerQ1
  CHECK(b->cost() == doctest::Approx(21.0f));
  m.shop.upgrades.erase("kmixerq1");
  CHECK(a->cost() == doctest::Approx(10.0f));
  // a missing price makes everything free
  m.world->values().setValue("layer_price", 0, false);
  CHECK(a->cost() == 0.0f);
}

TEST_CASE("VChocolate::update: a decay-enabled shaped chocolate loses life by the rate, fades out once and counts once; pauses and buckets keep it") {
  Mx m;
  m.world->values().setValue("chocolate_decay_rate", 0.005, false);  // 20 s of life
  auto c = m.vendor->getEmptyChocolate(0, true);
  m.mgr.addObject(c);
  m.step(0);
  m.step(1000);
  CHECK(c->decay() == 100.0f);  // a bucket does not decay
  c->addLayer(kShapeBar, kLayerChocolate, 0);
  m.step(1000);
  CHECK(c->decay() == doctest::Approx(95.0f));
  m.mgr.softPause(true);
  m.step(1000);
  CHECK(c->decay() == doctest::Approx(95.0f));  // a pause keeps it
  m.mgr.softPause(false);
  CHECK_FALSE(c->hasDecayed());
  for (int i = 0; i < 20; ++i) m.step(1000);
  CHECK(c->decay() <= 0.0f);
  CHECK_FALSE(c->hasDecayed());  // the fade (400 ms) is running
  CHECK(c->effectCount() == 1);
  CHECK(m.world->values().getInt("decayed_this_level", 0) == 1);
  m.step(300);
  CHECK_FALSE(c->hasDecayed());
  m.step(300);
  CHECK(c->hasDecayed());
  m.step(1000);
  CHECK(m.world->values().getInt("decayed_this_level", 0) == 1);  // once
  // without a rate, or without the flag, nothing decays
  m.world->values().setValue("chocolate_decay_rate", 0, false);
  auto d = m.vendor->getEmptyChocolate(0, true);
  d->addLayer(kShapeBar, kLayerChocolate, 0);
  m.mgr.addObject(d);
  m.step(0);
  m.step(5000);
  CHECK(d->decay() == 100.0f);
  m.world->values().setValue("chocolate_decay_rate", 0.005, false);
  auto e = m.vendor->getEmptyChocolate(0, false);
  e->addLayer(kShapeBar, kLayerChocolate, 0);
  m.mgr.addObject(e);
  m.step(0);
  m.step(5000);
  CHECK(e->decay() == 100.0f);
  CHECK(m.clean());
}

// ================================================================ the avatar's hands

TEST_CASE("VPlayer::getObject: the preferred hand first, an empty hand turns the preference, the hand id comes back") {
  Mx m;
  auto p = m.makePlayer();
  auto a = m.vendor->getEmptyChocolate(0, true);
  auto b = m.vendor->getEmptyChocolate(1, true);
  int hand = -1;
  CHECK(p->getObject(0, &hand) == nullptr);  // nothing held: the preference turns each ask
  CHECK(p->getObject(2, &hand) == nullptr);
  CHECK(p->attachObject(a));
  CHECK(p->handA() == a);
  CHECK(p->getObject(2, &hand) == a);
  CHECK(hand == 2);
  CHECK(p->getObject(3, &hand) == a);  // 3 is slot A too
  hand = -1;
  CHECK(p->getObject(1, &hand) == nullptr);  // slot B is empty
  CHECK(hand == -1);
  CHECK(p->attachObject(b));
  CHECK(p->getObject(1, &hand) == b);
  CHECK(hand == 1);
  CHECK(p->getChocolate(1) == b);
  CHECK(p->getChocolate(2) == a);
  // hand 0 asks the preferred slot first, then (when it is empty) the other
  p->detachObject(2);  // slot A free; the preference is now 1
  CHECK(p->preferredHand() == 1);
  CHECK(p->getObject(0, &hand) == b);
  CHECK(hand == 1);
  p->detachObject(1);
  CHECK(p->getObject(0, &hand) == nullptr);
  // a sauce or a drink is an item but not a chocolate: the cast is null and the hand id is still reported
  auto sauce = VCarriable::create(m.ctx, 1);
  sauce->setType("sauce");
  CHECK(p->attachObject(sauce));
  hand = -1;
  CHECK(p->getChocolate(0, &hand) == nullptr);
  CHECK(hand == 2);
  CHECK(p->getObject(2) == sauce);
}

// ================================================================ the station and the machine base

TEST_CASE("VStation: its machines load from the table, the enabled one is the single machine, the station is a mouse target") {
  Mx m;
  auto s = m.makeMixer();
  REQUIRE(s->machines().size() == 1);
  auto mixer = Mx::mixerOf(s);
  REQUIRE(mixer);
  CHECK(s->machines().count("machine_mixer") == 1);
  CHECK(mixer->name() == "machine_mixer");
  CHECK(mixer->enabled());
  CHECK(mixer->depth() == 7);
  CHECK(mixer->clickable());
  CHECK_FALSE(mixer->leaf());  // SetStation
  CHECK(mixer->station() == s);
  CHECK(s->singleMachine() == mixer);
  CHECK(s->machineCount() == 1);
  CHECK(s->mouse());
  CHECK(mixer->parent() == s.get());
  CHECK(mixer->frameDelay() == 20.0f);
  CHECK(mixer->visible());
  CHECK(mixer->selectionCount() == 3);
  // the children are leaves and the `anim_` ones hidden
  CHECK_FALSE(mixer->childObject("anim_arm")->visible());
  CHECK(mixer->childObject("anim_arm")->leaf());
  CHECK(mixer->childObject("panel")->visible());
  CHECK(mixer->currentAsset() == "base");
  CHECK(mixer->delay() == 20);
  CHECK(mixer->childObject("anim_arm")->delay() == 20);
  CHECK(m.clean());
}

TEST_CASE("VStation: without an enabled machine the station is not a mouse target; machine types with no class are logged once and skipped") {
  Mx m;
  m.shop.upgrades.clear();
  auto s = m.makeMixer();
  REQUIRE(s->machines().size() == 1);
  CHECK_FALSE(Mx::mixerOf(s)->enabled());
  CHECK_FALSE(Mx::mixerOf(s)->visible());
  CHECK(s->machineCount() == 0);
  CHECK(s->singleMachine() == nullptr);
  CHECK_FALSE(s->mouse());
  // a machine of a type that has no class (a counter has no machines)
  auto t = stationTable(7, kStationCounter, "p14", {{"machine_odd", machineEntry(mixerStyle(), {})}});
  auto st = VStation::create(m.ctx, 0, kStationCounter);
  st->setHost(&m.shop);
  const StyleChain sc(*t, nullptr);
  st->loadFromTable(sc);
  st->init();
  CHECK(st->machines().empty());
  CHECK_FALSE(st->mouse());
  CHECK(m.log.contains("a machine of station type 1 has no machine class"));
  // ... and the same type is not logged twice
  auto st2 = VStation::create(m.ctx, 0, kStationCounter);
  st2->setHost(&m.shop);
  st2->loadFromTable(sc);
  int count = 0;
  for (const std::string& l : m.log.lines()) count += l.find("station type 1 has no machine class") != std::string::npos;
  CHECK(count == 1);
}

TEST_CASE("VMixer::init: the buttons, tanks and quality marks follow the upgrades; a machine with none is hidden, in endless mode it is built hidden") {
  Mx m;
  m.shop.upgrades = {"kmixer1", "kmixer3", "kmixerq3"};
  auto s = m.makeMixer();
  auto mixer = Mx::mixerOf(s);
  REQUIRE(mixer->buttons().size() == 3);
  CHECK(mixer->buttons().at("button1")->visible());
  CHECK_FALSE(mixer->buttons().at("button2")->visible());
  CHECK(mixer->buttons().at("button3")->visible());
  CHECK(mixer->childObject("tank1")->visible());
  CHECK_FALSE(mixer->childObject("tank2")->visible());
  CHECK(mixer->childObject("tank3")->visible());
  // a quality upgrade tints its button cyan, the others keep the white default
  CHECK(mixer->buttons().at("button3")->upgraded());
  CHECK(near(mixer->buttons().at("button3")->tint().r, 0.0f));
  CHECK(near(mixer->buttons().at("button3")->tint().g, 1.0f));
  CHECK_FALSE(mixer->buttons().at("button1")->upgraded());
  CHECK(near(mixer->buttons().at("button1")->tint().r, 1.0f));
  // the buttons were made from the table: id, position, name, layer 10, the type `machine button`
  VMachineButton& b2 = *mixer->buttons().at("button2");
  CHECK(b2.id() == 1);
  CHECK(b2.name() == "btn_mix_milk");
  CHECK(b2.layer() == 10);
  CHECK(b2.position().x == 116);
  CHECK(b2.position().y == 530);
  CHECK(b2.currentAsset() == "button");
  // only an enabled machine was initialised (it has its base asset and delay)
  CHECK(mixer->currentAsset() == "base");
  // no upgrade: not enabled, hidden, never initialised
  Mx n;
  n.shop.upgrades.clear();
  auto s2 = n.makeMixer();
  auto idle = Mx::mixerOf(s2);
  CHECK_FALSE(idle->enabled());
  CHECK_FALSE(idle->visible());
  CHECK(idle->currentAsset().empty());
  CHECK_FALSE(idle->buttons().at("button1")->visible());
  // endless mode builds every machine (hidden unless enabled)
  Mx e;
  e.shop.upgrades.clear();
  e.shop.endless = true;
  auto s3 = e.makeMixer();
  auto built = Mx::mixerOf(s3);
  CHECK_FALSE(built->enabled());
  CHECK_FALSE(built->visible());
  CHECK(built->currentAsset() == "base");
}

TEST_CASE("VMachineButton: a stripe shows first and the icon swaps in after a 501 ms hover with a 300 ms cross-fade; a dark icon is tinted") {
  Mx m;
  auto s = m.makeMixer({buttonEntry(0, 75, 518, "btn_dark", "dark", "stripe_01"), buttonEntry(1, 116, 530, "btn_milk", "milk")});
  auto mixer = Mx::mixerOf(s);
  VMachineButton& striped = *mixer->buttons().at("button1");
  VMachineButton& plain = *mixer->buttons().at("button2");
  REQUIRE(striped.iconSprite());
  REQUIRE(striped.stripeSprite());
  CHECK(striped.iconSprite()->currentAsset() == "icon");
  CHECK(striped.iconSprite()->currentState() == "dark");
  CHECK(striped.stripeSprite()->currentState() == "stripe_01");
  CHECK(striped.iconSprite()->layer() == 1);
  CHECK(near(striped.iconSprite()->tint().r, 0.5f));
  CHECK(near(striped.iconSprite()->tint().g, 0.5f));
  CHECK(near(striped.iconSprite()->tint().b, 1.0f));
  CHECK_FALSE(striped.iconSprite()->visible());  // a button with a stripe shows the stripe
  CHECK(striped.stripeSprite()->visible());
  CHECK(plain.stripeSprite() == nullptr);
  CHECK(plain.iconSprite()->visible());
  CHECK(near(plain.iconSprite()->tint().r, 1.0f));
  m.mgr.setNow(m.now);
  striped.onMouseHover(500);  // not long enough
  CHECK_FALSE(striped.iconSprite()->visible());
  striped.onMouseHover(501);
  CHECK(striped.iconSprite()->visible());
  CHECK(striped.stripeSprite()->visible());  // still fading out
  m.step(0);
  m.step(150);
  CHECK(striped.stripeSprite()->visible());
  m.step(200);
  m.step(16);
  CHECK_FALSE(striped.stripeSprite()->visible());  // the fade-out ended
  CHECK(striped.iconSprite()->visible());
  // the pointer leaves: the stripe comes back, the icon goes
  striped.onMouseLeave();
  CHECK(striped.stripeSprite()->visible());
  m.step(0);
  m.step(350);
  m.step(16);
  CHECK_FALSE(striped.iconSprite()->visible());
  CHECK(striped.stripeSprite()->visible());
  // the swap needs both children: a button without a stripe changes nothing
  plain.onMouseHover(900);
  CHECK(plain.iconSprite()->visible());
  CHECK(plain.iconSprite()->effectCount() == 0);
  CHECK(m.clean());
}

TEST_CASE("VMachineButton: a click on the button chooses its option and queues the avatar's walk; a click while busy changes no choice") {
  Mx m;
  auto p = m.makePlayer();
  auto s = m.makeMixer();
  auto mixer = Mx::mixerOf(s);
  m.step(16);
  // the dark button's art: a 30x20 plain texture at (75, 518); only kMixer1 is on, so the others are hidden
  CHECK_FALSE(mixer->buttons().at("button2")->visible());
  m.mgr.onMouseMove({80, 525});
  m.step(16);  // the hit test runs in the update
  CHECK(m.mgr.hover() == mixer->buttons().at("button1").get());
  m.mgr.onMouseDown({80, 525});
  m.mgr.onMouseUp({80, 525});
  CHECK(mixer->option() == 0);
  // the click went on to the station: the machine's name is waiting, the avatar has the point queued
  CHECK(s->pendingClicks().size() == 1);
  CHECK(s->pendingClicks().front() == "machine_mixer");
  CHECK(p->queue().size() + (p->moving() ? 1 : 0) == 1);
  // the avatar arrives (walking): the choice is used, the click list is forgotten
  m.until([&] { return mixer->state() > 0; }, 4000);
  CHECK(mixer->state() >= 1);
  CHECK(s->pendingClicks().empty());
  CHECK(mixer->option() == -1);  // Activate used it
  // a click on a button while the machine is locked: no choice is stored, but the click still queues a visit
  m.mgr.onMouseMove({81, 526});
  m.step(16);
  m.mgr.onMouseDown({80, 525});
  m.mgr.onMouseUp({80, 525});
  CHECK(mixer->locked());
  CHECK(mixer->option() == -1);
  CHECK(s->pendingClicks().size() == 1);
  CHECK(m.clean());
}

// ================================================================ the mixer

TEST_CASE("VMixer: the steps run at 620, 1040, 1840 (unlock), 2260 and 2880 ms, and the station is suspended from the start to the unlock") {
  Mx m;
  m.makePlayer();
  m.shop.who.reset();
  auto s = m.makeMixer();
  auto mixer = Mx::mixerOf(s);
  m.step(16);
  mixer->chooseOption(0);
  CHECK(mixer->option() == 0);
  CHECK(mixer->state() == 0);
  m.arrive(s);
  const double t0 = m.now;
  CHECK(mixer->state() == 1);
  CHECK(mixer->locked());
  CHECK(mixer->processed());
  CHECK(mixer->option() == -1);
  CHECK(mixer->suspended());
  CHECK(s->suspended());  // the station takes no arrivals while the machine works
  REQUIRE(mixer->chocolate());
  CHECK(mixer->chocolate()->toString() == "(dark)");
  CHECK(mixer->chocolate()->parent() == mixer.get());
  CHECK(mixer->chocolate()->layer() == 2);
  CHECK(mixer->chocolate()->anchorName() == "chocolate");
  CHECK(mixer->bucketIcon() != nullptr);
  CHECK_FALSE(mixer->bucketIcon()->visible());
  // the arm swings: visible, playing
  VObject* arm = mixer->childObject("anim_arm");
  CHECK(arm->visible());
  CHECK(arm->animName() == "DoAnim");
  CHECK(mixer->childObject("anim_tube")->currentState() == "option_0");
  CHECK(near(mixer->childObject("anim_tube")->tint().r, 0.5f));  // dark: dimmed
  auto at = [&](int expectState, int expectedMs) {
    const int t = m.until([&] { return mixer->state() == expectState; });
    CHECK(std::abs(static_cast<int>(m.now - t0) - expectedMs) <= 25);
    (void)t;
  };
  at(2, 620);
  CHECK(mixer->childObject("anim_tube")->visible());
  CHECK(mixer->childObject("anim_tube")->animName() == "DoAnim");
  CHECK(s->suspended());
  at(3, 1040);
  CHECK(mixer->childObject("anim_spray")->visible());
  CHECK(mixer->childObject("anim_spray")->animName() == "DoAnim");
  CHECK(mixer->bucketIcon()->visible());  // wiped in over 800 ms
  CHECK(effectsOf<WipeEffect>(*mixer->bucketIcon()) == 1);
  CHECK(mixer->locked());
  at(4, 1840);
  CHECK_FALSE(mixer->locked());  // Unlock: the bucket can be picked up
  CHECK_FALSE(s->suspended());
  CHECK_FALSE(mixer->childObject("anim_spray")->visible());
  CHECK(mixer->childObject("anim_tube")->animName() == "Reverse");
  at(5, 2260);
  CHECK_FALSE(mixer->childObject("anim_tube")->visible());
  CHECK(mixer->childObject("anim_arm")->animName() == "Reverse");
  at(0, 2880);
  CHECK_FALSE(mixer->suspended());
  CHECK_FALSE(mixer->childObject("anim_arm")->visible());
  CHECK(mixer->chocolate());  // the bucket waits to be collected
  CHECK(mixer->processed());
  CHECK(mixer->bucketIcon() == nullptr);
  CHECK(m.clean());
}

TEST_CASE("VMixer: the steam booster speeds the frames by ten percent (2756 ms, unlock at 1778 ms)") {
  Mx m;
  m.makePlayer();
  m.shop.who.reset();
  m.shop.upgrades.insert("ksteam1");
  auto s = m.makeMixer();
  auto mixer = Mx::mixerOf(s);
  m.step(16);
  mixer->chooseOption(2);
  m.arrive(s);
  CHECK(mixer->delay() == 18);
  const double t0 = m.now;
  m.until([&] { return !mixer->locked(); });
  CHECK(std::abs(static_cast<int>(m.now - t0) - 1778) <= 25);
  m.until([&] { return mixer->state() == 0; });
  CHECK(std::abs(static_cast<int>(m.now - t0) - 2756) <= 30);
  CHECK(mixer->chocolate()->toString() == "(white)");
  CHECK(mixer->delay() == 18);
}

TEST_CASE("VMixer: a visit collects the finished bucket and starts the next in one go; hands full, the bucket stays and blocks the machine") {
  Mx m;
  auto p = m.makePlayer();
  m.shop.who.reset();
  auto s = m.makeMixer();
  auto mixer = Mx::mixerOf(s);
  m.step(16);
  auto runOut = [&] { m.until([&] { return mixer->state() == 0 && !mixer->suspended(); }); };
  mixer->chooseOption(0);
  m.arrive(s);
  runOut();
  auto first = mixer->chocolate();
  REQUIRE(first);
  // a visit with no choice collects the bucket (it is processed, unlocked and a hand is free)
  m.arrive(s);
  CHECK(mixer->chocolate() == nullptr);
  CHECK(p->handA() == first);
  CHECK(mixer->state() == 0);
  // a visit with a choice and an empty machine starts a new bucket
  mixer->chooseOption(1);
  m.arrive(s);
  CHECK(mixer->state() == 1);
  runOut();
  auto second = mixer->chocolate();
  REQUIRE(second);
  CHECK(second->toString() == "(milk)");
  // a choice while the old bucket is still there: one visit collects it, then starts the new one
  mixer->chooseOption(2);
  m.arrive(s);
  CHECK(p->handB() == second);
  CHECK(mixer->state() == 1);
  CHECK(mixer->chocolate()->toString() == "(white)");
  runOut();
  // both hands are full: the bucket stays, the machine is blocked, the choice stays
  mixer->chooseOption(0);
  m.arrive(s);
  CHECK(mixer->state() == 0);
  CHECK(mixer->option() == 0);
  CHECK(mixer->chocolate()->toString() == "(white)");
  CHECK(p->handA() == first);
  CHECK(p->handB() == second);
  // a hand is freed: the next visit collects and starts the choice
  p->detachObject(VPlayer::kHandB);
  m.arrive(s);
  CHECK(p->handB() != nullptr);
  CHECK(mixer->state() == 1);
  CHECK(mixer->chocolate()->toString() == "(dark)");
  CHECK(m.clean());
}

TEST_CASE("VMixer: a choice made while busy is ignored, an arrival at the working station does nothing, one made after the unlock runs after the reset") {
  Mx m;
  auto p = m.makePlayer();
  m.shop.who.reset();
  auto s = m.makeMixer();
  auto mixer = Mx::mixerOf(s);
  m.step(16);
  mixer->chooseOption(0);
  m.arrive(s);
  m.run(100);
  // locked: the choice is not stored
  mixer->chooseOption(2);
  CHECK(mixer->option() == -1);
  CHECK(s->pendingClicks().size() == 1);  // the click on the machine is still queued
  // an arrival at the station is ignored while the machine works (the station is suspended): nothing is picked up, the click is
  // forgotten
  m.arrive(s);
  CHECK(s->pendingClicks().empty());
  CHECK(p->handA() == nullptr);
  CHECK(mixer->state() == 1);
  // after the unlock (state 4) a choice is stored
  m.until([&] { return !mixer->locked(); });
  CHECK(mixer->state() == 4);
  mixer->chooseOption(1);
  CHECK(mixer->option() == 1);
  // she visits before the machine has wound down: the bucket is collected, the new mix waits for the end of the sequence
  m.arrive(s);
  CHECK(mixer->chocolate() == nullptr);
  CHECK(p->handA() != nullptr);
  CHECK(mixer->state() == 4);  // not started: the machine is still suspended
  CHECK(mixer->option() == 1);
  m.until([&] { return mixer->state() == 1; });  // Reset ran and applied the choice (state 5 -> 0 -> Activate)
  CHECK(mixer->state() == 1);
  CHECK(mixer->chocolate()->toString() == "(milk)");
  CHECK(mixer->option() == -1);
  CHECK(m.clean());
}

TEST_CASE("VMachine::reset: mid-sequence it stops the work, unlocks, frees the station and does not resume; the bucket stays") {
  Mx m;
  m.makePlayer();
  m.shop.who.reset();
  auto s = m.makeMixer();
  auto mixer = Mx::mixerOf(s);
  m.step(16);
  mixer->chooseOption(1);
  m.arrive(s);
  m.run(700);
  CHECK(mixer->state() == 2);
  CHECK(mixer->childObject("anim_tube")->visible());
  mixer->reset();
  CHECK(mixer->state() == 0);
  CHECK_FALSE(mixer->locked());
  CHECK_FALSE(mixer->suspended());
  CHECK_FALSE(s->suspended());
  CHECK_FALSE(mixer->childObject("anim_tube")->visible());
  CHECK_FALSE(mixer->childObject("anim_arm")->visible());
  CHECK(mixer->childObject("panel")->visible());
  CHECK(mixer->chocolate());
  m.step(3000);  // no resume: nothing happens any more
  CHECK(mixer->state() == 0);
  CHECK_FALSE(mixer->locked());
  // a machine that is not suspended resets plainly too
  mixer->reset();
  CHECK(mixer->state() == 0);
  CHECK(m.clean());
}

// ================================================================ the press

namespace {
std::shared_ptr<VChocolate> bucketOf(Mx& m, int mix) { return m.vendor->getEmptyChocolate(mix, true); }
}  // namespace

TEST_CASE("VPress: a bucket in hand and a choice start the press; unlock at 450 ms shows the bar, the end is at 1230 ms, bars are counted") {
  Mx m;
  auto p = m.makePlayer();
  m.shop.who.reset();
  auto s = m.makePress();
  auto press = Mx::pressOf(s);
  m.step(16);
  CHECK(press->currentFrame() == 3);
  auto bucket = bucketOf(m, 2);
  REQUIRE(p->attachObject(bucket));
  press->chooseOption(0);
  m.arrive(s);
  const double t0 = m.now;
  CHECK(press->state() == 1);
  CHECK(press->locked());
  CHECK(press->shape() == 0);
  CHECK(press->suspended());
  CHECK(s->suspended());
  CHECK(press->chocolate() == bucket);
  CHECK(p->handA() == nullptr);  // the bucket left her hand
  CHECK(bucket->parent() == press.get());
  CHECK(bucket->layer() == 1);
  CHECK(bucket->anchorName() == "chocolate");
  CHECK_FALSE(bucket->visible());  // hidden until the stamp
  CHECK(bucket->toString() == "(white,bar)");  // the shape layer is on already
  CHECK(bucket->layerAt(kLayerBucket) == nullptr);
  REQUIRE(press->fillLayer());
  CHECK(press->fillLayer()->parent() == press.get());
  CHECK(press->fillLayer()->anchorName() == "bucket");
  CHECK(press->fillAnimation() == "DoAnim_2");
  CHECK(m.world->values().getInt("chocolates_made_this_level", 0) == 1);
  CHECK(press->childObject("anim_fill")->visible());
  CHECK(press->childObject("anim_fill")->animName() == "DoAnim_2");
  CHECK(press->childObject("static_door")->visible());  // the three statics stay shown through the first step
  CHECK(press->childObject("static_wheel")->visible());
  CHECK(press->childObject("static_largeweight")->visible());
  CHECK(effectsOf<WipeEffect>(*press->fillLayer()->childObject("icon")) == 1);  // the fill wipes away
  // step 2 at 300 ms
  m.until([&] { return press->state() == 2; });
  CHECK(std::abs(static_cast<int>(m.now - t0) - 300) <= 25);
  CHECK_FALSE(press->childObject("static_door")->visible());
  CHECK_FALSE(press->childObject("static_wheel")->visible());
  CHECK_FALSE(press->childObject("static_largeweight")->visible());
  CHECK(press->childObject("anim_door")->animName() == "DoDoor");
  CHECK(press->childObject("anim_largeweight")->animName() == "DoAnim");
  CHECK(press->childObject("anim_wheel")->animName() == "DoAnim");
  // the base frame follows the weight: 3 until the weight is at its frame 5, then 4
  CHECK(press->currentFrame() == 3);
  // drop() at 300 + 6 x 20 = 420 ms: the barrel falls and wipes
  m.until([&] { return effectsOf<SpriteMove>(*press->fillLayer()) > 0; });
  CHECK(std::abs(static_cast<int>(m.now - t0) - 420) <= 25);
  CHECK(effectsOf<SpriteMove>(*press->fillLayer()) == 1);
  CHECK(effectsOf<WipeEffect>(*press->fillLayer()) == 1);
  // unlock() at 300 + 5 x 30 = 450 ms: the bar shows, the fill hides, the press is unlocked and the station takes arrivals
  CHECK(press->locked());
  m.until([&] { return !press->locked(); });
  CHECK(std::abs(static_cast<int>(m.now - t0) - 450) <= 25);
  CHECK(bucket->visible());
  CHECK_FALSE(press->childObject("anim_fill")->visible());
  CHECK_FALSE(s->suspended());
  CHECK(press->suspended());  // the press itself still waits for the weight
  m.until([&] { return press->state() == 0; });
  CHECK(std::abs(static_cast<int>(m.now - t0) - 1230) <= 30);
  CHECK(m.world->values().getInt("num_bars", 0) == 1);
  CHECK(m.world->values().getInt("num_truffles", 0) == 0);
  CHECK(press->shape() == -1);
  CHECK(press->fillLayer() == nullptr);
  CHECK(press->fillAnimation() == "DoAnim_0");
  CHECK(press->currentFrame() == 3);
  CHECK(press->chocolate() == bucket);
  CHECK(press->processed());
  CHECK_FALSE(press->childObject("anim_largeweight")->visible());
  CHECK(press->childObject("static_largeweight")->visible());
  // the output is picked up by a visit
  m.arrive(s);
  CHECK(p->handA() == bucket);
  CHECK(press->chocolate() == nullptr);
  CHECK(m.clean());
}

TEST_CASE("VPress: the steam booster, truffles and novelties, and the shapes that need a prototype") {
  Mx m;
  auto p = m.makePlayer();
  m.shop.who.reset();
  m.shop.upgrades.insert("ksteam1");
  auto s = m.makePress();
  auto press = Mx::pressOf(s);
  m.step(16);
  for (int shape = 0; shape < 3; ++shape) {
    auto b = bucketOf(m, shape);
    REQUIRE(p->attachObject(b));
    press->chooseOption(shape);
    m.arrive(s);
    const double t0 = m.now;
    CHECK(press->state() == 1);
    CHECK(b->barType() == shape);
    m.until([&] { return !press->locked(); });
    CHECK(std::abs(static_cast<int>(m.now - t0) - 435) <= 25);  // 300 + 5 x 27
    m.until([&] { return press->state() == 0; });
    CHECK(std::abs(static_cast<int>(m.now - t0) - 1137) <= 30);  // 300 + 31 x 27
    m.arrive(s);  // collect
    CHECK(p->handA() == b);
    p->detachObject(VPlayer::kHandA);
  }
  CHECK(m.world->values().getInt("num_bars", 0) == 1);
  CHECK(m.world->values().getInt("num_truffles", 0) == 1);  // the novelty is not counted
  CHECK(m.world->values().getInt("chocolates_made_this_level", 0) == 3);
  CHECK(m.clean());
}

TEST_CASE("VPress: the bucket's filling carries into the pressed bar, and a bar with a filling is its own comparison") {
  Mx m;
  auto p = m.makePlayer();
  m.shop.who.reset();
  auto s = m.makePress();
  auto press = Mx::pressOf(s);
  m.step(16);
  auto b = bucketOf(m, 1);
  b->addIngredient(2);
  REQUIRE(p->attachObject(b));
  press->chooseOption(1);
  m.arrive(s);
  CHECK(b->toString() == "(milk,ingredient:2,truffle)");
  REQUIRE(b->layerAt(kLayerIngredient));
  CHECK(b->layerAt(kLayerIngredient)->option() == 2);
  CHECK(b->layerAt(kLayerIngredient)->barType() == kShapeTruffle);
  m.until([&] { return press->state() == 0; });
  CHECK(m.world->values().getInt("num_truffles", 0) == 1);
}

TEST_CASE("VPress: a bucket put in without a choice waits, a later choice and visit start it; a bar in hand is refused and the choice cleared") {
  Mx m;
  auto p = m.makePlayer();
  m.shop.who.reset();
  auto s = m.makePress();
  auto press = Mx::pressOf(s);
  m.step(16);
  auto bucket = bucketOf(m, 0);
  p->attachObject(bucket);
  m.arrive(s);  // no choice
  CHECK(press->chocolate() == bucket);
  CHECK(press->state() == 0);
  CHECK(p->handA() == nullptr);
  CHECK_FALSE(press->processed());
  CHECK(bucket->isBucket());
  press->chooseOption(2);
  CHECK(press->option() == 2);
  m.arrive(s);  // ProcessChocolate with the bucket inside
  CHECK(press->state() == 1);
  CHECK(bucket->barType() == 2);
  m.until([&] { return press->state() == 0; });
  m.arrive(s);
  CHECK(p->handA() == bucket);
  // a shaped bar cannot go in: the choice is cleared and nothing else happens
  press->chooseOption(0);
  m.arrive(s);
  CHECK(press->option() == -1);
  CHECK(press->chocolate() == nullptr);
  CHECK(press->state() == 0);
  CHECK(p->handA() == bucket);
  // the press is hands-free for an arrival with something else in hand
  auto sauce = VCarriable::create(m.ctx, 1);
  sauce->setType("sauce");
  p->detachObject(VPlayer::kHandA);
  p->attachObject(sauce);
  press->chooseOption(1);
  m.arrive(s);
  CHECK(press->option() == -1);
  CHECK(press->state() == 0);
  CHECK(p->handA() == sauce);
  CHECK(m.clean());
}

TEST_CASE("VPress: a new bucket swaps with the finished bar, the bar takes the freed hand, and the new one is processed") {
  Mx m;
  auto p = m.makePlayer();
  m.shop.who.reset();
  auto s = m.makePress();
  auto press = Mx::pressOf(s);
  m.step(16);
  auto first = bucketOf(m, 0);
  p->attachObject(first);
  press->chooseOption(0);
  m.arrive(s);
  m.until([&] { return press->state() == 0; });
  REQUIRE(press->chocolate() == first);
  auto second = bucketOf(m, 1);
  p->attachObject(second);
  auto other = bucketOf(m, 2);
  p->attachObject(other);  // both hands busy: the bar could not be picked up without the swap
  CHECK(p->numHandsFree() == 0);
  press->chooseOption(1);
  m.arrive(s);
  // the hand that gave the bucket took the bar
  // the avatar's preferred hand gave a bucket; the press holds it, shaped, and she holds the bar and the other bucket
  auto pressed = press->chocolate();
  REQUIRE(pressed);
  CHECK((pressed == second || pressed == other));
  CHECK(pressed->barType() == 1);
  CHECK(press->state() == 1);
  const auto left = pressed == second ? other : second;
  CHECK((p->handA() == first || p->handB() == first));
  CHECK((p->handA() == left || p->handB() == left));
  CHECK(left->isBucket());
  m.until([&] { return press->state() == 0; });
  CHECK(m.world->values().getInt("num_bars", 0) == 1);
  CHECK(m.world->values().getInt("num_truffles", 0) == 1);
  CHECK(m.clean());
}

TEST_CASE("VPress: an arrival at the working press does nothing; one made once the stamp has fallen is taken") {
  Mx m;
  auto p = m.makePlayer();
  m.shop.who.reset();
  auto s = m.makePress();
  auto press = Mx::pressOf(s);
  m.step(16);
  auto b = bucketOf(m, 0);
  p->attachObject(b);
  press->chooseOption(0);
  m.arrive(s);
  auto c = bucketOf(m, 1);
  p->attachObject(c);
  press->chooseOption(1);  // locked: ignored
  CHECK(press->option() == -1);
  m.arrive(s);  // the station is suspended
  CHECK(p->handA() == c);
  CHECK(press->state() == 1);
  // after the unlock the press itself is still in its last step (suspended): a new choice is stored, the arrival swaps the bar
  // out and the new bucket in, but the press cannot process it yet
  m.until([&] { return !press->locked(); });
  press->chooseOption(1);
  CHECK(press->option() == 1);
  m.arrive(s);
  CHECK(press->chocolate() == c);
  CHECK(p->handA() == b);
  // ProcessChocolate refuses while suspended; Reset applies the stored choice at the end of the sequence
  CHECK(press->state() == 2);
  m.until([&] { return press->state() == 1; });
  CHECK(c->barType() == 1);
  CHECK(m.world->values().getInt("num_bars", 0) == 1);
  CHECK(m.clean());
}

TEST_CASE("VPress: Reset in the middle drops the fill layer and stops the work; the callbacks of a machine that left the scene are harmless") {
  Mx m;
  auto p = m.makePlayer();
  m.shop.who.reset();
  auto s = m.makePress();
  auto press = Mx::pressOf(s);
  m.step(16);
  auto b = bucketOf(m, 0);
  p->attachObject(b);
  press->chooseOption(0);
  m.arrive(s);
  m.run(310);
  REQUIRE(press->fillLayer());
  press->reset();
  CHECK(press->state() == 0);
  CHECK(press->fillLayer() == nullptr);
  CHECK(press->shape() == -1);
  CHECK_FALSE(press->locked());
  CHECK_FALSE(s->suspended());
  CHECK(press->currentFrame() == 3);
  m.step(2000);
  CHECK(press->state() == 0);
  CHECK(m.world->values().getInt("num_bars", 0) == 0);  // the work never finished
  CHECK(m.clean());
}

TEST_CASE("VPress: the scene is taken down inside the unlock() callback of the weight's own script") {
  Mx m;
  auto p = m.makePlayer();
  m.shop.who.reset();
  auto s = m.makePress();
  auto press = Mx::pressOf(s);
  m.step(16);
  auto b = bucketOf(m, 0);
  p->attachObject(b);
  press->chooseOption(0);
  m.arrive(s);
  std::weak_ptr<VPress> weak = press;
  std::weak_ptr<VStation> weakStation = s;
  bool fired = false;
  // the weight's script now tears the scene down when it calls unlock(), before the machine's own handler would run
  press->childObject("anim_largeweight")->setScriptSetup([&](sdk::SpriteScript& script) {
    script.addNative("unlock", [&](lua_State*) {
      fired = true;
      m.mgr.removeAllObjects();
      m.shop.stations.clear();
      m.shop.who.reset();
      return 0;
    });
    script.addNative("drop", [](lua_State*) { return 0; });
  });
  b.reset();
  p->detachObject(VPlayer::kHandA);
  press.reset();
  s.reset();
  m.player.reset();
  m.until([&] { return fired; }, 3000);
  CHECK(fired);
  m.step(50);
  CHECK(weak.expired());
  CHECK(weakStation.expired());
  CHECK(m.clean());
}

TEST_CASE("VPress: unlock() and drop() as the scripts call them: the stamp shows the bar and the barrel falls, repeated calls are harmless") {
  Mx m;
  auto p = m.makePlayer();
  m.shop.who.reset();
  auto s = m.makePress();
  auto press = Mx::pressOf(s);
  VObject* weight = press->childObject("anim_largeweight");
  VObject* door = press->childObject("anim_door");
  REQUIRE(weight);
  REQUIRE(weight->script());
  REQUIRE(door->script());
  // unlock() called directly: the locked press is unlocked and the bar is shown
  m.step(16);
  auto b = bucketOf(m, 0);
  p->attachObject(b);
  press->chooseOption(0);
  m.arrive(s);
  CHECK(press->locked());
  press->unlockStamp();
  CHECK_FALSE(press->locked());
  CHECK(b->visible());
  CHECK_FALSE(press->childObject("anim_fill")->visible());
  press->dropBarrel();
  CHECK(effectsOf<SpriteMove>(*press->fillLayer()) == 1);
  CHECK(effectsOf<WipeEffect>(*press->fillLayer()) == 1);
  // a second call: nothing is locked, nothing breaks
  press->unlockStamp();
  press->dropBarrel();
  CHECK(m.clean());
}

// ================================================================ the machine base, decay and the hands

TEST_CASE("VMachine: a decayed chocolate is dropped from the machine silently, and from the avatar's hands by her drop check") {
  Mx m;
  auto p = m.makePlayer();
  m.shop.who.reset();
  p->setDropCheck([](const VCarriable& item) {
    const auto* c = dynamic_cast<const VChocolate*>(&item);
    return c && c->hasDecayed();
  });
  m.world->values().setValue("chocolate_decay_rate", 100.0 / 1500.0, false);  // 1.5 s of life, then a 400 ms fade
  auto s = m.makePress();
  auto press = Mx::pressOf(s);
  m.step(16);
  auto b = bucketOf(m, 0);
  p->attachObject(b);
  press->chooseOption(0);
  m.arrive(s);
  m.until([&] { return press->state() == 0; });
  REQUIRE(press->chocolate() == b);
  // the shaped bar spent its life while the press ran
  m.run(700);
  CHECK(b->hasDecayed());
  CHECK(press->chocolate() == nullptr);
  CHECK(m.world->values().getInt("decayed_this_level", 0) == 1);
  // in the hands
  auto held = bucketOf(m, 1);
  held->addLayer(kShapeBar, kLayerChocolate, 1);
  p->attachObject(held);
  m.step(0);
  m.run(1400);
  CHECK(p->handA() == held);
  m.run(600);
  CHECK(held->hasDecayed());
  CHECK(p->handA() == nullptr);
  CHECK(p->numHandsFree() == 2);
  CHECK(m.clean());
}

// ================================================================ slice 12: the game states and the display influence

namespace {
// A finished bar of a mix, with a topping when `topping` >= 0.
std::shared_ptr<VChocolate> barOf(Mx& m, int mix, int topping = -1, int shape = kShapeBar) {
  auto c = m.vendor->getEmptyChocolate(mix, true);
  c->addLayer(shape, kLayerChocolate, mix);
  if (topping >= 0) c->addLayer(shape, kLayerTopping, topping);
  return c;
}
std::shared_ptr<VCarriable> bottleOf(Mx& m, const char* type) {
  auto b = VCarriable::create(m.ctx, 1);
  b->setType(type);
  return b;
}
ChocolateSpec specOf(int mix, int bar, std::optional<int> filling, std::optional<int> topping, std::optional<int> infusion = std::nullopt) {
  ChocolateSpec c;
  c.setMixOption(mix);
  c.setBarType(bar);
  c.store({kLayerChocolate, bar, mix});
  if (filling) c.store({kLayerIngredient, bar, *filling});
  if (infusion) c.store({kLayerInfusion, bar, *infusion});
  if (topping) c.store({kLayerTopping, bar, *topping});
  return c;
}
}  // namespace

TEST_CASE("GameStateStore: a key with ++ counts the integer under the rest, any other key stores the string, an unset key reads empty") {
  GameStateStore g;
  CHECK(g.get("TrashUsed").empty());
  g.set("++TrashUsed");
  CHECK(g.get("TrashUsed") == "1");
  g.set("++TrashUsed", "ignored");
  CHECK(g.get("TrashUsed") == "2");
  CHECK(g.get("++TrashUsed").empty());  // the ++ is not part of the stored key
  g.set("TrashUsedItemID", "sauce");
  CHECK(g.get("TrashUsedItemID") == "sauce");
  g.set("TrashUsedItemID", "");
  CHECK(g.get("TrashUsedItemID").empty());
  g.set("Word", "abc");
  g.set("++Word");  // a non-number counts as 0
  CHECK(g.get("Word") == "1");
}

TEST_CASE("DisplayInfluence: a chocolate's mix, shape and filled layers go in by index, a bucket adds nothing, removal erases the option even when another chocolate has it") {
  DisplayInfluence d;
  ChocolateSpec bucket;
  bucket.store({kLayerBucket, -1, 0});
  bucket.setMixOption(1);
  d.add(bucket);
  for (int i = 0; i < 5; ++i) CHECK(d.options(i).empty());
  const ChocolateSpec truffle = specOf(1, kShapeTruffle, 2, 0);
  d.add(truffle);
  CHECK(d.options(0) == std::set<int>{1});      // the mix
  CHECK(d.options(1) == std::set<int>{2});      // the filling
  CHECK(d.options(2) == std::set<int>{kShapeTruffle});  // the shape is the bar type, not the layer's option
  CHECK(d.options(3).empty());
  CHECK(d.options(4) == std::set<int>{0});
  // an infusion bar fills index 3 with its infusion and index 2 with the infusion bar type
  d.add(specOf(0, kShapeInfusion, std::nullopt, std::nullopt, 2));
  CHECK(d.options(0) == std::set<int>{0, 1});
  CHECK(d.options(2) == std::set<int>{kShapeTruffle, kShapeInfusion});
  CHECK(d.options(3) == std::set<int>{2});
  // two chocolates with the same mix: taking one erases it for both
  d.add(specOf(1, kShapeBar, std::nullopt, std::nullopt));
  CHECK(d.options(0) == std::set<int>{0, 1});
  d.remove(truffle);
  CHECK(d.options(0) == std::set<int>{0});  // the bar still shows mix 1, the set forgot it (the kept quirk)
  CHECK(d.options(1).empty());
  CHECK(d.options(2) == std::set<int>{kShapeBar, kShapeInfusion});
  // removing what was never added, and a bucket, change nothing
  d.remove(bucket);
  d.remove(truffle);
  CHECK(d.options(0) == std::set<int>{0});
  d.clear();
  CHECK(d.options(0).empty());
}

TEST_CASE("DisplayInfluence::pick: no draw for an empty index, <= against the percentage, no second draw for one option, the draw % size for more") {
  DisplayInfluence d;
  int draws = 0;
  std::vector<unsigned> script;
  auto draw = [&]() -> unsigned {
    ++draws;
    return script.at(static_cast<std::size_t>(draws - 1));
  };
  CHECK(d.pick(0, 7, 100, draw) == 7);
  CHECK(draws == 0);  // nothing on display: no draw
  d.add(specOf(2, kShapeBar, std::nullopt, std::nullopt));
  script = {30};
  CHECK(d.pick(0, 7, 30, draw) == 2);  // 30 <= 30 is a hit
  CHECK(draws == 1);                     // a single option: no second draw
  draws = 0;
  script = {31};
  CHECK(d.pick(0, 7, 30, draw) == 7);  // 31 > 30 is a miss
  CHECK(draws == 1);
  draws = 0;
  script = {131};  // 131 % 101 = 30
  CHECK(d.pick(0, 7, 30, draw) == 2);
  draws = 0;
  script = {0};
  CHECK(d.pick(0, 7, 0, draw) == 2);  // a percentage of 0 still hits on a roll of 0
  d.add(specOf(0, kShapeTruffle, std::nullopt, std::nullopt));
  d.add(specOf(1, kShapeNovelty, std::nullopt, std::nullopt));
  CHECK(d.options(0) == std::set<int>{0, 1, 2});
  draws = 0;
  script = {10, 4};  // a hit, then 4 % 3 = 1: the second option in ascending order
  CHECK(d.pick(0, 7, 30, draw) == 1);
  CHECK(draws == 2);
  draws = 0;
  script = {10, 5};
  CHECK(d.pick(0, 7, 30, draw) == 2);
  // an index outside the five names is never a hit
  CHECK(DisplayInfluence::valueName(5) == nullptr);
  CHECK(std::string(DisplayInfluence::valueName(3)) == "display_influence_infusion");
  CHECK(d.pick(5, 9, 100, draw) == 9);
}

TEST_CASE("VChocolateVendor::getDisplayInfluence: the value names the chance, the shop's rng draws, a reload forgets the display") {
  Mx m;
  CHECK(m.vendor->getDisplayInfluence(0, 7) == 7);  // nothing on display
  auto c = barOf(m, 1, 2);
  m.vendor->addDisplayChocolate(*c);
  m.world->values().setValue("display_influence_mix", 100, false);
  for (int i = 0; i < 50; ++i) CHECK(m.vendor->getDisplayInfluence(0, 7) == 1);  // every roll is <= 100
  m.world->values().setValue("display_influence_topping", 0, false);
  int hits = 0;
  for (int i = 0; i < 1010; ++i) hits += m.vendor->getDisplayInfluence(4, 9) == 2;
  CHECK(hits < 60);  // about one roll in 101
  // a bucket is not registered, a removal erases
  auto b = m.vendor->getEmptyChocolate(2, true);
  m.vendor->addDisplayChocolate(*b);
  CHECK(m.vendor->displayInfluence().options(0) == std::set<int>{1});
  m.vendor->removeDisplayChocolate(*c);
  CHECK(m.vendor->getDisplayInfluence(0, 7) == 7);
  m.vendor->addDisplayChocolate(*c);
  const StyleChain chain(*m.protos, nullptr);
  m.vendor->loadFromTable(chain);  // ClearVendor
  CHECK(m.vendor->displayInfluence().options(0).empty());
}

// ================================================================ slice 12: the sauce and drink machines

TEST_CASE("VStation::create: the disposal and the display are their own classes with the station glow, the rest are plain stations") {
  Mx m;
  auto disposal = VStation::create(m.ctx, 0, kStationDisposal);
  auto display = VStation::create(m.ctx, 0, kStationDisplay);
  auto plain = VStation::create(m.ctx, 0, kStationEmpty);
  auto counter = VStation::create(m.ctx, 0, kStationCounter);
  CHECK(std::string(disposal->kind()) == "VDisposal");
  CHECK(std::string(display->kind()) == "VDisplay");
  CHECK(std::string(plain->kind()) == "VStation");
  CHECK(std::string(counter->kind()) == "VStation");
  CHECK(disposal->stationType() == kStationDisposal);
  CHECK(display->stationType() == kStationDisplay);
  CHECK(effectsOf<HoverGlow>(*disposal) == 1);
  CHECK(effectsOf<HoverGlow>(*display) == 1);
  CHECK(effectsOf<HoverGlow>(*plain) == 0);
}

TEST_CASE("VSauceDrink::init: kSauce1 enables the sauce machine, kSauce3 without kDrink1 the second, kDrink1 the drink machine; the others stay hidden and unbuilt") {
  {
    Mx m;
    m.shop.upgrades = {"ksauce1"};
    auto s = m.makeSauceStation(true, true);
    auto a = Mx::sauceOf(s), b = Mx::sauceOf(s, "machine_sauce_2"), d = Mx::sauceOf(s, "machine_drink");
    REQUIRE((a && b && d));
    CHECK(std::string(a->kind()) == "VSauce");
    CHECK(std::string(d->kind()) == "VDrink");
    CHECK(b->name() == "machine_sauce_2");
    CHECK(a->enabled());
    CHECK(a->visible());
    CHECK_FALSE(b->enabled());
    CHECK_FALSE(b->visible());
    CHECK_FALSE(d->enabled());
    CHECK_FALSE(d->visible());
    CHECK(s->machineCount() == 1);
    CHECK(s->singleMachine() == a);
    CHECK(b->bottle() == nullptr);  // never initialised: no bottle, no base asset
    CHECK(b->currentAsset().empty());
    CHECK(d->bottle() == nullptr);
    CHECK(a->currentAsset() == "base");
    CHECK(a->frameDelay() == 30.0f);
  }
  {
    Mx m;
    m.shop.upgrades = {"ksauce1", "ksauce3"};
    auto s = m.makeSauceStation(true, true);
    CHECK(Mx::sauceOf(s)->enabled());
    CHECK(Mx::sauceOf(s, "machine_sauce_2")->enabled());
    CHECK_FALSE(Mx::sauceOf(s, "machine_drink")->enabled());
    CHECK(s->machineCount() == 2);
    CHECK(s->singleMachine() == nullptr);  // two enabled: the clicked one is found by its name
  }
  {
    Mx m;  // the drink machine takes the second sauce machine's place
    m.shop.upgrades = {"ksauce1", "ksauce3", "kdrink1"};
    auto s = m.makeSauceStation(true, true);
    CHECK(Mx::sauceOf(s)->enabled());
    CHECK_FALSE(Mx::sauceOf(s, "machine_sauce_2")->enabled());
    CHECK(Mx::sauceOf(s, "machine_drink")->enabled());
    CHECK(Mx::sauceOf(s, "machine_drink")->visible());
  }
  {
    Mx m;  // endless mode builds every machine (a bottle in each) and shows only the enabled
    m.shop.upgrades.clear();
    m.shop.endless = true;
    auto s = m.makeSauceStation(true, true);
    for (const char* key : {"machine_sauce", "machine_sauce_2", "machine_drink"}) {
      auto x = Mx::sauceOf(s, key);
      CHECK_FALSE(x->enabled());
      CHECK_FALSE(x->visible());
      CHECK(x->currentAsset() == "base");
      REQUIRE(x->bottle());
      CHECK(x->bottle()->visible());
    }
  }
}

TEST_CASE("VSauce: a full bottle stands in the machine from the start, copied from the style's bottle child") {
  Mx m;
  m.shop.upgrades = {"ksauce1", "kdrink1"};
  auto s = m.makeSauceStation(false, true);
  auto sauce = Mx::sauceOf(s);
  auto drink = Mx::sauceOf(s, "machine_drink");
  REQUIRE(sauce->bottle());
  CHECK(sauce->bottle()->type() == "sauce");
  CHECK(sauce->bottle()->hands() == 1);
  CHECK(sauce->bottle()->visible());
  CHECK(sauce->bottle()->leaf());
  CHECK(sauce->bottle()->parent() == sauce.get());
  CHECK(sauce->bottle()->anchorName() == "bottle");
  CHECK(sauce->bottle()->layer() == 4);
  CHECK(sauce->bottle()->currentAsset() == "base");
  CHECK(sauce->bottle() != sauce->childShared("bottle"));  // a copy; the template stays hidden
  CHECK_FALSE(sauce->childObject("bottle")->visible());
  REQUIRE(drink->bottle());
  CHECK(drink->bottle()->type() == "drink");
  CHECK(drink->bottle()->childObject("steam") != nullptr);  // the cup's steam child came along
  CHECK(sauce->cooldownMs() == 0);
  // the tank is full, the fill bar is away, the top is retracted
  CHECK(sauce->childObject("static_fill")->visible());
  CHECK(sauce->childObject("static_fill")->currentFrame() == 12);
  CHECK_FALSE(sauce->childObject("anim_fill")->visible());
  CHECK(sauce->childObject("static_top")->currentFrame() == 1);
  CHECK(drink->childObject("static_top")->currentFrame() == 28);
  CHECK_FALSE(sauce->childObject("anim_top")->visible());
  CHECK_FALSE(sauce->locked());
  CHECK(m.clean());
}

TEST_CASE("VSauce: a visit takes the bottle and starts the cooldown; the tank empties into the fill bar, which follows the time") {
  Mx m;
  auto p = m.makePlayer();
  m.shop.upgrades = {"ksauce1"};
  auto s = m.makeSauceStation();
  auto sauce = Mx::sauceOf(s);
  m.step(16);
  const auto bottle = sauce->bottle();
  m.arrive(s);
  CHECK(p->handA() == bottle);
  CHECK(sauce->bottle() == nullptr);
  CHECK(bottle->parent() != nullptr);  // on one of her arms
  CHECK(bottle->parent() != sauce.get());
  CHECK(sauce->cooldownMs() == 15000);
  CHECK(sauce->cooldownTotal() == 15000.0f);
  CHECK_FALSE(sauce->childObject("static_fill")->visible());
  CHECK(sauce->childObject("anim_fill")->visible());
  CHECK_FALSE(sauce->locked());  // taking a bottle does not lock the machine
  CHECK_FALSE(s->suspended());
  m.run(7500);
  CHECK(std::abs(sauce->cooldownMs() - 7500) <= 30);
  const int frame = sauce->childObject("anim_fill")->currentFrame();  // 32 frames, half the time gone
  CHECK(frame >= 15);
  CHECK(frame <= 17);
  // another visit has no bottle to take
  m.arrive(s);
  CHECK(p->handB() == nullptr);
  CHECK(sauce->cooldownMs() > 0);
  // the time counts in the sauce_cooldown value as it was at the pick-up
  CHECK(m.clean());
}

TEST_CASE("VSauce: full hands or a locked machine leave the bottle; the carried bottle counts as a sauce for the hands") {
  Mx m;
  auto p = m.makePlayer();
  m.shop.upgrades = {"ksauce1"};
  auto s = m.makeSauceStation();
  auto sauce = Mx::sauceOf(s);
  m.step(16);
  p->attachObject(bottleOf(m, "x"));
  p->attachObject(bottleOf(m, "y"));
  m.arrive(s);
  REQUIRE(sauce->bottle());  // no hand free
  CHECK(sauce->cooldownMs() == 0);
  p->detachObject(VPlayer::kHandB);
  m.arrive(s);
  CHECK(sauce->bottle() == nullptr);
  CHECK(p->handB() != nullptr);
  CHECK(p->handB()->type() == "sauce");
  CHECK(p->handA()->type() == "x");
  CHECK(sauce->cooldownMs() == 15000);
}

TEST_CASE("VSauce: the refill sequence takes 930, 960 and 930 ms, the bottle is there and unlocked at 1890 ms, the station is suspended until then") {
  Mx m;
  auto p = m.makePlayer();
  m.shop.upgrades = {"ksauce1"};
  m.world->values().setValue("sauce_cooldown", 1000, false);
  auto s = m.makeSauceStation();
  auto sauce = Mx::sauceOf(s);
  m.step(16);
  m.arrive(s);
  CHECK(sauce->cooldownMs() == 1000);
  CHECK(sauce->state() == 0);
  m.until([&] { return sauce->state() == 1; }, 2000);
  const double t0 = m.now;
  CHECK(sauce->locked());
  CHECK(sauce->processed());
  CHECK(sauce->suspended());
  CHECK(s->suspended());  // a visit now does nothing
  REQUIRE(sauce->bottle());
  CHECK_FALSE(sauce->bottle()->visible());  // the new bottle is hidden until the fill has run
  CHECK(sauce->cooldownMs() == 0);
  VObject* dispenser = sauce->childObject("anim_dispenser");
  VObject* top = sauce->childObject("anim_top");
  CHECK(dispenser->visible());
  CHECK(dispenser->animName() == "DoAnim");
  CHECK(top->visible());
  CHECK(top->animName() == "DoAnim");
  CHECK(sauce->childObject("static_fill")->visible());  // shown at frame 13 (the empty tank)
  CHECK(sauce->childObject("static_fill")->currentFrame() == 13);
  CHECK_FALSE(sauce->childObject("static_top")->visible());
  CHECK_FALSE(sauce->childObject("static_dispenser")->visible());
  // a visit while it works leaves everything as it is
  const auto first = sauce->bottle();
  const auto original = p->handA();
  m.arrive(s);
  CHECK(sauce->bottle() == first);
  CHECK(p->handA() == original);
  CHECK(p->handB() == nullptr);
  // the clink sounds once, when the dispenser passes its 23rd frame
  CHECK_FALSE(sauce->clinkPlayed());
  const int clinkAt = m.until([&] { return sauce->clinkPlayed(); }, 2000);
  CHECK(std::abs(clinkAt - 700) <= 60);
  auto at = [&](int expectState, int expectedMs) {
    m.until([&] { return sauce->state() == expectState; }, 4000);
    CHECK(std::abs(static_cast<int>(m.now - t0) - expectedMs) <= 40);
  };
  at(2, 930);
  CHECK_FALSE(dispenser->visible());
  CHECK(sauce->childObject("static_dispenser")->visible());
  CHECK_FALSE(top->visible());
  CHECK(sauce->childObject("static_top")->visible());
  CHECK(sauce->childObject("static_top")->currentFrame() == 2);
  CHECK(sauce->childObject("anim_fill")->visible());
  CHECK(sauce->childObject("anim_fill")->animName() == "Reverse");
  CHECK_FALSE(sauce->childObject("static_fill")->visible());
  CHECK(sauce->childObject("anim_spray")->visible());
  CHECK(sauce->childObject("anim_spray")->animName() == "DoSauceFill");
  CHECK(sauce->locked());
  CHECK(s->suspended());
  CHECK_FALSE(sauce->bottle()->visible());
  at(3, 1890);
  CHECK(sauce->bottle()->visible());
  CHECK_FALSE(sauce->locked());  // Unlock
  CHECK_FALSE(s->suspended());
  CHECK(sauce->childObject("static_fill")->visible());
  CHECK(sauce->childObject("static_fill")->currentFrame() == 12);
  CHECK_FALSE(sauce->childObject("anim_fill")->visible());
  CHECK_FALSE(sauce->childObject("anim_spray")->visible());
  CHECK_FALSE(sauce->childObject("static_top")->visible());
  CHECK(top->visible());
  CHECK(top->animName() == "Reverse");
  CHECK(sauce->suspended());  // still waiting for the top to go back up
  at(0, 2820);
  CHECK_FALSE(sauce->suspended());
  CHECK(sauce->processed());
  CHECK_FALSE(top->visible());
  CHECK(sauce->childObject("static_top")->visible());
  CHECK(sauce->childObject("static_top")->currentFrame() == 1);
  CHECK(sauce->childObject("static_fill")->visible());
  CHECK_FALSE(sauce->clinkPlayed());  // forgotten by the reset
  CHECK(sauce->bottle() == first);
  CHECK(first->visible());
  // the new bottle is picked up as the first was, the cooldown starts again
  m.arrive(s);
  CHECK(p->handB() == first);
  CHECK(sauce->cooldownMs() == 1000);
  CHECK(m.clean());
}

TEST_CASE("VSauce: the bottle can be taken at the unlock, while the top is still going up; the cooldown then keeps the tank's bar") {
  Mx m;
  auto p = m.makePlayer();
  m.shop.upgrades = {"ksauce1"};
  m.world->values().setValue("sauce_cooldown", 1000, false);
  auto s = m.makeSauceStation();
  auto sauce = Mx::sauceOf(s);
  m.step(16);
  m.arrive(s);
  m.until([&] { return sauce->state() == 3; }, 4000);
  CHECK_FALSE(sauce->locked());
  CHECK(sauce->suspended());
  const auto second = sauce->bottle();
  m.arrive(s);
  CHECK(p->handB() == second);
  CHECK(sauce->cooldownMs() == 1000);
  CHECK(sauce->state() == 3);  // the top is still on its way up
  m.until([&] { return sauce->state() == 0; }, 2000);
  // the reset at the end shows the fill bar, not the full tank, because a cooldown is running
  CHECK(sauce->cooldownMs() > 0);
  CHECK_FALSE(sauce->childObject("static_fill")->visible());
  CHECK(sauce->childObject("anim_fill")->visible());
  CHECK(sauce->bottle() == nullptr);
  CHECK(m.clean());
}

TEST_CASE("VSauce: a cooldown that ends while the machine is still finishing its sequence refills nothing (Refill is skipped while suspended)") {
  Mx m;
  auto p = m.makePlayer();
  m.shop.upgrades = {"ksauce1"};
  m.world->values().setValue("sauce_cooldown", 1000, false);
  auto s = m.makeSauceStation();
  auto sauce = Mx::sauceOf(s);
  m.step(16);
  m.arrive(s);
  m.until([&] { return sauce->state() == 3; }, 4000);
  m.world->values().setValue("sauce_cooldown", 100, false);
  m.arrive(s);  // takes the second bottle: 100 ms of cooldown
  CHECK(sauce->cooldownMs() == 100);
  m.until([&] { return sauce->state() == 0; }, 2000);
  m.run(300);
  CHECK(sauce->cooldownMs() == 0);
  CHECK(sauce->bottle() == nullptr);  // the cooldown ended while suspended: no new bottle
  CHECK_FALSE(sauce->locked());
  CHECK(sauce->childObject("static_fill")->visible());
  CHECK(sauce->state() == 0);
  CHECK(m.clean());
}

TEST_CASE("VSauce: a cooldown whose length changes mid-way rescales what is left, and the bar frame follows the new total") {
  Mx m;
  m.makePlayer();
  m.shop.upgrades = {"ksauce1"};
  auto s = m.makeSauceStation();
  auto sauce = Mx::sauceOf(s);
  m.step(16);
  m.arrive(s);
  m.run(5000);
  const int left = sauce->cooldownMs();
  CHECK(std::abs(left - 10000) <= 30);
  m.world->values().setValue("sauce_cooldown", 7500, false);  // kSauce2 bought: the value is halved
  m.step(4);
  CHECK(sauce->cooldownTotal() == 7500.0f);
  CHECK(std::abs(sauce->cooldownMs() - left / 2) <= 30);
  // a third of the new bar has run: 32 * (1 - 5000/7500)
  const int frame = sauce->childObject("anim_fill")->currentFrame();
  CHECK(frame >= 10);
  CHECK(frame <= 11);
  CHECK(m.clean());
}

TEST_CASE("VDrink: its own cooldown value, the cup's frames and spray, the steam loop; the sauce value does not touch it") {
  Mx m;
  auto p = m.makePlayer();
  m.shop.upgrades = {"ksauce1", "kdrink1"};
  m.world->values().setValue("drink_cooldown", 800, false);
  auto s = m.makeSauceStation(false, true);
  auto drink = Mx::sauceOf(s, "machine_drink");
  REQUIRE(drink);
  CHECK(s->machineCount() == 2);
  m.step(16);
  // two machines: the click names the one that works
  s->onMachineClick("machine_drink");
  const auto cup = drink->bottle();
  m.arrive(s);
  CHECK(p->handA() == cup);
  CHECK(cup->type() == "drink");
  CHECK(drink->cooldownMs() == 800);
  CHECK(drink->cooldownTotal() == 800.0f);
  m.world->values().setValue("sauce_cooldown", 5000, false);  // no effect on the drink
  m.step(4);
  CHECK(drink->cooldownTotal() == 800.0f);
  CHECK(Mx::sauceOf(s)->bottle() != nullptr);  // the sauce machine was not visited
  m.until([&] { return drink->state() == 1; }, 2000);
  const double t0 = m.now;
  CHECK(drink->childObject("static_top")->currentFrame() == 28);
  m.until([&] { return drink->state() == 2; }, 2000);
  CHECK(std::abs(static_cast<int>(m.now - t0) - 930) <= 40);
  CHECK(drink->childObject("static_top")->currentFrame() == 29);
  CHECK(drink->childObject("anim_spray")->animName() == "DoDrinkFill");
  VObject* steam = drink->childObject("anim_fill_steam");
  CHECK(steam->animName() == "DoDrinkSteam");
  CHECK_FALSE(steam->visible());  // the machine only starts it; Reset hid the `anim_` children and nothing shows this one
  m.until([&] { return drink->state() == 3; }, 2000);
  CHECK(std::abs(static_cast<int>(m.now - t0) - 1890) <= 40);
  m.until([&] { return drink->state() == 0; }, 2000);
  CHECK(std::abs(static_cast<int>(m.now - t0) - 2820) <= 40);
  CHECK(drink->childObject("static_top")->currentFrame() == 28);
  CHECK(drink->bottle());
  CHECK(m.clean());
}

TEST_CASE("VSauce: Reset in the middle of the refill stops the work and frees the station; the scene taken down mid-refill is harmless") {
  {
    Mx m;
    m.makePlayer();
    m.shop.upgrades = {"ksauce1"};
    m.world->values().setValue("sauce_cooldown", 200, false);
    auto s = m.makeSauceStation();
    auto sauce = Mx::sauceOf(s);
    m.step(16);
    m.arrive(s);
    m.until([&] { return sauce->state() == 2; }, 3000);
    CHECK(sauce->locked());
    CHECK(s->suspended());
    sauce->reset();
    CHECK(sauce->state() == 0);
    CHECK_FALSE(sauce->locked());
    CHECK_FALSE(sauce->suspended());
    CHECK_FALSE(s->suspended());
    m.step(2000);
    CHECK(sauce->state() == 0);  // nothing resumed it
    CHECK(m.clean());
  }
  {
    Mx m;
    m.makePlayer();
    m.shop.upgrades = {"ksauce1"};
    m.world->values().setValue("sauce_cooldown", 200, false);
    auto s = m.makeSauceStation();
    auto sauce = Mx::sauceOf(s);
    m.step(16);
    m.arrive(s);
    m.until([&] { return sauce->state() == 1; }, 3000);
    std::weak_ptr<VSauceDrink> weak = sauce;
    std::weak_ptr<VStation> weakStation = s;
    m.mgr.removeAllObjects();  // the level is loaded again in the middle of the refill
    m.shop.stations.clear();
    sauce.reset();
    s.reset();
    m.step(50);
    m.step(2000);
    CHECK(weak.expired());
    CHECK(weakStation.expired());
    CHECK(m.clean());
  }
}

// ================================================================ slice 12: the disposal

TEST_CASE("VDisposal: what she carries is thrown, hand 2 then hand 1, counted and written into the game state; the animation and the state run even with nothing") {
  Mx m;
  auto p = m.makePlayer();
  m.shop.upgrades = {"kdisposal"};
  auto s = m.makeDisposal();
  auto* d = dynamic_cast<VDisposal*>(s.get());
  REQUIRE(d);
  CHECK(d->visible());
  CHECK(d->currentAsset() == "base");
  CHECK(d->mouse());  // VDisposal::Init leaves the mouse flag as it is
  CHECK(d->clickable());
  m.step(16);
  VObject* active = d->childObject("active");
  REQUIRE(active);
  CHECK(active->animName().empty());
  // nothing in her hands: the machine runs, the counters stay, ++TrashUsed counts
  m.arrive(s);
  CHECK(active->animName() == "DoAnim");
  CHECK(m.world->gameStates().get("TrashUsed") == "1");
  CHECK(m.world->gameStates().get("TrashUsedItemID").empty());
  CHECK(m.world->values().getInt("num_disposed", 0) == 0);
  CHECK(m.world->values().getInt("disposed_this_level", 0) == 0);
  // a bar in slot A, a sauce in slot B
  auto bar = barOf(m, 0, 1);
  auto sauce = bottleOf(m, "sauce");
  p->attachObject(bar);
  p->attachObject(sauce);
  m.arrive(s);
  CHECK(p->numHandsFree() == 2);
  CHECK(bar->parent() == nullptr);
  CHECK(m.world->values().getInt("num_disposed", 0) == 2);
  CHECK(m.world->values().getInt("disposed_this_level", 0) == 2);
  CHECK(m.world->gameStates().get("TrashUsed") == "2");
  CHECK(m.world->gameStates().get("TrashUsedItemID") == "chocolate(dark,bar,topping:1),sauce");
  // another throw appends after a comma; "-1" counts as empty
  p->attachObject(bottleOf(m, "drink"));
  m.arrive(s);
  CHECK(m.world->gameStates().get("TrashUsedItemID") == "chocolate(dark,bar,topping:1),sauce,drink");
  m.world->gameStates().set("TrashUsedItemID", "-1");
  p->attachObject(bottleOf(m, "sauce"));
  m.arrive(s);
  CHECK(m.world->gameStates().get("TrashUsedItemID") == "sauce");
  // during a tutorial the lifetime count stays, the level count does not
  m.shop.tutorial = true;
  p->attachObject(bottleOf(m, "sauce"));
  m.arrive(s);
  CHECK(m.world->values().getInt("num_disposed", 0) == 4);
  CHECK(m.world->values().getInt("disposed_this_level", 0) == 5);
  // she turns to the point's facing at the end of the arrival
  CHECK(p->facing() == kFaceFront);
  CHECK(m.clean());
}

TEST_CASE("VDisposal: without kDisposal the station is hidden; the scene taken down in the middle of the animation is harmless") {
  {
    Mx m;
    m.shop.upgrades.clear();
    auto s = m.makeDisposal();
    CHECK_FALSE(s->visible());
    CHECK(s->currentAsset().empty());
  }
  Mx m;
  auto p = m.makePlayer();
  m.shop.upgrades = {"kdisposal"};
  auto s = m.makeDisposal();
  p->attachObject(bottleOf(m, "sauce"));
  m.step(16);
  m.arrive(s);
  std::weak_ptr<VStation> weak = s;
  m.mgr.removeAllObjects();
  m.shop.stations.clear();
  s.reset();
  m.step(500);
  CHECK(weak.expired());
  CHECK(m.clean());
}

// ================================================================ slice 12: the display case

TEST_CASE("VDisplay::init: shown by kDisplay1 with its base asset, hidden without; the capacity is 1, or 2 with kDisplay2") {
  {
    Mx m;
    m.shop.upgrades.clear();
    auto s = m.makeDisplay();
    auto* d = dynamic_cast<VDisplay*>(s.get());
    REQUIRE(d);
    CHECK_FALSE(d->visible());
    CHECK(d->currentAsset().empty());
    CHECK(d->capacity() == 1);
  }
  Mx m;
  m.shop.upgrades = {"kdisplay1"};
  auto s = m.makeDisplay();
  auto* d = dynamic_cast<VDisplay*>(s.get());
  CHECK(d->visible());
  CHECK(d->currentAsset() == "base");
  CHECK(d->capacity() == 1);
  CHECK(d->slots().size() == 1);
  CHECK(d->count() == 0);
  CHECK(s->stationType() == kStationDisplay);
  Mx n;
  n.shop.upgrades = {"kdisplay1", "kdisplay2"};
  auto s2 = n.makeDisplay();
  auto* d2 = dynamic_cast<VDisplay*>(s2.get());
  CHECK(d2->capacity() == 2);
  CHECK(d2->slots().size() == 2);
}

TEST_CASE("VDisplay: a chocolate put on the table is a child at the first anchor, clickable, glowing and registered with the vendor; a click and a visit take it back") {
  Mx m;
  auto p = m.makePlayer();
  m.shop.upgrades = {"kdisplay1"};
  auto s = m.makeDisplay();
  auto* d = dynamic_cast<VDisplay*>(s.get());
  m.step(16);
  auto bar = barOf(m, 1, 2);
  p->attachObject(bar);
  m.arrive(s);  // the table takes what she carries
  CHECK(d->count() == 1);
  CHECK(d->slots()[0] == bar);
  CHECK(p->numHandsFree() == 2);
  CHECK(bar->parent() == d);
  CHECK(bar->anchorName() == "chocolate1");
  CHECK_FALSE(bar->leaf());
  CHECK(bar->clickable());
  CHECK(bar->layer() == 1);  // the capacity
  CHECK(effectsOf<HoverGlow>(*bar) == 1);
  m.step(0);
  CHECK(bar->listenerCount() == 2);  // the table's and the hover glow's
  CHECK(m.vendor->displayInfluence().options(0) == std::set<int>{1});
  CHECK(m.vendor->displayInfluence().options(2) == std::set<int>{kShapeBar});
  CHECK(m.vendor->displayInfluence().options(4) == std::set<int>{2});
  CHECK(m.vendor->displayInfluence().options(1).empty());
  CHECK(m.world->gameStates().get("DisplayUsed") == "1");
  CHECK(d->picked() == nullptr);
  // the table is full: another visit with something in her hand changes nothing
  auto second = barOf(m, 0);
  p->attachObject(second);
  m.arrive(s);
  CHECK(d->count() == 1);
  CHECK(p->handA() == second);
  p->detachObject(VPlayer::kHandA);
  // a click on the table (one slot) picks what it holds; the visit gives it back
  d->onClick();
  CHECK(d->picked() == bar);
  CHECK(p->queue().size() == 1);  // her walk was queued
  m.arrive(s);
  CHECK(d->count() == 0);
  CHECK(d->slots()[0] == nullptr);
  CHECK(p->handA() == bar);
  CHECK(bar->parent() != d);
  CHECK(bar->parent() != nullptr);
  CHECK(bar->leaf());
  CHECK_FALSE(bar->clickable());
  CHECK(effectsOf<HoverGlow>(*bar) == 0);
  CHECK(d->picked() == nullptr);
  CHECK(m.vendor->displayInfluence().options(0).empty());
  CHECK(m.vendor->displayInfluence().options(4).empty());
  m.step(0);
  CHECK(bar->listenerCount() == 0);
  // a visit with empty hands and an empty table does nothing
  p->detachObject(VPlayer::kHandA);
  m.arrive(s);
  CHECK(d->count() == 0);
  CHECK(m.clean());
}

TEST_CASE("VDisplay: a pick she has no free hand for is not given, and does not let her put anything either") {
  Mx m;
  auto p = m.makePlayer();
  m.shop.upgrades = {"kdisplay1"};
  auto s = m.makeDisplay();
  auto* d = dynamic_cast<VDisplay*>(s.get());
  m.step(16);
  auto bar = barOf(m, 1);
  p->attachObject(bar);
  m.arrive(s);
  p->attachObject(barOf(m, 0));
  p->attachObject(barOf(m, 2));
  d->onClick();
  CHECK(d->picked() == bar);
  m.arrive(s);
  CHECK(d->count() == 1);  // the hands are full: nothing is taken
  CHECK(p->numHandsFree() == 0);
  CHECK(d->picked() == nullptr);  // the pick is forgotten
  CHECK(m.clean());
}

TEST_CASE("VDisplay: with kDisplay2 two things fit, each at its own anchor; a click on an item takes that one; a click on the table takes none") {
  Mx m;
  auto p = m.makePlayer();
  m.shop.upgrades = {"kdisplay1", "kdisplay2"};
  auto s = m.makeDisplay();
  auto* d = dynamic_cast<VDisplay*>(s.get());
  m.step(16);
  auto a = barOf(m, 0);
  auto b = barOf(m, 2, 0, kShapeTruffle);
  auto sauce = bottleOf(m, "sauce");
  p->attachObject(a);
  p->attachObject(b);
  m.arrive(s);  // the preferred hand first
  CHECK(d->count() == 1);
  m.arrive(s);
  CHECK(d->count() == 2);
  CHECK(p->numHandsFree() == 2);
  std::set<std::string> anchors{a->anchorName(), b->anchorName()};
  CHECK(anchors == std::set<std::string>{"chocolate1", "chocolate2"});
  CHECK(a->layer() == 2);  // the capacity
  CHECK(effectsOf<HoverGlow>(*a) == 1);
  CHECK(m.vendor->displayInfluence().options(0) == std::set<int>{0, 2});
  CHECK(m.vendor->displayInfluence().options(2) == std::set<int>{kShapeBar, kShapeTruffle});
  CHECK(m.world->gameStates().get("DisplayUsed") == "2");
  // full: a third thing stays in her hand
  p->attachObject(sauce);
  m.arrive(s);
  CHECK(d->count() == 2);
  CHECK(p->handA() == sauce);
  p->detachObject(VPlayer::kHandA);
  // a click on the table with two slots picks nothing; the visit with empty hands then does nothing
  d->onClick();
  CHECK(d->picked() == nullptr);
  m.arrive(s);
  CHECK(d->count() == 2);
  // a click on an item (its own listener) picks it and clicks the table
  m.step(0);
  const std::size_t queued = p->queue().size();
  b->doEvent(SpriteEvent::Click, {});
  CHECK(d->picked() == b);
  CHECK(p->queue().size() == queued + 1);
  m.arrive(s);
  CHECK(d->count() == 1);
  CHECK(p->handA() == b);
  CHECK(d->slots()[0] == a);
  CHECK(m.clean());
}

TEST_CASE("VDisplay: the influence sets are sets, not counts: two equal chocolates, one taken, and the option is gone from the sets (kept)") {
  Mx m;
  auto p = m.makePlayer();
  m.shop.upgrades = {"kdisplay1", "kdisplay2"};
  auto s = m.makeDisplay();
  auto* d = dynamic_cast<VDisplay*>(s.get());
  m.step(16);
  auto a = barOf(m, 1);
  auto b = barOf(m, 1);
  p->attachObject(a);
  p->attachObject(b);
  m.arrive(s);
  m.arrive(s);
  REQUIRE(d->count() == 2);
  CHECK(m.vendor->displayInfluence().options(0) == std::set<int>{1});
  m.step(0);
  a->doEvent(SpriteEvent::Click, {});
  m.arrive(s);
  CHECK(d->count() == 1);
  CHECK(d->slots()[1] == b);  // the other one is still on the table
  CHECK(m.vendor->displayInfluence().options(0).empty());
  CHECK(m.vendor->displayInfluence().options(2).empty());
  CHECK(m.vendor->getDisplayInfluence(0, 7) == 7);  // so orders no longer lean to it
  CHECK(m.clean());
}

TEST_CASE("VDisplay: a sauce can be put on the table too (it is not registered with the vendor), and a tutorial puts the item into the exclusive set") {
  Mx m;
  auto p = m.makePlayer();
  m.shop.upgrades = {"kdisplay1"};
  auto s = m.makeDisplay();
  auto* d = dynamic_cast<VDisplay*>(s.get());
  m.step(16);
  auto sauce = bottleOf(m, "sauce");
  p->attachObject(sauce);
  m.arrive(s);
  CHECK(d->slots()[0] == sauce);
  CHECK(m.vendor->displayInfluence().options(0).empty());
  CHECK(m.world->gameStates().get("DisplayUsed") == "1");
  CHECK(m.mgr.exclusiveCount() == 0);
  d->onClick();
  m.arrive(s);
  CHECK(p->handA() == sauce);
  m.shop.tutorial = true;
  m.arrive(s);  // she puts it back during a tutorial
  CHECK(d->count() == 1);
  CHECK(m.mgr.exclusiveCount() == 1);
  CHECK(m.clean());
}

TEST_CASE("VDisplay: a chocolate that decays on the table is taken off it, unless the game is softly paused; loading again empties the table and the vendor's sets") {
  Mx m;
  auto p = m.makePlayer();
  m.shop.upgrades = {"kdisplay1"};
  auto s = m.makeDisplay();
  auto* d = dynamic_cast<VDisplay*>(s.get());
  m.step(16);
  m.world->values().setValue("chocolate_decay_rate", 100.0 / 500.0, false);  // 500 ms of life, then the 400 ms fade
  // one that decays on the table goes by itself
  auto bar = barOf(m, 1);
  p->attachObject(bar);
  m.arrive(s);
  REQUIRE(d->count() == 1);
  CHECK(m.vendor->displayInfluence().options(0) == std::set<int>{1});
  m.run(1200);
  CHECK(d->count() == 0);
  CHECK(bar->parent() == nullptr);
  CHECK(m.vendor->displayInfluence().options(0).empty());
  CHECK(m.world->values().getInt("decayed_this_level", 0) == 1);
  // one that decayed in her hand is put on the table, and a soft pause leaves it there
  auto old = barOf(m, 2);
  p->attachObject(old);
  m.run(1200);
  REQUIRE(old->hasDecayed());
  p->detachObject(VPlayer::kHandA);
  d->attachCarriable(old);
  REQUIRE(d->count() == 1);
  d->update(true);
  CHECK(d->count() == 1);
  d->update(false);
  CHECK(d->count() == 0);
  CHECK(old->parent() == nullptr);
  CHECK(m.vendor->displayInfluence().options(0).empty());
  // loading the table again: the children and the vendor's sets go
  auto other = barOf(m, 2);
  p->attachObject(other);
  m.arrive(s);
  REQUIRE(d->count() == 1);
  CHECK(m.vendor->displayInfluence().options(0) == std::set<int>{2});
  auto t = displayTable();
  const StyleChain sc(*t, nullptr);
  s->loadFromTable(sc);
  CHECK(d->count() == 0);
  CHECK(other->parent() == nullptr);
  CHECK(m.vendor->displayInfluence().options(0).empty());
  CHECK(m.clean());
}

TEST_CASE("VDisplay: the level taken down while an item is on the table, and the table's listener outliving it, are harmless") {
  Mx m;
  auto p = m.makePlayer();
  m.shop.upgrades = {"kdisplay1"};
  auto s = m.makeDisplay();
  m.step(16);
  auto bar = barOf(m, 1);
  p->attachObject(bar);
  m.arrive(s);
  m.step(0);
  std::weak_ptr<VStation> weak = s;
  m.mgr.removeAllObjects();
  m.shop.stations.clear();
  s.reset();
  m.step(50);
  CHECK(weak.expired());
  bar->doEvent(SpriteEvent::Click, {});  // the listener finds the table gone and does nothing
  m.step(50);
  CHECK(m.clean());
}
