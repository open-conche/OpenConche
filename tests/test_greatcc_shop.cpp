// The Great Chocolate Chase, slice 9 (docs/design/greatcc.md section 6): the shop shell. Synthetic fixtures only: level files and
// pictures made here, the HUD and the location tables built from MapStyle, a world with a few values and level goals.
#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "assets/AssetFS.h"
#include "games/Registry.h"
#include "games/greatcc/GreatCCNatives.h"
#include "games/greatcc/GreatCCWorld.h"
#include "games/greatcc/core/AssetsXml.h"
#include "assets/Xml.h"
#include "games/greatcc/core/LevelData.h"
#include "games/greatcc/core/LevelTimer.h"
#include "games/greatcc/natives/StateNatives.h"
#include "games/greatcc/shop/FloatingTexts.h"
#include "games/greatcc/shop/ShopTick.h"
#include "games/greatcc/meta/VQuestWindow.h"
#include "games/greatcc/shop/ShopWindow.h"
#include "games/greatcc/shop/VBackground.h"
#include "games/greatcc/shop/VChocolate.h"
#include "games/greatcc/shop/VCustomer.h"
#include "games/greatcc/shop/VDisplay.h"
#include "games/greatcc/shop/VMixer.h"
#include "games/greatcc/shop/VPlayer.h"
#include "games/greatcc/shop/VShopHud.h"
#include "games/greatcc/shop/VStation.h"
#include "games/greatcc/ui/BLayeredWindow.h"
#include "games/greatcc/ui/KDial.h"
#include "games/greatcc/ui/RActiveTextLayer.h"
#include "games/greatcc/ui/RShrinkText.h"
#include "games/greatcc/ui/RTimer.h"
#include "gfx/TextureCache.h"
#include "platform/Png.h"
#include "sdk/Users.h"
#include "ScriptHarness.h"
#include "ui/Style.h"
#include "ui/widgets/TextWidgets.h"
#include "ui/widgets/Widgets.h"

using namespace choco;
using namespace choco::games::greatcc;
namespace fs = std::filesystem;

namespace {

using MS = ui::MapStyle;
using Tbl = std::shared_ptr<MS>;
Tbl tbl() { return std::make_shared<MS>(); }

// ---- the level file ----

LevelData parse(const std::string& xml, int number = 1, bool fix = true, LevelData::Result* result = nullptr) {
  LevelData level;
  const LevelData::Result r = LevelData::load(number, xml, fix, level);
  if (result) *result = r;
  return level;
}

const char* kLevel1 =
    "<level><name>Level 1</name><location>iquitos</location><minutes>0</minutes><seconds>4</seconds>"
    "<plot>story_iquitos_1</plot><plot>story_iquitos_2</plot><lockdown min='0' max='10'/>"
    "<maxlayers>2</maxlayers><chocolatedecay>15000</chocolatedecay><customerdecay>0.25</customerdecay>"
    "<customer><name>tut_cust_1</name><type>professor</type><start>0</start><order type='kSauce'/><station>station01</station></customer>"
    "<customer><type>diplomat</type><start>4.5</start><end>9</end><decay>70000</decay><tip>9</tip>"
    "<order type='kChocolate' shape='kBar' mix='0'/>"
    "<order type='KChocolate' shape='knovelty' mix='' layers='9'><layer type='kIngredientLayer' option='1'/>"
    "<layer type='kToppingLayer'/><!--<layer type='kInfusionLayer'/>--><layer type='kBucketLayer' option='2'/></order>"
    "<order type='kDrink'/></customer>"
    "<customer><type>ghost</type><start>10</start></customer>"
    "<zeppelin><order type='kChocolate' shape='kTruffle' layers='2'/><order type='kSauce'/></zeppelin></level>";

}  // namespace

// ================================================================ the level file

TEST_CASE("LevelData: the tags, the lockdown window, the decay settings, the customers and their orders") {
  LevelData::Result r;
  const LevelData l = parse(kLevel1, 1, true, &r);
  CHECK(r.parsed);
  CHECK(l.number == 1);
  CHECK(l.name == "Level 1");
  CHECK(l.location == "iquitos");
  CHECK(l.durationMs() == 4000);
  CHECK(l.plots == std::vector<std::string>{"story_iquitos_1", "story_iquitos_2"});
  CHECK(l.needPlot());
  CHECK_FALSE(l.needPrelevel);  // levels 1..3 never show the pre-level dialog
  CHECK(l.maxLayers == 2);
  CHECK(l.chocolateDecay == 15000);
  CHECK(l.customerDecay == doctest::Approx(0.25f));
  CHECK(l.lockdownMin == 0);
  CHECK(l.lockdownMax == 10);
  REQUIRE(l.customers.size() == 3);
  const CustomerData& a = *l.customers[0];
  CHECK(a.name == "tut_cust_1");
  CHECK(a.type == "professor");
  CHECK(a.station == "station01");
  CHECK(a.start == 0);
  CHECK(a.end == FLT_MAX);
  CHECK(a.decay == -1);
  REQUIRE(a.orders.size() == 1);
  CHECK(a.orders[0].type == kOrderSauce);
  CHECK(a.orders[0].shape == kShapeUnspecified);  // a sauce order reads nothing more
  const CustomerData& b = *l.customers[1];
  CHECK(b.station.empty());
  CHECK(b.start == doctest::Approx(4.5f));
  CHECK(b.end == 9);
  CHECK(b.decay == 70000);
  CHECK(b.tip == 9);
  REQUIRE(b.orders.size() == 3);
  CHECK(b.orders[0].type == kOrderChocolate);
  CHECK(b.orders[0].shape == kShapeBar);
  CHECK(b.orders[0].mix == 0);
  CHECK(b.orders[0].layerBudget == 1);
  CHECK(b.orders[0].layer[kLayerIngredient] == -2);
  // case-insensitive type and shape; an empty mix is random; layers above 3 are 3; a layer without an option is "random"
  CHECK(b.orders[1].type == kOrderChocolate);
  CHECK(b.orders[1].shape == kShapeNovelty);
  CHECK(b.orders[1].mix == -1);
  CHECK(b.orders[1].layerBudget == 3);
  CHECK(b.orders[1].layer[kLayerIngredient] == 1);
  CHECK(b.orders[1].layer[kLayerTopping] == -1);
  CHECK(b.orders[1].layer[kLayerInfusion] == -2);  // the commented-out layer is not read
  CHECK(b.orders[1].layer[kLayerBucket] == -2);    // the bucket layer is not a requestable one
  CHECK(b.orders[2].type == kOrderDrink);
  CHECK(l.customers[2]->type == "ghost");
  REQUIRE(l.zeppelin.size() == 2);
  CHECK(l.zeppelin[0].shape == kShapeTruffle);
  CHECK(l.zeppelin[0].layerBudget == 2);
  CHECK(l.zeppelin[1].type == kOrderSauce);
  // a customer is in range once the level has run to its start
  CHECK(b.inRange(4.5f));
  CHECK_FALSE(b.inRange(4.4f));
  CHECK_FALSE(CustomerData{}.inRange(100));  // the default start never comes
}

TEST_CASE("LevelData: layers are clamped to 1..3, an unknown type is invalid and a pre-level shows from level 4") {
  const LevelData l = parse(
      "<level><customer><type>x</type>"
      "<order type='chocolate' layers='0'/><order type='chocolate' layers='1'/><order type='chocolate' layers='2'/>"
      "<order type='chocolate' layers='3'/><order type='chocolate' layers='4'/><order type='chocolate'/>"
      "<order type='bogus' shape='kBar'/></customer></level>",
      4);
  CHECK(l.needPrelevel);
  REQUIRE(l.customers.size() == 1);
  const auto& o = l.customers[0]->orders;
  REQUIRE(o.size() == 7);
  const int want[] = {1, 1, 2, 3, 3, 1};
  for (int i = 0; i < 6; ++i) CHECK(o[static_cast<std::size_t>(i)].layerBudget == want[i]);
  CHECK(o[6].type == kOrderInvalid);
  CHECK(o[6].shape == kShapeUnspecified);
  CHECK_FALSE(parse("<level/>", 3).needPrelevel);
}

TEST_CASE("LevelData: F2 reads the typo kTruffler as kTruffle with the fixes on and as unspecified with them off") {
  const std::string xml =
      "<level><customer><type>x</type><order type='kChocolate' shape='kTruffler'/><order type='kChocolate' shape='kTruffle'/>"
      "<order type='kChocolate' shape='kBogus'/></customer></level>";
  LevelData::Result on, off;
  const LevelData fixed = parse(xml, 26, true, &on);
  const LevelData original = parse(xml, 26, false, &off);
  CHECK(on.fixedF2);
  CHECK_FALSE(off.fixedF2);
  CHECK(fixed.customers[0]->orders[0].shape == kShapeTruffle);
  CHECK(original.customers[0]->orders[0].shape == kShapeUnspecified);
  // the fix touches only that word
  CHECK(fixed.customers[0]->orders[1].shape == kShapeTruffle);
  CHECK(fixed.customers[0]->orders[2].shape == kShapeUnspecified);
  CHECK(original.customers[0]->orders[2].shape == kShapeUnspecified);
  LevelData::Result bogus;
  parse("<level><customer><order type='kChocolate' shape='kBogus'/></customer></level>", 1, true, &bogus);
  CHECK_FALSE(bogus.fixedF2);
}

TEST_CASE("LevelData: the lockdown test is inclusive and a malformed file leaves the defaults") {
  const LevelData l = parse("<level><lockdown min='0' max='10'/></level>");
  CHECK(l.inLockDown(0));
  CHECK(l.inLockDown(10));
  CHECK_FALSE(l.inLockDown(10.01f));
  CHECK_FALSE(l.inLockDown(-0.5f));
  const LevelData none = parse("<level/>");
  CHECK_FALSE(none.inLockDown(0));  // the default -1..-1 is never in range for a percentage
  LevelData::Result r;
  // a file with no <level> root leaves the defaults
  const LevelData bad = parse("<notlevel/>", 28, true, &r);
  CHECK_FALSE(r.parsed);
  CHECK_FALSE(r.error.empty());
  CHECK(bad.number == 28);
  CHECK(bad.customers.empty());
  CHECK(bad.needPrelevel);  // the constructor's defaults still apply
  CHECK_FALSE(parse("", 1, true, &r).customers.size());
  CHECK_FALSE(r.parsed);
}

TEST_CASE("LevelData: F1 closes an <order> left open at its customer's end tag; with the fixes off the part before the error is used") {
  // the shape of level 28: the second order of the middle customer is never closed
  const std::string xml =
      "<level><name>L</name>"
      "<customer><type>a</type><order type='kSauce'/></customer>"
      "<customer><type>b</type><order type='kChocolate' shape='kBar'><layer type='kIngredientLayer'/></order>"
      "<order type='kChocolate' shape='kInfusion'><layer type='kInfusionLayer'/><station>station03</station></customer>"
      "<customer><type>c</type><order type='kSauce'/></customer></level>";
  LevelData::Result on, off;
  const LevelData fixed = parse(xml, 28, true, &on);
  CHECK(on.parsed);
  CHECK(on.fixedF1);
  CHECK_FALSE(on.partial);
  REQUIRE(fixed.customers.size() == 3);
  CHECK(fixed.customers[1]->orders.size() == 2);
  CHECK(fixed.customers[1]->orders[1].shape == kShapeInfusion);
  // the customer keeps its counter and the one after it loads
  CHECK(fixed.customers[1]->station == "station03");
  CHECK(fixed.customers[2]->type == "c");
  const LevelData original = parse(xml, 28, false, &off);
  CHECK(off.parsed);
  CHECK(off.partial);
  CHECK_FALSE(off.fixedF1);
  CHECK_FALSE(off.error.empty());
  // what the parser had built: the first customer whole, the second up to its open order, none after it
  REQUIRE(original.customers.size() == 2);
  CHECK(original.customers[0]->orders.size() == 1);
  // a file that is fine is not touched, and an end tag that closes something other than an order is not repaired
  LevelData::Result fine, other;
  parse("<level><customer><order type='kSauce'/></customer></level>", 1, true, &fine);
  CHECK_FALSE(fine.fixedF1);
  const LevelData stray = parse("<level><customer><type>x</customer><customer/></level>", 1, true, &other);
  CHECK_FALSE(other.fixedF1);
  CHECK(other.partial);
}

namespace {
// A "ZXML" file as the install stores it: magic, the stream's length, the text's length, a zlib stream (one stored block).
std::string zxml(const std::string& text) {
  std::string z;
  z += '\x78';
  z += '\x01';
  z += '\x01';
  const std::uint16_t len = static_cast<std::uint16_t>(text.size()), nlen = static_cast<std::uint16_t>(~len);
  z.append(reinterpret_cast<const char*>(&len), 2);
  z.append(reinterpret_cast<const char*>(&nlen), 2);
  z += text;
  z.append(4, '\0');  // the adler sum (not checked)
  std::string out = "ZXML";
  const std::uint32_t comp = static_cast<std::uint32_t>(z.size()), raw = static_cast<std::uint32_t>(text.size());
  out.append(reinterpret_cast<const char*>(&comp), 4);
  out.append(reinterpret_cast<const char*>(&raw), 4);
  return out + z;
}
}  // namespace

TEST_CASE("LevelData: F1 repairs a file stored in the install's ZXML wrapper too, and Xml::plainText gives the text back") {
  const std::string xml =
      "<level><customer><type>b</type><order type='kChocolate' shape='kBar'><layer type='kIngredientLayer'/></order>"
      "<order type='kChocolate' shape='kInfusion'><layer type='kInfusionLayer'/><station>station03</station></customer></level>";
  CHECK(assets::Xml::plainText(xml) == xml);
  CHECK(assets::Xml::plainText(zxml(xml)) == xml);
  std::string bad = zxml(xml);
  bad[14] = '\xff';  // damage the zlib header
  std::string err;
  CHECK(assets::Xml::plainText(bad, &err).empty());
  CHECK_FALSE(err.empty());
  LevelData::Result on, off;
  const LevelData fixed = parse(zxml(xml), 28, true, &on);
  CHECK(on.fixedF1);
  REQUIRE(fixed.customers.size() == 1);
  CHECK(fixed.customers[0]->orders.size() == 2);
  CHECK(fixed.customers[0]->station == "station03");
  parse(zxml(xml), 28, false, &off);
  CHECK(off.partial);
}

TEST_CASE("order names: shapes, layers and order types") {
  CHECK(chocolateShapeFromString("KBAR") == kShapeBar);
  CHECK(chocolateShapeFromString("truffle") == kShapeTruffle);
  CHECK(chocolateShapeFromString("kNovelty") == kShapeNovelty);
  CHECK(chocolateShapeFromString("kinfusion") == kShapeInfusion);
  CHECK(chocolateShapeFromString("kTruffler") == -1);
  CHECK(chocolateLayerFromString("kIngredientLayer") == kLayerIngredient);
  CHECK(chocolateLayerFromString("topping") == kLayerTopping);
  CHECK(chocolateLayerFromString("KInfusion") == kLayerInfusion);
  CHECK(chocolateLayerFromString("kChocolateLayer") == kLayerChocolate);
  CHECK(chocolateLayerFromString("bucket") == kLayerBucket);
  CHECK(chocolateLayerFromString("nope") == -1);
  CHECK(orderTypeFromString("KSauce") == kOrderSauce);
  CHECK(orderTypeFromString("drink") == kOrderDrink);
  CHECK(orderTypeFromString("kChocolate") == kOrderChocolate);
  CHECK(orderTypeFromString("") == kOrderInvalid);
}

