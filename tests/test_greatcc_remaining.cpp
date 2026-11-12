// The Great Chocolate Chase, slice 15 (docs/design/greatcc.md section 6): the ingredient, topping and infusion machines, the delivery
// station with its zeppelin, the conveyor belt, and the chocolate's decay on the belt. Synthetic fixtures only (the machines' and the
// customers' harness with the sheets of these machines).
#include "GreatccCustomerFixture.h"

#include "games/greatcc/shop/BZeppelin.h"
#include "games/greatcc/shop/VConveyor.h"
#include "games/greatcc/shop/VDelivery.h"
#include "games/greatcc/shop/VIngredient.h"
#include "games/greatcc/shop/VInfuse.h"
#include "games/greatcc/shop/VTopping.h"

namespace {

std::shared_ptr<VChocolate> bucketOf(Mx& m, int mix) { return m.vendor->getEmptyChocolate(mix, true); }
// A shaped bar of that mix (no bucket), as the press makes it.
std::shared_ptr<VChocolate> barOf(Mx& m, int mix, bool decays = false) {
  auto c = m.vendor->getEmptyChocolate(mix, decays);
  c->addLayer(kShapeBar, kLayerChocolate, mix);
  return c;
}
void settle(Mx& m, int ms = 3000) { m.run(ms); }

// What the shipped values.xml gives the delivery.
void deliveryValues(Mx& m, int interval = 30000, int variance = 5000) {
  m.world->values().setValue("delivery_interval", interval, false);
  m.world->values().setValue("delivery_variance", variance, false);
  m.world->values().setValue("zeppelin_served_this_level", 0, false);
  m.world->values().setValue("num_deliveries", 0, false);
}

OrderInfo barOrder(int mix = 0) {
  OrderInfo o;
  o.type = kOrderChocolate;
  o.shape = kShapeBar;
  o.mix = mix;
  o.layerBudget = 1;
  return o;
}

}  // namespace

// ================================================================ ingredient

TEST_CASE("VIngredient::init: the buttons follow kIngredient<i>, the tanks show the art of the enabled levels, a machine with none is hidden") {
  Mx m;
  m.shop.upgrades = {"kingredient1", "kingredient2", "kingredientq2"};
  auto s = m.makeIngredient();
  auto ing = Mx::machineOf<VIngredient>(s);
  REQUIRE(ing);
  CHECK(ing->enabled());
  CHECK(ing->visible());
  REQUIRE(ing->buttons().size() == 3);
  CHECK(ing->buttons().at("button1")->visible());
  CHECK(ing->buttons().at("button2")->visible());
  CHECK_FALSE(ing->buttons().at("button3")->visible());
  CHECK(ing->buttons().at("button2")->upgraded());
  CHECK_FALSE(ing->buttons().at("button1")->upgraded());
  // the lit tanks take the art of the highest enabled level
  CHECK(ing->childObject("static_top1")->currentState() == "upgrade2");
  CHECK(ing->childObject("anim_top2")->currentState() == "upgrade2");
  CHECK(s->singleMachine() == ing);
  CHECK(s->mouse());
  // no upgrade: hidden and never built; the station is no mouse target
  Mx n;
  n.shop.upgrades.clear();
  auto idle = Mx::machineOf<VIngredient>(n.makeIngredient());
  CHECK_FALSE(idle->enabled());
  CHECK_FALSE(idle->visible());
  CHECK(idle->currentAsset().empty());
  CHECK(m.clean());
}

TEST_CASE("VIngredient: the sequence runs 660, 1660 (unlock) and 2320 ms; the filling fades in on the barrel; the station is suspended to the unlock") {
  Mx m;
  auto p = m.makePlayer();
  m.shop.who.reset();
  m.shop.upgrades = {"kingredient1"};
  auto s = m.makeIngredient();
  auto ing = Mx::machineOf<VIngredient>(s);
  m.step(16);
  auto bucket = bucketOf(m, 0);
  REQUIRE(p->attachObject(bucket));
  ing->chooseOption(0);
  m.arrive(s);
  const double t0 = m.now;
  CHECK(ing->state() == 1);
  CHECK(ing->locked());
  CHECK(ing->processed());
  CHECK(ing->option() == -1);
  CHECK(ing->suspended());
  CHECK(s->suspended());
  CHECK(ing->chocolate() == bucket);
  CHECK(p->handA() == nullptr);
  CHECK(bucket->layer() == 0);  // the swap path puts it at sprite layer 0
  CHECK(bucket->ingredientId() == 0);
  CHECK(bucket->toString() == "(dark,ingredient:0)");
  REQUIRE(ing->overlay());
  CHECK_FALSE(ing->overlay()->visible());
  for (const char* k : {"anim_intro", "anim_top1", "anim_top2"}) {
    CHECK(ing->childObject(k)->visible());
    CHECK(ing->childObject(k)->animName() == "DoAnim");
  }
  m.until([&] { return ing->state() == 2; });
  CHECK(std::abs(static_cast<int>(m.now - t0) - 660) <= 25);
  CHECK(ing->overlay()->visible());
  CHECK(effectsOf<AlphaEffect>(*ing->overlay()) == 1);
  CHECK_FALSE(ing->childObject("anim_intro")->visible());
  CHECK(ing->childObject("anim_pump")->visible());
  CHECK(ing->childObject("anim_pump")->animName() == "DoAnim");
  CHECK(ing->locked());
  m.until([&] { return ing->state() == 3; });
  CHECK(std::abs(static_cast<int>(m.now - t0) - 1660) <= 25);  // 660 + the 1 s pump
  CHECK_FALSE(ing->locked());  // Unlock at the start of the third step
  CHECK_FALSE(s->suspended());
  CHECK(ing->childObject("anim_intro")->animName() == "Reverse");
  CHECK_FALSE(ing->childObject("anim_pump")->visible());
  m.until([&] { return ing->state() == 0; });
  CHECK(std::abs(static_cast<int>(m.now - t0) - 2320) <= 30);
  CHECK(ing->processed());
  CHECK(ing->chocolate() == bucket);
  CHECK(ing->overlay() == nullptr);
  // the output is picked up by a visit
  m.arrive(s);
  CHECK(p->handA() == bucket);
  CHECK(ing->chocolate() == nullptr);
  CHECK(m.clean());
}

