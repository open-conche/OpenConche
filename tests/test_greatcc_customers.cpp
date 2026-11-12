// The Great Chocolate Chase, slice 13 (docs/design/greatcc.md section 6): the customers and what they pay. The arithmetic
// (core/CustomerLogic), the thought bubble, the customer vendor, the customer sprite's states, patience, orders, serving and
// payout, and the vendor's order-to-chocolate builder. Synthetic fixtures only (the machines' harness and a few tables).
#include "GreatccCustomerFixture.h"

#include <random>

#include "games/greatcc/core/CustomerLogic.h"
#include "games/greatcc/shop/CustomerVendor.h"
#include "games/greatcc/shop/ThoughtBubble.h"
#include "games/greatcc/shop/VSuspendable.h"
#include "games/greatcc/tutorial/TutorialManager.h"
#include "games/greatcc/tutorial/TutorialSpider.h"


// ================================================================ the arithmetic

TEST_CASE("moodForPatience: above 80, 60, 40, 20 and 0 are the five moods, nothing left is none") {
  CHECK(moodForPatience(150) == kMoodHappy);
  CHECK(moodForPatience(80.01f) == kMoodHappy);
  CHECK(moodForPatience(80.0f) == kMoodContent);
  CHECK(moodForPatience(60.5f) == kMoodContent);
  CHECK(moodForPatience(60.0f) == kMoodNeutral);
  CHECK(moodForPatience(40.0f) == kMoodAnnoyed);
  CHECK(moodForPatience(20.0f) == kMoodAngry);
  CHECK(moodForPatience(0.001f) == kMoodAngry);
  CHECK(moodForPatience(0.0f) == kMoodNone);
  CHECK(moodForPatience(-3.0f) == kMoodNone);
  CHECK(moodForPatience(std::nanf("")) == kMoodNone);
}

TEST_CASE("decayRateFromTable and drainPatience: ms for all patience, or the rate itself; a drain never goes below nothing") {
  CHECK(decayRateFromTable(100000.0f) == doctest::Approx(0.001f));
  CHECK(decayRateFromTable(1.0f) == doctest::Approx(100.0f));
  CHECK(decayRateFromTable(0.5f) == doctest::Approx(0.5f));
  CHECK(decayRateFromTable(0.0f) == 0.0f);
  CHECK(std::isnan(decayRateFromTable(std::nanf(""))));
  // 1000 ms of a 100 s customer is one point; the modifiers multiply
  CHECK(drainPatience(100.0f, 1000, 0.001f, 1.0f, 1.0f) == doctest::Approx(99.0f));
  CHECK(drainPatience(100.0f, 1000, 0.001f, 0.9f, 1.25f) == doctest::Approx(100.0f - 1.125f));
  CHECK(drainPatience(0.5f, 1000, 0.001f, 1.0f, 100.0f) == 0.0f);  // raised from below nothing
  CHECK(drainPatience(100.0f, 0, 0.001f, 1.0f, 1.0f) == 100.0f);
}

TEST_CASE("payoutDollarCount: floor(price / 8) below 48, five from there, none for a small price") {
  CHECK(payoutDollarCount(0.9f) == 0);
  CHECK(payoutDollarCount(7.9f) == 0);
  CHECK(payoutDollarCount(8.0f) == 1);
  CHECK(payoutDollarCount(15.0f) == 1);
  CHECK(payoutDollarCount(16.0f) == 2);
  CHECK(payoutDollarCount(30.0f) == 3);
  CHECK(payoutDollarCount(47.9f) == 5);
  CHECK(payoutDollarCount(48.0f) == 5);
  CHECK(payoutDollarCount(400.0f) == 5);
}

TEST_CASE("tipBase and ComboChain: the tip is the patience fraction of the customer's tip; the first event of a kind starts the chain with no bonus") {
  CHECK(tipBase(100.0f, 8.0f) == doctest::Approx(8.0f));
  CHECK(tipBase(50.0f, 8.0f) == doctest::Approx(4.0f));
  CHECK(tipBase(125.0f, 8.0f) == doctest::Approx(10.0f));  // patience has no upper clamp
  ComboChain c;
  CHECK_FALSE(c.add(kComboMenu));  // 0 -> 1: a new kind
  CHECK(c.lastType == kComboMenu);
  CHECK(c.counter == 0);
  CHECK(c.add(kComboMenu));
  CHECK(c.counter == 1);
  CHECK(c.add(kComboMenu));
  CHECK(c.counter == 2);
  CHECK_FALSE(c.add(kComboService));  // another kind restarts it
  CHECK(c.counter == 0);
  CHECK(c.add(kComboService));
  c.reset();
  CHECK(c.lastType == 0);
  CHECK_FALSE(c.add(kComboPayment));
  CHECK(std::string(comboBonusValue(kComboMenu)) == "menu_combobonus");
  CHECK(std::string(comboBonusValue(kComboService)) == "service_combobonus");
  CHECK(std::string(comboBonusValue(kComboPayment)) == "payout_combobonus");
  CHECK(std::string(comboBonusValue(3)) == "menu_combobonus");
  CHECK(std::string(comboLabelKey(kComboPayment)) == "combo_payment");
  CHECK(std::string(comboMultiplierValue(kComboService)) == "service_multiplier_this_level");
  CHECK(comboMultiplierValue(8) == nullptr);
  CHECK(std::string(comboPointsState(kComboPayment)) == "CashPickupChainPointsEarned");
  CHECK(comboPointsState(0) == nullptr);
}

TEST_CASE("ValueManager::getProduct: the value (1 when it does not exist) times x in single precision, clamped when asked") {
  Cx m;
  CHECK(m.world->values().getProduct("never_declared", 3.0f, false) == doctest::Approx(3.0f));
  m.world->values().load("<values><value name='a' value='2' min='0' max='5'/></values>");
  CHECK(m.world->values().getProduct("a", 2.5f, false) == doctest::Approx(5.0f));
  CHECK(m.world->values().getProduct("a", 4.0f, false) == doctest::Approx(8.0f));
  CHECK(m.world->values().getProduct("a", 4.0f, true) == doctest::Approx(5.0f));
  CHECK(m.world->values().getProduct("a", -4.0f, true) == doctest::Approx(0.0f));
}

// ================================================================ the vendor and the sprite's tables

TEST_CASE("CustomerVendor: the posse and the locals are prototypes by their own name; the posse wins, an unknown name gives nothing") {
  Cx m;
  CHECK(m.customers.posseCount() == 2);  // the thought bubble table is not a character
  CHECK(m.customers.localCount() == 2);
  CHECK(m.customers.knows("professor"));
  CHECK(m.customers.knows("iqu_male"));
  CHECK_FALSE(m.customers.knows("thought_bubble"));
  CHECK_FALSE(m.customers.knows("ghost"));
  CHECK(m.customers.getCustomer("ghost", &m.shop) == nullptr);
  auto p = m.customers.getCustomer("professor", &m.shop);
  REQUIRE(p);
  CHECK(p->decayRate() == doctest::Approx(0.001f));  // the posse's 100000, not the local's 50000
  CHECK(p->tip() == doctest::Approx(8.0f));
  // a fresh copy each time, with its own pieces
  auto q = m.customers.getCustomer("professor", &m.shop);
  CHECK(p.get() != q.get());
  CHECK(p->bubble().get() != q->bubble().get());
  CHECK(p->menu().get() != q->menu().get());
  CHECK(p->money().get() != q->money().get());
  m.customers.clearLocals();
  CHECK_FALSE(m.customers.knows("iqu_male"));
  CHECK(m.customers.knows("professor"));
}