TEST_CASE("assets.xml: the sounds, with their group") {
  const auto s = parseAssetSounds(
      "<assets><sound file='audio/sfx/a.ogg' type='1000' memory='true'/><texture file='x'/><sound file='audio/sfx/b.ogg'/></assets>");
  REQUIRE(s.size() == 2);
  CHECK(s[0].file == "audio/sfx/a.ogg");
  CHECK(s[0].group == 1000);
  CHECK(s[1].group == 0);
  CHECK(parseAssetSounds("<assets><sound").empty());
}

// ================================================================ the level clock

TEST_CASE("LevelTimer: elapsed follows the clock while running, the text counts down or up, the percentage is capped") {
  LevelTimer t;
  t.incrementing = false;
  t.durationMs = 30000;
  CHECK(t.text() == "0:30");
  t.tick(5000);  // the first reading only takes the stamp
  CHECK(t.elapsedMs == 0);
  t.tick(7500);
  CHECK(t.elapsedMs == 2500);
  CHECK(t.text() == "0:27");  // 27.5 s left, truncated
  CHECK(t.percentElapsed() == doctest::Approx(100.0f * 2500 / 30000));
  t.running = false;
  t.tick(99999);  // a stopped timer does not move
  CHECK(t.elapsedMs == 2500);
  t.resume(100000);  // measuring from now: the stopped time is not counted
  t.tick(100400);
  CHECK(t.elapsedMs == 2900);
  t.tick(200000);
  CHECK(t.percentElapsed() == 100.0f);
  CHECK(t.text() == "0:00");  // never negative
  t.incrementing = true;
  CHECK(t.text() == "1:42");  // 102.9 s elapsed
  t.reset();
  CHECK(t.durationMs == 0);
  CHECK(t.elapsedMs == 0);
  CHECK(t.running);
  CHECK(t.percentElapsed() == 0);  // a zero duration is 0 percent
  t.durationMs = -5;
  t.tick(1);
  CHECK(t.durationMs == 0);
}

TEST_CASE("LevelTimer: minutes and seconds are truncated, and a long run shows minutes") {
  LevelTimer t;
  t.incrementing = true;
  t.elapsedMs = 61999;
  CHECK(t.text() == "1:01");
  t.elapsedMs = 600000;
  CHECK(t.text() == "10:00");
  t.durationMs = 90000;
  t.incrementing = false;
  t.elapsedMs = 0;
  CHECK(t.text() == "1:30");
}

// ================================================================ the tick

namespace {

// Records what the tick calls, in order, over fields the tests set.
struct RecordingEnv : ShopTickEnv {
  std::vector<std::string> calls;
  int elapsed = 16;
  bool level = true, tutorial = false, tutorials = true, timer = true, needPrelevel = false, needPlot = false;
  bool activeEmpty = true, pendingEmpty = false;
  float percent = 0;
  bool inLock = false;
  int timerElapsed = 0, timerDuration = 30000;
  unsigned angry = 0, limit = 5;
  void note(const std::string& s) { calls.push_back(s); }
  bool did(const std::string& s) const {
    for (const auto& c : calls)
      if (c == s) return true;
    return false;
  }
  int windowElapsedMs() override { return elapsed; }
  bool hasLevel() override { return level; }
  bool tutorialInEffect() override { return tutorial; }
  bool showTutorials() override { return tutorials; }
  bool hasTimer() override { return timer; }
  float percentElapsed() override { return percent; }
  bool levelInLockDown(float p) override {
    note("inLockDown");
    return inLock && p <= 10;
  }
  void setLockDown(bool on) override { note(on ? "lock" : "unlock"); }
  void startLevel() override { note("start"); }
  void updateCustomers() override { note("customers"); }
  void updateHud() override { note("hud"); }
  bool levelNeedsPrelevel() override { return needPrelevel; }
  void showPrelevel() override { note("prelevel"); }
  bool levelNeedsPlot() override { return needPlot; }
  void launchPlotDialog() override { note("plot"); }
  int timerElapsedMs() override { return timerElapsed; }
  int timerDurationMs() override { return timerDuration; }
  bool activeCustomersEmpty() override { return activeEmpty; }
  bool pendingCustomersEmpty() override { return pendingEmpty; }
  void updateEndlessMode() override { note("endless"); }
  unsigned angryThisLevel() override { return angry; }
  unsigned angryLimit() override { return limit; }
  void doEndLevel() override { note("end"); }
};

}  // namespace

TEST_CASE("ShopTick: the pre-level second counts down by the window's elapsed time, then the level starts on the next tick") {
  RecordingEnv env;
  ShopTickState s;
  s.startCountdownMs = 1000;
  s.waitingToStart = true;
  env.elapsed = 400;
  CHECK(runShopTick(s, env) == TickResult::Continue);
  CHECK(s.startCountdownMs == 600);
  CHECK_FALSE(env.did("start"));
  env.elapsed = 900;  // clamped at 0
  runShopTick(s, env);
  CHECK(s.startCountdownMs == 0);
  CHECK(s.waitingToStart);
  CHECK_FALSE(env.did("start"));
  env.calls.clear();
  runShopTick(s, env);
  CHECK_FALSE(s.waitingToStart);
  // the order of the phases: start, customers, hud
  REQUIRE(env.calls.size() >= 3);
  CHECK(env.calls[0] == "start");
  CHECK(env.calls[1] == "customers");
  CHECK(env.calls[2] == "hud");
}

TEST_CASE("ShopTick: a story level waits for the pre-level dialog and the diary, an endless one does not") {
  RecordingEnv env;
  ShopTickState s;
  s.waitingToStart = true;
  s.prelevelPending = true;  // ReadyForPrelevel
  env.needPrelevel = true;
  runShopTick(s, env);
  CHECK_FALSE(env.did("start"));   // pending: the level does not start in this tick
  CHECK(env.did("prelevel"));      // the dialog opens in the same tick (after the start gate)
  CHECK_FALSE(s.prelevelPending);
  CHECK_FALSE(s.plotPending);
  runShopTick(s, env);
  CHECK(env.did("start"));  // nothing pending now
  // no pre-level: the diary flag, and the diary opens when the level has one
  RecordingEnv e2;
  ShopTickState s2;
  s2.waitingToStart = true;
  s2.prelevelPending = true;
  e2.needPlot = true;
  runShopTick(s2, e2);
  CHECK_FALSE(e2.did("prelevel"));
  CHECK(e2.did("plot"));
  CHECK_FALSE(s2.plotPending);
  // endless: pending flags do not hold the start
  RecordingEnv e3;
  ShopTickState s3;
  s3.endless = true;
  s3.waitingToStart = true;
  s3.plotPending = true;
  runShopTick(s3, e3);
  CHECK(e3.did("start"));
  CHECK(e3.did("endless"));
}

TEST_CASE("ShopTick: the lockdown follows the level's window while a story level runs, and is off with tutorials off") {
  RecordingEnv env;
  ShopTickState s;  // running: waitingToStart false
  env.inLock = true;
  env.percent = 5;
  runShopTick(s, env);
  CHECK(env.did("lock"));
  env.calls.clear();
  env.percent = 50;
  runShopTick(s, env);
  CHECK(env.did("unlock"));
  env.calls.clear();
  env.tutorials = false;  // forced off every tick, the level's window is not asked
  env.percent = 5;
  runShopTick(s, env);
  CHECK(env.did("unlock"));
  CHECK_FALSE(env.did("inLockDown"));
  env.calls.clear();
  env.tutorials = true;
  env.tutorial = true;  // a tutorial owns the flag
  runShopTick(s, env);
  CHECK_FALSE(env.did("lock"));
  CHECK_FALSE(env.did("unlock"));
  env.calls.clear();
  env.tutorial = false;
  env.timer = false;  // no timer: unlocked
  runShopTick(s, env);
  CHECK(env.did("unlock"));
  // endless never evaluates the window
  RecordingEnv e2;
  ShopTickState s2;
  s2.endless = true;
  runShopTick(s2, e2);
  CHECK_FALSE(e2.did("lock"));
  CHECK_FALSE(e2.did("unlock"));
}

TEST_CASE("ShopTick: the end is noticed when the time is up with nobody in the shop, or when every customer has come and gone") {
  ShopTickState s;
  RecordingEnv env;
  env.timerElapsed = 29999;
  runShopTick(s, env);  // time not up, customers still to come
  CHECK(s.levelOverStamp == 0);
  env.timerElapsed = 30000;
  env.activeEmpty = false;  // time up but a customer is still in the shop
  runShopTick(s, env);
  CHECK(s.levelOverStamp == 0);
  env.activeEmpty = true;
  runShopTick(s, env);
  CHECK(s.levelOverStamp == 30000);
  // the early end: nobody pending, nobody active, time left
  ShopTickState s2;
  RecordingEnv e2;
  e2.pendingEmpty = true;
  e2.timerElapsed = 1234;
  runShopTick(s2, e2);
  CHECK(s2.levelOverStamp == 1234);
  // waiting on the zeppelin blocks the stamp
  ShopTickState s3;
  s3.waitingOnZeppelin = true;
  RecordingEnv e3;
  e3.pendingEmpty = true;
  runShopTick(s3, e3);
  CHECK(s3.levelOverStamp == 0);
}

TEST_CASE("ShopTick: the level ends more than 2000 ms after the stamp, and the task stops") {
  ShopTickState s;
  s.levelOverStamp = 5000;
  RecordingEnv env;
  env.timerElapsed = 7000;  // exactly 2000: not yet
  CHECK(runShopTick(s, env) == TickResult::Continue);
  CHECK_FALSE(env.did("end"));
  env.timerElapsed = 7001;
  CHECK(runShopTick(s, env) == TickResult::Stopped);
  CHECK(env.did("end"));
  CHECK(env.calls.back() == "end");  // DoEndLevel may destroy the window: the tick touches nothing after it
  // the stamp is set by the very tick that notices it and the delay is measured from it
  ShopTickState s2;
  RecordingEnv e2;
  e2.timerElapsed = 30000;
  e2.timerDuration = 30000;
  e2.pendingEmpty = true;
  CHECK(runShopTick(s2, e2) == TickResult::Continue);
  CHECK(s2.levelOverStamp == 30000);
  e2.timerElapsed = 32001;
  CHECK(runShopTick(s2, e2) == TickResult::Stopped);
}

TEST_CASE("ShopTick: an endless run ends two seconds after the anger limit is reached") {
  ShopTickState s;
  s.endless = true;
  RecordingEnv env;
  env.limit = 5;
  env.angry = 4;
  env.timerElapsed = 60000;
  runShopTick(s, env);
  CHECK(s.levelOverStamp == 0);
  env.angry = 5;
  runShopTick(s, env);
  CHECK(s.levelOverStamp == 60000);
  env.timerElapsed = 62001;
  CHECK(runShopTick(s, env) == TickResult::Stopped);
  // the time limit means nothing in an endless run
  ShopTickState s2;
  s2.endless = true;
  RecordingEnv e2;
  e2.timerElapsed = 1000000;
  e2.pendingEmpty = true;
  runShopTick(s2, e2);
  CHECK(s2.levelOverStamp == 0);
}

// ================================================================ the gauges

namespace {

MS rect(double x, double y, double w, double h) {
  MS s;
  s.set("x", x).set("y", y).set("w", w).set("h", h);
  return s;
}

}  // namespace

TEST_CASE("KDial: the angle follows the position between minrot and maxrot, and the needle registration is an offset from its centre") {
  test::ScriptEnv e(std::string(CHOCO_TEST_ASSETS) + "/greatcc/state");
  e.wm.pushModal("m");
  MS s = rect(0, 0, 350, 350);
  s.set("center_x", 108.0).set("center_y", 75.0).set("needle_regx", 3.0).set("needle_regy", 19.0);
  s.set("minrot", -1.5).set("maxrot", 1.5);
  auto dial = std::make_unique<KDial>(e.wm);
  auto& d = static_cast<KDial&>(e.wm.initWindow(std::move(dial), s));
  e.wm.popCreation();
  CHECK(d.angle() == doctest::Approx(-1.5f));  // startpos 0
  CHECK(d.gauge() == doctest::Approx(0.0f));
  d.setGauge(0.5f);
  CHECK(d.angle() == doctest::Approx(0.0f));
  d.setGauge(2.0f);  // clamped
  CHECK(d.angle() == doctest::Approx(1.5f));
  d.setGauge(-1.0f);
  CHECK(d.angle() == doctest::Approx(-1.5f));
  d.setGauge(std::nanf(""));  // 0/0 from the HUD: the original's compare lets a NaN through to 1
  CHECK(d.angle() == doctest::Approx(1.5f));
  CHECK(d.pivot().x == 108);
  CHECK(d.pivot().y == 75);
  CHECK_FALSE(d.hasNeedle());
}

TEST_CASE("KSweepDial: where the ray from the pivot leaves the window, and the wedge of the current position") {
  test::ScriptEnv e(std::string(CHOCO_TEST_ASSETS) + "/greatcc/state");
  e.wm.pushModal("m");
  MS s = rect(0, 0, 62, 62);
  s.set("center_x", 31.0).set("center_y", 31.0);
  auto& d = static_cast<KSweepDial&>(e.wm.initWindow(std::make_unique<KSweepDial>(e.wm), s));
  d.postChildrenInit(s);
  e.wm.popCreation();
  const float pi = 3.14159265f;
  auto at = [&](float theta) { return d.thetaToWindow(theta); };
  CHECK(at(0).x == doctest::Approx(31));
  CHECK(at(0).y == doctest::Approx(0));
  CHECK(at(pi / 2).x == doctest::Approx(62));
  CHECK(at(pi / 2).y == doctest::Approx(31).epsilon(0.01));
  CHECK(at(pi).x == doctest::Approx(31).epsilon(0.01));
  CHECK(at(pi).y == doctest::Approx(62));
  CHECK(at(3 * pi / 2).x == doctest::Approx(0));
  CHECK(at(3 * pi / 2).y == doctest::Approx(31).epsilon(0.01));
  CHECK(at(pi / 4).x == doctest::Approx(62).epsilon(0.001));  // the up-right corner
  CHECK(at(pi / 4).y == doctest::Approx(0).epsilon(0.001));
  // position 0: no triangle of any use; half: from 12 o'clock clockwise to 6, the closing edge pi/32 beyond
  d.setGauge(1.0f);
  CHECK(d.gauge() == doctest::Approx(1.0f));
  d.setGauge(0.5f);
  CHECK(d.triangles() == 17);
  REQUIRE(d.wedge().size() == 18);
  CHECK(d.wedge().front().x == doctest::Approx(31));
  CHECK(d.wedge().front().y == doctest::Approx(0));
  const float closing = pi + 0.09817477f;
  CHECK(d.wedge().back().y == doctest::Approx(d.thetaToWindow(closing).y));
  d.setGauge(1.0f);
  CHECK(d.triangles() == 31);  // the fan never has more than numsegments - 1 triangles
}

