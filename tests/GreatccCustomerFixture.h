#pragma once

// The customers' fixtures of the Great Chocolate Chase tests (slices 13 and 15): the posse, locals and objects tables, and the `Cx`
// harness that adds a customer vendor and counters to the machines' `Mx`. Included by the test files that need them.
#include "GreatccMachineFixture.h"

#include "games/greatcc/core/CustomerLogic.h"
#include "games/greatcc/shop/CustomerVendor.h"
#include "games/greatcc/shop/ThoughtBubble.h"

namespace {

// ---------------------------------------------------------------------------------------------------------------- tables

Tbl customerTable(const std::string& name, double decay, double tip) {
  auto t = tbl();
  t->set("baseDir", "anim/").set("name", name).set("decay", decay).set("tip", tip);
  t->set("assets", assetsOf({{"base", group({state("default", "base", 0)})}}));
  auto body = tbl();
  body->set("layer", 1.0).set("assets", assetsOf({{"base", group({state("default", "still", 0), state("angry", "still", 1), state("menu", "still", 2)})}}));
  t->set("static_body", body);
  auto head = tbl();
  head->set("layer", 0.0).set("continuous", 1.0);
  head->set("assets", assetsOf({{"base", group({state("happy", "still", 0), state("neutral", "still", 1), state("annoyed", "still", 2),
                                                 state("angry", "still", 3), state("menu", "still", 1)})}}));
  t->set("static_head", head);
  for (const char* idle : {"idle_happy", "idle_angry"}) {
    auto i = tbl();
    i->set("layer", 1.0).set("assets", assetsOf({{"base", group({state("default", "base", 0)})}}));
    t->set(idle, i);
  }
  return t;
}

Tbl posseTable() {
  auto posse = tbl();
  posse->set("leaf", 1.0);
  posse->set("professor", customerTable("professor", 100000, 8));
  posse->set("diplomat", customerTable("diplomat", 0.002, 5));  // a rate below 1 is used as it is
  auto junk = tbl();
  junk->set("name", "thought_bubble");
  posse->set("thought_bubble", junk);
  return posse;
}

Tbl localsTable() {
  auto locals = tbl();
  locals->set("iqu_male", customerTable("iqu_male", 80000, 8));
  locals->set("professor", customerTable("local_professor", 50000, 3));  // the posse's professor wins
  return locals;
}

Tbl objectsTable() {
  auto objects = tbl();
  objects->set("baseDir", "anim/");
  auto bubble = tbl();
  bubble->set("name", "bubble").set("layer", 100.0);
  bubble->set("assets", assetsOf({{"bubble_single", group({state("happy", "still", 0), state("content", "still", 1), state("neutral", "still", 2),
                                                           state("annoyed", "still", 3), state("angry", "still", 4)})},
                                  {"transparent", group({state("default", "plain", 0, false)})}}));
  objects->set("thought_bubble", bubble);
  auto menu = tbl();
  menu->set("name", "menu").set("anchor", "menu").set("continuous", 1.0);
  menu->set("assets", assetsOf({{"menu", group({state("default", "plain", 0, false), state("dots", "still", 0)})}}));
  objects->set("anim_menu", menu);
  auto money = tbl();
  money->set("name", "money");
  money->set("tint", std::vector<double>{0.77, 0.27, 0.27, 1.0});
  money->set("assets", assetsOf({{"money", group({state("dollar", "still", 0)})}}));
  objects->set("anim_money", money);
  auto orderables = tbl();
  orderables->set("name", "other_order").set("anchor", "sauce").set("type", "sauce");
  orderables->set("assets", assetsOf({{"sauce", group({state("default", "still", 0)})}, {"drink", group({state("default", "still", 1)})}}));
  objects->set("orderables", orderables);
  return objects;
}

// ---------------------------------------------------------------------------------------------------------------- fixture

struct Cx : Mx {
  Cx() : Mx(), customers(ctx) {
    for (const auto& [name, v] : std::vector<std::pair<const char*, double>>{
             {"patience_decay_modifier", 1.0}, {"level_customer_decay", 1.0}, {"order_patience_bonus", 20}, {"complimentary_patience_bonus", 25},
             {"sauce_price", 15}, {"drink_price", 15}, {"customer_fade_time", 500}, {"transition_time", 200}, {"thought_offset", 5},
             {"thought_tint", 0.3}, {"customer_idle_cooldown_base", 15000}, {"customer_idle_cooldown_var", 5000}, {"num_sauce_sold", 0},
             {"num_served", 0}, {"num_angry", 0}, {"served_this_level", 0}, {"angry_this_level", 0}, {"multi_order_min_level", 5},
             {"multi_order_chance", 10}, {"endless_multi_chance", 0.001}, {"endless_diff_modifier", 0}})
      world->values().setValue(name, v, false);
    shop.upgrades = {"kmixer1", "kpress1", "ksauce1"};
    world->values().setValue("saved_marker", 0, false);
    posse = posseTable();
    locals = localsTable();
    objects = objectsTable();
    customers.loadPosse(*posse);
    customers.loadObjects(*objects);
    customers.loadLocals(*locals);
    updatePools();
  }
  // The vendor's pools follow the shop's upgrades.
  void updatePools() {
    vendor->updateCapability(true, [&](const char* name) {
      std::string l = name;
      for (char& c : l) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      return shop.upgrades.count(l) != 0;
    });
  }
  // A counter (station 1, customer point customer01 at (5, 6), the pile of money at (128, 293)), on the scene.
  std::shared_ptr<VStation> makeCounter(int id = 1) {
    auto s = VStation::create(ctx, 0, kStationCounter);
    s->setHost(&shop);
    auto t = stationTable(id, kStationCounter, "start", {});
    char name[16];
    std::snprintf(name, sizeof name, "station%02i", id);
    t->set("name", name);
    auto m = tbl();
    m->append(128.0).append(293.0);
    t->set("money", m);
    const StyleChain sc(*t, nullptr);
    s->loadFromTable(sc);
    s->resolvePoints(*graph);
    s->init();
    shop.stations.push_back(s);
    mgr.addObject(s);
    return s;
  }
  // A customer of `type` with the given orders, on the scene and at `station` (state 1).
  std::shared_ptr<VCustomer> comeIn(const std::shared_ptr<VStation>& station, const std::string& type = "professor",
                                    std::vector<OrderInfo> orders = {}, const std::string& name = "") {
    auto c = customers.getCustomer(type, &shop);
    REQUIRE(c);
    auto data = std::make_shared<CustomerData>();
    data->type = type;
    data->name = name;
    data->orders = std::move(orders);
    c->setCustomerData(data);
    mgr.addObject(c);
    c->setActive(station, 100.0f);
    step(0);
    return c;
  }
  static OrderInfo chocolateOrder(int mix = 0, int shape = kShapeBar) {
    OrderInfo o;
    o.type = kOrderChocolate;
    o.shape = shape;
    o.mix = mix;
    o.layerBudget = 1;
    return o;
  }
  static OrderInfo plainOrder(int type) {
    OrderInfo o;
    o.type = type;
    return o;
  }
  // The avatar visits (what her arrival does).
  void visit(const std::shared_ptr<VStation>& s) { arrive(s); }
  // The reading of the menu has run out: the orders show.
  void finishReading(const std::shared_ptr<VCustomer>& c) {
    run(c->orderDelayMs() + 20);
    REQUIRE(c->state() == kCustomerOrdering);
  }
  double value(const char* name, double def = 0) { return world->values().getReal(name, def); }

  CustomerVendor customers;
  Tbl posse, locals, objects;
};

[[maybe_unused]] std::shared_ptr<VChocolate> finishedBar(Cx& m, int mix) {
  auto c = m.vendor->getEmptyChocolate(mix, false);
  c->addLayer(kShapeBar, kLayerChocolate, mix);
  return c;
}

}  // namespace