TEST_CASE("VCustomer: the table's decay, tip and order delay, and the static and idle sprites by key; a copy keeps them and the data names it") {
  Cx m;
  auto dip = m.customers.getCustomer("diplomat", &m.shop);
  REQUIRE(dip);
  CHECK(dip->decayRate() == doctest::Approx(0.002f));  // below 1: the rate itself
  CHECK(dip->tip() == doctest::Approx(5.0f));
  CHECK(dip->orderDelayMs() == 5000);  // the default
  auto p = m.customers.getCustomer("professor", &m.shop);
  REQUIRE(p);
  REQUIRE(p->statics().size() == 2);
  REQUIRE(p->idles().size() == 2);
  CHECK(p->statics().count("static_body"));
  CHECK(p->statics().count("static_head"));
  CHECK(p->idles().count("idle_happy"));
  CHECK(p->idles().count("idle_angry"));
  // init: the statics shown, the idle sprites hidden, every one a leaf; the draw depth is 0; the mood from the full patience
  for (const auto& [k, o] : p->statics()) {
    CHECK(o->visible());
    CHECK(o->leaf());
  }
  for (const auto& [k, o] : p->idles()) CHECK_FALSE(o->visible());
  CHECK(p->depth() == 0);
  CHECK(p->mood() == kMoodHappy);
  CHECK(p->statics().at("static_head")->currentState() == "happy");
  CHECK(p->statics().at("static_body")->currentState() == "default");
  CHECK(p->state() == kCustomerPrototype);
  CHECK(p->clickable());
  // the data: a name renames the sprite, the tip and the decay are replaced only when they are not negative
  auto data = std::make_shared<CustomerData>();
  data->type = "professor";
  data->name = "tut_cust_1";
  p->setCustomerData(data);
  CHECK(p->name() == "tut_cust_1");
  CHECK(p->tip() == doctest::Approx(8.0f));
  CHECK(p->decayRate() == doctest::Approx(0.001f));
  auto data2 = std::make_shared<CustomerData>();
  data2->tip = 12;
  data2->decay = 0.01f;  // taken as the rate, not converted
  p->setCustomerData(data2);
  CHECK(p->name() == "tut_cust_1");  // no name: unchanged
  CHECK(p->tip() == doctest::Approx(12.0f));
  CHECK(p->decayRate() == doctest::Approx(0.01f));
}

TEST_CASE("CustomerVendor pieces: the bubble starts transparent and is a leaf, the money and the menu are leaves, the orderables copy the table") {
  Cx m;
  auto bubble = m.customers.makeThoughtBubble();
  REQUIRE(bubble);
  CHECK(bubble->leaf());
  CHECK(bubble->currentAsset() == "transparent");
  CHECK(bubble->layer() == 100);
  auto menu = m.customers.makeMenu();
  REQUIRE(menu);
  CHECK(menu->leaf());
  CHECK(menu->currentAsset() == "menu");
  auto money = m.customers.makeMoney();
  REQUIRE(money);
  CHECK(money->leaf());  // after the copy: a click on the pile is a click on the counter
  CHECK(money->currentAsset() == "money");
  CHECK(money->tint().r == doctest::Approx(0.77f));
  auto orderables = m.customers.makeOrderables();
  REQUIRE(orderables);
  CHECK(orderables->hasAsset("sauce"));
  CHECK(orderables->hasAsset("drink"));
  CHECK(orderables->type() == "sauce");
  // a vendor with no objects table makes none
  CustomerVendor bare(m.ctx);
  CHECK(bare.makeThoughtBubble() == nullptr);
  CHECK(bare.makeMenu() == nullptr);
  CHECK(bare.makeMoney() == nullptr);
  CHECK(bare.makeOrderables() == nullptr);
}

// ================================================================ the thought bubble

namespace {
std::shared_ptr<VObject> picture(Cx& m, const char* name) {
  auto o = VObject::create(m.ctx, name, "pic");
  return o;
}
}  // namespace

TEST_CASE("VThoughtBubble: bubbles stack diagonally behind the front one, tinted by the root of their place; a disabled one is red") {
  Cx m;
  auto b = m.customers.makeThoughtBubble();
  m.mgr.addObject(b);
  auto first = picture(m, "first"), second = picture(m, "second"), third = picture(m, "third");
  b->addOrder(first, "menu", true);
  b->addOrder(second, "chocolate", true);
  b->addOrder(third, "sauce", false);
  REQUIRE(b->bubbleCount() == 3);
  CHECK(b->liveBubbles() == 3);
  CHECK(b->currentBubble() == 0);
  // positions (-offset * n), layers (-n), the item a leaf child on its anchor
  CHECK(b->bubbleAt(0)->position().x == -0.0f);
  CHECK(b->bubbleAt(1)->position().x == -5.0f);
  CHECK(b->bubbleAt(1)->position().y == -5.0f);
  CHECK(b->bubbleAt(2)->position().x == -10.0f);
  CHECK(b->bubbleAt(1)->layer() == -1);
  CHECK(b->bubbleAt(2)->layer() == -2);
  CHECK(second->parent() == b->bubbleAt(1));
  CHECK(second->leaf());
  CHECK(second->anchorName() == "chocolate");
  CHECK(first->anchorName() == "menu");
  CHECK(third->anchorName() == "sauce");
  // the disabled one is tinted red; the bubbles behind the front one carry a tint effect, the front one none
  CHECK(b->bubbleDisabled(2));
  CHECK_FALSE(b->bubbleDisabled(1));
  CHECK(b->bubbleAt(2)->tint().g == doctest::Approx(0.75f));
  CHECK(effectsOf<TintEffect>(*b->bubbleAt(0)) == 0);
  CHECK(effectsOf<TintEffect>(*b->bubbleAt(1)) == 1);
  CHECK(effectsOf<AlphaEffect>(*b->bubbleAt(0)) == 1);
  // the intensity is thought_tint * sqrt(n) (the third's is the red one)
  for (const auto& e : b->bubbleAt(2)->effectList())
    if (auto* t = dynamic_cast<TintEffect*>(e.get())) CHECK(t->target() == doctest::Approx(0.3f * std::sqrt(2.0f)));
  b->setMood(4);
  CHECK(b->bubbleAt(1)->currentState() == "annoyed");
  b->setMood(2);
  CHECK(b->bubbleAt(2)->currentState() == "content");
  b->setMood(1);
  CHECK(b->bubbleAt(0)->currentState() == "happy");
  b->setMood(0);
  CHECK(b->bubbleAt(0)->currentState() == "happy");
}

TEST_CASE("VThoughtBubble: the front bubble that is done fades out in transition_time and goes; the next steps forward to the front place") {
  Cx m;
  auto b = m.customers.makeThoughtBubble();
  m.mgr.addObject(b);
  b->addOrder(picture(m, "first"), "chocolate", true);
  b->addOrder(picture(m, "second"), "chocolate", true);
  m.run(400);
  CHECK(b->bubbleAt(1)->position().x == -5.0f);
  b->bubbleAt(0)->setAlpha(1.0f);  // (the fade-in sets the sprite's alpha when it draws; nothing draws here)
  CHECK(b->nextBubble());   // one more is left
  CHECK(b->currentBubble() == 1);
  m.run(8);
  // the first one starts to fade (its own effect) and goes away once it is done
  CHECK(b->liveBubbles() == 2);
  CHECK(b->bubbleAt(0)->effectCount() >= 1);
  m.run(300);
  CHECK(b->liveBubbles() == 1);
  CHECK(b->bubbleAt(0) == nullptr);
  CHECK(b->bubbleAt(1)->position().x == doctest::Approx(0.0f));  // the second stepped to the front place
  CHECK(b->bubbleAt(1)->position().y == doctest::Approx(0.0f));
  CHECK_FALSE(b->nextBubble());  // none left
  m.run(300);
  CHECK(b->liveBubbles() == 0);
  // Clear starts everything over
  b->clear();
  CHECK(b->bubbleCount() == 0);
  CHECK(b->currentBubble() == 0);
  CHECK(b->children().empty());
}