TEST_CASE("VIngredient: a bucket in hand goes into the empty machine with no choice; a held bucket is handed back; full hands change nothing") {
  Mx m;
  auto p = m.makePlayer();
  m.shop.who.reset();
  m.shop.upgrades = {"kingredient1"};
  auto s = m.makeIngredient();
  auto ing = Mx::machineOf<VIngredient>(s);
  m.step(16);
  auto bucket = bucketOf(m, 1);
  REQUIRE(p->attachObject(bucket));
  m.arrive(s);  // no choice: the empty machine takes the chocolate that takes a filling
  CHECK(ing->chocolate() == bucket);
  CHECK(ing->state() == 0);
  CHECK(p->handA() == nullptr);
  CHECK(bucket->layer() == 1);
  CHECK(ing->option() == -1);
  // a visit with a free hand and no choice gives the bucket back
  m.arrive(s);
  CHECK(p->handA() == bucket);
  CHECK(ing->chocolate() == nullptr);
  // the machine holds one, both hands are full: nothing happens (and the choice is dropped)
  m.arrive(s);  // takes it again
  REQUIRE(ing->chocolate() == bucket);
  auto a = barOf(m, 0), b = barOf(m, 1);
  REQUIRE(p->attachObject(a));
  REQUIRE(p->attachObject(b));
  ing->chooseOption(2);
  m.arrive(s);  // a choice, a bucket in the machine and no chocolate in hand that takes a filling: the choice is dropped
  CHECK(ing->option() == -1);
  CHECK(ing->chocolate() == bucket);
  CHECK(p->handA() == a);
  CHECK(p->handB() == b);
  m.arrive(s);
  CHECK(ing->chocolate() == bucket);  // no hand free: the bucket stays
  CHECK(m.clean());
}

TEST_CASE("VIngredient: a choice made with an unfitting bucket in the machine swaps it for the one in hand and runs") {
  Mx m;
  auto p = m.makePlayer();
  m.shop.who.reset();
  m.shop.upgrades = {"kingredient1"};
  auto s = m.makeIngredient();
  auto ing = Mx::machineOf<VIngredient>(s);
  m.step(16);
  // the machine holds a bucket that has its filling already (a second visit would not take another)
  auto first = bucketOf(m, 0);
  REQUIRE(p->attachObject(first));
  ing->chooseOption(0);
  m.arrive(s);
  settle(m, 2500);
  REQUIRE(ing->chocolate() == first);
  REQUIRE(ing->processed());
  auto second = bucketOf(m, 2);
  REQUIRE(p->attachObject(second));
  ing->chooseOption(1);
  m.arrive(s);  // the finished one goes to the free hand, the bare one goes in and is processed
  CHECK(ing->chocolate() == second);
  CHECK(second->ingredientId() == 1);
  CHECK(ing->state() == 1);
  CHECK((p->handA() == first || p->handB() == first));
  CHECK(m.clean());
}

TEST_CASE("VMachine::getWhichHand: the hand whose chocolate takes the layer; with two, the order a counter customer waits for decides; a third answer is -1") {
  Cx m;
  auto p = m.makePlayer();
  m.shop.who.reset();
  m.shop.upgrades.insert("kingredient1");
  auto s = m.makeIngredient();
  auto ing = Mx::machineOf<VIngredient>(s);
  auto counter = m.makeCounter(1);
  m.step(16);
  auto bucketA = bucketOf(m, 0);
  auto bucketB = bucketOf(m, 1);
  auto bar = barOf(m, 0);
  // nothing in her hands
  CHECK(ing->getWhichHand(*p, kLayerIngredient) == -1);
  REQUIRE(p->attachObject(bar));
  CHECK(ing->getWhichHand(*p, kLayerIngredient) == -1);  // a bar takes no filling
  REQUIRE(p->attachObject(bucketA));
  CHECK(ing->getWhichHand(*p, kLayerIngredient) == 1);  // only the bucket in hand B does
  p->detachObject(1);
  p->detachObject(2);
  REQUIRE(p->attachObject(bucketA));
  REQUIRE(p->attachObject(bucketB));
  // both take it, and the customer's order cannot tell them apart (a bucket never "heads toward" an order, which has no bucket layer):
  // the preferred hand's chocolate is the answer
  OrderInfo want = barOrder(0);
  want.layer[kLayerIngredient] = 0;
  m.comeIn(counter, "professor", {want});
  auto cust = counter->customer();
  m.visit(counter);
  m.finishReading(cust);
  REQUIRE(cust->wanted());
  ing->chooseOption(0);
  const int preferred = p->preferredHand();
  CHECK(ing->getWhichHand(*p, kLayerIngredient) == preferred);
  CHECK(m.clean());
}

TEST_CASE("VMachine::getWhichHand: with two toppable bars the one the customer's order is heading toward is chosen, from either hand") {
  Cx m;
  auto p = m.makePlayer();
  m.shop.who.reset();
  m.shop.upgrades.insert("ktopping1");
  auto s = m.makeTopping();
  auto top = Mx::machineOf<VTopping>(s);
  auto counter = m.makeCounter(1);
  m.step(16);
  OrderInfo want = barOrder(0);  // a dark bar with topping 1
  want.layer[kLayerTopping] = 1;
  m.comeIn(counter, "professor", {want});
  auto cust = counter->customer();
  m.visit(counter);
  m.finishReading(cust);
  REQUIRE(cust->wanted());
  auto milk = barOf(m, 1), dark = barOf(m, 0);
  REQUIRE(p->attachObject(milk));  // hand A
  REQUIRE(p->attachObject(dark));  // hand B
  top->chooseOption(1);
  CHECK(top->getWhichHand(*p, kLayerTopping) == VPlayer::kHandB);  // the dark one, though it is not the first
  p->detachObject(2);
  p->detachObject(1);
  REQUIRE(p->attachObject(dark));  // hand A
  REQUIRE(p->attachObject(milk));  // hand B
  CHECK(top->getWhichHand(*p, kLayerTopping) == VPlayer::kHandA);
  // neither heads toward the order (a different topping asked for): the answer falls to the preferred hand's... (FindOrderWithLayer
  // gives c0, which is one of the two)
  top->chooseOption(2);
  CHECK(top->getWhichHand(*p, kLayerTopping) != -1);
  CHECK(m.clean());
}

