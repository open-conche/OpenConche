#pragma once

// The fixtures of the Great Chocolate Chase's machine and customer tests (docs/design/greatcc.md section 6, slices 11-13): synthetic sheets,
// tables and zone file, a fake shop that answers the sprites' questions, and the `Mx` harness that builds the stations, the avatar
// and the vendor on a sprite manager. Included by the test files that need them (the contents are in an anonymous namespace).
#include <doctest/doctest.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "assets/AssetFS.h"
#include "games/Registry.h"
#include "games/greatcc/GreatCCWorld.h"
#include "games/greatcc/core/ZoneGraph.h"
#include "games/greatcc/scene/SpriteManager.h"
#include "games/greatcc/shop/ShopHost.h"
#include "games/greatcc/shop/VCustomer.h"
#include "games/greatcc/shop/VChocolate.h"
#include "games/greatcc/core/DisplayInfluence.h"
#include "games/greatcc/core/GameStateStore.h"
#include "games/greatcc/shop/VDisplay.h"
#include "games/greatcc/shop/VDisposal.h"
#include "games/greatcc/shop/VMachine.h"
#include "games/greatcc/shop/VMixer.h"
#include "games/greatcc/shop/VPlayer.h"
#include "games/greatcc/shop/VPress.h"
#include "games/greatcc/shop/VSauceDrink.h"
#include "games/greatcc/shop/VStation.h"
#include "gfx/SheetCache.h"
#include "gfx/TextureCache.h"
#include "platform/Png.h"
#include "GreatccAnm.h"
#include "ScriptHarness.h"
#include "ui/Style.h"

using namespace choco;
using namespace choco::games::greatcc;
using greatcc_test::FrameSpec;
using greatcc_test::makeAnm;
namespace fs = std::filesystem;