// ================================================================ a customer's life

TEST_CASE("VCustomer::setActive: at the counter's customer point fading in, with the menu bubble; a counter holds one customer") {
  Cx m;
  auto station = m.makeCounter();
  m.makePlayer();
  auto c = m.comeIn(station, "professor", {}, "tut_cust_1");
  CHECK(c->state() == kCustomerArrived);
  CHECK(c->patience() == doctest::Approx(100.0f));
  CHECK(c->station() == station);
  CHECK(station->customer() == c);
  CHECK(station->mouse());  // a counter with a customer is a mouse target
  CHECK(c->position().x == 5);  // customer01
  CHECK(c->position().y == 6);
  CHECK(effectsOf<AlphaEffect>(*c) == 1);
  REQUIRE(c->bubble());
  CHECK(c->bubble()->parent() == c.get());
  CHECK(c->bubble()->anchorName() == "bubble_single");
  CHECK(c->bubble()->bubbleCount() == 1);
  CHECK(c->bubble()->bubbleAt(0)->children().front().get() == c->menu().get());
  CHECK(c->menu()->anchorName() == "menu");
  CHECK(c->bubble()->bubbleAt(0)->currentState() == "happy");
  // a second customer cannot take the same counter
  CHECK_FALSE(station->attachCustomer(m.customers.getCustomer("diplomat", &m.shop)));
  // a station that is not a counter takes nobody
  auto other = m.makeMixer();
  CHECK_FALSE(other->attachCustomer(c));
  // a customer for no station does nothing
  auto d = m.customers.getCustomer("diplomat", &m.shop);
  d->setActive(nullptr, 100.0f);
  CHECK(d->state() == kCustomerPrototype);
}

TEST_CASE("VCustomer: patience drains in states 1 to 3 only, by the elapsed time, the decay, the modifier and the level's factor, and stops while paused") {
  Cx m;
  auto station = m.makeCounter();
  auto c = m.comeIn(station, "professor");
  m.world->values().setValue("level_customer_decay", 2.0, false);
  m.world->values().setValue("patience_decay_modifier", 0.5, false);
  m.step(1000);
  // 1000 ms * 0.001 * 0.5 * 2.0 = 1 point
  CHECK(c->patience() == doctest::Approx(99.0f).epsilon(0.02));
  const float p1 = c->patience();
  m.mgr.softPause(true);  // a paused update does not drain
  m.step(1000);
  m.mgr.softPause(false);
  CHECK(c->patience() == doctest::Approx(p1));
  // a stall of 3 s or more is not counted at all
  m.step(3000);
  CHECK(c->patience() == doctest::Approx(p1));
  // state 3 drains too; the served and the leaving states do not
  c->setState(kCustomerOrdering);
  m.step(1000);
  CHECK(c->patience() < p1);
  const float p3 = c->patience();
  for (int s : {kCustomerPrototype, kCustomerServed, kCustomerMoney, kCustomerDone}) {
    c->setState(s);
    m.step(1000);
    CHECK(c->patience() == doctest::Approx(p3));
  }
}

TEST_CASE("VCustomer: the mood follows the patience bands, the statics show it, the bubbles too; nothing is re-applied while the band holds") {
  Cx m;
  auto station = m.makeCounter();
  auto c = m.comeIn(station, "professor");
  auto head = [&] { return c->statics().at("static_head")->currentState(); };
  auto body = [&] { return c->statics().at("static_body")->currentState(); };
  CHECK(c->mood() == kMoodHappy);
  CHECK(head() == "happy");
  c->setPatience(70);
  c->updateMood();
  CHECK(c->mood() == kMoodContent);
  CHECK(head() == "happy");  // content and happy share the face
  CHECK(c->bubble()->bubbleAt(0)->currentState() == "content");
  c->setPatience(50);
  c->updateMood();
  CHECK(c->mood() == kMoodNeutral);
  CHECK(head() == "neutral");
  CHECK(body() == "default");
  c->setPatience(30);
  c->updateMood();
  CHECK(head() == "annoyed");
  c->setPatience(10);
  c->updateMood();
  CHECK(c->mood() == kMoodAngry);
  CHECK(head() == "angry");
  CHECK(c->bubble()->bubbleAt(0)->currentState() == "angry");
  // a customer reading the menu shows the menu faces whatever the mood
  c->setState(kCustomerReading);
  c->setMood(kMoodAngry);
  CHECK(head() == "menu");
  CHECK(body() == "menu");
}

TEST_CASE("VCustomer: a patience that runs out in states 1 to 3 sends it away angry, and it counts as lost") {
  Cx m;
  auto station = m.makeCounter();
  m.world->values().setValue("num_angry", 0, true);
  auto c = m.comeIn(station, "professor");
  c->setPatience(0.5f);
  m.world->gameStates().set("TotalCustomersLost", "0");
  m.world->values().setValue("level_customer_decay", 100.0, false);
  m.step(1000);
  // nothing was served: it leaves with no money (state 7 at once), fading out
  CHECK(c->patience() == 0.0f);
  CHECK(c->state() == kCustomerDone);
  CHECK(m.value("angry_this_level") == 1);
  CHECK(m.value("served_this_level") == 0);
  CHECK(m.value("num_angry") == 1);
  CHECK(m.world->gameStates().get("TotalCustomersLost") == "1");
  CHECK_FALSE(c->shouldDelete());  // the fade is still running
  m.run(600);
  CHECK(c->shouldDelete());
  CHECK_FALSE(c->visible());  // the end of the fade hides the sprite
}

TEST_CASE("VCustomer::onArrival at state 1: the menu is read for the order delay, the menu bubble goes, a menu chain event is counted") {
  Cx m;
  auto station = m.makeCounter();
  m.makePlayer();
  auto c = m.comeIn(station, "professor", {Cx::plainOrder(kOrderSauce)});
  m.visit(station);
  CHECK(c->state() == kCustomerReading);
  CHECK(m.shop.combos == std::vector<int>{kComboMenu});
  CHECK(c->suspended());
  CHECK(c->bubble()->currentBubble() == 1);
  CHECK(c->statics().at("static_head")->currentState() == "menu");
  // a second visit while reading does nothing
  m.visit(station);
  CHECK(c->state() == kCustomerReading);
  CHECK(m.shop.combos.size() == 1);
  // the orders come with the end of the delay: a state 3 customer wants the first and the bubble shows them
  m.run(4900);
  CHECK(c->state() == kCustomerReading);
  m.run(150);
  CHECK(c->state() == kCustomerOrdering);
  CHECK(c->wanted());
  CHECK(c->wanted()->type() == "sauce");
  CHECK(c->orders().empty());
  CHECK(c->bubble()->bubbleCount() == 1);  // the menu's bubble was cleared, the order's put in
  CHECK(c->bubble()->currentBubble() == 0);
  CHECK(c->statics().at("static_head")->currentState() == "happy");
}