TEST_CASE("VIngredient: an empty machine visited with no choice and no fitting chocolate takes nothing, and the preferred hand turns, fixes on or off") {
  for (const bool fix : {true, false}) {
    Mx m;
    m.world->settings().fixBugs = fix;
    auto p = m.makePlayer();
    m.shop.who.reset();
    m.shop.upgrades = {"kingredient1"};
    auto s = m.makeIngredient();
    auto ing = Mx::machineOf<VIngredient>(s);
    m.step(16);
    auto sauce = VCarriable::create(m.ctx, 1);
    sauce->setType("sauce");
    auto bar = barOf(m, 0);
    REQUIRE(p->attachObject(bar));    // hand A
    REQUIRE(p->attachObject(sauce));  // hand B
    const int before = p->preferredHand();
    m.arrive(s);
    CHECK(ing->chocolate() == nullptr);
    CHECK(p->handA() == bar);
    CHECK(p->handB() == sauce);
    CHECK(p->preferredHand() != before);
    CHECK_FALSE(m.log.contains("[fix] F6"));
  }
}

// ================================================================ topping


TEST_CASE("VTopping: a shaped chocolate and a choice start it; the layer fades in over 810 ms, the particles fade out, and it is done at 900 ms") {
  Mx m;
  auto p = m.makePlayer();
  m.shop.who.reset();
  m.shop.upgrades = {"ktopping1", "ktopping2"};
  auto s = m.makeTopping();
  auto top = Mx::machineOf<VTopping>(s);
  REQUIRE(top);
  CHECK(top->enabled());
  CHECK(top->buttons().at("button2")->visible());
  CHECK_FALSE(top->buttons().at("button3")->visible());
  m.step(16);
  auto bar = barOf(m, 1);
  REQUIRE(p->attachObject(bar));
  top->chooseOption(1);
  m.arrive(s);
  const double t0 = m.now;
  CHECK(top->state() == 1);
  CHECK(top->locked());
  CHECK(top->chocolate() == bar);
  CHECK(bar->toString() == "(milk,bar,topping:1)");
  REQUIRE(top->toppingLayer());
  CHECK(effectsOf<AlphaEffect>(*top->toppingLayer()) == 1);  // fading in
  CHECK(top->childObject("anim_funnel")->animName() == "DoAnim");
  CHECK(top->childObject("anim_particles")->animName() == "DoParticles");
  CHECK(near(top->childObject("anim_particles")->alpha(), 1.0f));
  CHECK(top->suspended());
  CHECK(s->suspended());
  m.until([&] { return top->state() == 2; });
  CHECK(std::abs(static_cast<int>(m.now - t0) - 600) <= 25);  // 30 particle frames of 20 ms
  CHECK(effectsOf<AlphaEffect>(*top->childObject("anim_particles")) == 1);  // fading out
  m.until([&] { return top->state() == 0; });
  CHECK(std::abs(static_cast<int>(m.now - t0) - 900) <= 30);
  CHECK(top->processed());
  CHECK_FALSE(top->locked());
  CHECK(top->toppingLayer() == nullptr);
  m.arrive(s);
  CHECK(p->handA() == bar);
  CHECK(m.clean());
}

TEST_CASE("VTopping: a bucket takes no topping; with no choice a bar in hand goes into the empty machine; a topped bar takes no second topping") {
  Mx m;
  auto p = m.makePlayer();
  m.shop.who.reset();
  m.shop.upgrades = {"ktopping1"};
  auto s = m.makeTopping();
  auto top = Mx::machineOf<VTopping>(s);
  m.step(16);
  auto bucket = bucketOf(m, 0);
  REQUIRE(p->attachObject(bucket));
  top->chooseOption(0);
  m.arrive(s);
  CHECK(top->state() == 0);  // no chocolate in hand takes a topping: the choice is dropped
  CHECK(top->option() == -1);
  CHECK(top->chocolate() == nullptr);
  p->detachObject(2);
  auto bar = barOf(m, 0);
  REQUIRE(p->attachObject(bar));
  m.arrive(s);  // no choice: the bar goes in
  CHECK(top->chocolate() == bar);
  CHECK(bar->layer() == 1);
  CHECK(bar->toString() == "(dark,bar)");
  top->chooseOption(0);
  m.arrive(s);  // the choice applies to what the machine holds
  CHECK(top->state() == 1);
  CHECK(bar->toString() == "(dark,bar,topping:0)");
  settle(m, 1200);
  top->chooseOption(0);
  m.arrive(s);  // already topped: nothing fits, the choice is dropped (and nothing is handed back on this visit)
  CHECK(top->state() == 0);
  CHECK(top->option() == -1);
  CHECK(top->chocolate() == bar);
  m.arrive(s);  // with no choice the output is handed over
  CHECK(p->handA() == bar);
  CHECK(top->chocolate() == nullptr);
  CHECK(m.clean());
}

// ================================================================ infusion