TEST_CASE("RActiveTextLayer: texts are updated by the tick's delta, drop when they say so, and can be cleared") {
  test::ScriptEnv e(std::string(CHOCO_TEST_ASSETS) + "/greatcc/state");
  e.wm.pushModal("m");
  struct T : ActiveText {
    T(double life, double* total, int* alive) : life(life), total(total), alive(alive) { ++*alive; }
    ~T() override { --*alive; }
    bool update(double dt) override {
      *total += dt;
      life -= dt;
      return life > 0;
    }
    double life;
    double* total;
    int* alive;
  };
  MS s = rect(0, 0, 800, 600);
  auto& layer = static_cast<RActiveTextLayer&>(e.wm.initWindow(std::make_unique<RActiveTextLayer>(e.wm), s));
  layer.postChildrenInit(s);
  e.wm.popCreation();
  double total = 0;
  int alive = 0;
  layer.adoptText(std::make_unique<T>(40, &total, &alive));
  layer.adoptText(std::make_unique<T>(100, &total, &alive));
  CHECK(layer.textCount() == 2);
  e.wm.tickWindowAnimations(1000);
  e.wm.tickWindowAnimations(1016);
  e.wm.tickWindowAnimations(1032);
  CHECK(layer.textCount() == 2);
  CHECK(total > 0);
  e.wm.tickWindowAnimations(1048);
  e.wm.tickWindowAnimations(1064);
  CHECK(layer.textCount() == 1);  // the 40 ms text is over
  CHECK(alive == 1);
  layer.clearText();
  CHECK(layer.textCount() == 0);
  CHECK(alive == 0);
}

// ================================================================ the shop

namespace {

struct ShopAssets {
  ShopAssets() {
    dir = fs::temp_directory_path() / "choco_greatcc_shop";
    fs::remove_all(dir);
    fs::create_directories(dir / "shop");
    fs::create_directories(dir / "levels");
    auto png = [&](const char* name, int w, int h) {
      REQUIRE(platform::writePng((dir / name).string(), w, h, std::vector<unsigned char>(static_cast<std::size_t>(w * h * 4), 255).data()));
    };
    png("shop/tab.png", 20, 12);
    png("shop/bg.png", 40, 30);
    png("shop/needle.png", 6, 30);
    png("shop/sweep.png", 62, 62);
    auto write = [&](const char* name, const std::string& text) { std::ofstream(dir / name, std::ios::binary) << text; };
    write("levels/level01.xml", kLevel1);
    write("levels/level02.xml", "<level><location>iquitos</location><minutes>1</minutes><seconds>0</seconds></level>");
    write("levels/level04.xml",
          "<level><location>iquitos</location><minutes>1</minutes><seconds>0</seconds><lockdown min='5' max='5'/>"
          "<customer><type>professor</type><start>0</start><order type='kSauce'/></customer></level>");
    write("levels/level26.xml",
          "<level><location>namche</location><minutes>1</minutes><customer><type>professor</type><start>0</start>"
          "<order type='kChocolate' shape='kTruffler'/></customer></level>");
    write("levels/level05.xml",
          "<level><location>iquitos</location><minutes>0</minutes><seconds>10</seconds>"
          "<customer><name>a</name><type>professor</type><start>0</start><order type='kSauce'/><station>station01</station></customer>"
          "<customer><name>b</name><type>diplomat</type><start>0</start><order type='kSauce'/><station>station01</station></customer>"
          "<customer><name>c</name><type>professor</type><start>50</start><order type='kSauce'/></customer>"
          "<customer><name>d</name><type>professor</type><start>0</start><order type='kSauce'/><station>nowhere</station></customer></level>");
    write("levels/level06.xml",
          "<level><location>iquitos</location><minutes>1</minutes><seconds>0</seconds>"
          "<customer><name>a</name><type>professor</type><start>0</start>"
          "<order type='kChocolate' shape='kBar' mix='0'><layer type='kIngredientLayer' option='0'/></order><station>station01</station></customer>"
          "<customer><name>b</name><type>diplomat</type><start>0</start>"
          "<order type='kChocolate' shape='kBar' mix='0'><layer type='kIngredientLayer' option='1'/></order><station>station02</station></customer>"
          "</level>");
    write("levels/level11.xml", "<level><location>grytviken</location><minutes>2</minutes><seconds>0</seconds></level>");
    write("assets.xml", "<assets><sound file='audio/sfx/a.ogg' type='1000' memory='true'/></assets>");
    fs::create_directories(dir / "locations/iquitos");
    write("locations/iquitos/upgrades.xml",
          "<upgrades><upgradetype type='station'>"
          "<upgrade name='kMixer1'><enabled>1</enabled></upgrade><upgrade name='kMixer2'><enabled>4</enabled></upgrade>"
          "<upgrade name='kPress1'><enabled>1</enabled></upgrade><upgrade name='kDisplay1'><enabled>2</enabled></upgrade>"
          "</upgradetype><upgradetype type='aesthetic'>"
          "<upgrade name='kStation1'><cost>100</cost><object name='station01'/><object name='station02' recursive='false'/></upgrade>"
          "<upgrade name='kStation2'><cost>300</cost><requires>kStation1</requires><object name='station01'/><object name='station02'/></upgrade>"
          "<upgrade name='kBackground1'><minlevel>4</minlevel><cost>100</cost><object name='background_object' effect='false'/></upgrade>"
          "<upgrade name='kStatic1'><cost>150</cost><object name='background_object' effect='false'/><object name='background_static_1'/></upgrade>"
          "</upgradetype></upgrades>");
    fs::create_directories(dir / "zones");
    write("zones/zones.xml",
          "<zonecontroller><zone name='avatar'>"
          "<point name='station01' x='100' y='100'/><point name='station01_b' x='100' y='150'/>"
          "<point name='station02' x='300' y='100'/><point name='station02_b' x='300' y='150'/>"
          "<connection first='station01' second='station01_b'/><connection first='station01_b' second='station02_b'/>"
          "<connection first='station02_b' second='station02'/>"
          "</zone><zone name='customers'><point name='customer01' x='60' y='90'/><point name='customer02' x='160' y='90'/></zone></zonecontroller>");
  }
  fs::path dir;
};

Tbl entry(const std::string& state, const std::string& file, double base = 0) {
  auto t = tbl();
  t->set("state", state).set("file", file).set("anim", 0.0).set("base", base);
  return t;
}
Tbl assetGroup(std::vector<Tbl> entries) {
  auto t = tbl();
  for (auto& e : entries) t->append(e);
  return t;
}

// The window's own style as game.lua builds it, in the parts the shop reads.
Tbl windowStyle(bool displayCase = false, bool machines = false, bool secondCounter = false) {
  auto root = tbl();
  root->set("x", 0.0).set("y", 0.0).set("w", 800.0).set("h", 600.0);
  auto posse = tbl();
  for (const char* n : {"professor", "diplomat"}) {
    auto c = tbl();
    c->set("name", std::string(n));
    posse->set(n, c);
  }
  auto bubble = tbl();  // not a character
  bubble->set("name", "thought");
  posse->set("thought_bubble", bubble);
  root->set("posse", posse);
  // the location tables
  auto loc = tbl();
  loc->set("music_01", "audio/music/iquitos.ogg");
  auto stations = tbl();
  stations->set("baseDir", "shop/");
  for (int i = 1; i <= 2; ++i) {
    auto st = tbl();
    char name[16];
    std::snprintf(name, sizeof name, "station%02d", i);
    const bool counter = i == 1 || (i == 2 && secondCounter);
    st->set("name", std::string(name)).set("stationType", counter ? 1.0 : 3.0).set("stationID", static_cast<double>(i));
    st->set("spriteLayer", 2.0);
    auto points = tbl();
    points->append(std::string(name)).append(0.0).append(std::string(name) + "_b").append(3.0);
    st->set("playerPoint", points);
    if (counter) {
      char cust[16];
      std::snprintf(cust, sizeof cust, "customer%02d", i);
      st->set("custPoint", std::string(cust));
      auto money = tbl();
      money->append(128.0 * i).append(293.0);
      st->set("money", money);
    }
    auto assets = tbl();
    assets->set("tabletop", assetGroup({entry("default", "tab", i), entry("upgrade1", "tab", i), entry("upgrade2", "tab", i)}));
    st->set("assets", assets);
    if (machines && i == 2) {
      // a mixer in the second station (the entry's own stationType wins over the station's)
      auto machine = tbl();
      machine->set("stationType", 2.0).set("baseDir", "shop/").set("name", "machine_mixer");
      auto massets = tbl();
      massets->set("base", assetGroup({entry("default", "tab", 0)}));
      machine->set("assets", massets);
      auto kids = tbl();
      for (const char* n : {"anim_arm", "anim_tube", "anim_spray", "panel"}) {
        auto c = tbl();
        auto cassets = tbl();
        cassets->set("anim", assetGroup({entry("default", "tab", 0), entry("option_0", "tab", 0)}));
        c->set("assets", cassets);
        kids->set(n, c);
      }
      machine->set("children", kids);
      auto selections = tbl();
      auto b1 = tbl();
      b1->set("baseDir", "shop/").set("id", 0.0).set("name", "btn_dark").set("x", 20.0).set("y", 30.0).set("icon", "dark");
      auto bassets = tbl();
      bassets->set("button", assetGroup({entry("default", "tab", 0)}));
      bassets->set("icon", assetGroup({entry("dark", "tab", 0)}));
      b1->set("assets", bassets);
      selections->set("button1", b1);
      machine->set("selections", selections);
      auto machineTable = tbl();
      machineTable->set("machine_mixer", machine);
      st->set("machines", machineTable);
    }
    char key[16];
    std::snprintf(key, sizeof key, "station_%02d", i);
    stations->set(key, st);
  }
  stations->set("junk", tbl());  // a table that is not a station
  if (displayCase) {
    auto st = tbl();
    st->set("name", "station16").set("stationType", 11.0).set("stationID", 16.0).set("spriteLayer", 2.0);
    auto points = tbl();
    points->append("station01_b").append(2.0);
    st->set("playerPoint", points);
    auto assets = tbl();
    assets->set("base", assetGroup({entry("default", "tab", 0)}));
    st->set("assets", assets);
    stations->set("station_16", st);
  }
  loc->set("stations", stations);
  if (machines) {
    auto chocolates = tbl();
    chocolates->set("baseDir", "shop/");
    auto layer = [&](int layerType, int barType, int option) {
      auto c = tbl();
      c->set("layerType", static_cast<double>(layerType)).set("barType", static_cast<double>(barType)).set("option", static_cast<double>(option));
      auto cassets = tbl();
      cassets->set("base", assetGroup({entry("default", "tab", 0)}));
      c->set("assets", cassets);
      return c;
    };
    auto bucket = layer(0, -1, 0);
    auto icon = tbl();
    auto iassets = tbl();
    iassets->set("choc", assetGroup({entry("option_0", "tab", 0)}));
    icon->set("assets", iassets);
    auto bucketKids = tbl();
    bucketKids->set("icon", icon);
    bucket->set("children", bucketKids);
    chocolates->set("bucket", bucket);
    chocolates->set("bar_dark", layer(2, 0, 0));
    chocolates->set("fill_bar_0", layer(1, 0, 0));
    chocolates->set("fill_bar_1", layer(1, 0, 1));
    loc->set("chocolates", chocolates);
  }
  auto bg = tbl();
  bg->set("baseDir", "shop/").set("name", "background_object").set("spriteLayer", -2.0).set("mouse", 0.0);
  auto bgAssets = tbl();
  bgAssets->set("background", assetGroup({entry("default", "bg"), entry("upgrade1", "bg"), entry("upgrade2", "bg")}));
  bg->set("assets", bgAssets);
  auto statics = tbl();
  statics->set("spriteLayer", -1.0).set("visible", 0.0);
  auto st1 = tbl();
  st1->set("name", "background_static_1").set("upgrade", "kStatic1");
  auto st1Assets = tbl();
  st1Assets->set("background", assetGroup({entry("default", "bg", 1), entry("upgrade1", "bg", 1), entry("upgrade2", "bg", 1)}));
  st1->set("assets", st1Assets);
  statics->set("static_1", st1);
  bg->set("statics", statics);
  auto anims = tbl();
  anims->set("anim_1", tbl());
  bg->set("anims", anims);
  loc->set("background", bg);
  auto objects = tbl();  // what every customer copies
  objects->set("baseDir", "shop/");
  {
    auto bubble = tbl();
    bubble->set("name", "bubble").set("layer", 100.0);
    auto bassets = tbl();
    bassets->set("bubble_single", assetGroup({entry("happy", "tab"), entry("content", "tab"), entry("neutral", "tab"), entry("annoyed", "tab"), entry("angry", "tab")}));
    bassets->set("transparent", assetGroup({entry("default", "tab")}));
    bubble->set("assets", bassets);
    objects->set("thought_bubble", bubble);
    auto menu = tbl();
    menu->set("name", "menu").set("anchor", "menu");
    auto massets = tbl();
    massets->set("menu", assetGroup({entry("default", "tab")}));
    menu->set("assets", massets);
    objects->set("anim_menu", menu);
    auto money = tbl();
    money->set("name", "money");
    auto cash = tbl();
    cash->set("money", assetGroup({entry("dollar", "tab")}));
    money->set("assets", cash);
    objects->set("anim_money", money);
    auto orderables = tbl();
    orderables->set("name", "other_order").set("anchor", "sauce").set("type", "sauce");
    auto oassets = tbl();
    oassets->set("sauce", assetGroup({entry("default", "tab")}));
    oassets->set("drink", assetGroup({entry("default", "tab")}));
    orderables->set("assets", oassets);
    objects->set("orderables", orderables);
  }
  loc->set("objects", objects);
  auto locals = tbl();
  auto local = tbl();
  local->set("name", "iqu_male");
  locals->set("iqu_male", local);
  loc->set("customers", locals);
  // the avatar
  auto player = tbl();
  player->set("baseDir", "shop/").set("name", "avatar").set("mouse", 0.0).set("spriteLayer", 3.0).set("startZone", "avatar");
  player->set("startPoint", "station01").set("baseY", 100.0).set("rangeY", 100.0).set("scaleRange", 0.1);
  auto playerAssets = tbl();
  for (const char* n : {"walkfront", "walkright", "walkback"}) playerAssets->set(n, assetGroup({entry("default", "tab")}));
  player->set("assets", playerAssets);
  loc->set("player", player);
  root->set("location_iquitos", loc);
  auto groot = tbl();
  groot->set("music_01", "audio/music/grytviken.ogg");
  root->set("location_grytviken", groot);
  return root;
}

struct Fx : ShopAssets, test::ScriptEnv {
  Fx() : ShopAssets(), ScriptEnv(dir.string(), scratch()), textures(fs, nullptr) {
    ctx.module = games::findModule("greatcc");
    ctx.module->attach(ctx);
    wm.setTextures(&textures);
    wm.setNowMs(now);
    wm.pushModal("m");
    world = worldOf(ctx);
    REQUIRE(world);
    // a current player, so saved values survive the reload that LevelSetup does
    ctx.users = std::make_shared<sdk::UserStore>(scratch() + "_store");
    world->progress().attach(ctx.users);
    ctx.users->create("Zed");
    world->values().load(
        "<values><value name='current_level' min='0' max='61' saved='true'/><value name='highest_level' min='0' max='60' saved='true'/>"
        "<value name='total_worth' value='0' saved='true'/><value name='current_worth' value='0' saved='true'/>"
        "<value name='completed_iquitos' value='0' saved='true'/><value name='num_levels_mastered' value='0' saved='true'/>"
        "<value name='do_map_transition' value='0' saved='true'/><value name='angry_customer_limit' value='10'/>"
        "<value name='endless_medium_modifier' value='-0.3'/><value name='endless_hard_modifier' value='-0.5'/>"
        "<value name='endless_diff_modifier' value='0'/></values>");
    std::string goals = "<levelgoals>";
    for (int i = 1; i <= 60; ++i)
      goals += "<levelgoal level='" + std::to_string(i) + "' goal='" + std::to_string(i * 100) + "' mastery='" + std::to_string(i * 160) + "'/>";
    world->levels().loadGoals(goals + "</levelgoals>");
    ctx.strings.loadRows({{"endlevel_blurb_1", "Well done"}, {"endlevel_blurb_2", "Try harder"}, {"endlevel_endless_blurb_1", "Endless"},
                          {"level_abbreviation", "Lvl"}, {"reached_goal", "Goal reached!"}, {"reached_master", "Mastery!"},
                          {"combo_menu", "Menu"}, {"combo_service", "Service"}, {"combo_payment", "Payment"}});
    run("gCommandQueue = {}; function DoModal(p) lastModal = p end; function DisplayDialog(t) lastDialog = t end");
    world->values().reload();
  }
  ~Fx() { wm.destroyAll(); }
  static std::string scratch() {
    const fs::path p = fs::temp_directory_path() / "choco_greatcc_shop_user";
    std::error_code ec;
    fs::remove_all(p, ec);
    fs::create_directories(p, ec);
    return p.string();
  }