TEST_CASE("VCustomer: scripted orders become a chocolate, a sauce and a drink; one bubble each, the first wanted, the unmakeable one red") {
  Cx m;
  auto station = m.makeCounter();
  m.makePlayer();
  m.updatePools();
  OrderInfo odd = Cx::chocolateOrder(1, kShapeBar);  // a milk bar: only dark is makeable with kmixer1
  auto c = m.comeIn(station, "professor", {Cx::chocolateOrder(0), Cx::plainOrder(kOrderSauce), Cx::plainOrder(kOrderDrink), odd});
  m.visit(station);
  m.finishReading(c);
  REQUIRE(c->wanted());
  CHECK(c->wanted()->type() == "chocolate");
  CHECK(static_cast<VChocolate*>(c->wanted().get())->toString() == "(,bar)");  // the order carries no mix
  REQUIRE(c->orders().size() == 3);
  auto it = c->orders().begin();
  CHECK((*it++)->type() == "sauce");
  CHECK((*it++)->type() == "drink");
  CHECK((*it++)->type() == "chocolate");
  REQUIRE(c->bubble()->bubbleCount() == 4);
  CHECK_FALSE(c->bubble()->bubbleDisabled(0));  // the dark bar can be made
  CHECK_FALSE(c->bubble()->bubbleDisabled(1));  // kSauce1 is on
  CHECK(c->bubble()->bubbleDisabled(2));        // kDrink1 is not
  CHECK(c->bubble()->bubbleDisabled(3));        // the milk bar needs kMixer2
  // the sauce picture is a smaller copy of the orderables, tagged `sauce`, the drink one `drink`
  auto* sauce = c->orders().front().get();
  CHECK(sauce->scale().x == doctest::Approx(0.8f));
  CHECK(static_cast<VObject*>(sauce)->currentAsset() == "sauce");
  CHECK(static_cast<VObject*>(c->bubble()->bubbleAt(2)->children().front().get())->type() == "drink");
}

TEST_CASE("VCustomer::serveCustomer: the wrong thing is refused with nothing changed; the right one is paid for, adds patience and shows the next order") {
  Cx m;
  auto station = m.makeCounter();
  auto player = m.makePlayer();
  m.world->values().setValue("num_sauce_sold", 0, true);
  auto c = m.comeIn(station, "professor", {Cx::chocolateOrder(0), Cx::plainOrder(kOrderSauce), Cx::plainOrder(kOrderDrink)});
  // nothing is wanted before the orders show
  CHECK_FALSE(c->serveCustomer(finishedBar(m, 0)));
  m.visit(station);
  m.finishReading(c);
  m.shop.combos.clear();
  // a sauce for a chocolate order, a milk bar for a dark one, a bucket, a bar with a topping: refused, nothing changes
  auto bottle = VCarriable::create(m.ctx, 1);
  bottle->setType("sauce");
  CHECK_FALSE(c->serveCustomer(bottle));
  CHECK_FALSE(c->serveCustomer(finishedBar(m, 1)));
  CHECK_FALSE(c->serveCustomer(m.vendor->getEmptyChocolate(0, false)));
  auto topped = finishedBar(m, 0);
  topped->addLayer(kShapeBar, kLayerTopping, 0);
  CHECK_FALSE(c->serveCustomer(topped));
  CHECK_FALSE(c->serveCustomer(nullptr));
  CHECK(c->price() == 0);
  CHECK(c->patience() < 100.0f);
  CHECK(m.shop.combos.empty());
  CHECK(c->orders().size() == 2);
  const float before = c->patience();
  // the dark bar: 10, a service event, +20 patience, the next order (the sauce) is wanted now, one bubble gone by
  auto bar = finishedBar(m, 0);
  CHECK(c->serveCustomer(bar));
  CHECK(c->price() == doctest::Approx(10.0f));
  CHECK(c->patience() == doctest::Approx(before + 20.0f));
  CHECK(m.shop.combos == std::vector<int>{kComboService});
  REQUIRE(c->wanted());
  CHECK(c->wanted()->type() == "sauce");
  CHECK(c->bubble()->currentBubble() == 1);
  CHECK(m.world->gameStates().get("OrderCompleted") == "1");
  // the sauce: 15 and counted as sold; the drink: 15
  CHECK(c->serveCustomer(bottle));
  CHECK(c->price() == doctest::Approx(25.0f));
  CHECK(m.value("num_sauce_sold") == 1);
  auto cup = VCarriable::create(m.ctx, 1);
  cup->setType("drink");
  CHECK(c->serveCustomer(cup));
  CHECK(c->price() == doctest::Approx(40.0f));
  CHECK_FALSE(c->wanted());
  CHECK_FALSE(c->hasMoreOrders());
  CHECK(m.shop.combos.size() == 3);
  (void)player;
}

TEST_CASE("VCustomer::onArrival at state 3: one item per visit, slot A before slot B, a refused one stays in her hand; the last item ends it") {
  Cx m;
  auto station = m.makeCounter();
  auto player = m.makePlayer();
  auto c = m.comeIn(station, "professor", {Cx::chocolateOrder(0), Cx::plainOrder(kOrderSauce)});
  m.visit(station);
  m.finishReading(c);
  auto bottle = VCarriable::create(m.ctx, 1);
  bottle->setType("sauce");
  auto bar = finishedBar(m, 0);
  // slot A holds the bottle (not wanted yet), slot B the bar: B is the one served
  REQUIRE(player->attachObject(bottle));
  REQUIRE(player->attachObject(bar));
  m.visit(station);
  CHECK(c->price() == doctest::Approx(10.0f));
  CHECK(player->handA() == bottle);
  CHECK_FALSE(player->handB());
  CHECK(c->state() == kCustomerOrdering);  // one more order
  // a visit with the wrong thing in hand changes nothing; the right one is taken and the customer is done
  auto cup = VCarriable::create(m.ctx, 1);
  cup->setType("drink");
  player->detachObject(VPlayer::kHandA);
  REQUIRE(player->attachObject(cup));
  m.visit(station);
  CHECK(player->handA() == cup);
  CHECK(c->price() == doctest::Approx(10.0f));
  player->detachObject(VPlayer::kHandA);
  REQUIRE(player->attachObject(bottle));
  REQUIRE(player->attachObject(finishedBar(m, 0)));  // slot B: a second bar nobody wants
  m.visit(station);
  CHECK(c->state() == kCustomerMoney);  // served completely: the money is on the counter
  CHECK(c->price() == doctest::Approx(25.0f));
  CHECK_FALSE(player->handA());  // the bottle (slot A) was the one taken
  CHECK(player->handB());
}

TEST_CASE("VCustomer::payout: a pile of money on the counter, fading customer, the counters; the dollar sprites are placed by three draws each") {
  Cx m;
  auto station = m.makeCounter();
  auto player = m.makePlayer();
  m.ctx.rng.seed(77);
  auto c = m.comeIn(station, "professor", {Cx::plainOrder(kOrderSauce)});
  m.world->values().setValue("num_served", 0, true);
  m.visit(station);
  m.finishReading(c);
  auto bottle = VCarriable::create(m.ctx, 1);
  bottle->setType("sauce");
  REQUIRE(player->attachObject(bottle));
  std::mt19937 mirror(77);
  m.ctx.rng.seed(77);
  m.visit(station);
  CHECK(c->state() == kCustomerMoney);
  CHECK(c->price() == doctest::Approx(15.0f));
  // price 15: one extra dollar; its place: x = (draw & 3) + 2, negative when the next draw is even, y = -1; the flip from a third draw
  REQUIRE(c->money()->children().size() == 1);
  const unsigned d1 = mirror(), d2 = mirror(), d3 = mirror();
  float x = static_cast<float>(d1 & 3u) + 2.0f;
  if ((d2 & 1u) == 0) x = -x;
  const auto& dollar = c->money()->children().front();
  CHECK(dollar->position().x == x);
  CHECK(dollar->position().y == -1.0f);
  CHECK(dollar->hflip() == (((d3 ^ 1u) & 1u) != 0));
  CHECK(dollar->tint().r == doctest::Approx(0.77f));
  CHECK(dollar->leaf());
  CHECK(effectsOf<AlphaEffect>(*dollar) == 1);
  // the pile sits on the counter at its money position and shows the dollar
  CHECK(c->money()->parent() == station.get());
  CHECK(c->money()->position().x == 128);
  CHECK(c->money()->position().y == 293);
  CHECK(c->money()->currentState() == "dollar");
  CHECK(c->money()->leaf());
  // the customer fades out; the counters: served, in the level and in the saved total
  CHECK(effectsOf<AlphaEffect>(*c) == 2);
  CHECK(m.value("served_this_level") == 1);
  CHECK(m.value("num_served") == 1);
  CHECK(m.value("angry_this_level") == 0);
}