TEST_CASE("VInfuse: a bucket and a choice start it; the fill drops, the bar unlocks at 1050 ms, the end is at 1230 ms and the infusions are counted") {
  Mx m;
  auto p = m.makePlayer();
  m.shop.who.reset();
  m.shop.upgrades = {"kinfusion1", "kinfusion3"};
  auto s = m.makeInfuse();
  auto inf = Mx::machineOf<VInfuse>(s);
  REQUIRE(inf);
  CHECK(inf->enabled());
  CHECK(inf->buttons().at("button1")->visible());
  CHECK_FALSE(inf->buttons().at("button2")->visible());
  CHECK(inf->buttons().at("button3")->visible());
  m.world->values().setValue("num_infusions", 0, false);
  m.step(16);
  CHECK(inf->currentFrame() == 1);
  auto bucket = bucketOf(m, 0);
  REQUIRE(p->attachObject(bucket));
  inf->chooseOption(2);
  m.arrive(s);
  const double t0 = m.now;
  CHECK(inf->state() == 1);
  CHECK(inf->locked());
  CHECK(inf->suspended());
  CHECK(s->suspended());
  CHECK(inf->chocolate() == bucket);
  CHECK(bucket->toString() == "(dark,infusion,filling:2)");
  CHECK_FALSE(bucket->visible());  // hidden until the unlock
  CHECK(bucket->parent() == inf->childObject("anim_top_working"));
  CHECK(bucket->anchorName() == "chocolate");
  REQUIRE(inf->fillLayer());
  CHECK(inf->fillLayer()->layer() == 2);
  CHECK(inf->fillLayer()->anchorName() == "bucket");
  CHECK(effectsOf<WipeEffect>(*inf->fillLayer()->childObject("icon")) == 1);
  CHECK(inf->childObject("static_base")->visible());
  CHECK(inf->childObject("static_top")->visible());
  CHECK(m.world->values().getInt("chocolates_made_this_level", 0) == 1);
  m.until([&] { return inf->state() == 2; });
  CHECK(std::abs(static_cast<int>(m.now - t0) - 300) <= 25);
  CHECK(inf->currentFrame() == 0);
  CHECK_FALSE(inf->childObject("static_base")->visible());
  CHECK_FALSE(inf->childObject("static_top")->visible());
  CHECK(inf->childObject("anim_top_working")->animName() == "DoAnim");
  CHECK(inf->childObject("anim_door")->animName() == "DoDoor");
  CHECK(inf->childObject("anim_base")->animName() == "DoAnim");
  // drop() at 300 + 7 x 20 = 440 ms: the fill falls and wipes
  m.until([&] { return effectsOf<SpriteMove>(*inf->fillLayer()) > 0; });
  CHECK(std::abs(static_cast<int>(m.now - t0) - 440) <= 25);
  // unlock() at 300 + 25 x 30 = 1050 ms: the bar shows and can be taken
  CHECK(inf->locked());
  m.until([&] { return !inf->locked(); });
  CHECK(std::abs(static_cast<int>(m.now - t0) - 1050) <= 25);
  CHECK(bucket->visible());
  CHECK_FALSE(s->suspended());
  m.until([&] { return inf->state() == 0; });
  CHECK(std::abs(static_cast<int>(m.now - t0) - 1230) <= 30);
  CHECK(m.world->values().getInt("num_infusions", 0) == 1);
  CHECK(bucket->parent() == inf.get());  // moved back under the machine
  CHECK(inf->fillLayer() == nullptr);
  CHECK(inf->currentFrame() == 1);
  CHECK(inf->processed());
  m.arrive(s);
  CHECK(p->handA() == bucket);
  CHECK(inf->chocolate() == nullptr);
  CHECK(m.clean());
}

TEST_CASE("VInfuse: a bucket with a filling is refused; a new bucket swaps with the finished bar") {
  Mx m;
  auto p = m.makePlayer();
  m.shop.who.reset();
  m.shop.upgrades = {"kinfusion1"};
  auto s = m.makeInfuse();
  auto inf = Mx::machineOf<VInfuse>(s);
  m.step(16);
  auto filled = bucketOf(m, 0);
  filled->addIngredient(0);
  REQUIRE(p->attachObject(filled));
  inf->chooseOption(0);
  m.arrive(s);
  CHECK(inf->state() == 0);
  CHECK(inf->option() == -1);
  CHECK(inf->chocolate() == nullptr);
  CHECK(p->handA() == filled);  // still hers
  p->detachObject(2);
  // a first run, then a second bucket arrives while the bar waits
  auto one = bucketOf(m, 1);
  REQUIRE(p->attachObject(one));
  inf->chooseOption(0);
  m.arrive(s);
  settle(m, 1500);
  REQUIRE(inf->chocolate() == one);
  REQUIRE(inf->processed());
  auto two = bucketOf(m, 2);
  REQUIRE(p->attachObject(two));
  inf->chooseOption(1);
  m.arrive(s);
  CHECK(inf->chocolate() == two);
  CHECK(two->toString() == "(white,infusion,filling:1)");
  CHECK(p->handA() == one);  // the finished bar took the hand the bucket left
  CHECK(inf->state() == 1);
  CHECK(m.clean());
}

// ================================================================ the values the delivery reads

TEST_CASE("ValueManager::getSum: the value plus the delta in single precision, held to its limits only when asked") {
  Mx m;
  m.world->values().load("<values><value name='lim' value='10' min='5' max='12'/><value name='free' value='7'/></values>");
  ValueManager& v = m.world->values();
  CHECK(v.getSum("lim", 1.5f, true) == 11.5f);
  CHECK(v.getSum("lim", 9.0f, true) == 12.0f);
  CHECK(v.getSum("lim", 9.0f, false) == 19.0f);
  CHECK(v.getSum("lim", -20.0f, true) == 5.0f);
  CHECK(v.getSum("free", -2.5f, true) == 4.5f);
  CHECK(v.getSum("free", 100.0f, true) == 107.0f);
}

// ================================================================ delivery

TEST_CASE("VDelivery::init: kDelivery1 enables it, a story level's zeppelin orders are copied, the airship joins the shop's scene, endless takes none") {
  Mx m;
  deliveryValues(m);
  m.shop.upgrades = {"kdelivery1"};
  m.shop.haveZeppelin = true;
  m.shop.zeppelin = {barOrder(0), barOrder(1)};
  auto s = m.makeDelivery();
  auto d = Mx::machineOf<VDelivery>(s);
  REQUIRE(d);
  CHECK(d->enabled());
  CHECK(d->visible());
  CHECK(d->orders().size() == 2);
  REQUIRE(d->zeppelin());
  REQUIRE(m.shop.sceneSprites.size() == 1);
  CHECK(m.shop.sceneSprites[0] == d->zeppelin());
  CHECK(d->zeppelin()->depth() == 3);
  CHECK_FALSE(d->zeppelin()->visible());
  CHECK(d->zeppelin()->state() == BZeppelin::kIdle);
  REQUIRE(d->bubble());
  CHECK(d->bubble()->parent() == d.get());
  CHECK(d->bubble()->anchorName() == "thought");
  CHECK(d->bubble()->hflip());
  CHECK(d->deliveryState() == VDelivery::kWaitingForOrder);
  // the first interval: 30000 plus a draw in [-2500, 2500)
  CHECK(d->timerMs() >= 27500);
  CHECK(d->timerMs() < 32500);
  // without kDelivery1: hidden, never built
  Mx n;
  n.shop.upgrades.clear();
  auto idle = Mx::machineOf<VDelivery>(n.makeDelivery());
  CHECK_FALSE(idle->enabled());
  CHECK_FALSE(idle->visible());
  CHECK(idle->timerMs() == 0);
  // endless mode: built, no scripted orders
  Mx e;
  e.shop.endless = true;
  e.shop.upgrades = {"kdelivery1"};
  e.shop.haveZeppelin = true;
  e.shop.zeppelin = {barOrder(0)};
  auto endless = Mx::machineOf<VDelivery>(e.makeDelivery());
  CHECK(endless->orders().empty());
  CHECK(m.clean());
}