  // The HUD of scripts/hud.lua in the parts the shop reads, and the shop around it; returns after the window's
  // postChildrenInit like the DSL's PostCreate.
  void build(const Tbl& style = windowStyle()) {
    // the floating text layer is a sibling made before the shop
    MS layerStyle = rect(0, 0, 800, 600);
    layer = static_cast<RActiveTextLayer*>(&wm.initWindow(std::make_unique<RActiveTextLayer>(wm), layerStyle));
    layer->setName("text_window");
    layer->postChildrenInit(layerStyle);
    wm.popCreation();
    auto source = [style](const std::function<void(const ui::Style&)>& fn) { fn(*style); };
    auto sw = std::make_unique<ShopWindow>(wm, ctx);
    sw->setStyleSource(source);
    shop = static_cast<ShopWindow*>(&wm.initWindow(std::move(sw), *style));
    // VShopHud
    MS hudStyle = rect(466, 0, 334, 200);
    auto& h = static_cast<VShopHud&>(wm.initWindow(std::make_unique<VShopHud>(wm, ctx), hudStyle));
    h.setName("shop_hud");
    {
      MS gs = rect(0, 0, 334, 200);
      auto& gauge = static_cast<BLayeredWindow&>(wm.initWindow(std::make_unique<BLayeredWindow>(wm), gs));
      gauge.setName("progress_gauge");
      for (int layerIndex = 0; layerIndex < 3; ++layerIndex) {
        gauge.setCurrentLayer(layerIndex);
        MS ts = rect(88, 76, 40, 18);
        auto& t = static_cast<ui::TextWindow&>(wm.initWindow(std::make_unique<ui::TextWindow>(wm), ts));
        t.setName(layerIndex == 0 ? "goal_score" : (layerIndex == 1 ? "mastery_score" : "anger_score"));
        wm.popCreation();
      }
      gauge.postChildrenInit(gs);
      wm.popCreation();
    }
    {
      MS ds = rect(0, 0, 350, 350);
      ds.set("center_x", 108.0).set("center_y", 75.0).set("minrot", -1.5).set("maxrot", 1.5).set("needle", "shop/needle");
      auto& d = wm.initWindow(std::make_unique<KDial>(wm), ds);
      d.setName("dialPower");
      d.postChildrenInit(ds);
      wm.popCreation();
    }
    {
      MS ss = rect(154, 49, 62, 62);
      ss.set("center_x", 31.0).set("center_y", 31.0).set("sweep", "shop/sweep");
      auto& d = wm.initWindow(std::make_unique<KSweepDial>(wm), ss);
      d.setName("timer_sweep");
      d.postChildrenInit(ss);
      wm.popCreation();
    }
    {
      MS ns = rect(152, 58, 62, 70);
      auto& t = wm.initWindow(std::make_unique<ui::TextWindow>(wm), ns);
      t.setName("level_name");
      wm.popCreation();
    }
    {
      MS ts = rect(152, 77, 62, 70);
      ts.set("hide", 0.0);
      auto& t = wm.initWindow(std::make_unique<RTimer>(wm), ts);
      t.setName("game_timer");
      t.postChildrenInit(ts);
      wm.popCreation();
    }
    {
      MS es = rect(225, 122, 100, 60);
      auto& ext = wm.initWindow(std::make_unique<ui::BitmapWindow>(wm), es);
      ext.setName("extension");
      wm.popCreation();
    }
    h.postChildrenInit(hudStyle);
    wm.popCreation();
    hud = &h;
    {
      MS qs = rect(0, 0, 245, 250);
      auto& q = wm.initWindow(std::make_unique<VQuestWindow>(wm, ctx), qs);
      q.setName("quest_window");
      q.postChildrenInit(qs);
      wm.popCreation();
    }
    shop->postChildrenInit(*style);
    wm.popCreation();
    // the layer is a sibling in the original (both children of one Bitmap); here both are children of the modal
  }

  // Advances the platform clock by `ms` in 16 ms steps, running the window animations.
  void run_ms(double ms) {
    for (double t = 0; t < ms; t += 16) tickWindowAnimations(now += 16);
  }
  void tickWindowAnimations(double t) { wm.tickWindowAnimations(t); }
  // Runs the command queue the shop and the world fill (RUtil::DoLuaString).
  std::string runQueue() {
    run("lastDialog = nil; lastModal = nil; for i, f in ipairs(gCommandQueue) do f() end gCommandQueue = {}");
    return lastModalOrDialog();
  }
  std::string lastModalOrDialog() { return getString("lastDialog") != "<nil>" ? getString("lastDialog") : getString("lastModal"); }
  std::string dialogText() {
    // DisplayDialog gets a table; the shop's call is checked through the table's fields
    run("local t = lastDialog; dlg = ''; if t then dlg = tostring(t[1])..'|'..tostring(t.level_pass)..'|'..tostring(t.level_num)..'|'..tostring(t.level_status)..'|'..tostring(t.time) end");
    return getString("dlg");
  }

  gfx::TextureCache textures;
  GreatCCWorld* world = nullptr;
  ShopWindow* shop = nullptr;
  VShopHud* hud = nullptr;
  RActiveTextLayer* layer = nullptr;
  double now = 1000;
};

}  // namespace

TEST_CASE("ShopWindow: it marks the game loaded, takes the modal's focus, finds the HUD and the text layer, and binds its natives") {
  Fx f;
  CHECK_FALSE(f.world->settings().gameLoaded);
  f.build();
  CHECK(f.world->settings().gameLoaded);
  CHECK(f.world->shopWindowId() == f.shop->id());
  CHECK(f.wm.topModal()->defaultFocus == f.shop->id());
  CHECK(f.shop->hud() == f.hud);
  CHECK(f.hud->timer());
  CHECK(f.hud->needle());
  CHECK(f.hud->sweep());
  CHECK(f.shop->textLayer() == f.layer);
  CHECK_FALSE(f.isNil("LevelSetup"));
  CHECK_FALSE(f.isNil("ReadyForPrelevel"));
  CHECK_FALSE(f.isNil("GetEndlevelText"));
  // the text layer was moved in front of the shop window
  CHECK(f.wm.topModal()->children().front().get() == f.layer);
  // a window that is destroyed gives the natives back and clears the flag
  f.wm.popModal("m");
  f.wm.clearDestroyed();
  CHECK_FALSE(f.world->settings().gameLoaded);
  CHECK(f.world->shopWindowId() == 0);
  CHECK(f.isNil("LevelSetup"));
  CHECK(f.isNil("ReadyForPrelevel"));
  CHECK(f.isNil("GetEndlevelText"));
}

TEST_CASE("ShopWindow::LevelSetup: level 1 loads with its stations, background, customers and HUD, and arms the pre-level second") {
  Fx f;
  f.build();
  f.world->values().setValue("current_level", 1, true);
  f.world->values().setValue("current_worth", 7, false);
  REQUIRE(f.run("LevelSetup()") == 0);
  REQUIRE(f.shop->level());
  CHECK(f.shop->level()->number == 1);
  CHECK(f.world->levels().currentLevel() == 1);
  CHECK_FALSE(f.shop->endless());
  // the world's cash for the location is the level's worth
  CHECK(f.world->values().getInt("current_worth", -1) == 0);
  // the two tables' stations, lower-cased names; the junk table is not one
  REQUIRE(f.shop->stations().size() == 2);
  VStation* s1 = f.shop->station("station01");
  REQUIRE(s1);
  CHECK(s1->stationType() == kStationCounter);
  CHECK(s1->stationId() == 1);
  REQUIRE(s1->playerPoints().size() == 2);
  CHECK(s1->playerPoints()[0].zonePoint == "station01");
  CHECK(s1->playerPoints()[0].facing == kFaceLeft);
  CHECK(s1->playerPoints()[1].facing == kFaceBack);
  CHECK(s1->customerPoint() == "customer01");
  CHECK(s1->money().x == 128);
  CHECK(s1->money().y == 293);
  CHECK(s1->depth() == 2);
  CHECK(s1->currentAsset() == "tabletop");
  CHECK(s1->currentState() == "default");
  CHECK_FALSE(s1->mouse());  // no machines, no customer
  VStation* s2 = f.shop->station("station02");
  REQUIRE(s2);
  CHECK(s2->customerPoint().empty());  // only counters have one
  CHECK(f.shop->station("station03") == nullptr);
  // the background: depth from the table, the statics tied to upgrades hidden (no upgrade is active), the anims present
  auto bg = f.shop->background();
  REQUIRE(bg);
  CHECK(bg->depth() == -2);
  CHECK(bg->currentState() == "default");
  REQUIRE(bg->statics().size() == 1);
  CHECK_FALSE(bg->statics().at("static_1")->visible());
  CHECK(bg->statics().at("static_1")->name() == "background_static_1");
  CHECK(bg->anims().size() == 1);
  CHECK(f.shop->sprites().objects().size() == 2 + 1 + 1 + 1 + 1);  // two stations, the background, one static, one anim, the avatar
  // the customers of the file the vendor knows: professor and diplomat (the posse); ghost is not
  CHECK(f.shop->pendingCustomers() == 2);
  CHECK(f.shop->roster().posseCount() == 2);
  CHECK(f.shop->roster().localCount() == 1);
  CHECK(f.shop->roster().knows("iqu_male"));
  CHECK_FALSE(f.shop->roster().knows("ghost"));
  // the values the level sets
  CHECK(f.world->values().getReal("chocolate_decay_rate", -1) == doctest::Approx(100.0f / 15000.0f));
  CHECK(f.world->values().getReal("level_customer_decay", -1) == doctest::Approx(1.25f));
  // the HUD: the clock is limited to the level's length and stopped, the goal and mastery texts, the extension window gone
  REQUIRE(f.hud->timer());
  CHECK(f.hud->timer()->label() == "0:04");
  CHECK(f.hud->timer()->timer().durationMs == 4000);
  CHECK_FALSE(f.hud->timer()->timer().running);
  CHECK(f.hud->find("goal_score")->label() == "$100");
  CHECK(f.hud->find("mastery_score")->label() == "$160");
  CHECK(f.hud->find("level_name")->label() == "Lvl  1");
  CHECK_FALSE(f.hud->find("extension")->shown());
  CHECK_FALSE(f.shop->find("quest_window")->shown());
  CHECK(f.hud->needle()->gauge() == doctest::Approx(0.0f));
  CHECK(f.hud->sweep()->gauge() == doctest::Approx(1.0f));
  // armed: a second of pre-level time, locked, soft-paused
  CHECK(f.shop->tickState().startCountdownMs == 1000);
  CHECK(f.shop->tickState().waitingToStart);
  CHECK(f.shop->sprites().lockDown());
  CHECK(f.shop->sprites().softPaused());
}

TEST_CASE("ShopWindow::LevelSetup: loading a level again replaces the stations and the background, and a new user's cash is the level's worth") {
  Fx f;
  f.build();
  f.world->values().setValue("current_level", 1, true);
  f.run("LevelSetup()");
  f.world->levels().updateLevelScore(1, 250);
  f.run("LevelSetup()");
  CHECK(f.shop->stations().size() == 2);
  CHECK(f.shop->sprites().objects().size() == 6);  // not doubled
  CHECK(f.world->values().getInt("current_worth", -1) == 250);
  CHECK(f.shop->pendingCustomers() == 2);
  // level 0 becomes level 1
  f.world->values().setValue("current_level", 0, true);
  f.run("LevelSetup()");
  CHECK(f.world->levels().currentLevel() == 1);
  CHECK(f.shop->level()->number == 1);
}

TEST_CASE("ShopWindow: a level file that is missing leaves a level without length or customers and logs it") {
  Fx f;
  f.build();
  f.world->values().setValue("current_level", 9, true);  // no levels/level09.xml in the fixture
  f.run("LevelSetup()");
  REQUIRE(f.shop->level());
  CHECK(f.shop->level()->number == 9);
  CHECK(f.shop->level()->durationMs() == 0);
  CHECK(f.shop->level()->needPrelevel);
  CHECK(f.shop->pendingCustomers() == 0);
  CHECK(f.log.contains("levels/level09.xml did not load"));
}

TEST_CASE("ShopWindow: the pre-level second, the start of the level, the clock and the soft pause") {
  Fx f;
  f.build();
  f.world->values().setValue("current_level", 1, true);
  f.run("LevelSetup()");
  f.run_ms(500);
  CHECK(f.shop->sprites().softPaused());
  CHECK(f.shop->sprites().lockDown());
  CHECK_FALSE(f.hud->timer()->timer().running);
  CHECK(f.hud->timer()->label() == "0:04");
  f.run_ms(700);  // past the second: the level starts on the tick after the countdown ends
  CHECK_FALSE(f.shop->tickState().waitingToStart);
  CHECK_FALSE(f.shop->sprites().softPaused());
  CHECK(f.hud->timer()->timer().running);
  // lockdown: level 1's window (0..10 percent of 4 s) is the first 400 ms of counted time
  f.run_ms(100);
  CHECK(f.shop->sprites().lockDown());
  f.run_ms(800);
  CHECK_FALSE(f.shop->sprites().lockDown());
  // the clock counts down
  CHECK(f.hud->timer()->timer().elapsedMs > 800);
  CHECK(f.hud->timer()->timer().elapsedMs < 1300);
  CHECK(f.hud->timer()->label() == "0:02");  // 4 s less about 1.1 s
  CHECK(f.hud->sweep()->gauge() < 1.0f);
  // tutorials off: the lockdown is forced off
  f.world->progress().setShowTutorials(false);
  f.shop->sprites().setLockDown(true);
  f.run_ms(32);
  CHECK_FALSE(f.shop->sprites().lockDown());
}