namespace {
// The customer is served everything it wants, one visit at a time; returns the pile's extra dollars.
std::size_t pileFor(Cx& m, const std::vector<OrderInfo>& orders) {
  auto station = m.makeCounter();
  auto c = m.comeIn(station, "diplomat", orders);
  m.visit(station);
  m.finishReading(c);
  while (c->state() == kCustomerOrdering) {
    const auto type = c->wanted()->type();
    std::shared_ptr<VCarriable> item;
    if (type == "chocolate") item = finishedBar(m, 0);
    else {
      item = VCarriable::create(m.ctx, 1);
      item->setType(type);
    }
    REQUIRE(c->serveCustomer(item));
    if (!c->hasMoreOrders()) {
      c->setState(kCustomerServed);
      c->payout();
    }
  }
  REQUIRE(c->state() == kCustomerMoney);
  return c->money()->children().size();
}
}  // namespace

namespace {
// A customer served its sauce and paid out at a counter: the pile of money is up and the customer waits for the avatar.
std::shared_ptr<VCustomer> customerWithPile(Cx& m, const std::shared_ptr<VStation>& station, const std::shared_ptr<VPlayer>& player) {
  auto c = m.comeIn(station, "professor", {Cx::plainOrder(kOrderSauce)});
  m.visit(station);
  m.finishReading(c);
  auto bottle = VCarriable::create(m.ctx, 1);
  bottle->setType("sauce");
  REQUIRE(player->attachObject(bottle));
  m.visit(station);
  REQUIRE(c->state() == kCustomerMoney);
  return c;
}
}  // namespace

TEST_CASE("VCustomer: in the endless run a pile of money left on the counter fades after endless_money_decay and the customer is done") {
  Cx m;
  m.world->values().setValue("endless_money_decay", 15000, false);
  m.shop.endless = true;
  auto station = m.makeCounter();
  auto player = m.makePlayer();
  auto c = customerWithPile(m, station, player);
  const int fadesBefore = effectsOf<AlphaEffect>(*c->money());
  m.run(14000);  // the stamp was set at the payout: not yet past the decay time
  CHECK(c->state() == kCustomerMoney);
  CHECK_FALSE(c->shouldDelete());
  m.run(1200);
  CHECK(c->state() == kCustomerDone);
  CHECK(effectsOf<AlphaEffect>(*c->money()) == fadesBefore + 1);  // the pile fades (transition_time)
  m.run(400);
  CHECK(c->shouldDelete());  // the fade has finished: the shop removes the customer
}

TEST_CASE("VCustomer: in story mode a pile of money never decays") {
  Cx m;
  m.world->values().setValue("endless_money_decay", 15000, false);
  m.shop.endless = false;
  auto station = m.makeCounter();
  auto player = m.makePlayer();
  auto c = customerWithPile(m, station, player);
  m.run(60000);
  CHECK(c->state() == kCustomerMoney);
  CHECK_FALSE(c->shouldDelete());
}

TEST_CASE("VCustomer::payout: the pile holds floor(price / 8) extra dollars up to five") {
  Cx m;
  m.makePlayer();
  CHECK(pileFor(m, {Cx::chocolateOrder(0)}) == 1);                                                                            // 10
  CHECK(pileFor(m, {Cx::plainOrder(kOrderSauce), Cx::chocolateOrder(0)}) == 3);                                               // 25
  CHECK(pileFor(m, {Cx::plainOrder(kOrderSauce), Cx::plainOrder(kOrderSauce), Cx::plainOrder(kOrderSauce)}) == 5);            // 45
  CHECK(pileFor(m, {Cx::plainOrder(kOrderSauce), Cx::plainOrder(kOrderSauce), Cx::plainOrder(kOrderSauce), Cx::chocolateOrder(0)}) == 5);  // 55
}

TEST_CASE("VCustomer::payout: an angry customer is paid for what it was served; no money for nothing; angry leavers are counted") {
  Cx m;
  auto station = m.makeCounter();
  auto player = m.makePlayer();
  m.world->values().setValue("num_angry", 0, true);
  m.world->gameStates().set("TotalCustomersLost", "0");
  auto c = m.comeIn(station, "professor", {Cx::plainOrder(kOrderSauce), Cx::plainOrder(kOrderSauce)});
  m.visit(station);
  m.finishReading(c);
  auto bottle = VCarriable::create(m.ctx, 1);
  bottle->setType("sauce");
  player->attachObject(bottle);
  m.visit(station);  // one served, one to come
  CHECK(c->state() == kCustomerOrdering);
  CHECK(c->price() == doctest::Approx(15.0f));
  c->setPatience(0.0f);
  c->updateMood();  // 0 or less in state 3: angry, it leaves with the money for the first
  CHECK(c->state() == kCustomerMoney);
  CHECK(m.value("angry_this_level") == 1);
  CHECK(m.value("served_this_level") == 0);
  CHECK(m.value("num_angry") == 1);
  CHECK(m.world->gameStates().get("TotalCustomersLost") == "1");
  CHECK(c->money()->parent() == station.get());
  // a patience of 0 does not touch a customer that is already served
  c->setState(kCustomerServed);
  c->updateMood();
  CHECK(c->state() == kCustomerServed);
}

TEST_CASE("VCustomer::onArrival at state 6: the pile is paid, the shop is told the price and the spot, the money fades, the customer is ready to go") {
  Cx m;
  auto station = m.makeCounter();
  auto player = m.makePlayer();
  auto c = m.comeIn(station, "professor", {Cx::plainOrder(kOrderSauce)}, "tut_cust_1");
  m.visit(station);
  m.finishReading(c);
  auto bottle = VCarriable::create(m.ctx, 1);
  bottle->setType("sauce");
  player->attachObject(bottle);
  m.visit(station);
  REQUIRE(c->state() == kCustomerMoney);
  CHECK(m.shop.payments.empty());
  m.visit(station);  // the next visit collects
  REQUIRE(m.shop.payments.size() == 1);
  CHECK(m.shop.payments[0].price == doctest::Approx(15.0f));
  CHECK(m.shop.payments[0].x == 128);
  CHECK(m.shop.payments[0].y == 243);  // the pile's place, 50 above
  CHECK(m.shop.payments[0].customer == c.get());
  CHECK(c->state() == kCustomerDone);
  CHECK(effectsOf<AlphaEffect>(*c->money()) == 1);
  CHECK_FALSE(c->shouldDelete());  // the fades are still going
  m.run(700);
  CHECK(c->shouldDelete());
  // the counter lets go: the money and the menu leave it, the customer forgets it, the counter is no mouse target again
  auto gone = station->detachCustomer();
  CHECK(gone == c);
  CHECK(c->money()->parent() == nullptr);
  CHECK(c->station() == nullptr);
  CHECK(station->customer() == nullptr);
  CHECK_FALSE(station->mouse());
  CHECK(station->detachCustomer() == nullptr);
  // a visit to the empty counter is harmless
  m.visit(station);
}

TEST_CASE("VCustomer::complimentaryService: only in states 1 to 3, and it adds the bonus without a cap") {
  Cx m;
  auto station = m.makeCounter();
  auto c = m.comeIn(station, "professor");
  auto cup = VCarriable::create(m.ctx, 1);
  CHECK_FALSE(c->complimentaryService(nullptr));
  CHECK(c->complimentaryService(cup));
  CHECK(c->patience() == doctest::Approx(125.0f));
  c->setState(kCustomerServed);
  CHECK_FALSE(c->complimentaryService(cup));
  c->setState(kCustomerOrdering);
  CHECK(c->complimentaryService(cup));
  CHECK(c->patience() == doctest::Approx(150.0f));
  c->setState(kCustomerPrototype);
  CHECK_FALSE(c->complimentaryService(cup));
}