namespace {

using MS = ui::MapStyle;
using Tbl = std::shared_ptr<MS>;
Tbl tbl() { return std::make_shared<MS>(); }

Tbl state(const std::string& name, const std::string& file, int base = 0, bool anim = true) {
  auto t = tbl();
  t->set("state", name).set("file", file).set("base", static_cast<double>(base));
  if (!anim) t->set("anim", 0.0);
  return t;
}
Tbl group(std::vector<Tbl> states) {
  auto t = tbl();
  for (auto& s : states) t->append(s);
  return t;
}
Tbl assetsOf(std::vector<std::pair<std::string, Tbl>> groups) {
  auto t = tbl();
  for (auto& [n, g] : groups) t->set(n, g);
  return t;
}
// n frame references cycling over the four frames of the test sheet
std::string frames(int n) {
  std::string s = "frames{";
  for (int i = 0; i < n; ++i) s += (i ? "," : "") + std::to_string(i % 4 + 1);
  return s + "}";
}

// frames{1, 2, ... n}: every frame of a sheet once, in order
std::string seq(int n) {
  std::string s = "frames{";
  for (int i = 1; i <= n; ++i) s += (i > 1 ? "," : "") + std::to_string(i);
  return s + "}";
}

void writeFile(const fs::path& p, const std::string& bytes) {
  fs::create_directories(p.parent_path());
  std::ofstream(p, std::ios::binary) << bytes;
}

// The sheets of the machines: scripts with the original's frame counts and delays (docs/re/greatcc-machines-and-chocolates.md 10.5).
struct MachineAssets {
  MachineAssets() {
    dir = fs::temp_directory_path() / "choco_greatcc_machines";
    fs::remove_all(dir);
    fs::create_directories(dir / "anim");
    REQUIRE(platform::writePng((dir / "anim/sheet.png").string(), 40, 10, std::vector<unsigned char>(40 * 10 * 4, 255).data()));
    REQUIRE(platform::writePng((dir / "anim/plain.png").string(), 30, 20, std::vector<unsigned char>(30 * 20 * 4, 255).data()));
    std::vector<FrameSpec> four;
    for (int i = 0; i < 4; ++i) {
      FrameSpec f;
      f.x = i * 10;
      f.anchors = {{3, 4, "chocolate"}, {6, 7, "bucket"}};
      four.push_back(f);
    }
    auto put = [&](const char* name, const std::string& script) { writeFile(dir / "anim" / (std::string(name) + ".anm"), makeAnm(four, script)); };
    put("base", "function DoAnim() frames{1} end");
    put("still", "function DoAnim() frames{1} end");
    const std::string d = "delay(getdelay()) ";
    put("arm", "function DoAnim() " + d + frames(31) + " end function Reverse() " + d + frames(31) + " end");
    put("tube", "function DoAnim() delay(60) " + frames(7) + " end function Reverse() delay(60) " + frames(7) + " end");
    put("spray", "function DoAnim() delay(60) " + frames(5) + " end");
    put("large", "function DoAnim() " + d + frames(5) + " unlock() " + frames(26) + " end");
    put("misc", "function DoAnim() " + d + frames(31) + " end function DoDoor() delay(20) " + frames(6) + " drop() " + frames(25) + " end");
    std::string fill;
    for (int i = 0; i < 3; ++i) fill += "function DoAnim_" + std::to_string(i) + "() delay(60) " + frames(6) + " end ";
    put("fill", fill);
    // the sauce machine's sheets: 32 frames each (sauce_base holds the stills and the spray scripts)
    std::vector<FrameSpec> many;
    for (int i = 0; i < 32; ++i) {
      FrameSpec f;
      f.x = (i % 4) * 10;
      f.anchors = {{3, 4, "bottle"}};
      many.push_back(f);
    }
    auto putMany = [&](const char* name, const std::string& script) { writeFile(dir / "anim" / (std::string(name) + ".anm"), makeAnm(many, script)); };
    putMany("sbase", "function DoSauceFill() delay(60) " + frames(8) + " end function DoDrinkFill() delay(60) " + frames(7) +
                         " end function DoDrinkSteam() delay(30) " + frames(5) + " end");
    const std::string arm30 = "function DoAnim() delay(30) " + seq(31) + " end function Reverse() delay(30) " + seq(31) + " end";
    putMany("sdisp", arm30);
    putMany("stop", arm30);
    putMany("sfill", "function DoAnim() delay(30) " + seq(32) + " end function Reverse() delay(30) " + seq(32) + " end");
    putMany("cup", "function DoAnim() delay(60) " + frames(18) + " end");
    put("disposal", "function DoAnim() delay(70) " + frames(7) + " end");
    // the ingredient, topping and infusion machines' sheets (docs/re/greatcc-machines-and-chocolates.md 10.5)
    put("iintro", "function DoAnim() " + d + frames(22) + " end function Reverse() " + d + frames(22) + " end");
    put("ipump", "function DoAnim() " + d + frames(27) + " end");
    put("itop", "function DoAnim() " + d + frames(30) + " end function Reverse() " + d + frames(30) + " end");
    put("tfunnel", "function DoAnim() " + d + frames(31) + " end");
    put("tpart", "function DoParticles() delay(20) " + frames(30) + " end");
    put("itopw", "function DoAnim() " + d + frames(25) + " unlock() " + frames(6) + " end");
    put("idoor", "function DoDoor() delay(20) " + frames(7) + " drop() " + frames(27) + " end");
    put("ibase", "function DoAnim() " + d + frames(31) + " end");
    put("cbase", "function DoAnim() delay(100) " + frames(9) + " end");
    // the delivery's boxing sheet: 62 frames, the front half and the back half
    std::vector<FrameSpec> sixtyTwo(62);
    std::string back = "frames{";
    for (int i = 32; i <= 62; ++i) back += (i > 32 ? "," : "") + std::to_string(i);
    writeFile(dir / "anim/dwork.anm",
              makeAnm(sixtyTwo, "function DoFront() delay(60) " + seq(31) + " end function DoAnim() delay(60) " + back + "} end"));
    // the airship's sheets, where the code looks for them
    fs::create_directories(dir / "anim/machines/delivery");
    REQUIRE(platform::writePng((dir / "anim/machines/delivery/sheet.png").string(), 40, 10, std::vector<unsigned char>(40 * 10 * 4, 255).data()));
    writeFile(dir / "anim/machines/delivery/zepp_no_box.anm", makeAnm({FrameSpec{}}, ""));
    writeFile(dir / "anim/machines/delivery/zepp_with_box.anm", makeAnm({FrameSpec{}}, ""));
    std::vector<FrameSpec> pickup(22);
    writeFile(dir / "anim/machines/delivery/zepp_pickup.anm", makeAnm(pickup, "function DoAnim() delay(30) " + seq(22) + " end"));
    std::vector<FrameSpec> table(2);
    for (FrameSpec& f : table) f.anchors = {{12, 20, "chocolate1"}, {30, 22, "chocolate2"}};
    writeFile(dir / "anim/ctable.anm", makeAnm(table, "function DoAnim() frames{1} end"));
    afs = std::make_unique<assets::AssetFS>(dir.string(), "");
    textures = std::make_unique<gfx::TextureCache>(*afs, nullptr);
    sheets = std::make_unique<gfx::SheetCache>(*afs, *textures);
  }
  fs::path dir;
  std::unique_ptr<assets::AssetFS> afs;
  std::unique_ptr<gfx::TextureCache> textures;
  std::unique_ptr<gfx::SheetCache> sheets;
};

// ---- tables ----

Tbl buttonAssets() {
  std::vector<Tbl> icons;
  for (const char* n : {"dark", "milk", "white", "bar", "truffle", "novelty"}) icons.push_back(state(n, "plain", 0, false));
  return assetsOf({{"button", group({state("default", "plain", 0, false), state("over", "plain", 0, false)})},
                   {"icon", group(icons)},
                   {"stripe", group({state("stripe_00", "plain", 0, false), state("stripe_01", "plain", 0, false)})}});
}
Tbl buttonEntry(int id, double x, double y, const std::string& name, const std::string& icon, const std::string& stripe = {}) {
  auto t = tbl();
  t->set("baseDir", "anim/").set("name", name).set("id", static_cast<double>(id)).set("x", x).set("y", y).set("icon", icon);
  if (!stripe.empty()) t->set("stripe", stripe);
  t->set("assets", buttonAssets());
  return t;
}

Tbl childOf(const std::string& file, std::vector<std::pair<std::string, Tbl>> extraAssets = {}, int layer = 0) {
  auto c = tbl();
  c->set("layer", static_cast<double>(layer));
  std::vector<std::pair<std::string, Tbl>> a;
  a.push_back({"anim", group({state("default", file)})});
  for (auto& e : extraAssets) a.push_back(e);
  c->set("assets", assetsOf(a));
  return c;
}

Tbl mixerStyle() {
  auto t = tbl();
  t->set("baseDir", "anim/").set("name", "machine_mixer").set("type", "machine").set("spriteLayer", 7.0);
  t->set("assets", assetsOf({{"base", group({state("default", "base", 2)})}}));
  auto ch = tbl();
  ch->set("leaf", 1.0);
  ch->set("anim_arm", childOf("arm"));
  auto tube = tbl(), spray = tbl();
  tube->set("layer", 3.0).set("assets", assetsOf({{"anim", group({state("option_0", "tube"), state("option_1", "tube"), state("option_2", "tube")})}}));
  spray->set("layer", 2.0).set("assets", assetsOf({{"anim", group({state("option_0", "spray"), state("option_1", "spray"), state("option_2", "spray")})}}));
  ch->set("anim_tube", tube);
  ch->set("anim_spray", spray);
  ch->set("panel", childOf("still"));
  for (int i = 1; i <= 3; ++i) {
    auto tank = childOf("still");
    tank->set("visible", 0.0);
    ch->set("tank" + std::to_string(i), tank);
  }
  t->set("children", ch);
  return t;
}
Tbl pressStyle() {
  auto t = tbl();
  t->set("baseDir", "anim/").set("name", "machine_press").set("type", "machine").set("spriteLayer", 7.0);
  t->set("assets", assetsOf({{"base", group({state("default", "base", 3)})}}));
  auto ch = tbl();
  ch->set("leaf", 1.0);
  ch->set("static_door", childOf("still"));
  ch->set("static_wheel", childOf("still"));
  ch->set("static_largeweight", childOf("still"));
  ch->set("anim_door", childOf("misc"));
  ch->set("anim_wheel", childOf("misc"));
  ch->set("anim_largeweight", childOf("large"));
  ch->set("anim_fill", childOf("fill"));
  ch->set("panel", childOf("still"));
  t->set("children", ch);
  return t;
}

// A sauce or drink machine's child: one `base` asset state, as the shipped style has them.
Tbl spriteChild(const std::string& file, int base = 0, int layer = 0) {
  auto c = tbl();
  c->set("layer", static_cast<double>(layer));
  c->set("assets", assetsOf({{"base", group({state("default", file, base)})}}));
  return c;
}
Tbl sauceStyle(bool drink) {
  auto t = tbl();
  t->set("baseDir", "anim/").set("name", drink ? "machine_drink" : "machine_sauce").set("type", "machine").set("spriteLayer", 3.0);
  t->set("stationType", drink ? 8.0 : 6.0);
  t->set("assets", assetsOf({{"base", group({state("default", "sbase", 11)})}}));
  auto ch = tbl();
  ch->set("leaf", 1.0);
  ch->set("glass", spriteChild("sbase", 22, 2));
  ch->set("static_fill", spriteChild("sbase", 12, 1));
  ch->set("anim_fill", spriteChild("sfill", 0, 1));
  ch->set("static_top", spriteChild("sbase", drink ? 28 : 1, 5));
  ch->set("anim_top", spriteChild("stop", 0, 5));
  ch->set("anim_spray", spriteChild("sbase", drink ? 14 : 0, 3));
  ch->set("static_dispenser", spriteChild("sbase", 0, 3));
  ch->set("anim_dispenser", spriteChild("sdisp", 0, 3));
  ch->set("shadow", spriteChild("sbase", 30, -1));
  if (drink) {
    auto steam = spriteChild("sbase", 23, 3);
    steam->set("mouse", 0.0);
    ch->set("anim_fill_steam", steam);
  }
  auto bottle = spriteChild("still", drink ? 5 : 0, 4);
  bottle->set("leaf", 1.0).set("name", drink ? "drink_cup" : "sauce_bottle").set("type", drink ? "drink" : "sauce");
  bottle->set("anchor", "bottle").set("visible", 0.0);
  if (drink) {
    auto steam = tbl();
    steam->set("layer", 1.0).set("continuous", 1.0).set("mouse", 0.0).set("leaf", 0.0);
    steam->set("assets", assetsOf({{"anim", group({state("default", "cup")})}}));
    auto kids = tbl();
    kids->set("steam", steam);
    bottle->set("children", kids);
  }
  ch->set("bottle", bottle);
  t->set("children", ch);
  return t;
}
Tbl machineEntry(const Tbl& style, const std::vector<Tbl>& buttons) {
  auto m = tbl();
  m->set("style", style);
  auto sel = tbl();
  for (std::size_t i = 0; i < buttons.size(); ++i) sel->set("button" + std::to_string(i + 1), buttons[i]);
  m->set("selections", sel);
  return m;
}
Tbl stationTable(int id, int type, const std::string& point, const std::vector<std::pair<std::string, Tbl>>& machines) {
  auto t = tbl();
  t->set("stationID", static_cast<double>(id)).set("stationType", static_cast<double>(type)).set("name", "station" + std::to_string(id));
  t->set("spriteLayer", 5.0);
  auto pts = tbl();
  pts->append(point).append(2.0);
  t->set("playerPoint", pts);
  auto ms = tbl();
  for (auto& [n, m] : machines) ms->set(n, m);
  t->set("machines", ms);
  return t;
}

// A state per upgrade level: upgrade1..3.
Tbl upgradeStates(const std::string& file) {
  return group({state("upgrade1", file), state("upgrade2", file), state("upgrade3", file)});
}
Tbl ingredientStyle() {
  auto t = tbl();
  t->set("baseDir", "anim/").set("name", "machine_ingredient").set("type", "machine").set("spriteLayer", 7.0);
  t->set("assets", assetsOf({{"base", group({state("default", "base", 0)})}}));
  auto ch = tbl();
  ch->set("leaf", 1.0);
  ch->set("panel", childOf("still"));
  ch->set("anim_intro", childOf("iintro", {}, 3));
  ch->set("anim_pump", childOf("ipump", {}, 4));
  for (const char* n : {"static_top1", "static_top2"}) {
    auto c = tbl();
    c->set("layer", 4.0).set("assets", assetsOf({{"anim", upgradeStates("still")}}));
    ch->set(n, c);
  }
  for (const char* n : {"anim_top1", "anim_top2"}) {
    auto c = tbl();
    c->set("layer", 4.0).set("assets", assetsOf({{"anim", upgradeStates("itop")}}));
    ch->set(n, c);
  }
  t->set("children", ch);
  return t;
}
Tbl toppingStyle() {
  auto t = tbl();
  t->set("baseDir", "anim/").set("name", "machine_topping").set("type", "machine").set("spriteLayer", 7.0);
  t->set("assets", assetsOf({{"base", group({state("default", "base", 2)})}}));
  auto ch = tbl();
  ch->set("leaf", 1.0);
  ch->set("static_funnel", childOf("still", {}, -1));
  ch->set("anim_funnel", childOf("tfunnel", {}, -1));
  ch->set("anim_particles", childOf("tpart", {}, 2));
  ch->set("panel", childOf("still", {}, 5));
  t->set("children", ch);
  return t;
}
Tbl infuseStyle() {
  auto t = tbl();
  t->set("baseDir", "anim/").set("name", "machine_infusion").set("type", "machine").set("spriteLayer", 7.0);
  t->set("assets", assetsOf({{"base", group({state("default", "base", 0)})}}));
  auto ch = tbl();
  ch->set("leaf", 1.0);
  ch->set("static_top", childOf("still", {}, 1));
  ch->set("static_base", childOf("still", {}, 1));
  ch->set("anim_door", childOf("idoor", {}, 1));
  ch->set("anim_top_working", childOf("itopw", {}, 1));
  ch->set("anim_base", childOf("ibase", {}, 0));
  ch->set("panel1", childOf("still", {}, 5));
  t->set("children", ch);
  return t;
}
Tbl deliveryStyle() {
  auto t = tbl();
  t->set("baseDir", "anim/").set("name", "machine_delivery").set("type", "machine").set("spriteLayer", 3.0);
  t->set("assets", assetsOf({{"base", group({state("default", "base", 1)})}}));
  auto bubble = tbl();
  bubble->set("baseDir", "anim/").set("anchor", "thought").set("hflip", 1.0).set("leaf", 1.0);
  bubble->set("assets", assetsOf({{"bubble_single", group({state("happy", "still", 0), state("content", "still", 1), state("neutral", "still", 2),
                                                           state("annoyed", "still", 3), state("angry", "still", 4)})}}));
  t->set("bubble", bubble);
  auto ch = tbl();
  ch->set("leaf", 1.0);
  ch->set("anim_boxing_back", spriteChild("dwork", 0, 0));
  ch->set("anim_boxing_front", spriteChild("dwork", 0, 2));
  ch->set("shadow", spriteChild("base", 0, -1));
  t->set("children", ch);
  return t;
}
// The belt of Gaberoun's stations.lua: the arc, its speed and the zones of the counters, the bin and the drop.
Tbl conveyorTable() {
  auto t = stationTable(17, kStationConveyor, "p17", {});
  t->set("name", "station17").set("baseDir", "anim/");
  auto point = [](double x, double y) {
    auto p = tbl();
    p->append(x).append(y);
    return p;
  };
  t->set("arcStart", point(612, 278)).set("arcEnd", point(103, 375)).set("arcCenter", point(410, 790)).set("arcVelocity", 5.0);
  auto zones = tbl();
  const struct {
    const char* name;
    double x, y;
  } list[] = {{"station06", 533, 247}, {"station05", 457, 237}, {"station04", 375, 236}, {"station03", 297, 246},
              {"station02", 225, 269}, {"station01", 166, 306}, {"station15", 145, 329}, {"drop", 103, 326}};
  for (const auto& z : list) {
    zones->append(std::string(z.name));
    zones->append(point(z.x, z.y));
  }
  t->set("stations", zones);
  t->set("assets", assetsOf({{"base", group({state("default", "cbase")})}}));
  return t;
}

Tbl displayTable() {
  auto t = stationTable(16, kStationDisplay, "p16", {});
  t->set("baseDir", "anim/");
  t->set("assets", assetsOf({{"base", group({state("default", "ctable")})}}));
  return t;
}
Tbl disposalTable() {
  auto t = stationTable(15, kStationDisposal, "p15", {});
  t->set("baseDir", "anim/");
  t->set("assets", assetsOf({{"base", group({state("default", "disposal")})}}));
  auto active = tbl();
  auto st = state("default", "disposal", 1);
  st->set("play", "DoAnim");
  active->set("assets", assetsOf({{"base", group({st})}}));
  auto kids = tbl();
  kids->set("leaf", 1.0);
  kids->set("active", active);
  t->set("children", kids);
  return t;
}

// The chocolates of a location: the bucket (with its icon), shapes, fillings, infusion layers and toppings, each with the three
// choices, from the shapes the test asks for.
Tbl layerTable(int layerType, int barType, int option, bool bucket = false) {
  auto t = tbl();
  t->set("baseDir", "anim/").set("layerType", static_cast<double>(layerType)).set("barType", static_cast<double>(barType));
  t->set("option", static_cast<double>(option));
  t->set("assets", assetsOf({{"base", group({state("default", "still")})}}));
  if (bucket) {
    auto icon = tbl();
    icon->set("leaf", 1.0).set("visible", 0.0);
    icon->set("assets", assetsOf({{"choc", group({state("option_0", "still"), state("option_1", "still"), state("option_2", "still")})},
                                  {"overlay", group({state("option_0", "still"), state("option_1", "still"), state("option_2", "still")})}}));
    auto ch = tbl();
    ch->set("icon", icon);
    t->set("children", ch);
  }
  return t;
}
Tbl chocolatesTable() {
  auto t = tbl();
  t->set("baseDir", "anim/chocolates/");
  t->set("bucket", layerTable(kLayerBucket, -1, 0, true));
  for (int bar : {kShapeBar, kShapeTruffle, kShapeNovelty, kShapeInfusion})
    for (int o = 0; o < 3; ++o) {
      t->set("shape_" + std::to_string(bar) + "_" + std::to_string(o), layerTable(kLayerChocolate, bar, o));
      if (bar != kShapeInfusion) t->set("fill_" + std::to_string(bar) + "_" + std::to_string(o), layerTable(kLayerIngredient, bar, o));
      t->set("top_" + std::to_string(bar) + "_" + std::to_string(o), layerTable(kLayerTopping, bar, o));
    }
  for (int o = 0; o < 3; ++o) t->set("infusion_" + std::to_string(o), layerTable(kLayerInfusion, kShapeInfusion, o));
  return t;
}

constexpr const char* kZones =
    "<zonecontroller><zone name='avatar'>"
    "<point name='start' x='100' y='100'/><point name='p14' x='200' y='100'/><point name='p12' x='300' y='100'/>"
    "<point name='p08' x='400' y='100'/><point name='p15' x='500' y='100'/><point name='p16' x='600' y='100'/>"
    "<point name='p13' x='250' y='100'/><point name='p11' x='650' y='100'/><point name='p10' x='700' y='100'/>"
    "<point name='p07' x='750' y='100'/><point name='p17' x='800' y='100'/>"
    "<connection first='start' second='p14'/><connection first='p14' second='p12'/><connection first='p12' second='p08'/>"
    "<connection first='p08' second='p15'/><connection first='p15' second='p16'/><connection first='p16' second='p11'/>"
    "<connection first='p11' second='p10'/><connection first='p10' second='p07'/><connection first='p07' second='p17'/>"
    "<connection first='p14' second='p13'/>"
    "</zone><zone name='customers'><point name='customer01' x='5' y='6'/><point name='customer02' x='7' y='8'/><point name='customer03' x='9' y='10'/></zone></zonecontroller>";

Tbl walkAssets(bool carry) {
  auto assets = tbl();
  for (const char* n : {"walkfront", "walkright", "walkback"}) {
    std::vector<Tbl> states{state("default", "plain", 0, false)};
    if (carry) states.push_back(state("carry", "plain", 1, false));
    assets->set(n, group(states));
  }
  return assets;
}
Tbl playerTable() {
  auto t = tbl();
  t->set("baseDir", "anim/").set("name", "avatar").set("mouse", 0.0).set("x", 400.0).set("y", 300.0).set("spriteLayer", 3.0);
  t->set("baseY", 100.0).set("rangeY", 100.0).set("scaleRange", 0.1).set("startZone", "avatar").set("startPoint", "start");
  t->set("assets", walkAssets(false));
  auto children = tbl();
  children->set("mouse", 0.0);
  for (const char* n : {"left_arm", "left_arm_upper", "right_arm", "right_arm_upper"}) {
    auto c = tbl();
    c->set("assets", walkAssets(true));
    children->set(n, c);
  }
  t->set("children", children);
  return t;
}

// The shop as the machines and the avatar see it.
struct FakeShop : ShopHost {
  std::shared_ptr<VPlayer> who;
  std::vector<std::shared_ptr<VStation>> stations;
  std::set<std::string> upgrades;  // lower case names of the station and gameplay upgrades that are on
  std::shared_ptr<VChocolateVendor> vendorPtr;
  bool endless = false;
  bool tutorial = false;
  std::shared_ptr<VPlayer> player() const override { return who; }
  bool tutorialInEffect() const override { return tutorial; }
  bool hasCenterTable() const override { return false; }
  std::shared_ptr<VStation> stationByZonePoint(const ZonePoint* p) const override {
    for (const auto& s : stations)
      if (s->hasPoint(p)) return s;
    return nullptr;
  }
  bool upgradeActive(const std::string& name, const std::string&) const override {
    std::string l = name;
    for (char& c : l) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return upgrades.count(l) != 0;
  }
  std::shared_ptr<VChocolateVendor> chocolateVendor() const override { return vendorPtr; }
  bool isEndless() const override { return endless; }
  // what the customers report (slice 13): the chain events, the payments and the level clock
  std::vector<int> combos;
  struct Payment {
    float price, x, y;
    const VCustomer* customer;
  };
  std::vector<Payment> payments;
  int elapsed = 0;
  void addCombo(int type) override { combos.push_back(type); }
  void orderComplete(float price, float x, float y, const VCustomer* customer) override { payments.push_back({price, x, y, customer}); }
  int levelElapsedMs() const override { return elapsed; }
  // the stations by name and number, the level's end, the zeppelin wait, the scene's own sprites and the level's zeppelin orders
  std::shared_ptr<VStation> stationByName(const std::string& n) const override {
    for (const auto& s : stations)
      if (s->name() == n) return s;
    return nullptr;
  }
  std::shared_ptr<VStation> stationByNumber(int n) const override {
    char key[16];
    std::snprintf(key, sizeof key, "station%02i", n);
    return stationByName(key);
  }
  bool levelDone = false;
  bool levelIsComplete() const override { return levelDone; }
  std::vector<bool> zeppelinWaits;
  void waitOnZeppelin(bool wait) override { zeppelinWaits.push_back(wait); }
  std::vector<std::shared_ptr<VSprite>> sceneSprites;
  SpriteManager* scene = nullptr;  // where the shop's own sprites (the zeppelin) join, when the test runs them
  void addSceneSprite(const std::shared_ptr<VSprite>& sprite) override {
    sceneSprites.push_back(sprite);
    if (scene) scene->addObject(sprite);
  }
  std::vector<OrderInfo> zeppelin;
  bool haveZeppelin = false;
  const std::vector<OrderInfo>* zeppelinOrders() const override { return haveZeppelin ? &zeppelin : nullptr; }
  // ShopWindow::FindOrderWithLayer over the counters in the shop
  std::shared_ptr<VChocolate> findOrderWithLayer(const std::shared_ptr<VChocolate>& c0, const std::shared_ptr<VChocolate>& c1, int layer,
                                                 int option) const override {
    for (int i = 1; i <= 6; ++i) {
      const auto s = stationByNumber(i);
      if (!s || s->stationType() != kStationCounter) continue;
      const auto guest = s->customer();
      if (!guest || !guest->wanted()) continue;
      const auto wanted = std::dynamic_pointer_cast<VChocolate>(guest->wanted());
      if (!wanted) continue;
      if (wanted->compareWithLayer(c0.get(), layer, option)) return c0;
      if (wanted->compareWithLayer(c1.get(), layer, option)) return c1;
    }
    return c0 ? c0 : c1;
  }
};

struct Mx : MachineAssets, test::ScriptEnv {
  Mx() : MachineAssets(), ScriptEnv(dir.string(), scratch()) {
    ctx.module = games::findModule("greatcc");
    ctx.module->attach(ctx);
    wm.setSheets(sheets.get());
    wm.setTextures(textures.get());
    wm.pushModal("m");
    world = worldOf(ctx);
    REQUIRE(world);
    world->values().load(
        "<values><value name='walkspeed' value='4.0'/><value name='player_idle_cooldown' value='100000'/>"
        "<value name='player_idle_first_cooldown' value='100000'/><value name='player_turnaround_cooldown' value='500'/>"
        "<value name='player_turn_speed' value='0.4'/><value name='layer_price' value='10'/>"
        "<value name='layer_quality_modifier' value='0.1'/><value name='chocolate_fade_time' value='400'/>"
        "<value name='num_bars' value='0'/><value name='num_truffles' value='0'/>"
        "<value name='sauce_cooldown' value='15000'/><value name='drink_cooldown' value='15000'/>"
        "<value name='display_influence_mix' value='30'/><value name='display_influence_ingredient' value='20'/>"
        "<value name='display_influence_shape' value='30'/><value name='display_influence_infusion' value='20'/>"
        "<value name='display_influence_topping' value='20'/></values>");
    graph = std::make_shared<ZoneGraph>();
    REQUIRE(graph->load(kZones));
    shop.upgrades = {"kmixer1", "kpress1"};
    vendor = VChocolateVendor::create(ctx);
    const StyleChain chain(*protos, nullptr);
    vendor->loadFromTable(chain);
    shop.vendorPtr = vendor;
    shop.scene = &mgr;
    mgr.setNow(now);
  }
  ~Mx() { mgr.removeAllObjects(); }
  static std::string scratch() {
    const fs::path p = fs::temp_directory_path() / "choco_greatcc_machines_user";
    std::error_code ec;
    fs::remove_all(p, ec);
    fs::create_directories(p, ec);
    return p.string();
  }