TEST_CASE("VDelivery: Reset draws the next interval from the values; the variance is a signed half either way") {
  Mx m;
  deliveryValues(m, 30000, 5000);
  m.shop.upgrades = {"kdelivery1"};
  auto d = Mx::machineOf<VDelivery>(m.makeDelivery());
  int lo = 1 << 30, hi = 0;
  for (int i = 0; i < 400; ++i) {
    d->reset();
    lo = std::min(lo, d->timerMs());
    hi = std::max(hi, d->timerMs());
    CHECK(d->timerMs() >= 27500);
    CHECK(d->timerMs() < 32500);
  }
  CHECK(lo < 28000);
  CHECK(hi > 32000);
  // a variance of 0 draws nothing
  deliveryValues(m, 15000, 0);
  d->reset();
  CHECK(d->timerMs() == 15000);
  CHECK(d->patience() == 100.0f);
  CHECK(d->mood() == 1);
  CHECK(d->deliveryState() == VDelivery::kWaitingForOrder);
  CHECK(d->currentFrame() == 1);
}

TEST_CASE("VDelivery: the timer counts down in game time only while waiting; at zero the order shows in the bubble with mood 1; a finished level shows none") {
  Mx m;
  deliveryValues(m);
  m.shop.upgrades = {"kdelivery1"};
  m.shop.haveZeppelin = true;
  m.shop.zeppelin = {barOrder(2), barOrder(0)};
  auto d = Mx::machineOf<VDelivery>(m.makeDelivery());
  m.step(16);
  d->setTimerMs(400);
  // a soft pause stops the countdown
  m.mgr.softPause(true);
  m.run(600);
  CHECK(d->timerMs() == 400);
  CHECK(d->deliveryState() == VDelivery::kWaitingForOrder);
  m.mgr.softPause(false);
  m.step(16);
  m.until([&] { return d->deliveryState() == VDelivery::kOrderShowing; });
  CHECK(d->deliveryState() == VDelivery::kOrderShowing);
  CHECK(d->timerMs() == 0);
  REQUIRE(d->wanted());
  CHECK(d->wanted()->compare(barOf(m, 2).get()));  // the first scripted order, a white bar
  CHECK(d->orders().size() == 1);                   // it was taken off the list
  CHECK(d->bubble()->visible());
  CHECK(d->bubble()->bubbleCount() == 1);
  CHECK(d->mood() == 1);
  CHECK(d->wanted()->hflip());
  CHECK(m.shop.zeppelinWaits.back());  // the level may not end now
  // the second order is the last scripted one; then the list is used up and a random chocolate comes
  d->reset();
  d->setTimerMs(10);
  m.until([&] { return d->deliveryState() == VDelivery::kOrderShowing; });
  REQUIRE(d->wanted());
  CHECK(d->wanted()->compare(barOf(m, 0).get()));
  CHECK(d->orders().empty());
  d->reset();
  d->setTimerMs(10);
  m.until([&] { return d->deliveryState() == VDelivery::kOrderShowing; });
  REQUIRE(d->wanted());
  // a finished level shows none: the machine only resets
  m.shop.levelDone = true;
  d->reset();
  d->setTimerMs(10);
  m.run(100);
  CHECK(d->deliveryState() == VDelivery::kWaitingForOrder);
  CHECK(d->wanted() == nullptr);
  CHECK(d->timerMs() > 20000);
  CHECK(m.clean());
}

TEST_CASE("VDelivery: the order's patience drops a point a second; the mood steps at 80, 60, 40 and 20; at 0 the bubble goes and the machine resets") {
  Mx m;
  deliveryValues(m);
  m.shop.upgrades = {"kdelivery1"};
  m.shop.haveZeppelin = true;
  m.shop.zeppelin = {barOrder(0)};
  auto d = Mx::machineOf<VDelivery>(m.makeDelivery());
  m.step(16);
  d->setTimerMs(10);
  m.until([&] { return d->deliveryState() == VDelivery::kOrderShowing; });
  const float p0 = d->patience();
  CHECK(p0 > 99.0f);
  m.run(5000);
  CHECK(near(d->patience(), p0 - 5.0f, 0.15f));
  CHECK(d->mood() == 1);
  struct Step {
    float patience;
    int mood;
  };
  for (const Step step : {Step{79.5f, 2}, Step{59.5f, 3}, Step{39.5f, 4}, Step{19.5f, 5}}) {
    d->setPatience(step.patience + 0.05f);
    m.run(100);
    CHECK(d->mood() == step.mood);
  }
  CHECK(d->bubble()->visible());
  d->setPatience(0.05f);
  m.run(200);
  CHECK(d->deliveryState() == VDelivery::kWaitingForOrder);  // lapsed: reset, nothing owed
  CHECK(d->wanted() == nullptr);
  CHECK(d->patience() == 100.0f);
  CHECK(d->timerMs() > 20000);
  m.run(400);  // the bubble's fade-out has ended
  CHECK_FALSE(d->bubble()->visible());
  CHECK(m.world->values().getInt("zeppelin_served_this_level", 0) == 0);
  CHECK(m.shop.payments.empty());
  CHECK(m.clean());
}