TEST_CASE("VCustomer: a click on the customer is a click on its counter; the hover goes to the counter too") {
  Cx m;
  auto station = m.makeCounter();
  m.makePlayer();
  auto c = m.comeIn(station, "professor");
  int clicks = 0;
  struct Counter : SpriteListener {
    int* n;
    explicit Counter(int* p) : n(p) {}
    void onClick(VSprite&) override { ++*n; }
  };
  station->addListener(std::make_shared<Counter>(&clicks));
  m.step(0);
  c->doEvent(SpriteEvent::Click, {});
  CHECK(clicks == 1);
  c->doEvent(SpriteEvent::Enter, {});
  CHECK(station->hovered());
  c->doEvent(SpriteEvent::Leave, {});
  CHECK_FALSE(station->hovered());
}

TEST_CASE("VCustomer: the idle chatter starts after its cooldown, shows an idle sprite in place of the statics, and at most four play at once") {
  Cx m;
  VCustomer::resetIdleAnimsRunning();
  m.ctx.rng.seed(5);
  auto station = m.makeCounter();
  auto c = m.comeIn(station, "professor");  // its first update draws the first cooldown: base + (draw % (var + 1)) - var / 2
  std::mt19937 mirror(5);
  const int cooldown = static_cast<int>(mirror() % 5001u) - 2500 + 15000;
  CHECK(c->idleCooldownMs() == cooldown);
  CHECK_FALSE(c->idleActive());
  // the cooldown counts down; at 0 the happy idle plays (the statics are hidden), and when it ends they come back
  m.run(cooldown - 100);
  CHECK_FALSE(c->idleActive());
  CHECK(m.until([&] { return c->idleActive(); }, 400) < 400);
  CHECK(VCustomer::idleAnimsRunning() == 1);
  CHECK(c->idles().at("idle_happy")->visible());
  for (const auto& [k, o] : c->statics()) CHECK_FALSE(o->visible());
  m.run(1000);
  CHECK_FALSE(c->idleActive());
  CHECK(VCustomer::idleAnimsRunning() == 0);
  for (const auto& [k, o] : c->statics()) CHECK(o->visible());
  CHECK_FALSE(c->idles().at("idle_happy")->visible());
  // an angry customer plays the angry one
  c->setPatience(10);
  c->updateMood();
  REQUIRE(c->playIdleAnim());
  CHECK(c->idles().at("idle_angry")->visible());
  c->stopIdleAnim();
  CHECK(VCustomer::idleAnimsRunning() == 0);
  // the shop-wide limit of four
  std::vector<std::shared_ptr<VCustomer>> crowd;
  for (int i = 0; i < 5; ++i) crowd.push_back(m.customers.getCustomer("diplomat", &m.shop));
  int started = 0;
  for (auto& x : crowd) started += x->playIdleAnim();
  CHECK(started == 4);
  CHECK(VCustomer::idleAnimsRunning() == 4);
  // a customer that is destroyed gives its place back
  crowd.clear();
  CHECK(VCustomer::idleAnimsRunning() == 0);
}

// ================================================================ GenerateOrders

TEST_CASE("VCustomer::generateOrders: a customer with no scripted orders gets one chocolate, or two from the level's fifth on by the roll") {
  Cx m;
  auto station = m.makeCounter();
  m.world->levels().setCurrentLevel(3);
  m.shop.upgrades = {"kmixer1", "kpress1"};  // no sauce: every order is a chocolate
  m.updatePools();
  auto c = m.comeIn(station, "professor");
  c->generateOrders();
  CHECK(c->orders().size() == 1);  // level 3: below multi_order_min_level (5)
  CHECK(c->orders().front()->type() == "chocolate");
  m.world->levels().setCurrentLevel(9);  // chance = 10 * (9 - 5 + 1) = 50 %
  int two = 0;
  for (int i = 0; i < 200; ++i) {
    c->generateOrders();
    REQUIRE((c->orders().size() == 1 || c->orders().size() == 2));
    two += c->orders().size() == 2;
  }
  CHECK(two > 60);
  CHECK(two < 140);
  // a scripted customer's orders are used as they are
  auto d = m.comeIn(m.makeCounter(), "diplomat", {Cx::plainOrder(kOrderSauce)});
  d->generateOrders();
  CHECK(d->orders().size() == 1);
  CHECK(d->orders().front()->type() == "sauce");
}

TEST_CASE("VCustomer::generateOrders: one draw in six makes a sauce or a drink (the coin: odd is a sauce), the others a chocolate") {
  Cx m;
  auto station = m.makeCounter();
  m.world->levels().setCurrentLevel(1);
  auto c = m.comeIn(station, "professor");
  // a sauce on offer only: a roll of 0 is a sauce, and the coin is not drawn
  m.shop.upgrades = {"kmixer1", "kpress1", "ksauce1"};
  m.updatePools();
  int sauces = 0;
  for (std::uint32_t seed = 1; seed <= 40; ++seed) {
    m.ctx.rng.seed(seed);
    std::mt19937 mirror(seed);
    const bool hit = mirror() % 6u == 0;
    c->generateOrders();
    REQUIRE(c->orders().size() == 1);
    CHECK(c->orders().front()->type() == (hit ? "sauce" : "chocolate"));
    sauces += hit;
  }
  CHECK(sauces > 0);
  // a drink on offer too: the coin follows, an odd draw is a sauce whatever is on offer
  m.shop.upgrades = {"kmixer1", "kpress1", "ksauce1", "kdrink1"};
  m.updatePools();
  int drinks = 0;
  for (std::uint32_t seed = 1; seed <= 80; ++seed) {
    m.ctx.rng.seed(seed);
    std::mt19937 mirror(seed);
    c->generateOrders();
    const bool hit = mirror() % 6u == 0;
    if (!hit) continue;
    const bool odd = (mirror() & 1u) != 0;
    CHECK(c->orders().front()->type() == (odd ? "sauce" : "drink"));
    drinks += !odd;
  }
  CHECK(drinks > 0);
}

TEST_CASE("VCustomer::generateOrders: a chocolate that the machines cannot make forces a second order and moves to the back") {
  Cx m;
  auto station = m.makeCounter();
  m.world->levels().setCurrentLevel(1);
  m.shop.upgrades = {"kmixer1", "kpress1"};
  m.updatePools();
  // the pools hold dark bars only; the vendor's random picks come from them, so every random order is makeable...
  auto c = m.comeIn(station, "professor");
  for (std::uint32_t seed = 1; seed <= 30; ++seed) {
    m.ctx.rng.seed(seed);
    c->generateOrders();
    REQUIRE(c->orders().size() == 1);
    CHECK(m.vendor->canCreate(*static_cast<VChocolate*>(c->orders().front().get())));
  }
  // ...until an upgrade is taken away after the prototypes were chosen: a pool that has no shape for them
  m.shop.upgrades = {"kmixer1"};
  m.updatePools();
  m.ctx.rng.seed(3);
  c->generateOrders();
  CHECK(c->orders().size() == 2);  // nothing can be made: the list is forced to two orders
}

// ================================================================ the vendor builds a chocolate from an order

namespace {
void allUpgrades(Cx& m) {
  m.shop.upgrades = {"kmixer1", "kmixer2", "kmixer3", "kpress1", "kpress2", "kpress3", "kingredient1", "kingredient2", "kingredient3",
                     "ktopping1", "ktopping2", "ktopping3", "kinfusion1", "kinfusion2", "kinfusion3"};
  m.updatePools();
}
const LayerSpec& layerOf(const VChocolate& c, int slot) {
  REQUIRE(c.spec().slot(slot).has_value());
  return *c.spec().slot(slot);
}
}  // namespace