  std::shared_ptr<VPlayer> makePlayer() {
    auto p = std::make_shared<VPlayer>(ctx, graph);
    shop.who = p;
    p->setHost(&shop);
    auto table = playerTable();
    const StyleChain chain(*table, nullptr);
    p->loadFromTable(chain);
    p->init();
    mgr.addObject(p);
    step(0);
    player = p;
    return p;
  }
  // A station with one machine, built like ShopWindow::LoadStations does it, and added to the scene.
  std::shared_ptr<VStation> makeStation(int id, int type, const std::string& point, const std::vector<std::pair<std::string, Tbl>>& machines) {
    auto s = VStation::create(ctx, 0, type);
    s->setHost(&shop);
    auto t = stationTable(id, type, point, machines);
    const StyleChain sc(*t, nullptr);
    s->loadFromTable(sc);
    s->resolvePoints(*graph);
    s->init();
    shop.stations.push_back(s);
    mgr.addObject(s);
    return s;
  }
  std::shared_ptr<VStation> makeMixer(const std::vector<Tbl>& buttons = {}) {
    std::vector<Tbl> b = buttons;
    if (b.empty())
      b = {buttonEntry(0, 75, 518, "btn_mix_dark", "dark"), buttonEntry(1, 116, 530, "btn_mix_milk", "milk"), buttonEntry(2, 162, 542, "btn_mix_white", "white")};
    return makeStation(14, kStationMixer, "p14", {{"machine_mixer", machineEntry(mixerStyle(), b)}});
  }
  std::shared_ptr<VStation> makePress(const std::vector<Tbl>& buttons = {}) {
    std::vector<Tbl> b = buttons;
    if (b.empty())
      b = {buttonEntry(0, 456, 573, "press_btn_1", "bar"), buttonEntry(1, 506, 570, "press_btn_2", "truffle"),
           buttonEntry(2, 556, 568, "press_btn_3", "novelty")};
    return makeStation(12, kStationBarPress, "p12", {{"machine_press", machineEntry(pressStyle(), b)}});
  }
  // The sauce station (8): the sauce machine, the second sauce machine and the drink machine, as the shipped table has them.
  std::shared_ptr<VStation> makeSauceStation(bool sauce2 = false, bool drink = false) {
    std::vector<std::pair<std::string, Tbl>> machines{{"machine_sauce", machineEntry(sauceStyle(false), {})}};
    if (sauce2) {
      auto e = machineEntry(sauceStyle(false), {});
      e->set("name", "machine_sauce_2").set("x", 30.0).set("y", 20.0);
      machines.push_back({"machine_sauce_2", e});
    }
    if (drink) {
      auto e = machineEntry(sauceStyle(true), {});
      e->set("x", 30.0).set("y", 20.0);
      machines.push_back({"machine_drink", e});
    }
    return makeStation(8, kStationEmpty, "p08", machines);
  }
  // The ingredient (13), topping (11) and infusion (10) stations with the shipped buttons (three each), and the delivery station (7).
  std::shared_ptr<VStation> makeIngredient() {
    std::vector<Tbl> b = {buttonEntry(0, 257, 562, "btn_ingred_1", "bar", "stripe_00"), buttonEntry(1, 306, 565, "btn_ingred_2", "truffle", "stripe_01"),
                          buttonEntry(2, 357, 568, "btn_ingred_3", "novelty", "stripe_00")};
    return makeStation(13, kStationIngredient, "p13", {{"machine_ingredient", machineEntry(ingredientStyle(), b)}});
  }
  std::shared_ptr<VStation> makeTopping() {
    std::vector<Tbl> b = {buttonEntry(0, 627, 557, "btn_topping_1", "bar"), buttonEntry(1, 671, 543, "btn_topping_2", "truffle"),
                          buttonEntry(2, 714, 529, "btn_topping_3", "novelty")};
    return makeStation(11, kStationTopping, "p11", {{"machine_topping", machineEntry(toppingStyle(), b)}});
  }
  std::shared_ptr<VStation> makeInfuse() {
    std::vector<Tbl> b = {buttonEntry(0, 768, 371, "btn_infuse_1", "bar"), buttonEntry(1, 768, 417, "btn_infuse_2", "truffle"),
                          buttonEntry(2, 768, 462, "btn_infuse_3", "novelty")};
    return makeStation(10, kStationInfuse, "p10", {{"machine_infusion", machineEntry(infuseStyle(), b)}});
  }
  std::shared_ptr<VStation> makeDelivery() {
    return makeStation(7, kStationDelivery, "p07", {{"machine_delivery", machineEntry(deliveryStyle(), {})}});
  }
  std::shared_ptr<VStation> makeConveyor() {
    auto s = VStation::create(ctx, 0, kStationConveyor);
    s->setHost(&shop);
    auto t = conveyorTable();
    const StyleChain sc(*t, nullptr);
    s->loadFromTable(sc);
    s->resolvePoints(*graph);
    s->init();
    shop.stations.push_back(s);
    mgr.addObject(s);
    return s;
  }
  template <class M>
  static std::shared_ptr<M> machineOf(const std::shared_ptr<VStation>& s) {
    return std::dynamic_pointer_cast<M>(s->machines().begin()->second);
  }
  std::shared_ptr<VStation> makeDisplay() {
    auto s = VStation::create(ctx, 0, kStationDisplay);
    s->setHost(&shop);
    auto t = displayTable();
    const StyleChain sc(*t, nullptr);
    s->loadFromTable(sc);
    s->resolvePoints(*graph);
    s->init();
    shop.stations.push_back(s);
    mgr.addObject(s);
    return s;
  }
  std::shared_ptr<VStation> makeDisposal() {
    auto s = VStation::create(ctx, 0, kStationDisposal);
    s->setHost(&shop);
    auto t = disposalTable();
    const StyleChain sc(*t, nullptr);
    s->loadFromTable(sc);
    s->resolvePoints(*graph);
    s->init();
    shop.stations.push_back(s);
    mgr.addObject(s);
    return s;
  }
  static std::shared_ptr<VSauceDrink> sauceOf(const std::shared_ptr<VStation>& s, const char* key = "machine_sauce") {
    const auto it = s->machines().find(key);
    return it == s->machines().end() ? nullptr : std::dynamic_pointer_cast<VSauceDrink>(it->second);
  }
  static std::shared_ptr<VMixer> mixerOf(const std::shared_ptr<VStation>& s) { return std::dynamic_pointer_cast<VMixer>(s->machines().begin()->second); }
  static std::shared_ptr<VPress> pressOf(const std::shared_ptr<VStation>& s) { return std::dynamic_pointer_cast<VPress>(s->machines().begin()->second); }