TEST_CASE("ShopWindow: the timer stops while the sprites are soft-paused and resumes without counting the gap") {
  Fx f;
  f.build();
  f.world->values().setValue("current_level", 1, true);
  f.run("LevelSetup()");
  f.run_ms(1400);
  const int before = f.hud->timer()->timer().elapsedMs;
  REQUIRE(before > 0);
  f.shop->sprites().softPause(true);
  f.run_ms(500);
  CHECK(f.hud->timer()->timer().elapsedMs <= before + 16);  // the tick that notices the pause still counts
  CHECK_FALSE(f.hud->timer()->timer().running);
  f.shop->sprites().softPause(false);
  f.run_ms(160);
  CHECK(f.hud->timer()->timer().running);
  CHECK(f.hud->timer()->timer().elapsedMs < before + 16 + 250);  // the 500 ms pause is not in it
}

TEST_CASE("ShopWindow: a story level that nobody plays fails after its time plus two seconds and offers the end-of-level dialog") {
  Fx f;
  f.build();
  f.world->values().setValue("current_level", 1, true);
  f.run("LevelSetup()");
  // both customers (the second waits for the first one's counter) lose their patience in about a second each and leave angry
  f.world->values().setValue("level_customer_decay", 100, false);
  f.run("ReadyForPrelevel()");  // level 1 has no pre-level: the diary flag runs (a plot seam), the level starts at once
  f.run_ms(1100);
  CHECK_FALSE(f.shop->tickState().waitingToStart);
  CHECK(f.shop->tickState().levelOverStamp == 0);
  for (int guard = 0; guard < 400 && f.shop->tickState().levelOverStamp == 0; ++guard) f.run_ms(16);
  // every customer came and went before the 4 s were up: the level is over the moment the last one is gone
  const int stamp = f.shop->tickState().levelOverStamp;
  CHECK(stamp > 1500);
  CHECK(stamp < 4000);
  CHECK(f.shop->activeCustomers() == 0);
  CHECK(f.shop->pendingCustomers() == 0);
  CHECK(f.world->values().getInt("angry_this_level", -1) == 2);
  CHECK(f.shop->levelIsComplete());
  f.run_ms(1900);  // under 2 s after the stamp: nothing yet
  f.runQueue();
  CHECK(f.getString("lastDialog") == "<nil>");
  f.run_ms(200);
  f.runQueue();
  CHECK(f.dialogText() == "scripts/endlevel.lua|endlevel_fail|1|0|nil");
  // the animation task ended: nothing more is queued
  f.run_ms(3000);
  f.runQueue();
  CHECK(f.getString("lastDialog") == "<nil>");
  // nothing was awarded
  CHECK(f.world->levels().currentLevel() == 1);
  CHECK(f.world->levels().status(1) == 0);
  CHECK(f.world->values().getInt("total_worth", -1) == 0);
}

TEST_CASE("ShopWindow::DoEndLevel: pass and master credit the level, move on and run the achievement checks; a fail changes nothing") {
  Fx f;
  f.build();
  f.world->values().setValue("current_level", 1, true);
  f.run("LevelSetup()");
  f.world->values().setValue("current_level_total", 100, false);  // exactly the goal
  f.shop->doEndLevel();
  f.runQueue();
  CHECK(f.dialogText() == "scripts/endlevel.lua|endlevel_pass|1|1|nil");
  CHECK(f.world->levels().status(1) == 1);
  CHECK(f.world->levels().currentLevel() == 2);
  CHECK(f.world->levels().highestComplete() == 1);
  CHECK(f.world->values().getInt("total_worth", -1) == 100);
  CHECK(f.world->values().getInt("current_worth", -1) == 100);
  CHECK(f.world->levels().locationCash(2) == 100);
  CHECK(f.world->values().getInt("completed_iquitos", -1) == 1);
  CHECK(f.world->values().getInt("num_levels_mastered", -1) == 0);
  // replaying level 1 for the master score pays only the improvement, counts the mastery and not a second completion
  f.world->values().setValue("current_level", 1, true);
  f.run("LevelSetup()");
  f.world->values().setValue("current_level_total", 160, false);
  f.shop->doEndLevel();
  f.runQueue();
  CHECK(f.dialogText() == "scripts/endlevel.lua|endlevel_master|1|2|nil");
  CHECK(f.world->levels().status(1) == 2);
  CHECK(f.world->values().getInt("total_worth", -1) == 160);
  CHECK(f.world->values().getInt("completed_iquitos", -1) == 1);
  CHECK(f.world->values().getInt("num_levels_mastered", -1) == 1);
  // a failed attempt at a later level touches nothing
  f.world->values().setValue("current_level", 2, true);
  f.run("LevelSetup()");
  const int worth = f.world->values().getInt("total_worth", -1);
  f.world->values().setValue("current_level_total", 5, false);
  f.shop->doEndLevel();
  f.runQueue();
  CHECK(f.dialogText() == "scripts/endlevel.lua|endlevel_fail|2|0|nil");
  CHECK(f.world->levels().currentLevel() == 2);
  CHECK(f.world->values().getInt("total_worth", -1) == worth);
  CHECK(f.world->levels().status(2) == 0);
}

TEST_CASE("ShopWindow::DoEndLevel: finishing the last level of a location sets the map transition flag") {
  Fx f;
  f.build();
  f.world->levels().setHighestComplete(9, true);
  f.world->values().setValue("current_level", 10, true);
  f.run("LevelSetup()");
  f.world->values().setValue("current_level_total", 1000000, false);
  f.shop->doEndLevel();
  CHECK(f.world->levels().currentLevel() == 11);
  CHECK(f.world->values().getInt("do_map_transition", -1) == 1);
}

TEST_CASE("ShopWindow: the pre-level dialog opens once for a level above 3, through the command queue") {
  Fx f;
  f.build();
  f.world->values().setValue("current_level", 4, true);
  f.run("LevelSetup()");
  REQUIRE(f.shop->level());
  CHECK(f.shop->level()->needPrelevel);
  f.run("ReadyForPrelevel()");
  f.run_ms(32);
  f.run("lastModal = nil");
  f.runQueue();
  CHECK(f.lastModalOrDialog() == "scripts/prelevel.lua");
  CHECK_FALSE(f.shop->level()->needPrelevel);  // shown once per load
  CHECK_FALSE(f.shop->tickState().prelevelPending);
  // the level has not started (a dialog was shown); the next tick starts it
  CHECK(f.shop->tickState().waitingToStart);
  f.run_ms(1100);
  CHECK_FALSE(f.shop->tickState().waitingToStart);
}

TEST_CASE("ShopWindow: an endless run starts at the first level of its location and ends after the anger limit") {
  Fx f;
  f.build();
  f.world->settings().setMode(1);
  f.world->values().setValue("current_level", 15, true);
  f.run("LevelSetup()");
  CHECK(f.shop->endless());
  CHECK(f.world->levels().currentLevel() == 11);
  REQUIRE(f.shop->level());
  CHECK(f.shop->level()->number == 11);
  CHECK(f.world->values().getReal("endless_diff_modifier", -1) == doctest::Approx(0.0));
  CHECK(f.hud->timer()->timer().incrementing);
  CHECK(f.hud->timer()->label() == "0:00");
  CHECK(f.hud->find("anger_score")->label() == "0/10");
  CHECK(f.hud->gauge()->currentLayer() == 0);  // the HUD picks the anger layer in its own PostChildrenInit only
  CHECK(f.hud->find("extension")->shown());     // endless keeps the Upgrade button
  CHECK(f.hud->sweep()->gauge() == doctest::Approx(1.0f));
  f.run_ms(1100);
  CHECK_FALSE(f.shop->tickState().waitingToStart);
  f.run_ms(2000);
  CHECK(f.hud->timer()->label() != "0:00");  // counts up
  CHECK(f.shop->tickState().levelOverStamp == 0);
  f.world->values().setValue("angry_this_level", 10, false);
  f.run_ms(100);
  CHECK(f.shop->tickState().levelOverStamp != 0);
  CHECK(f.hud->find("anger_score")->label() == "10/10");
  f.run_ms(2100);
  f.runQueue();
  CHECK(f.dialogText().rfind("scripts/endlevel.lua|endlevel_endless|nil|0|0:", 0) == 0);
  CHECK(f.world->values().getInt("endless_time", -1) >= 3);
  // the difficulty picks the modifier, not the mode number
  f.world->settings().difficulty = 2;
  f.run("LevelSetup()");
  CHECK(f.world->values().getReal("endless_diff_modifier", 0) == doctest::Approx(-0.5));
  f.world->settings().difficulty = 1;
  f.run("LevelSetup()");
  CHECK(f.world->values().getReal("endless_diff_modifier", 0) == doctest::Approx(-0.3));
  // level 20 starts at 11, level 21 stays
  f.world->values().setValue("current_level", 20, true);
  f.run("LevelSetup()");
  CHECK(f.world->levels().currentLevel() == 11);
}

TEST_CASE("ShopWindow: the first endless customer arrives at 3000 ms and schedules the next from the delay and the draw") {
  Fx f;
  f.ctx.rng.seed(1);
  f.build(windowStyle(false, false, true));  // two counters: the second arrival has somewhere to sit
  f.world->settings().setMode(1);
  f.world->values().setValue("current_level", 1, true);
  f.run("LevelSetup()");
  for (const auto& [name, v] : std::vector<std::pair<const char*, double>>{{"customer_delay", 15000}, {"customer_delay_variance", 6000}, {"customer_delay_modifier", 0.0006}})
    f.world->values().setValue(name, v, false);  // Easy's numbers (values.xml)
  REQUIRE(f.shop->endless());
  CHECK(f.shop->nextEndlessSpawnMs() == 3000);
  // nobody before the clock reaches 3000; the first tick at or after it brings one
  int firstAt = -1;
  for (int i = 0; i < 400 && firstAt < 0; ++i) {
    f.run_ms(16);
    if (f.shop->activeCustomers() > 0) firstAt = f.hud->timer()->timer().elapsedMs;
    else CHECK(f.hud->timer()->timer().elapsedMs < 3000 + 20);
  }
  REQUIRE(firstAt >= 3000);
  CHECK(firstAt < 3000 + 40);
  CHECK(f.shop->activeCustomers() == 1);
  // the next one is due delay - variance / 2 .. delay + variance / 2 after that (customer_delay 15000, variance 6000 on Easy), shortened by
  // the elapsed-time modifier
  const int next = f.shop->nextEndlessSpawnMs();
  const int wait = next - firstAt;
  const float shorten = 1.0f - f.world->values().getProduct("customer_delay_modifier", 3.0f, true);
  CHECK(wait >= static_cast<int>(12000 * shorten) - 40);
  CHECK(wait < static_cast<int>(18000 * shorten) + 40);
  // nothing arrives before it is due; the tick that reaches it brings the second customer
  while (f.hud->timer()->timer().elapsedMs < next - 40) {
    f.run_ms(16);
    f.world->values().setValue("angry_this_level", 0, false);  // no run-ending anger while the test waits
    REQUIRE(f.shop->activeCustomers() <= 1);
  }
  for (int i = 0; i < 10 && f.shop->activeCustomers() < 2; ++i) f.run_ms(16);
  CHECK(f.shop->activeCustomers() == 2);
  CHECK(f.hud->timer()->timer().elapsedMs >= next);
  CHECK(f.shop->nextEndlessSpawnMs() > next);
}

TEST_CASE("DoCheatGame: refused without cheatmode") {
  Fx f;
  f.build();
  f.run("LevelSetup()");
  f.world->values().setValue("current_level_total", 50, false);
  f.run("r = DoCheatGame(2, '')");
  CHECK(f.getString("r") == "Invalid Cheat");
  CHECK(f.world->values().getInt("current_level_total", 0) == 50);
  f.runQueue();
  CHECK(f.getString("lastDialog") == "<nil>");  // the level did not end
  bool logged = false;
  for (const std::string& l : f.log.lines()) logged = logged || l.find("DoCheatGame refused") != std::string::npos;
  CHECK(logged);
  f.run("r = DoCheatGame(5, '30')");
  CHECK(f.world->levels().highestComplete() == 0);
}

TEST_CASE("DoCheatGame: fail, pass and master move the total into their band and end the level at once") {
  struct Case {
    int type;
    int before;
    int total;
    const char* dialog;
  };
  // level 1: goal 100, mastery 160
  for (const Case& c : {Case{0, 130, 99, "|endlevel_fail|"}, Case{1, 10, 100, "|endlevel_pass|"}, Case{1, 500, 159, "|endlevel_pass|"},
                        Case{2, 5, 160, "|endlevel_master|"}, Case{2, 400, 400, "|endlevel_master|"}}) {
    Fx f;
    f.build();
    f.ctx.config.set("cheatmode", "1");
    f.run("LevelSetup()");
    REQUIRE(f.world->levels().goal(1) == 100);
    REQUIRE(f.world->levels().mastery(1) == 160);
    f.world->values().setValue("current_level_total", c.before, false);
    f.run("r = DoCheatGame(" + std::to_string(c.type) + ", '')");
    CHECK(f.getString("r").empty());
    f.runQueue();
    const std::string dlg = f.dialogText();
    CHECK(dlg.find(c.dialog) != std::string::npos);
    CHECK(dlg.find("scripts/endlevel.lua") == 0);
    // DoEndLevel read the moved total: a passed level is in the levels' book with that score as its best
    CHECK(f.world->levels().bestScore(1) == doctest::Approx(c.total));
  }
}

TEST_CASE("DoCheatGame: money, go to a level, set the highest complete, and what is out of range") {
  Fx f;
  f.build();
  f.ctx.config.set("cheatmode", "1");
  f.run("LevelSetup()");
  const double cash = f.world->levels().currentScore();
  const double worth = f.world->values().getReal("total_worth", 0);
  f.run("r = DoCheatGame(3, '250')");
  CHECK(f.getString("r") == "Added 250 to current and lifetime total");
  CHECK(f.world->values().getInt("current_level_total", 0) == 250);
  CHECK(f.world->levels().currentScore() == doctest::Approx(cash + 250));
  CHECK(f.world->values().getReal("total_worth", 0) == doctest::Approx(worth + 250));
  f.run("r = DoCheatGame(3, '-4')");
  CHECK(f.world->values().getInt("current_level_total", 0) == 250);  // not positive: nothing is added (the text still says so)
  f.run("r = DoCheatGame(4, '61')");
  CHECK(f.getString("r") == "Invalid Level");
  f.run("r = DoCheatGame(4, '12')");
  CHECK(f.getString("r").empty());
  CHECK(f.world->levels().currentLevel() == 12);
  f.run("r = DoCheatGame(5, '0')");
  CHECK(f.getString("r") == "Invalid Level");
  f.run("r = DoCheatGame(5, '30')");
  CHECK(f.getString("r") == "Set highest level complete to 30");
  CHECK(f.world->levels().highestComplete() == 30);
  f.run("r = DoCheatGame(9, '')");
  CHECK(f.getString("r") == "Invalid Cheat");
}