TEST_CASE("VChocolateVendor::getChocolate: an order of the level file is an exact specification and draws nothing; the budget is spent on the shape") {
  Cx m;
  allUpgrades(m);
  m.ctx.rng.seed(41);
  OrderInfo bar = Cx::chocolateOrder(1, kShapeTruffle);
  auto c = m.vendor->getChocolate(bar, true);
  REQUIRE(c);
  CHECK(c->name().compare(0, 10, "chocolate_") == 0);
  CHECK(c->type() == "chocolate");
  CHECK(c->barType() == kShapeTruffle);
  CHECK(layerOf(*c, kLayerChocolate).option == 1);
  CHECK(layerOf(*c, kLayerChocolate).barType == kShapeTruffle);
  CHECK(c->numLayers() == 1);
  CHECK_FALSE(c->spec().slot(kLayerIngredient).has_value());
  CHECK_FALSE(c->spec().slot(kLayerTopping).has_value());
  std::mt19937 mirror(41);
  CHECK(m.ctx.rng.next32() == mirror());  // not one draw was made
  // the same order compares equal to a pressed truffle of that mix
  auto made = m.vendor->getEmptyChocolate(1, false);
  made->addLayer(kShapeTruffle, kLayerChocolate, 1);
  CHECK(c->compare(made.get()));
  // an order that is not for a chocolate gives nothing, and the counter that names chocolates does not move
  const int count = m.vendor->chocolateCount();
  CHECK(m.vendor->getChocolate(Cx::plainOrder(kOrderSauce), true) == nullptr);
  CHECK(m.vendor->getChocolate(Cx::plainOrder(kOrderInvalid), true) == nullptr);
  CHECK(m.vendor->chocolateCount() == count);
  CHECK(m.vendor->getChocolate(bar, true)->name() == "chocolate_" + std::to_string(count + 1));
}

TEST_CASE("VChocolateVendor::getChocolate: pinned layers are added whatever the budget; random ones draw from the pools in slot order") {
  Cx m;
  allUpgrades(m);
  OrderInfo o = Cx::chocolateOrder(2, kShapeBar);
  o.layer[kLayerIngredient] = 1;
  o.layer[kLayerTopping] = 0;
  m.ctx.rng.seed(8);
  auto c = m.vendor->getChocolate(o, true);
  CHECK(layerOf(*c, kLayerIngredient).option == 1);
  CHECK(layerOf(*c, kLayerChocolate).option == 2);
  CHECK(layerOf(*c, kLayerTopping).option == 0);
  CHECK(c->numLayers() == 3);
  std::mt19937 mirror(8);
  CHECK(m.ctx.rng.next32() == mirror());  // pinned: no draw
  // random options: the mix, then the filling, then the topping, each a draw % 3 from the sorted options of the pool
  OrderInfo r;
  r.type = kOrderChocolate;
  r.shape = kShapeBar;
  r.layer[kLayerIngredient] = -1;
  r.layer[kLayerTopping] = -1;
  r.layerBudget = 3;
  m.ctx.rng.seed(9);
  std::mt19937 g(9);
  auto d = m.vendor->getChocolate(r, true);
  const unsigned mix = g() % 3u, filling = g() % 3u, topping = g() % 3u;
  CHECK(layerOf(*d, kLayerChocolate).option == static_cast<int>(mix));
  CHECK(layerOf(*d, kLayerIngredient).option == static_cast<int>(filling));
  CHECK(layerOf(*d, kLayerTopping).option == static_cast<int>(topping));
  // a filling asked for at random that already took the last of the budget: nothing is added after it (the budget was 3 - 1 - 2)
  r.layerBudget = 2;
  m.ctx.rng.seed(10);
  auto e = m.vendor->getChocolate(r, true);
  CHECK(e->numLayers() == 3);  // shape, filling and topping were all asked for
}

TEST_CASE("VChocolateVendor::getChocolate: what is left of the budget buys a random filling (a coin with one left) and a random topping") {
  Cx m;
  allUpgrades(m);
  // a random order of three layers: after the shape two are left, which buys a filling and then a topping
  OrderInfo r;
  r.type = kOrderChocolate;
  r.shape = kShapeBar;
  r.mix = 0;
  r.layerBudget = 3;
  m.ctx.rng.seed(21);
  std::mt19937 g(21);
  auto c = m.vendor->getChocolate(r, true);
  const unsigned fill = g() % 3u, top = g() % 3u;
  CHECK(layerOf(*c, kLayerIngredient).option == static_cast<int>(fill));
  CHECK(layerOf(*c, kLayerTopping).option == static_cast<int>(top));
  // two layers: one is left, a coin decides the filling (odd skips it) and the topping follows only when the filling was skipped
  r.layerBudget = 2;
  int withFilling = 0, withTopping = 0;
  for (std::uint32_t seed = 1; seed <= 30; ++seed) {
    m.ctx.rng.seed(seed);
    std::mt19937 h(seed);
    auto d = m.vendor->getChocolate(r, true);
    const bool skip = (h() & 1u) != 0;
    CHECK(d->spec().slot(kLayerIngredient).has_value() == !skip);
    CHECK(d->spec().slot(kLayerTopping).has_value() == skip);
    withFilling += !skip;
    withTopping += skip;
  }
  CHECK(withFilling > 5);
  CHECK(withTopping > 5);
  // one layer: nothing is left
  r.layerBudget = 1;
  auto e = m.vendor->getChocolate(r, true);
  CHECK(e->numLayers() == 1);
  // a random filling asked for is built in the loop; with one layer of budget left the coin then either adds a topping (odd) or
  // builds the filling again over the first (even): the original's extra branch does not look at what the slots already hold
  OrderInfo f;
  f.type = kOrderChocolate;
  f.shape = kShapeBar;
  f.mix = 0;
  f.layer[kLayerIngredient] = -1;
  f.layerBudget = 3;
  int again = 0, topped = 0;
  for (std::uint32_t seed = 50; seed < 80; ++seed) {
    m.ctx.rng.seed(seed);
    std::mt19937 k(seed);
    auto h = m.vendor->getChocolate(f, true);
    const unsigned first = k() % 3u;
    const bool coinOdd = (k() & 1u) != 0;
    if (coinOdd) {
      CHECK(layerOf(*h, kLayerIngredient).option == static_cast<int>(first));
      CHECK(layerOf(*h, kLayerTopping).option == static_cast<int>(k() % 3u));
      ++topped;
    } else {
      CHECK(layerOf(*h, kLayerIngredient).option == static_cast<int>(k() % 3u));  // the second pick replaced the first
      CHECK_FALSE(h->spec().slot(kLayerTopping).has_value());
      ++again;
    }
  }
  CHECK(again > 5);
  CHECK(topped > 5);
}

TEST_CASE("VChocolateVendor::getChocolate: an unspecified shape is a draw from the shapes the machines make; infusion bars carry their infusion") {
  Cx m;
  allUpgrades(m);
  OrderInfo u;
  u.type = kOrderChocolate;
  u.mix = 0;
  u.layerBudget = 1;
  m.ctx.rng.seed(30);
  std::mt19937 g(30);
  auto c = m.vendor->getChocolate(u, true);
  CHECK(c->barType() == static_cast<int>(g() % 4u));  // bar, truffle, novelty and (with kInfusion1) infusion
  // an infusion order: no filling, the infusion layer is always there (a random option when none is pinned), the mix gives the shape layer
  OrderInfo inf = Cx::chocolateOrder(1, kShapeInfusion);
  inf.layerBudget = 2;
  m.ctx.rng.seed(31);
  std::mt19937 h(31);
  auto d = m.vendor->getChocolate(inf, true);
  CHECK(d->barType() == kShapeInfusion);
  CHECK(layerOf(*d, kLayerInfusion).option == static_cast<int>(h() % 3u));
  CHECK_FALSE(d->spec().slot(kLayerIngredient).has_value());
  CHECK(layerOf(*d, kLayerChocolate).option == 1);
  // a filling asked of an infusion order is ignored; a pinned infusion option is used
  inf.layer[kLayerIngredient] = 0;
  inf.layer[kLayerInfusion] = 2;
  auto e = m.vendor->getChocolate(inf, true);
  CHECK_FALSE(e->spec().slot(kLayerIngredient).has_value());
  CHECK(layerOf(*e, kLayerInfusion).option == 2);
  // no shapes at all: -1, and no shape layer
  m.shop.upgrades = {};
  m.updatePools();
  m.vendor->pools().updatePool(2, false, ChocolatePools::kAllBars, ChocolatePools::kBarSet, 0);
  auto none = m.vendor->getChocolate(u, true);
  CHECK(none->barType() == -1);
}