TEST_CASE("VDelivery: the matching chocolate is boxed (hand A first) and paid; the box animation runs 1860 ms, the airship comes, the machine resets once it has taken the box") {
  Mx m;
  deliveryValues(m);
  auto p = m.makePlayer();
  m.shop.who.reset();
  m.shop.upgrades = {"kdelivery1"};
  m.shop.haveZeppelin = true;
  m.shop.zeppelin = {barOrder(0)};
  auto s = m.makeDelivery();
  auto d = Mx::machineOf<VDelivery>(s);
  m.step(16);
  d->setTimerMs(10);
  m.until([&] { return d->deliveryState() == VDelivery::kOrderShowing; });
  // a wrong chocolate changes nothing
  auto wrong = barOf(m, 1);
  REQUIRE(p->attachObject(wrong));
  m.arrive(s);
  CHECK(d->deliveryState() == VDelivery::kOrderShowing);
  CHECK(p->handA() == wrong);
  // the right one in hand B is found after A
  auto right = barOf(m, 0);
  REQUIRE(p->attachObject(right));
  m.arrive(s);
  const double t0 = m.now;
  CHECK(d->deliveryState() == VDelivery::kBoxing);
  CHECK(p->handA() == wrong);
  CHECK(p->handB() == nullptr);
  CHECK(d->chocolate() == right);
  CHECK(right->layer() == 1);
  CHECK(near(right->scale().x, 0.8f));  // a bar is scaled down for the box
  REQUIRE(m.shop.payments.size() == 1);
  CHECK(m.shop.payments[0].price == 10.0f);
  CHECK(m.shop.payments[0].x == 622.0f);
  CHECK(m.shop.payments[0].y == 246.0f);
  CHECK(m.shop.payments[0].customer == nullptr);
  CHECK(m.world->values().getInt("zeppelin_served_this_level", 0) == 1);
  CHECK(d->state() == 1);
  CHECK(d->locked());
  CHECK(d->suspended());
  CHECK(s->suspended());
  CHECK(d->childObject("anim_boxing_front")->animName() == "DoFront");
  CHECK(d->childObject("anim_boxing_back")->animName() == "DoAnim");
  REQUIRE(d->zeppelin());
  CHECK(d->zeppelin()->visible());
  CHECK(d->zeppelin()->approaching());
  CHECK(d->zeppelin()->p0().x == -116.0f);
  CHECK(m.shop.zeppelinWaits.back());
  // the box hides the chocolate once its animation is past the 58th frame
  m.until([&] { return !right->visible(); });
  CHECK(d->childObject("anim_boxing_back")->currentFrame() > 57);
  CHECK(static_cast<int>(m.now - t0) > 1500);
  CHECK(static_cast<int>(m.now - t0) < 1900);
  m.until([&] { return d->deliveryState() == VDelivery::kBoxed; });
  CHECK(std::abs(static_cast<int>(m.now - t0) - 1860) <= 90);
  CHECK(d->currentFrame() == 2);
  CHECK(d->chocolate() == nullptr);  // the box has it
  CHECK_FALSE(d->childObject("anim_boxing_back")->visible());
  CHECK(d->zeppelin()->ready());
  CHECK(m.world->values().getInt("num_deliveries", 0) == 1);
  CHECK(d->locked());  // unlocked by the airship's pickup
  // the airship arrives at 4.5 s and takes the box: the machine resets while the pickup animation is past its 8th frame
  m.until([&] { return d->zeppelin()->state() == BZeppelin::kGrabbing; }, 8000);
  CHECK(std::abs(static_cast<int>(m.now - t0) - 4500) <= 60);
  m.until([&] { return d->deliveryState() == VDelivery::kWaitingForOrder; });
  CHECK(std::abs(static_cast<int>(m.now - t0) - 4500 - 8 * 30) <= 90);
  CHECK_FALSE(d->locked());
  CHECK(d->timerMs() > 20000);
  CHECK(m.shop.zeppelinWaits.back());  // the airship still flies
  // it leaves after the pickup animation (22 frames of 30 ms), and when it is gone the level may end
  m.until([&] { return d->zeppelin()->leaving(); });
  CHECK(std::abs(static_cast<int>(m.now - t0) - 4500 - 22 * 30) <= 90);
  m.until([&] { return d->zeppelin()->state() == BZeppelin::kIdle; }, 8000);
  CHECK(std::abs(static_cast<int>(m.now - t0) - 4500 - 22 * 30 - 4500) <= 120);
  CHECK_FALSE(d->zeppelin()->visible());
  m.run(100);
  CHECK_FALSE(m.shop.zeppelinWaits.back());
  CHECK(m.clean());
}

TEST_CASE("BZeppelin: it flies in along the Bezier curve from the left over 4.5 s, hovers at (590, 106) until the box is ready, and flies off to the left") {
  Mx m;
  auto zep = BZeppelin::create(m.ctx);
  m.mgr.addObject(zep);
  zep->init();
  m.step(16);
  CHECK_FALSE(zep->visible());
  CHECK(zep->state() == BZeppelin::kIdle);
  zep->pickup();
  CHECK(zep->visible());
  CHECK(zep->approaching());
  CHECK(zep->p0().x == -116.0f);
  CHECK(zep->p0().y >= 0.0f);
  CHECK(zep->p0().y < 300.0f);
  const gfx::PointF a = zep->p0(), b = zep->p1(), c = zep->p2(), e = zep->p3();
  CHECK(e.x == 633.0f);
  CHECK(e.y == 145.0f);
  m.run(2252);
  // the curve at t = 0.5
  const gfx::PointF mid{(a.x + 3 * b.x + 3 * c.x + e.x) / 8, (a.y + 3 * b.y + 3 * c.y + e.y) / 8};
  CHECK(std::fabs(zep->flightPosition().x - mid.x) < 4.0f);
  CHECK(std::fabs(zep->flightPosition().y - mid.y) < 4.0f);
  CHECK(zep->body()->position().x == zep->flightPosition().x);
  // a pickup while it flies changes nothing
  zep->pickup();
  CHECK(zep->p0().x == a.x);
  m.run(2400);
  // arrived with no box: it waits at the stand-by point
  CHECK(zep->state() == BZeppelin::kWaiting);
  CHECK(zep->flightPosition().x == 590.0f);
  CHECK(zep->flightPosition().y == 106.0f);
  CHECK_FALSE(zep->leaving());
  m.run(3000);
  CHECK(zep->state() == BZeppelin::kWaiting);
  CHECK(zep->flightPosition().x == 590.0f);
  CHECK(m.clean());
}