TEST_CASE("DoCheatGame in an endless run: fail adds ten angry customers and sets five served; money only goes to the run's total") {
  Fx f;
  f.build();
  f.ctx.config.set("cheatmode", "1");
  f.world->settings().setMode(1);
  f.run("LevelSetup()");
  f.world->values().setValue("angry_this_level", 1, false);
  f.run("r = DoCheatGame(0, '')");
  CHECK(f.world->values().getInt("angry_this_level", 0) == 11);
  CHECK(f.world->values().getInt("served_this_level", 0) == 5);
  f.runQueue();
  CHECK(f.dialogText().find("scripts/endlevel.lua|endlevel_endless|") == 0);
  const double worth = f.world->values().getReal("total_worth", 0);
  f.run("r = DoCheatGame(3, '40')");
  CHECK(f.world->values().getInt("current_level_total", 0) == 40);
  CHECK(f.world->values().getReal("total_worth", 0) == doctest::Approx(worth));
}

TEST_CASE("GetEndlevelText: the blurb by status in story, the endless blurb in an endless run") {
  Fx f;
  f.build();
  f.run("LevelSetup()");
  f.run("t0 = GetEndlevelText(0); t1 = GetEndlevelText(1); t2 = GetEndlevelText(2)");
  CHECK(f.getString("t0") == "Try harder");
  CHECK(f.getString("t1") == "Well done");
  CHECK(f.getString("t2") == "Well done");
  f.world->settings().setMode(2);
  f.run("LevelSetup()");
  f.run("t0 = GetEndlevelText(0); t1 = GetEndlevelText(1)");
  CHECK(f.getString("t0") == "Endless");
  CHECK(f.getString("t1") == "Endless");
}

TEST_CASE("VShopHud: gauge layer and needle follow the score, the sweep follows the time, the timer follows the pause") {
  Fx f;
  f.build();
  f.world->values().setValue("current_level", 1, true);
  f.run("LevelSetup()");
  VShopHud& h = *f.hud;
  h.updateHud(false, false, false);
  CHECK(h.gauge()->currentLayer() == 0);
  CHECK(h.needle()->gauge() == doctest::Approx(0.0f));
  f.world->values().setValue("current_level_total", 50, false);
  h.updateHud(false, false, false);
  CHECK(h.needle()->gauge() == doctest::Approx(0.5f));
  CHECK(h.gauge()->currentLayer() == 0);
  f.world->values().setValue("current_level_total", 100, false);
  h.updateHud(false, false, false);
  CHECK(h.gauge()->currentLayer() == 1);  // goal reached
  CHECK(h.needle()->gauge() == doctest::Approx(0.0f));
  f.world->values().setValue("current_level_total", 130, false);
  h.updateHud(false, false, false);
  CHECK(h.needle()->gauge() == doctest::Approx(0.5f));  // (130 - 100) / (160 - 100)
  // the sweep: 1 - elapsed / duration, written only when different
  h.timer()->timer().elapsedMs = 1000;
  h.updateHud(false, false, false);
  CHECK(h.sweep()->gauge() == doctest::Approx(0.75f));
  // the timer runs unless the sprites are paused or a tutorial is up
  h.updateHud(true, false, false);
  CHECK_FALSE(h.timer()->timer().running);
  h.updateHud(false, false, false);
  CHECK(h.timer()->timer().running);
  h.updateHud(false, true, false);
  CHECK_FALSE(h.timer()->timer().running);
  // the endless anger count
  f.world->values().setValue("angry_this_level", 4, false);
  h.updateHud(false, false, true);
  CHECK(h.needle()->gauge() == doctest::Approx(0.4f));
  CHECK(h.find("anger_score")->label() == "4/10");
  CHECK(h.sweep()->gauge() == doctest::Approx(1.0f));
}

// ================================================================ pause, focus, quit

TEST_CASE("pause: Escape and losing the foreground pause a running game once; entering a game in the background pauses it") {
  Fx f;
  bindSettingsNatives(f.lua.L(), f.ctx);
  auto queued = [&] { return f.runQueue(); };
  // not inside a game: nothing
  f.ctx.module->onEscapeKey(f.ctx);
  queued();
  CHECK(f.getString("lastModal") == "<nil>");
  f.run("InsideGame(true)");
  CHECK(f.world->settings().insideGame);
  queued();
  CHECK(f.getString("lastModal") == "<nil>");  // the application is in front
  f.ctx.module->onEscapeKey(f.ctx);
  CHECK_FALSE(f.world->settings().insideGame);  // InsideGame(false)
  CHECK(queued() == "scripts/pause.lua");
  f.ctx.module->onEscapeKey(f.ctx);  // the pause dialog is up (InsideGame false): a second Escape does nothing
  queued();
  CHECK(f.getString("lastModal") == "<nil>");
  // the dialog's own InsideGame(true) on resume, then the focus is lost
  f.run("InsideGame(true)");
  f.world->setForeground(false, 5000);
  CHECK_FALSE(f.world->foreground());
  CHECK_FALSE(f.world->settings().insideGame);
  CHECK(queued() == "scripts/pause.lua");
  // InsideGame(true) while in the background opens the pause dialog at once
  f.run("InsideGame(true)");
  CHECK(f.world->settings().insideGame);
  CHECK(queued() == "scripts/pause.lua");
  f.world->setForeground(true, 6000);
  CHECK(f.world->foreground());
}

TEST_CASE("QuitToMainMenu pops every modal that has a parent, top first: all but the Screen, DoMainWindow's included") {
  Fx f;
  bindSettingsNatives(f.lua.L(), f.ctx);
  f.wm.pushModal("game");  // "m" is the modal DoMainWindow pushed (the screens swap into it); the Screen is under it
  f.wm.pushModal("pause");
  f.wm.pushModal("yesno");
  REQUIRE(f.wm.modalDepth() == 5);
  f.run("QuitToMainMenu()");
  f.wm.clearDestroyed();
  CHECK(f.wm.modalDepth() == 1);
  f.run("QuitToMainMenu()");  // nothing left to pop
  CHECK(f.wm.modalDepth() == 1);
}

TEST_CASE("QuitToMainMenu destroys the shop window: the game is no longer loaded and its natives are gone") {
  Fx f;
  bindSettingsNatives(f.lua.L(), f.ctx);
  f.wm.pushModal("game");  // above "m", the stand-in for the main menu's modal
  f.build();
  f.wm.pushModal("loading");
  CHECK(f.world->settings().gameLoaded);
  f.run("QuitToMainMenu()");
  f.wm.clearDestroyed();
  CHECK_FALSE(f.world->settings().gameLoaded);
  CHECK(f.isNil("LevelSetup"));
}

TEST_CASE("GreatCCSound::preload decodes nothing without an audio service") {
  Fx f;
  CHECK_FALSE(f.world->sound().preload("audio/sfx/a.ogg"));
  CHECK_FALSE(f.world->sound().preload(""));
  CHECK_FALSE(f.world->sound().preload("audio/sfx/not_ogg.wav"));
  f.build();
  f.run("LevelSetup()");  // reads assets.xml and preloads: harmless without audio
  CHECK(f.shop->level());
}

TEST_CASE("ShopWindow: F2 reads kTruffler as kTruffle with the fixes on, as unspecified with them off, and says so once") {
  {
    Fx f;
    f.build();
    f.world->values().setValue("current_level", 26, true);
    f.run("LevelSetup()");
    REQUIRE(f.shop->level());
    REQUIRE(f.shop->level()->customers.size() == 1);
    CHECK(f.shop->level()->customers[0]->orders[0].shape == kShapeTruffle);
    CHECK(f.log.contains("[fix] F2"));
    f.run("LevelSetup()");
    CHECK(f.log.count("[fix] F2") == 1);
  }
  {
    Fx f;
    f.world->settings().fixBugs = false;
    f.build();
    f.world->values().setValue("current_level", 26, true);
    f.run("LevelSetup()");
    REQUIRE(f.shop->level());
    CHECK(f.shop->level()->customers[0]->orders[0].shape == kShapeUnspecified);
    CHECK_FALSE(f.log.contains("[fix] F2"));
  }
}

TEST_CASE("SpriteManager::reset empties the manager and starts its clock over; a background leaves with its manager without disturbing it") {
  Fx f;
  f.build();
  f.world->values().setValue("current_level", 1, true);
  f.run("LevelSetup()");
  SpriteManager& m = f.shop->sprites();
  REQUIRE(m.objects().size() == 6);
  m.softPause(true);
  m.setLockDown(true);
  m.setNow(5000);
  m.updateObjects();
  m.setNow(5100);
  CHECK(m.getElapsed() == 100);
  m.reset();
  CHECK(m.objects().empty());
  CHECK_FALSE(m.softPaused());
  CHECK(m.lockDown());  // the lockdown is not part of Reset
  CHECK(m.getElapsed() == 0);
  CHECK(m.hover() == nullptr);
  // loading again after a reset works, and a manager that dies with a background in it does not trip over its statics
  f.run("LevelSetup()");
  CHECK(f.shop->sprites().objects().size() == 6);
  auto bg = f.shop->background();
  REQUIRE(bg);
  auto stat = bg->statics().at("static_1");
  f.wm.popModal("m");
  f.wm.clearDestroyed();
  CHECK(stat->manager() == nullptr);
  CHECK(bg->manager() == nullptr);
}

// ================================================================ the player in the shop

TEST_CASE("ShopWindow: the zone graph is read once, the avatar stands on her start point, and the stations know their points") {
  Fx f;
  f.build();
  CHECK(f.shop->zones() == nullptr);  // the first level load makes it
  f.world->values().setValue("current_level", 1, true);
  f.run("LevelSetup()");
  REQUIRE(f.shop->zones());
  auto graph = f.shop->zones();
  auto p = f.shop->playerSprite();
  REQUIRE(p);
  CHECK(p->name() == "avatar");
  CHECK(p->depth() == 3);
  CHECK(p->position().x == 100);
  CHECK(p->position().y == 100);
  CHECK(p->currentNode() == graph->findPoint("avatar", "station01"));
  CHECK(p->host() != nullptr);
  CHECK(p->walkSpeed() == doctest::Approx(5.0f));  // the default of walkspeed when values.xml has none
  CHECK(f.shop->station("station01")->points().size() == 2);
  CHECK(f.shop->stationAt(graph->findPoint("avatar", "station02_b")) == f.shop->stations().at("station02").lock());
  CHECK(f.shop->stationAt(graph->findPoint("customers", "customer01")) == nullptr);
  CHECK(f.shop->station("station01")->customerZonePoint() == graph->findPoint("customers", "customer01"));
  // a second load keeps the graph and makes a new avatar
  f.run("LevelSetup()");
  CHECK(f.shop->zones() == graph);
  CHECK(f.shop->playerSprite() != p);
  CHECK(p->manager() == nullptr);
  CHECK(f.shop->detail().find(" player=100.0,100.0 face=2 idle=- moving=0 queue=0 dest=station01 trail=-") != std::string::npos);
}

TEST_CASE("ShopWindow: a click on a station walks her to its nearest point, the arrival turns her, and clicks queue in order") {
  Fx f;
  f.build();
  f.world->values().setValue("current_level", 1, true);
  f.run("LevelSetup()");
  auto p = f.shop->playerSprite();
  REQUIRE(p);
  VStation* s2 = f.shop->station("station02");
  VStation* s1 = f.shop->station("station01");
  s2->onClick();  // nearest of (300,100) and (300,150) to (100,100) is (300,100)
  s1->onClick();  // she is still at station01: its nearest is station01 itself
  s2->onClick();
  REQUIRE(p->queue().size() == 3);
  CHECK(p->queue().front()->name == "station02");
  bool sawLeft = false;
  for (int i = 0; i < 400; ++i) {
    f.run_ms(16);
    sawLeft = sawLeft || (p->queue().empty() && !p->moving() && p->facing() == kFaceLeft);
  }
  CHECK(p->queue().empty());
  CHECK_FALSE(p->moving());
  CHECK(p->position().x == 300.0f);
  CHECK(p->position().y == 100.0f);
  CHECK(p->nodeTrail() == std::vector<std::string>{"station02", "station01", "station02"});
  // the last arrival turned her to the point's facing (kLeft for station02), and half a second later she faces front again
  CHECK(sawLeft);
  CHECK(p->facing() == kFaceFront);
}

TEST_CASE("ShopWindow: she walks and takes her queue while the sprites are soft-paused, and the level clock does not run") {
  Fx f;
  f.build();
  f.world->values().setValue("current_level", 1, true);
  f.run("LevelSetup()");
  REQUIRE(f.shop->sprites().softPaused());  // the pre-level second
  auto p = f.shop->playerSprite();
  f.shop->station("station02")->onClick();
  f.run_ms(400);
  REQUIRE(f.shop->sprites().softPaused());
  CHECK(p->moving());
  CHECK(p->position().x > 150);
  CHECK_FALSE(f.hud->timer()->timer().running);
}

TEST_CASE("ShopWindow: loading the level again with a move queued gives a new avatar with an empty queue, and the old one is gone") {
  Fx f;
  f.build();
  f.world->values().setValue("current_level", 1, true);
  f.run("LevelSetup()");
  auto old = f.shop->playerSprite();
  f.shop->station("station02")->onClick();
  f.shop->station("station02")->onClick();
  f.run_ms(200);
  REQUIRE(old->moving());
  REQUIRE(old->queue().size() == 1);
  f.run("LevelSetup()");
  auto fresh = f.shop->playerSprite();
  REQUIRE(fresh);
  CHECK(fresh != old);
  CHECK(fresh->queue().empty());
  CHECK(old->manager() == nullptr);
  CHECK(fresh->position().x == 100);
  // the old avatar does not take part in the scene any more and leaves her queue alone
  f.run_ms(500);
  CHECK(old->queue().size() == 1);
  // the route the old one left in the graph (the centre table is absent here, so none) does not move the new one
  CHECK_FALSE(fresh->moving());
}