  void step(double ms) {
    now += ms;
    mgr.setNow(now);
    mgr.updateObjects();
  }
  // `ms` of time in 4 ms updates (a script advances one frame per update, so long jumps do not catch up).
  void run(int ms) {
    for (int t = 0; t < ms; t += 4) step(4);
  }
  // Updates every ms until `done` or `limit` ms have passed; returns the ms that passed.
  int until(const std::function<bool()>& done, int limit = 6000) {
    int t = 0;
    while (t < limit && !done()) {
      step(1);
      ++t;
    }
    return t;
  }
  // The avatar arrives at the station now (what VPlayer::AtDestination does).
  void arrive(const std::shared_ptr<VStation>& s) { s->onArrival(*player, s->points().front()); }

  bool clean() const { return !log.contains("[lua-error]") && !log.contains("[content]") && !log.contains("[native-error]"); }

  GreatCCWorld* world = nullptr;
  std::shared_ptr<ZoneGraph> graph;
  FakeShop shop;
  Tbl protos = chocolatesTable();
  std::shared_ptr<VChocolateVendor> vendor;
  std::shared_ptr<VPlayer> player;
  SpriteManager mgr{ctx};
  double now = 1000;
};

[[maybe_unused]] bool near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) < eps; }

// The wipes and moves a sprite carries (the machine's propagating hover glow is on its children too).
template <class E>
int effectsOf(const VSprite& s) {
  int n = 0;
  for (const auto& e : s.effectList()) n += dynamic_cast<E*>(e.get()) != nullptr;
  return n;
}

}  // namespace