// ================================================================ conveyor

namespace {
// The conveyor with a bin, and counters 1 to `customers` each with a customer (state 1).
struct Belt {
  Cx m;
  std::shared_ptr<VStation> station, bin;
  std::shared_ptr<VConveyor> belt;
  std::vector<std::shared_ptr<VStation>> counters;
  std::vector<std::shared_ptr<VCustomer>> guests;
  explicit Belt(int customers = 0) {
    m.shop.upgrades = {"kconveyor1", "kdisposal"};
    m.makePlayer();
    m.shop.who.reset();
    station = m.makeConveyor();
    belt = std::dynamic_pointer_cast<VConveyor>(station);
    bin = m.makeDisposal();
    for (int i = 1; i <= customers; ++i) {
      counters.push_back(m.makeCounter(i));
      guests.push_back(m.comeIn(counters.back()));
    }
    m.step(16);
  }
};
}  // namespace

TEST_CASE("VConveyor: the arc, its angles and radii, and the zones come from the table; shown only with kConveyor1, then the base asset") {
  Belt b;
  CHECK(b.belt->visible());
  CHECK(b.belt->currentAsset() == "base");
  CHECK(near(b.belt->startAngle(), 1.195f, 0.001f));
  CHECK(near(b.belt->startRadius(), 550.4f, 0.1f));
  CHECK(near(b.belt->endAngle(), 2.208f, 0.001f));
  CHECK(near(b.belt->endRadius(), 516.2f, 0.1f));
  CHECK(b.belt->velocity() == 5.0f);
  CHECK(b.belt->center().x == 410.0f);
  CHECK(b.belt->center().y == 790.0f);
  CHECK(b.belt->zones().size() == 8);
  CHECK(near(b.belt->zones().at("station06").angle, 1.348f, 0.001f));
  CHECK(near(b.belt->zones().at("station01").angle, 2.038f, 0.001f));
  CHECK(near(b.belt->zones().at("station01").radius, std::sqrt(484.0f * 484.0f + 244.0f * 244.0f), 0.1f));
  // the nearest zones around an angle
  std::string ahead, behind;
  b.belt->getStationZones(1.5f, ahead, behind);
  CHECK(ahead == "station04");
  CHECK(behind == "station05");
  ahead.clear();
  behind.clear();
  b.belt->getStationZones(0.5f, ahead, behind);
  CHECK(ahead == "station06");
  CHECK(behind.empty());
  ahead.clear();
  behind.clear();
  b.belt->getStationZones(2.2f, ahead, behind);
  CHECK(ahead.empty());
  CHECK(behind == "drop");
  // between two zones the radius lies between theirs; before the first it is the arc's own start
  const float r = b.belt->calculateRadius(1.5f, "station04", "station05");
  const float r4 = b.belt->zones().at("station04").radius, r5 = b.belt->zones().at("station05").radius;
  CHECK(r > std::min(r4, r5) - 0.01f);
  CHECK(r < std::max(r4, r5) + 0.01f);
  CHECK(near(b.belt->calculateRadius(b.belt->startAngle(), "station06", ""), b.belt->startRadius(), 0.01f));
  // without the upgrade it is hidden
  Mx n;
  n.shop.upgrades.clear();
  auto hidden = n.makeConveyor();
  CHECK_FALSE(hidden->visible());
  CHECK(b.m.clean());
}

TEST_CASE("VConveyor: a chocolate put on the belt starts at the start angle and crosses in about twelve seconds; one past the end falls into the bin") {
  Belt b;
  auto bar = barOf(b.m, 0);
  b.m.world->values().setValue("conveyor_max_this_level", 0, false);
  b.belt->addItemToBelt(bar);
  REQUIRE(b.belt->items().size() == 1);
  CHECK(b.belt->items().front().theta == b.belt->startAngle());
  CHECK(b.belt->items().front().zone == "station07");
  CHECK(bar->parent() == b.belt.get());
  CHECK(bar->layer() == 1);
  CHECK(b.belt->animName() == "DoAnim");
  CHECK(b.m.world->values().getInt("conveyor_max_this_level", 0) == 1);
  const double t0 = b.m.now;
  b.m.step(16);
  // placed on the arc: the point at the start angle and radius
  const gfx::PointF at = bar->position();
  CHECK(std::fabs(at.x - (410.0f + b.belt->startRadius() * std::cos(b.belt->startAngle()))) < 3.0f);
  CHECK(std::fabs(at.y - (790.0f - b.belt->startRadius() * std::sin(b.belt->startAngle()))) < 3.0f);
  VObject* active = b.bin->childObject("active");
  REQUIRE(active);
  CHECK_FALSE(active->animPlaying());
  b.m.until([&] { return b.belt->items().empty(); }, 20000);
  const double crossing = b.m.now - t0;
  CHECK(crossing > 10500);
  CHECK(crossing < 13500);
  CHECK(bar->parent() == nullptr);
  CHECK(active->animName() == "DoAnim");  // the bin took it
  CHECK(b.m.clean());
}

TEST_CASE("VConveyor: items keep 0.08 rad apart: a second one put on close behind the first stands still until the gap is there, then follows") {
  Belt b;
  auto first = barOf(b.m, 0), second = barOf(b.m, 1);
  b.belt->addItemToBelt(first);
  b.m.run(500);
  b.belt->addItemToBelt(second);
  REQUIRE(b.belt->items().size() == 2);
  const float lead = b.belt->items().front().theta;
  CHECK(lead - b.belt->startAngle() < 0.08f);
  float minGapMoving = 10.0f;
  float stood = 0;
  bool moved = false;
  const float start = b.belt->startAngle();
  for (int i = 0; i < 1400; ++i) {  // 5.6 s
    b.m.step(4);
    const float a = b.belt->items().front().theta, c = b.belt->items().back().theta;
    if (!moved && c > start) moved = true;
    if (!moved) stood = std::max(stood, std::fabs(c - start));
    if (moved) minGapMoving = std::min(minGapMoving, a - c);
  }
  CHECK(stood == 0.0f);                // it stood still while the first one was within 0.08
  CHECK(moved);                        // and followed once the gap was there
  CHECK(minGapMoving > 0.0795f);       // never closer than 0.08 once it moves
  CHECK(b.m.world->values().getInt("conveyor_max_this_level", 0) == 2);
  CHECK(b.m.clean());
}