TEST_CASE("ShopWindow: an arrival that loads the level again is safe, and the old avatar does not walk on") {
  Fx f;
  f.build();
  f.world->values().setValue("current_level", 1, true);
  f.run("LevelSetup()");
  auto old = f.shop->playerSprite();
  struct Reloader : VStation {
    using VStation::VStation;
    std::function<void()> go;
    void onArrival(VPlayer& pl, const ZonePoint* p) override {
      VStation::onArrival(pl, p);
      if (go) go();
    }
  };
  // a shop that hands the avatar a station whose arrival loads the level again
  struct ReloadHost : ShopHost {
    ShopWindow* shop = nullptr;
    std::shared_ptr<VStation> station;
    std::shared_ptr<VPlayer> player() const override { return shop->playerSprite(); }
    bool hasCenterTable() const override { return false; }
    std::shared_ptr<VStation> stationByZonePoint(const ZonePoint*) const override { return station; }
  } host;
  auto spy = std::make_shared<Reloader>(f.ctx, 9, kStationEmpty);
  int reloads = 0;
  spy->go = [&] {
    ++reloads;
    f.run("LevelSetup()");
  };
  host.shop = f.shop;
  host.station = spy;
  old->setHost(&host);
  const ZonePoint* far = f.shop->zones()->findPoint("avatar", "station02");
  old->queueMovePoint(const_cast<ZonePoint*>(far));
  old->queueMovePoint(const_cast<ZonePoint*>(far));
  f.run_ms(5000);
  CHECK(reloads == 1);  // the second queued point was never started: the old avatar left the scene at the first arrival
  CHECK(f.shop->playerSprite() != old);
  CHECK(old->manager() == nullptr);
  CHECK(old->queue().size() == 1);
  CHECK(f.shop->playerSprite()->queue().empty());
  CHECK(f.shop->playerSprite()->position().x == 100);
}

TEST_CASE("ShopWindow: station16 shows only while kDisplay1 is on (from level 2), so the avatar walks straight on level 1 and round the table on level 2") {
  for (const int level : {1, 2}) {
    const bool shown = level == 2;
    // the level with a display case in its stations
    Fx g;
    auto style = windowStyle(true);
    g.build(style);
    g.world->values().setValue("current_level", level, true);
    g.run("LevelSetup()");
    VStation* table = g.shop->station("station16");
    REQUIRE(table);
    CHECK(table->stationType() == kStationDisplay);
    CHECK(dynamic_cast<VDisplay*>(table) != nullptr);
    CHECK(g.world->upgradeActive("kDisplay1", "station") == shown);
    CHECK(table->visible() == shown);  // VDisplay::Init
    CHECK(g.shop->centerTable() == shown);
    auto p = g.shop->playerSprite();
    g.shop->station("station02")->onClick();
    g.run_ms(4000);
    CHECK(p->position().x == 300.0f);
    CHECK(p->position().y == 100.0f);
    if (shown) CHECK(p->nodeTrail() == std::vector<std::string>{"station01", "station01_b", "station02_b", "station02", "station02"});
    else CHECK(p->nodeTrail() == std::vector<std::string>{"station02"});
  }
}

TEST_CASE("ShopWindow: a level load grants the location's upgrades by level, makes the vendor and the machines; a reload in the middle of a mixing starts clean") {
  Fx f;
  f.build(windowStyle(false, true));
  f.world->values().setValue("current_level", 1, true);
  f.run("LevelSetup()");
  REQUIRE(f.shop->vendor());
  CHECK(f.shop->vendor()->prototypeCount() == 4);
  CHECK(f.world->upgradeActive("kMixer1", "station"));
  CHECK_FALSE(f.world->upgradeActive("kMixer2", "station"));
  // UpdateCapability(true) ran with those upgrades: the bar is available, the first mix too
  CHECK(f.shop->vendor()->pools().barTypes(true) == std::set<int>{0});
  CHECK(f.shop->vendor()->pools().options(true, 0, kLayerChocolate) == std::set<int>{0});
  CHECK(f.shop->vendor()->pools().maxLayers() == 2);  // the level file's maxlayers
  CHECK(f.shop->vendor()->chocolateCount() == 0);
  VStation* st = f.shop->station("station02");
  REQUIRE(st);
  REQUIRE(st->machines().size() == 1);
  auto mixer = std::dynamic_pointer_cast<VMixer>(st->machines().begin()->second);
  REQUIRE(mixer);
  CHECK(mixer->enabled());
  CHECK(mixer->host() != nullptr);
  CHECK(st->mouse());
  CHECK_FALSE(f.shop->station("station01")->mouse());  // a counter without a customer
  // start a mixing: the choice, then the avatar's arrival at the station
  auto player = f.shop->playerSprite();
  REQUIRE(player);
  mixer->chooseOption(0);
  st->onArrival(*player, st->points().front());
  CHECK(mixer->state() == 1);
  REQUIRE(mixer->chocolate());
  CHECK(f.shop->vendor()->chocolateCount() == 1);
  CHECK(f.shop->vendor()->canCreate(*mixer->chocolate()) == false);  // a bucket has no shape: not makeable
  std::weak_ptr<VMachine> old = mixer;
  std::weak_ptr<VStation> oldStation = st->singleMachine()->station();
  mixer.reset();
  f.run_ms(100);
  // the level is loaded again in the middle of it
  f.run("LevelSetup()");
  f.run_ms(200);
  CHECK(old.expired());
  CHECK(oldStation.expired());
  VStation* again = f.shop->station("station02");
  REQUIRE(again);
  auto fresh = std::dynamic_pointer_cast<VMixer>(again->machines().begin()->second);
  REQUIRE(fresh);
  CHECK(fresh->state() == 0);
  CHECK(fresh->chocolate() == nullptr);
  CHECK(fresh->option() == -1);
  CHECK(fresh->enabled());
  CHECK(f.shop->vendor()->chocolateCount() == 0);
  CHECK(f.shop->vendor()->pools().barTypes(true) == std::set<int>{0});
  // a later level of the location has the second mix
  f.world->values().setValue("current_level", 4, true);
  f.run("LevelSetup()");
  CHECK(f.world->upgradeActive("kMixer2", "station"));
  CHECK(f.shop->vendor()->pools().options(true, 0, kLayerChocolate) == std::set<int>{0, 1});
  CHECK(f.log.contains("[lua-error]") == false);
}

TEST_CASE("ShopWindow: F5 drops a route left in the graph at the next level load with the fixes on, and keeps it with them off") {
  for (const bool fix : {true, false}) {
    Fx g;
    g.world->settings().fixBugs = fix;
    auto style = windowStyle(true);
    g.build(style);
    g.world->values().setValue("current_level", 1, true);
    g.run("LevelSetup()");
    g.shop->station("station16")->setVisible(true);
    g.shop->station("station02")->onClick();
    g.run_ms(100);
    REQUIRE_FALSE(g.shop->zones()->pathEmpty());
    g.run("LevelSetup()");
    CHECK(g.log.contains("[fix] F5") == fix);
    CHECK(g.shop->playerSprite()->moving() == !fix);
  }
}

TEST_CASE("ShopWindow: every level setup starts the level's game states over (InitMetrics)") {
  Fx f;
  f.build();
  f.world->values().setValue("current_level", 1, true);
  f.run("LevelSetup()");
  GameStateStore& gs = f.world->gameStates();
  CHECK(gs.get("TrashUsed") == "0");
  CHECK(gs.get("TrashUsedItemID") == "-1");
  CHECK(gs.get("LevelNumber") == "1");
  gs.set("++TrashUsed");
  gs.set("++DisplayUsed");
  gs.set("TrashUsedItemID", "sauce");
  f.run("LevelSetup()");
  CHECK(gs.get("TrashUsed") == "0");
  CHECK(gs.get("DisplayUsed") == "0");
  CHECK(gs.get("TrashUsedItemID") == "-1");
}

// ================================================================ customers and scoring (slice 13)

namespace {

// The level of `number` is loaded and its second of pre-level time is over.
void startLevel(Fx& f, int number) {
  f.world->values().setValue("current_level", number, true);
  f.run("LevelSetup()");
  f.run("ReadyForPrelevel()");
  f.run_ms(1200);
  REQUIRE_FALSE(f.shop->tickState().waitingToStart);
}
void priceValues(Fx& f) {
  ValueManager& v = f.world->values();
  for (const auto& [n, x] : std::vector<std::pair<const char*, double>>{
           {"salemodifier", 1.0}, {"tipmodifier", 1.0}, {"menu_combobonus", 3}, {"service_combobonus", 3}, {"payout_combobonus", 3},
           {"sauce_price", 15}, {"order_patience_bonus", 20}, {"customer_fade_time", 500}, {"transition_time", 200},
           {"patience_decay_modifier", 1}, {"layer_price", 10}, {"customer_idle_cooldown_base", 15000}, {"customer_idle_cooldown_var", 5000}})
    v.setValue(n, x, false);
}
std::vector<std::string> names(const Fx& f) {
  std::vector<std::string> out;
  for (const auto& c : f.shop->activeList()) out.push_back(c->name());
  return out;
}
std::shared_ptr<VCustomer> customerNamed(const Fx& f, const std::string& n) {
  for (const auto& c : f.shop->activeList())
    if (c->name() == n) return c;
  return nullptr;
}
std::vector<const FloatingText*> floating(const Fx& f) {
  std::vector<const FloatingText*> out;
  for (const auto& t : f.layer->texts())
    if (const auto* ft = dynamic_cast<const FloatingText*>(t.get())) out.push_back(ft);
  return out;
}
void visit(Fx& f, const char* station) {
  VStation* s = f.shop->station(station);
  REQUIRE(s);
  auto p = f.shop->playerSprite();
  REQUIRE(p);
  s->onArrival(*p, s->points().front());
}

}  // namespace

TEST_CASE("ShopWindow::UpdateCustomers: a pending customer arrives when its counter is free and its percent is reached; one that names a taken counter waits") {
  Fx f;
  f.build(windowStyle(false, false, true));
  f.world->values().setValue("current_level", 5, true);
  f.run("LevelSetup()");
  REQUIRE(f.shop->pendingCustomers() == 4);
  CHECK(f.shop->activeCustomers() == 0);
  f.run("ReadyForPrelevel()");
  // nobody comes during the pre-level second (the sprites are soft-paused)
  f.run_ms(500);
  CHECK(f.shop->activeCustomers() == 0);
  f.run_ms(700);
  REQUIRE_FALSE(f.shop->tickState().waitingToStart);
  f.run_ms(50);
  // a asked for station01; b asked for it too and waits; c's time (50 %) has not come; d named a counter nobody has: it takes the free one
  CHECK(names(f) == std::vector<std::string>{"a", "d"});
  CHECK(f.shop->pendingCustomers() == 2);
  CHECK(f.shop->station("station01")->customer() == customerNamed(f, "a"));
  CHECK(f.shop->station("station02")->customer() == customerNamed(f, "d"));
  CHECK(customerNamed(f, "a")->state() == kCustomerArrived);
  CHECK(customerNamed(f, "a")->patience() > 99.0f);
  CHECK(f.world->gameStates().get("TotalCustomers") == "2");
  CHECK(f.shop->sprites().objects().size() > 0);
  // a leaves angry (nothing served: no money), its counter is free, and b comes for it
  auto a = customerNamed(f, "a");
  a->setPatience(0.0f);
  a->updateMood();
  f.run_ms(700);
  CHECK(names(f) == std::vector<std::string>{"d", "b"});
  CHECK(f.shop->station("station01")->customer() == customerNamed(f, "b"));
  CHECK(f.world->gameStates().get("TotalCustomers") == "3");
  CHECK(f.world->gameStates().get("TotalCustomersLost") == "1");
  CHECK(f.shop->pendingCustomers() == 1);
  // the angry one's sprite left the scene with its station
  CHECK(a->parent() == nullptr);
  CHECK(a->station() == nullptr);
  // c's time comes at 5 s but both counters are taken; after the 10 s of the level no one comes any more
  f.run_ms(5000);
  CHECK(f.shop->pendingCustomers() == 1);
  customerNamed(f, "d")->setPatience(0.0f);
  customerNamed(f, "d")->updateMood();
  f.run_ms(700);
  CHECK(f.shop->pendingCustomers() == 0);  // c took the free counter once its time had come (the level has run about 6 s)
  CHECK(names(f) == std::vector<std::string>{"b", "c"});
}

TEST_CASE("ShopWindow::UpdateCustomers: once the level's time is up nobody else arrives, and a level ends when the last customer has gone") {
  Fx f;
  f.build(windowStyle(false, false, true));
  f.world->values().setValue("current_level", 5, true);
  f.run("LevelSetup()");
  f.run("ReadyForPrelevel()");
  f.run_ms(1300);
  REQUIRE(f.shop->activeCustomers() == 2);  // a and d; b waits behind a, c's time has not come
  // a and d are served nothing and leave; b comes at once, c at its time (5 s); then both go
  for (int round = 0; round < 3 && f.shop->activeCustomers() > 0; ++round) {
    for (const auto& c : f.shop->activeList()) {
      c->setPatience(0.0f);
      c->updateMood();
    }
    f.run_ms(700);
  }
  CHECK(f.shop->pendingCustomers() == 1);  // c is still waiting for its time
  f.run_ms(6000);  // past 5 s of level time, past the first few seconds: c arrived
  CHECK(f.shop->pendingCustomers() == 0);
  for (const auto& c : f.shop->activeList()) {
    c->setPatience(0.0f);
    c->updateMood();
  }
  f.run_ms(700);
  CHECK(f.shop->activeCustomers() == 0);
  // every customer has come and gone: the end of the level is noticed
  f.run_ms(100);
  CHECK(f.shop->tickState().levelOverStamp > 0);
}

TEST_CASE("ShopWindow::OrderComplete: sale and tip with their modifiers, the whole dollars shown, the customer's patience in the tip") {
  Fx f;
  f.build(windowStyle(false, false, false));
  startLevel(f, 1);
  priceValues(f);
  f.world->values().setValue("salemodifier", 1.2, false);
  f.world->values().setValue("tipmodifier", 1.5, false);
  auto c = customerNamed(f, "tut_cust_1");
  REQUIRE(c);
  c->setPatience(50.0f);  // the fixture's professor has no `tip`: 100
  f.shop->orderComplete(30.0f, 128, 243, c.get());
  // sale 1.2 * 30 = 36, tip 1.5 * (50 / 100 * 100) = 75
  CHECK(f.world->values().getReal("current_level_total", 0) == doctest::Approx(111.0));
  const auto texts = floating(f);
  REQUIRE(texts.size() == 2);  // the payment, and the goal's banner (111 >= 100)
  CHECK(texts[0]->kind() == FloatingText::Kind::Payment);
  CHECK(texts[0]->markup() == "<outline color='000000' size='2'><b>$111</b></outline>");
  CHECK(texts[0]->x() == 128);
  CHECK(texts[0]->y() == 243);
  CHECK(texts[1]->kind() == FloatingText::Kind::Goal);
  CHECK(texts[1]->markup().find("Goal reached!") != std::string::npos);
  CHECK(f.shop->goalPopupState() == 1);
  // no customer, no tip; the amount is not rounded in the total, only in the text
  f.shop->orderComplete(7.5f, 10, 20, nullptr);
  // (the second payment in a row is also the first bonus of the payment chain: 3 * 1)
  CHECK(f.world->values().getReal("current_level_total", 0) == doctest::Approx(111.0 + 9.0 + 3.0));
  CHECK(floating(f)[2]->kind() == FloatingText::Kind::Payment);
  CHECK(floating(f)[2]->markup().find("$09<") != std::string::npos);
  CHECK(floating(f)[3]->kind() == FloatingText::Kind::Combo);
  CHECK(f.shop->comboChain().lastType == kComboPayment);
  CHECK(f.shop->comboChain().counter == 1);
}