TEST_CASE("VChocolateVendor::getChocolate: a displayed shape or mix can replace the order's, a draw of 100 percent always does") {
  Cx m;
  allUpgrades(m);
  auto shown = m.vendor->getEmptyChocolate(2, false);
  shown->addLayer(kShapeNovelty, kLayerChocolate, 2);
  m.vendor->addDisplayChocolate(*shown);
  m.world->values().setValue("display_influence_shape", 100, false);
  m.world->values().setValue("display_influence_mix", 100, false);
  auto c = m.vendor->getChocolate(Cx::chocolateOrder(0, kShapeBar), true);
  CHECK(c->barType() == kShapeNovelty);
  CHECK(layerOf(*c, kLayerChocolate).option == 2);
  // an infusion order keeps its shape
  auto d = m.vendor->getChocolate(Cx::chocolateOrder(0, kShapeInfusion), true);
  CHECK(d->barType() == kShapeInfusion);
  m.world->values().setValue("display_influence_shape", 0, false);
  m.world->values().setValue("display_influence_mix", 0, false);
  m.vendor->clearDisplayChocolates();
  CHECK(m.vendor->getChocolate(Cx::chocolateOrder(0, kShapeBar), true)->barType() == kShapeBar);
}

// ---------------------------------------------------------------------------------------------------------------- the tutorial triggers

namespace {

// A tutorial that starts at the arrival (or the order) of the customer named `object`.
Tbl triggerTutorials(const std::string& type, const std::string& object) {
  auto root = tbl();
  auto modes = tbl();
  modes->append(0.0);
  root->set("modes", modes);
  auto t = tbl();
  t->set("trigger_type", type).set("trigger_object", object).set("minlevel", 1.0).set("maxlevel", 1.0);
  auto step = tbl();
  step->set("x", 10.0).set("y", 10.0);  // no size: no panel
  auto steps = tbl();
  steps->append(step);
  t->set("steps", steps);
  root->set("tutorial_x", t);
  return root;
}

}  // namespace

TEST_CASE("VCustomer: the tutorial trigger customer_arrival fires at setActive with the customer's name") {
  Cx m;
  m.world->tutorials().loadFromTable(*triggerTutorials("customer_arrival", "tut_cust_1"));
  m.world->tutorials().setCurrentLevel(1);
  auto station = m.makeCounter();
  m.makePlayer();
  auto other = m.comeIn(station, "professor", {}, "someone_else");
  CHECK_FALSE(m.world->tutorials().active());  // another customer's name
  other->setActive(station, 100.0f);
  m.world->tutorials().resetTutorials();
  station->detachCustomer();
  auto c = m.comeIn(station, "professor", {}, "tut_cust_1");
  REQUIRE(m.world->tutorials().active());
  CHECK(m.world->tutorials().active()->tutorialName() == "tutorial_x");
  CHECK(m.world->tutorials().active()->stepNumber() == 1);
  CHECK(c->state() == kCustomerArrived);
}

TEST_CASE("VCustomer: the tutorial trigger customer_order fires when the menu has been read, not at the arrival") {
  Cx m;
  m.world->tutorials().loadFromTable(*triggerTutorials("customer_order", "tut_cust_1"));
  m.world->tutorials().setCurrentLevel(1);
  auto station = m.makeCounter();
  m.makePlayer();
  auto c = m.comeIn(station, "professor", {Cx::chocolateOrder()}, "tut_cust_1");
  CHECK_FALSE(m.world->tutorials().active());
  m.visit(station);
  CHECK_FALSE(m.world->tutorials().active());  // reading the menu
  m.finishReading(c);
  REQUIRE(m.world->tutorials().active());
  CHECK(m.world->tutorials().active()->tutorialName() == "tutorial_x");
}

namespace {

// A tutorial of two steps that each wait for `trigger` on `name`, activated without a window (the listeners are attached by hand).
std::shared_ptr<Tutorial> twoStepTutorial(Cx& m, int trigger, const std::string& name) {
  auto step = [&] {
    auto s = tbl();
    auto e = tbl();
    e->append(name).append(static_cast<double>(trigger));
    if (trigger == kResume) e->set("action", 0.0);
    auto trig = tbl();
    trig->append(e);
    s->set("trigger_objects", trig);
    return s;
  };
  auto t = tbl();
  auto steps = tbl();
  steps->append(step()).append(step());
  t->set("steps", steps);
  auto tut = Tutorial::create(m.ctx, "x", 0);
  const StyleChain chain(*t, nullptr);
  tut->loadFromTable(chain);
  m.mgr.addObject(tut);  // its fade-ins run on the scene's clock
  tut->activate(true);
  return tut;
}

}  // namespace

TEST_CASE("VStation::onArrival tells the tutorial listeners of the station (kArrival), after the customer") {
  Cx m;
  auto station = m.makeCounter();
  m.makePlayer();
  auto tut = twoStepTutorial(m, kArrival, station->name());
  station->addListener(std::make_shared<TutorialListener>(tut));
  m.step(0);  // the listener is taken at the sprite's next update
  CHECK(tut->stepNumber() == 1);
  m.run(600);  // the first step's fade in
  m.arrive(station);
  CHECK(tut->stepNumber() == 2);
}

TEST_CASE("VStation::onArrival does not tell them while the station is suspended") {
  Cx m;
  auto station = m.makeCounter();
  m.makePlayer();
  auto tut = twoStepTutorial(m, kArrival, station->name());
  station->addListener(std::make_shared<TutorialListener>(tut));
  m.step(0);
  m.run(600);
  station->suspend(100000);
  m.arrive(station);
  CHECK(tut->stepNumber() == 1);
}

namespace {
// A suspendable that asks for one more wait at the first end of its wait.
struct Sleeper : VSuspendable {
  Sleeper(script::ScriptContext& c, const std::string& n) : VSuspendable(c, n, "") {}
  bool onResume() override {
    if (again) {
      again = false;
      suspend(50);
      return false;
    }
    return true;
  }
  bool again = false;
};
}  // namespace

TEST_CASE("VSuspendable: the end of a wait tells the tutorial listeners once it is really over; Reset does too") {
  Cx m;
  auto s = std::make_shared<Sleeper>(m.ctx, "sleeper");
  m.mgr.addObject(s);
  auto tut = twoStepTutorial(m, kResume, "sleeper");
  s->addListener(std::make_shared<TutorialListener>(tut));
  m.step(0);
  m.run(600);
  // the first end of the wait sets another wait: not over, no news
  s->again = true;
  s->suspend(100);
  m.run(120);
  CHECK(s->suspended());
  CHECK(tut->stepNumber() == 1);
  m.run(100);  // the second wait ends for good
  CHECK_FALSE(s->suspended());
  CHECK(tut->stepNumber() == 2);
  // Reset of a suspended one resumes it and tells them too
  auto tut2 = twoStepTutorial(m, kResume, "sleeper");
  s->addListener(std::make_shared<TutorialListener>(tut2));
  m.step(0);
  m.run(600);
  s->suspend(100000);
  s->reset();
  CHECK_FALSE(s->suspended());
  CHECK(tut2->stepNumber() == 2);
}