TEST_CASE("VConveyor: the customer at a counter the item passes takes it once (+25 patience, the item gone); one that refuses is not asked again for that counter") {
  Belt b(1);
  auto cust = b.guests[0];
  cust->setPatience(50.0f);
  auto bar = barOf(b.m, 0);
  b.belt->addItemToBelt(bar);
  float before = cust->patience();
  while (!b.belt->items().empty() && b.m.now < 1000 + 30000) {
    before = cust->patience();
    b.m.step(4);
  }
  REQUIRE(b.belt->items().empty());
  CHECK(near(cust->patience() - before, 25.0f, 0.2f));
  CHECK(bar->parent() == nullptr);
  CHECK(b.m.world->values().getReal("decayed_this_level", 0) == 0);

  // a customer that cannot take it (served already): the zone is recorded and it is not asked again
  Belt c(1);
  auto gone = c.guests[0];
  gone->setState(kCustomerAngry);
  auto bar2 = barOf(c.m, 0);
  c.belt->addItemToBelt(bar2);
  c.m.until([&] { return c.belt->items().empty() || c.belt->items().front().zone == "station01"; }, 20000);
  REQUIRE_FALSE(c.belt->items().empty());
  gone->setState(kCustomerOrdering);  // now it would accept, but this zone is done
  gone->setPatience(50.0f);
  c.m.run(300);
  REQUIRE_FALSE(c.belt->items().empty());
  CHECK(c.belt->items().front().zone == "station01");
  CHECK(gone->patience() < 50.0f);
  c.m.until([&] { return c.belt->items().empty(); }, 20000);
  CHECK(gone->patience() < 50.0f);  // it never got the sample
  CHECK(c.m.clean());
}

namespace {
// Runs the belt until it is empty; the largest single-step rise of each customer's patience.
std::vector<float> watchJumps(Belt& b, int limitMs) {
  std::vector<float> last, jump(b.guests.size(), 0.0f);
  for (const auto& g : b.guests) last.push_back(g->patience());
  for (int t = 0; t < limitMs && !b.belt->items().empty(); t += 4) {
    b.m.step(4);
    for (std::size_t i = 0; i < b.guests.size(); ++i) {
      jump[i] = std::max(jump[i], b.guests[i]->patience() - last[i]);
      last[i] = b.guests[i]->patience();
    }
  }
  return jump;
}
}  // namespace

TEST_CASE("VConveyor: a customer with less patience at a lower counter is served first; the more urgent is skipped over only by a customer with less") {
  {
    // counter 2's customer is the more patient one: counter 1's (more urgent) gets the item as it comes by
    Belt b(2);
    b.guests[0]->setPatience(40.0f);
    b.guests[1]->setPatience(60.0f);
    b.belt->addItemToBelt(barOf(b.m, 0));
    const auto jumps = watchJumps(b, 30000);
    CHECK(jumps[0] > 20.0f);  // +25 as it comes by counter 1
    CHECK(jumps[1] < 1.0f);   // only the drain
  }
  {
    // counter 2's customer is the more urgent: it takes the item at once
    Belt b(2);
    b.guests[0]->setPatience(40.0f);
    b.guests[1]->setPatience(30.0f);
    b.belt->addItemToBelt(barOf(b.m, 0));
    const auto jumps = watchJumps(b, 30000);
    CHECK(jumps[1] > 20.0f);
    CHECK(jumps[0] < 1.0f);
  }
}

TEST_CASE("VConveyor: a chocolate that has decayed is dropped from the belt at the next counter it passes, and counts once") {
  Belt b;
  b.m.world->values().setValue("chocolate_decay_rate", 0.05, false);  // all of its life in 2 s
  b.m.world->values().setValue("decayed_this_level", 0, false);
  auto bar = barOf(b.m, 0, true);
  b.belt->addItemToBelt(bar);
  b.m.until([&] { return bar->decay() <= 0.0f; }, 5000);
  CHECK(b.m.world->values().getInt("decayed_this_level", 0) == 1);
  CHECK_FALSE(b.belt->items().empty());  // fading: still there
  b.m.until([&] { return b.belt->items().empty(); }, 20000);
  CHECK(bar->hasDecayed());
  CHECK(bar->parent() == nullptr);
  // dropped at a counter, not in the bin: the bin did not play
  CHECK_FALSE(b.bin->childObject("active")->animPlaying());
  b.m.run(1000);
  CHECK(b.m.world->values().getInt("decayed_this_level", 0) == 1);  // once
  CHECK(b.m.clean());
}

TEST_CASE("VConveyor::onArrival: the chocolate that is not a bucket goes on the belt, hand A first; buckets and sauces stay in her hands") {
  Belt b;
  auto p = b.m.player;
  auto bucket = bucketOf(b.m, 0);
  auto bar = barOf(b.m, 1);
  REQUIRE(p->attachObject(bucket));  // hand A
  REQUIRE(p->attachObject(bar));     // hand B
  b.m.arrive(b.station);
  REQUIRE(b.belt->items().size() == 1);
  CHECK(b.belt->items().front().item == bar);
  CHECK(p->handA() == bucket);
  CHECK(p->handB() == nullptr);
  // nothing but a bucket: nothing goes
  b.m.arrive(b.station);
  CHECK(b.belt->items().size() == 1);
  CHECK(p->handA() == bucket);
  // two bars: hand A's goes first
  p->detachObject(2);
  auto one = barOf(b.m, 0), two = barOf(b.m, 2);
  REQUIRE(p->attachObject(one));
  REQUIRE(p->attachObject(two));
  b.m.arrive(b.station);
  REQUIRE(b.belt->items().size() == 2);
  CHECK(b.belt->items().back().item == one);
  CHECK(p->handB() == two);
  // a sauce is not a chocolate
  p->detachObject(1);
  auto sauce = VCarriable::create(b.m.ctx, 1);
  sauce->setType("sauce");
  REQUIRE(p->attachObject(sauce));
  b.m.arrive(b.station);
  CHECK(b.belt->items().size() == 2);
  CHECK(p->handA() == sauce);
  CHECK(b.m.clean());
}