TEST_CASE("ShopWindow::AddCombo: the first event of a kind starts the chain, each next gives the bonus times the chain, another kind restarts it") {
  Fx f;
  f.build();
  startLevel(f, 1);
  priceValues(f);
  f.world->values().setValue("service_combobonus", 4, false);
  f.shop->addCombo(kComboMenu);
  CHECK(f.world->values().getReal("current_level_total", 0) == 0);
  CHECK(floating(f).empty());
  f.shop->addCombo(kComboMenu);  // the first bonus: 3 * 1, "x2"
  CHECK(f.world->values().getReal("current_level_total", 0) == doctest::Approx(3.0));
  REQUIRE(floating(f).size() == 1);
  CHECK(floating(f)[0]->kind() == FloatingText::Kind::Combo);
  CHECK(floating(f)[0]->markup() == "<outline color='000000' size='2'><b>Menu x2\n+$3</b></outline>");
  // at the avatar's place, 75 up
  const auto pos = f.shop->playerSprite()->position();
  CHECK(floating(f)[0]->x() == static_cast<int>(pos.x));
  CHECK(floating(f)[0]->y() == static_cast<int>(pos.y) - 75);
  f.shop->addCombo(kComboMenu);  // 3 * 2, "x3"
  CHECK(f.world->values().getReal("current_level_total", 0) == doctest::Approx(9.0));
  CHECK(floating(f)[1]->markup().find("Menu x3") != std::string::npos);
  CHECK(floating(f)[1]->markup().find("+$6") != std::string::npos);
  CHECK(f.world->gameStates().get("MenuDeliveryChainPointsEarned") == "9");
  CHECK(f.world->values().getInt("menu_multiplier_this_level", 0) == 3);
  // another kind starts over; its bonus is its own value
  f.shop->addCombo(kComboService);
  CHECK(f.world->values().getReal("current_level_total", 0) == doctest::Approx(9.0));
  CHECK(f.shop->comboChain().counter == 0);
  f.shop->addCombo(kComboService);
  CHECK(f.world->values().getReal("current_level_total", 0) == doctest::Approx(13.0));
  CHECK(f.world->gameStates().get("ServiceChainPointsEarned") == "4");
  CHECK(f.world->values().getInt("service_multiplier_this_level", 0) == 2);
  // a level's start resets the chain
  startLevel(f, 1);
  CHECK(f.shop->comboChain().lastType == 0);
  CHECK(f.shop->comboChain().counter == 0);
  CHECK(f.shop->goalPopupState() == 0);
}

TEST_CASE("ShopWindow: the goal and the mastery banners come once each, the mastery first when both are passed together; endless has none") {
  Fx f;
  f.build();
  startLevel(f, 1);  // goal 100, mastery 160
  priceValues(f);
  f.shop->orderComplete(50.0f, 0, 0, nullptr);
  CHECK(f.shop->goalPopupState() == 0);
  f.shop->orderComplete(60.0f, 0, 0, nullptr);  // 110: the goal
  CHECK(f.shop->goalPopupState() == 1);
  CHECK(f.world->values().getInt("mastery_this_level", 0) == 0);
  f.shop->orderComplete(10.0f, 0, 0, nullptr);
  CHECK(f.shop->goalPopupState() == 1);
  std::size_t banners = 0;
  for (const auto* t : floating(f)) banners += t->kind() == FloatingText::Kind::Goal;
  CHECK(banners == 1);
  f.shop->orderComplete(100.0f, 0, 0, nullptr);  // 220: the mastery, the goal's banner is not shown again
  CHECK(f.shop->goalPopupState() == 2);
  CHECK(f.world->values().getInt("mastery_this_level", 0) == 1);
  banners = 0;
  for (const auto* t : floating(f)) banners += t->kind() == FloatingText::Kind::Goal;
  CHECK(banners == 2);
  CHECK(floating(f).back()->kind() != FloatingText::Kind::Goal);  // (the payment's text and the chain come after the banner)
  // both passed in one payment: the mastery alone
  startLevel(f, 1);
  priceValues(f);
  f.layer->clearText();
  f.shop->orderComplete(500.0f, 0, 0, nullptr);
  CHECK(f.shop->goalPopupState() == 2);
  banners = 0;
  for (const auto* t : floating(f))
    if (t->kind() == FloatingText::Kind::Goal) {
      ++banners;
      CHECK(t->markup().find("Mastery!") != std::string::npos);
    }
  CHECK(banners == 1);
  // an endless run: the score is the cash, the counter too, no banners
  f.world->settings().mode = 1;
  startLevel(f, 1);
  priceValues(f);
  f.layer->clearText();
  f.shop->orderComplete(300.0f, 0, 0, nullptr);
  CHECK(f.world->values().getReal("current_level_total", 0) == doctest::Approx(300.0));
  CHECK(f.world->values().getReal("current_worth", 0) == doctest::Approx(300.0));
  CHECK(f.world->values().getReal("endless_level_counter", 0) == doctest::Approx(300.0));
  CHECK(f.shop->goalPopupState() == 0);
  CHECK(floating(f).size() == 1);
}

TEST_CASE("ShopWindow: a customer served and paid for in the shop adds its sale and tip to the level, once the money is collected") {
  Fx f;
  f.build();
  startLevel(f, 1);
  priceValues(f);
  auto c = customerNamed(f, "tut_cust_1");
  REQUIRE(c);
  visit(f, "station01");  // the menu
  f.run_ms(5100);
  REQUIRE(c->state() == kCustomerOrdering);
  auto bottle = VCarriable::create(f.ctx, 1);
  bottle->setType("sauce");
  REQUIRE(f.shop->playerSprite()->attachObject(bottle));
  visit(f, "station01");  // the sauce
  REQUIRE(c->state() == kCustomerMoney);
  CHECK(f.world->values().getReal("current_level_total", 0) == 0);  // nothing is paid before the money is picked up
  CHECK(f.world->values().getInt("served_this_level", 0) == 1);
  const float patience = c->patience();
  CHECK(patience > 100.0f);  // the +20 for the order (the customer had been served within seconds)
  f.run_ms(600);
  CHECK(c->visible() == false);
  visit(f, "station01");  // the money
  CHECK(c->state() == kCustomerDone);
  // 15 for the sauce and the tip: patience % of the customer's 100
  CHECK(f.world->values().getReal("current_level_total", 0) == doctest::Approx(15.0 + patience).epsilon(0.001));
  f.run_ms(800);
  CHECK(c->parent() == nullptr);  // gone from the scene
  CHECK(f.shop->station("station01")->customer() == nullptr);
}

TEST_CASE("ShopWindow::FindOrderWithLayer: the first counter whose customer wants a chocolate that the layer would head toward decides") {
  Fx f;
  f.build(windowStyle(false, true, true));
  startLevel(f, 6);
  priceValues(f);
  REQUIRE(f.shop->activeCustomers() == 2);
  auto a = customerNamed(f, "a");
  auto b = customerNamed(f, "b");
  REQUIRE((a && b));
  // before they order nothing is wanted: the first chocolate is the answer (the second when there is none)
  auto vendor = f.shop->vendor();
  auto bucket = vendor->getEmptyChocolate(0, false);
  auto bar = vendor->getEmptyChocolate(0, false);
  bar->addLayer(kShapeBar, kLayerChocolate, 0);  // shaping takes the bucket off: slot 0 is empty
  CHECK(f.shop->findOrderWithLayer(bucket, bar, kLayerIngredient, 0) == bucket);
  CHECK(f.shop->findOrderWithLayer(nullptr, bar, kLayerIngredient, 0) == bar);
  CHECK(f.shop->findOrderWithLayer(nullptr, nullptr, kLayerIngredient, 0) == nullptr);
  visit(f, "station01");
  visit(f, "station02");
  f.run_ms(5100);
  REQUIRE(a->wanted());
  REQUIRE(b->wanted());
  // a wants a filling 0 (station01), b a filling 1 (station02): the bar fits a layer-1 order (no bucket under it), the bucket does not
  CHECK(f.shop->findOrderWithLayer(bar, bucket, kLayerIngredient, 0) == bar);
  CHECK(f.shop->findOrderWithLayer(bucket, bar, kLayerIngredient, 0) == bar);
  CHECK(f.shop->findOrderWithLayer(bucket, bar, kLayerIngredient, 1) == bar);  // station02's customer
  // nobody wants filling 2: the fallback is the first chocolate
  CHECK(f.shop->findOrderWithLayer(bucket, bar, kLayerIngredient, 2) == bucket);
  CHECK(f.shop->findOrderWithLayer(nullptr, bar, kLayerIngredient, 2) == bar);
  // the lookup reads what is wanted, not the state; a counter that lost its customer is skipped
  b->setState(kCustomerDone);
  CHECK(f.shop->findOrderWithLayer(bucket, bar, kLayerIngredient, 1) == bar);
  f.shop->station("station02")->detachCustomer();
  CHECK(f.shop->findOrderWithLayer(bucket, bar, kLayerIngredient, 1) == bucket);
}

TEST_CASE("ShopWindow: a level loaded again while customers are in the shop starts clean, and the window can be destroyed with them in it") {
  Fx f;
  f.build(windowStyle(false, false, true));
  startLevel(f, 5);
  priceValues(f);
  REQUIRE(f.shop->activeCustomers() == 2);
  visit(f, "station01");
  f.run_ms(5100);
  const std::size_t sprites = f.shop->sprites().objects().size();
  CHECK(sprites > 5);
  f.run("LevelSetup()");
  CHECK(f.shop->activeCustomers() == 0);
  CHECK(f.shop->pendingCustomers() == 4);
  CHECK(f.world->gameStates().get("TotalCustomers") == "0");
  CHECK(f.world->values().getReal("current_level_total", 0) == 0);
  CHECK(f.shop->comboChain().lastType == 0);
  CHECK(f.shop->station("station01")->customer() == nullptr);
  CHECK(f.shop->sprites().objects().size() < sprites);  // the customers left the scene with the reload
  f.run("ReadyForPrelevel()");
  f.run_ms(1300);
  CHECK(f.shop->activeCustomers() == 2);
  // the modal goes with customers, bubbles and money in it
  f.wm.popModal("m");
  f.wm.clearDestroyed();
  CHECK_FALSE(f.world->settings().gameLoaded);
}

// ================================================================ slice 16: the upgrade manager at a level load

TEST_CASE("ShopWindow: a level load gives the manager the location, the level and the player's purchases; a purchase re-initialises what it is bound to and lasts") {
  Fx f;
  f.build(windowStyle(false, true));
  f.world->values().setValue("current_level", 4, true);
  f.run("LevelSetup()");
  UpgradeManager& um = f.world->upgrades();
  CHECK(um.location() == "iquitos");
  CHECK(um.currentLevel() == 4);
  CHECK_FALSE(um.noSave());
  CHECK(um.isActive("kMixer2", "station"));                            // enabled at 4
  CHECK(f.world->progress().get("upgrade.iquitos.kmixer2") == "1");   // the grant by level is saved
  CHECK(um.getUpgrade("kBackground1", "aesthetic")->state == kUpgradeUnlocked);  // minlevel 4
  CHECK(um.getUpgrade("kStation2", "aesthetic")->state == kUpgradeLocked);       // requires kStation1
  // the sprites of the bound objects were found
  CHECK(um.getUpgrade("kStation1", "aesthetic")->bound.at("station01").resolved);
  CHECK(um.getUpgrade("kBackground1", "aesthetic")->bound.at("background_object").resolved);
  CHECK(um.getUpgrade("kStatic1", "aesthetic")->bound.at("background_static_1").resolved);
  VStation* s1 = f.shop->station("station01");
  VStation* s2 = f.shop->station("station02");
  auto bg = f.shop->background();
  REQUIRE(s1);
  REQUIRE(s2);
  REQUIRE(bg);
  CHECK(s1->currentState() == "default");
  CHECK(bg->currentState() == "default");
  CHECK_FALSE(bg->statics().at("static_1")->visible());
  // buying the tabletop: the stations it is bound to take the art (VStation::Init), the second one the better art
  um.enableUpgrade("kStation1", "aesthetic", true);
  CHECK(s1->currentState() == "upgrade1");
  CHECK(s2->currentState() == "upgrade1");
  CHECK(bg->currentState() == "default");
  um.unlockUpgrade("kStation2", "aesthetic");
  um.enableUpgrade("kStation2", "aesthetic", true);
  CHECK(s1->currentState() == "upgrade2");
  // the floor: the backdrop (VBackground::Init)
  um.enableUpgrade("kBackground1", "aesthetic", true);
  CHECK(bg->currentState() == "upgrade1");
  // a decoration is visible only while its upgrade is on, and the object it is bound to is re-initialised to show it
  um.enableTempUpgrade("kStatic1", "aesthetic");
  CHECK(bg->statics().at("static_1")->visible());
  um.disableTempUpgrades("", "");
  CHECK_FALSE(bg->statics().at("static_1")->visible());
  // the next load reads the purchases back
  f.run("LevelSetup()");
  CHECK(f.shop->station("station01")->currentState() == "upgrade2");
  CHECK(f.shop->background()->currentState() == "upgrade1");
  CHECK(f.world->upgrades().isActive("kStation2", "aesthetic"));
  // another location starts with none of them (the next level is Grytviken's, which has no upgrades.xml in this fixture)
  f.world->values().setValue("current_level", 11, true);
  f.run("LevelSetup()");
  CHECK(f.world->upgrades().location() == "grytviken");
  CHECK(f.world->upgrades().size() == 0);
  CHECK(f.log.contains("locations/grytviken/upgrades.xml is missing"));
}

TEST_CASE("ShopWindow: an endless load saves nothing and reads nothing, UpgradeComplete sets the plot flag, takes the new upgrades into the vendor and counts the machines again") {
  Fx f;
  f.build(windowStyle(false, true));
  f.world->values().setValue("current_level", 1, true);
  f.world->progress().setInt("upgrade.iquitos.kpress1", 1);
  f.world->settings().setMode(1);
  f.run("LevelSetup()");
  CHECK(f.world->upgrades().noSave());
  CHECK(f.world->progress().get("upgrade.iquitos.kmixer1") == "");  // nothing is written ...
  CHECK(f.world->upgrades().isActive("kMixer1", "station"));         // ... though the level grants
  f.world->settings().setMode(0);
  f.world->values().setValue("current_level", 1, true);
  f.run("LevelSetup()");
  CHECK_FALSE(f.world->upgrades().noSave());
  // UpgradeComplete
  REQUIRE(f.shop->vendor());
  f.shop->tickState().plotPending = false;
  CHECK_FALSE(f.shop->vendor()->pools().options(true, 0, kLayerChocolate).count(1));
  f.world->upgrades().enableUpgrade("kMixer2", "station", false);
  f.world->tutorials().setLevelWindow(0);
  f.shop->upgradeComplete();
  CHECK(f.shop->tickState().plotPending);  // ShopWindow+0x1dd: the diary may follow the shop
  CHECK(f.shop->vendor()->pools().options(true, 0, kLayerChocolate).count(1));  // UpdateCapability(true)
  CHECK(f.world->tutorials().levelWindow() == f.shop->id());
  // each station counts its machines again: a machine whose upgrade came on makes its station a mouse target
  VStation* st = f.shop->station("station02");
  REQUIRE(st);
  CHECK(st->mouse());
}
