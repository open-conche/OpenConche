// The Great Chocolate Chase, slice 10 (docs/design/greatcc.md section 6): the zone graph, the characters and the player. Synthetic
// fixtures only: a zone file and a player table built here, a fake shop that answers the player's questions.
#include <doctest/doctest.h>

#include <cmath>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "games/Registry.h"
#include "games/greatcc/GreatCCWorld.h"
#include "games/greatcc/core/ZoneGraph.h"
#include "games/greatcc/scene/SpriteManager.h"
#include "games/greatcc/shop/ShopHost.h"
#include "games/greatcc/shop/VCarriable.h"
#include "games/greatcc/shop/VPlayer.h"
#include "games/greatcc/shop/VStation.h"
#include "ScriptHarness.h"
#include "ui/Style.h"

using namespace choco;
using namespace choco::games::greatcc;
namespace fs = std::filesystem;

namespace {

using MS = ui::MapStyle;
using Tbl = std::shared_ptr<MS>;
Tbl tbl() { return std::make_shared<MS>(); }

std::vector<std::string> names(const std::list<ZonePoint*>& l) {
  std::vector<std::string> out;
  for (const ZonePoint* p : l) out.push_back(p->name);
  return out;
}
using Names = std::vector<std::string>;

}  // namespace

// ================================================================ the zone graph

TEST_CASE("ZoneGraph: zones, points and connections are read in file order and listed newest first") {
  ZoneGraph g;
  REQUIRE(g.load(
      "<ZoneController>"
      "<zone name='a'>"
      "<point name='p1' x='0' y='0' isentrance='1' isexit='0'/><point name='p2' x='3' y='4' isexit='1'/>"
      "<point name='p3' x='6' y='8' isentrance='01' isexit='x'/>"
      "<connection first='p1' second='p2'/><connection first='p2' second='p3'/><connection first='p9' second='p1'/>"
      "<connection first='p3' second='p1'/>"
      "</zone><zone name='b'><point name='q' x='1' y='1'/></zone>"
      "<crosszone><connection firstzone='a' firstnode='p3' secondzone='b' secondnode='q'/>"
      "<connection firstzone='nope' firstnode='p3' secondzone='b' secondnode='q'/></crosszone>"
      "</ZoneController>"));
  REQUIRE(g.zones().size() == 2);
  CHECK(g.zones().front()->name == "b");  // a new zone goes to the front
  Zone* a = g.findZone("a");
  REQUIRE(a);
  Names order;
  for (const auto& p : a->points) order.push_back(p->name);
  CHECK(order == Names{"p3", "p2", "p1"});
  // entrance and exit: only the text `1` counts (to_int of the text), and the lists are newest first as well
  CHECK(names(a->entrances) == Names{"p3", "p1"});  // `01` is 1
  CHECK(names(a->exits) == Names{"p2"});
  ZonePoint* p2 = g.findPoint("a", "p2");
  REQUIRE(p2);
  CHECK(p2->x == 3);
  CHECK(p2->y == 4);
  // p2: connected to p1 first, then to p3: the newest first; a two-way connection with the length of the pair
  REQUIRE(p2->connections.size() == 2);
  CHECK(p2->connections.front().target->name == "p3");
  CHECK(p2->connections.back().target->name == "p1");
  CHECK(p2->connections.back().length == doctest::Approx(5.0));
  CHECK_FALSE(p2->connections.front().temporary);
  // a connection to a point that does not exist is dropped; p3-p1 exists
  CHECK(g.findPoint("a", "p1")->connections.size() == 2);
  // the crosszone connection with a known zone is made, the one with an unknown zone is not
  CHECK(g.findPoint("b", "q")->connections.size() == 1);
  CHECK(g.findPoint("b", "none") == nullptr);
  CHECK(g.findPoint("zzz", "p1") == nullptr);
}

TEST_CASE("ZoneGraph: reading a zone again keeps the points, moves them, and drops the connections the zone had") {
  ZoneGraph g;
  g.load("<zonecontroller><zone name='a'><point name='p1' x='0' y='0' isentrance='1'/><point name='p2' x='3' y='4'/>"
         "<connection first='p1' second='p2'/></zone></zonecontroller>");
  ZonePoint* p1 = g.findPoint("a", "p1");
  g.load("<zonecontroller><zone name='a'><point name='p1' x='7' y='9'/></zone></zonecontroller>");
  CHECK(g.zones().size() == 1);
  CHECK(g.findPoint("a", "p1") == p1);  // the same point
  CHECK(p1->x == 7);
  CHECK(p1->connections.empty());
  CHECK(g.findZone("a")->entrances.empty());  // no longer an entrance
  CHECK(g.findZone("a")->points.size() == 2);
  CHECK_FALSE(g.load("<zonecontroller><zone"));  // not XML: refused
}

namespace {
std::string diamond(bool aFirst) {
  // S(0,0) to A(3,4) is 5 and to B(4,3) is 5; A and B reach T(6,8) by 5 and by sqrt(29)
  std::string s = "<zonecontroller><zone name='z'><point name='S' x='0' y='0'/><point name='A' x='3' y='4'/><point name='B' x='4' y='3'/>"
                  "<point name='T' x='6' y='8'/>";
  s += aFirst ? "<connection first='S' second='A'/><connection first='S' second='B'/>"
              : "<connection first='S' second='B'/><connection first='S' second='A'/>";
  s += "<connection first='A' second='T'/><connection first='B' second='T'/></zone></zonecontroller>";
  return s;
}
}  // namespace

TEST_CASE("ZoneGraph::addPath: the route ends with the destination twice, and a standing start gives the point twice") {
  ZoneGraph g;
  g.load("<zonecontroller><zone name='a'><point name='p1' x='0' y='0'/><point name='p2' x='3' y='4'/><point name='p3' x='6' y='8'/>"
         "<connection first='p1' second='p2'/><connection first='p2' second='p3'/></zone></zonecontroller>");
  ZonePoint *p1 = g.findPoint("a", "p1"), *p3 = g.findPoint("a", "p3");
  g.addPath(p1, p3);
  CHECK(names(g.path()) == Names{"p1", "p2", "p3", "p3"});
  CHECK(g.nextPoint() == p1);
  CHECK(g.nextPoint()->name == "p2");
  CHECK_FALSE(g.pathEmpty());
  CHECK(g.existsInPath(p3));
  g.nextPoint();
  g.nextPoint();
  CHECK(g.pathEmpty());
  CHECK(g.nextPoint() == nullptr);
  g.addPath(p1, p1);
  CHECK(names(g.path()) == Names{"p1", "p1"});
  // a new search replaces the route
  g.addPath(p3, p1);
  CHECK(names(g.path()) == Names{"p3", "p2", "p1", "p1"});
}

TEST_CASE("ZoneGraph::addPath: the stored cost is the edge length truncated, the comparison is exact, and equal costs go to the last node found") {
  {
    ZoneGraph g;
    g.load(diamond(true));
    g.addPath(g.findPoint("z", "S"), g.findPoint("z", "T"));
    // S-A is 5 and S-B is 5; connecting A before B lists B first, so B is found first and A is found last: A is expanded first
    CHECK(names(g.path()) == Names{"S", "A", "T", "T"});
    CHECK(g.findPoint("z", "A")->cost == 5);
    CHECK(g.findPoint("z", "T")->cost == 10);  // 5 + (int)5 via A; via B it would be 5 + 5 as well: not better
  }
  {
    ZoneGraph g;
    g.load(diamond(false));
    g.addPath(g.findPoint("z", "S"), g.findPoint("z", "T"));
    CHECK(names(g.path()) == Names{"S", "B", "T", "T"});
  }
  {
    // S-B 4.24 is stored as 4, B-T 5.83 added exactly: T costs 9 through B although the exact length is longer than through A (10)
    ZoneGraph g;
    g.load("<zonecontroller><zone name='z'><point name='S' x='0' y='0'/><point name='A' x='3' y='4'/><point name='B' x='3' y='3'/>"
           "<point name='T' x='6' y='8'/><connection first='S' second='A'/><connection first='S' second='B'/>"
           "<connection first='A' second='T'/><connection first='B' second='T'/></zone></zonecontroller>");
    g.addPath(g.findPoint("z", "S"), g.findPoint("z", "T"));
    CHECK(g.findPoint("z", "B")->cost == 4);
    CHECK(g.findPoint("z", "T")->cost == 9);
    CHECK(names(g.path()) == Names{"S", "B", "T", "T"});
  }
}

TEST_CASE("ZoneGraph::addPath: a disabled point is ignored, an unreachable destination gives the destination twice") {
  ZoneGraph g;
  g.load("<zonecontroller><zone name='a'><point name='S' x='0' y='0'/><point name='M' x='3' y='4'/><point name='T' x='6' y='8'/>"
         "<point name='X' x='9' y='9'/><connection first='S' second='M'/><connection first='M' second='T'/></zone></zonecontroller>");
  ZonePoint *S = g.findPoint("a", "S"), *T = g.findPoint("a", "T"), *X = g.findPoint("a", "X");
  g.disablePoint("a", "M", true);
  g.addPath(S, T);
  CHECK(names(g.path()) == Names{"T", "T"});  // the character walks straight to it
  g.disablePoint("a", "M", false);
  g.addPath(S, T);
  CHECK(names(g.path()) == Names{"S", "M", "T", "T"});
  g.addPath(S, X);  // no connections at all
  CHECK(names(g.path()) == Names{"X", "X"});
  g.disablePoint("a", "nope", true);  // unknown: nothing happens
  g.addPath(S, nullptr);
  CHECK(g.pathEmpty());
  g.addPath(nullptr, T);  // no start: taken as unreachable
  CHECK(names(g.path()) == Names{"T", "T"});
}

TEST_CASE("ZoneGraph::addPath: points of two zones are welded exit to entrance for one search and unwelded after") {
  ZoneGraph g;
  g.load("<zonecontroller>"
         "<zone name='one'><point name='a' x='0' y='0' isexit='1'/><point name='b' x='0' y='10'/><connection first='a' second='b'/></zone>"
         "<zone name='two'><point name='c' x='10' y='0' isentrance='1'/><point name='d' x='10' y='10'/>"
         "<connection first='c' second='d'/></zone></zonecontroller>");
  ZonePoint *b = g.findPoint("one", "b"), *d = g.findPoint("two", "d");
  g.addPath(b, d);
  CHECK(names(g.path()) == Names{"b", "a", "c", "d", "d"});
  for (const char* z : {"one", "two"})
    for (const auto& p : g.findZone(z)->points)
      for (const ZoneConnection& c : p->connections) CHECK_FALSE(c.temporary);
  CHECK(g.findPoint("one", "a")->connections.size() == 1);
  // nothing connects them any more
  g.addPath(b, d);
  CHECK(names(g.path()) == Names{"b", "a", "c", "d", "d"});
  CHECK(g.findPoint("two", "c")->connections.size() == 1);
}

// ================================================================ characters

namespace {

constexpr const char* kZones =
    "<zonecontroller><zone name='avatar'>"
    "<point name='s1' x='100' y='100'/><point name='s2' x='200' y='100'/><point name='s3' x='200' y='200'/><point name='s4' x='100' y='200'/>"
    "<connection first='s1' second='s2'/><connection first='s2' second='s3'/><connection first='s3' second='s4'/>"
    "</zone><zone name='customers'><point name='customer01' x='5' y='6'/></zone></zonecontroller>";

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
// The three walk assets of a body part, each with a default state and (with `carry`) a carry state.
Tbl walkAssets(bool carry) {
  auto assets = tbl();
  for (const char* n : {"walkfront", "walkright", "walkback"}) {
    std::vector<Tbl> states{entry("default", "p")};
    if (carry) states.push_back(entry("carry", "p", 1));
    assets->set(n, assetGroup(states));
  }
  return assets;
}

// iquitosPlayer in the parts the player reads.
Tbl playerTable() {
  auto t = tbl();
  t->set("baseDir", "anim/").set("name", "avatar").set("mouse", 0.0).set("x", 400.0).set("y", 300.0).set("spriteLayer", 3.0);
  t->set("baseY", 100.0).set("rangeY", 100.0).set("scaleRange", 0.1).set("startZone", "avatar").set("startPoint", "s1");
  t->set("assets", walkAssets(false));
  auto children = tbl();
  children->set("mouse", 0.0);
  for (const char* n : {"left_arm", "left_arm_upper", "right_arm", "right_arm_upper"}) {
    auto c = tbl();
    c->set("assets", walkAssets(true));
    children->set(n, c);
  }
  auto torso = tbl();
  torso->set("assets", walkAssets(false));
  children->set("torso", torso);
  for (const char* n : {"idle_1", "idle_2"}) {
    auto c = tbl();
    auto assets = tbl();
    assets->set("base", assetGroup({entry("default", "p")}));
    c->set("assets", assets);
    children->set(n, c);
  }
  t->set("children", children);
  return t;
}

// The shop as the player sees it.
struct FakeHost : ShopHost {
  std::shared_ptr<VPlayer> who;
  bool center = false, tutorial = false;
  std::vector<std::shared_ptr<VStation>> stations;
  std::shared_ptr<VPlayer> player() const override { return who; }
  bool hasCenterTable() const override { return center; }
  std::shared_ptr<VStation> stationByZonePoint(const ZonePoint* p) const override {
    for (const auto& s : stations)
      if (s->hasPoint(p)) return s;
    return nullptr;
  }
  bool tutorialInEffect() const override { return tutorial; }
};

// A station that notes its arrivals.
struct SpyStation : VStation {
  using VStation::VStation;
  std::vector<std::string>* log = nullptr;
  std::function<void()> extra;
  void onArrival(VPlayer& pl, const ZonePoint* p) override {
    if (log) log->push_back(p ? p->name : "-");
    VStation::onArrival(pl, p);
    if (extra) extra();
  }
};

// A player that shows its heading to the test.
struct TestPlayer : VPlayer {
  using VPlayer::VPlayer;
  void setHeading(float h, float prev) {
    heading_ = h;
    previousHeading_ = prev;
  }
};

struct Fx : test::ScriptEnv {
  Fx() : ScriptEnv(scratchAssets(), scratch()) {
    ctx.module = games::findModule("greatcc");
    ctx.module->attach(ctx);
    wm.pushModal("m");
    world = worldOf(ctx);
    REQUIRE(world);
    world->values().load(
        "<values><value name='walkspeed' value='4.0'/><value name='player_idle_cooldown' value='10000'/>"
        "<value name='player_idle_first_cooldown' value='3000'/><value name='player_turnaround_cooldown' value='500'/>"
        "<value name='player_turn_speed' value='0.4'/></values>");
    graph = std::make_shared<ZoneGraph>();
    REQUIRE(graph->load(kZones));
    mgr.setNow(now);
  }
  static std::string scratchAssets() {
    const fs::path p = fs::temp_directory_path() / "choco_greatcc_player_assets";
    fs::create_directories(p);
    return p.string();
  }
  static std::string scratch() {
    const fs::path p = fs::temp_directory_path() / "choco_greatcc_player_user";
    std::error_code ec;
    fs::remove_all(p, ec);
    fs::create_directories(p, ec);
    return p.string();
  }
  ~Fx() {
    mgr.removeAllObjects();
  }

  // The avatar at s1, loaded and initialised like ShopWindow::LoadCharacters does, then added.
  std::shared_ptr<TestPlayer> makePlayer(bool center = false) {
    auto p = std::make_shared<TestPlayer>(ctx, graph);
    host.who = p;
    host.center = center;
    p->setHost(&host);
    auto table = playerTable();
    const StyleChain chain(*table, nullptr);
    p->loadFromTable(chain);
    p->init();
    mgr.addObject(p);
    step(0);  // the first update seeds the sprite's clock
    return p;
  }
  std::shared_ptr<SpyStation> makeStation(int id, const std::vector<std::pair<std::string, int>>& points) {
    auto s = std::make_shared<SpyStation>(ctx, id, kStationEmpty);
    s->log = &arrivals;
    auto t = tbl();
    t->set("stationID", static_cast<double>(id)).set("stationType", 0.0);
    auto list = tbl();
    for (const auto& [n, f] : points) list->append(n).append(static_cast<double>(f));
    t->set("playerPoint", list);
    const StyleChain chain(*t, nullptr);
    s->loadFromTable(chain);
    s->resolvePoints(*graph);
    s->setHost(&host);
    host.stations.push_back(s);
    return s;
  }
  // One update, `ms` later.
  void step(double ms) {
    now += ms;
    mgr.setNow(now);
    mgr.updateObjects();
  }
  void steps(int n, double ms = 16) {
    for (int i = 0; i < n; ++i) step(ms);
  }
  ZonePoint* pt(const char* name) { return graph->findPoint("avatar", name); }

  GreatCCWorld* world = nullptr;
  std::shared_ptr<ZoneGraph> graph;
  FakeHost host;
  SpriteManager mgr{ctx};
  std::vector<std::string> arrivals;
  double now = 1000;
};

bool near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) < eps; }

}  // namespace

TEST_CASE("VPlayer: it loads from the table, stands on its start point, faces front and is not a mouse target") {
  Fx f;
  auto p = f.makePlayer();
  CHECK(p->type() == "player_character");
  CHECK_FALSE(p->mouse());
  CHECK(p->name() == "avatar");
  CHECK(p->depth() == 3);
  CHECK(p->baseY() == 100);
  CHECK(p->rangeY() == 100);
  CHECK(p->scaleRange() == doctest::Approx(0.1f));
  CHECK(p->walkSpeed() == doctest::Approx(4.0f));
  CHECK(p->startPoint() == f.pt("s1"));
  CHECK(p->currentNode() == f.pt("s1"));
  CHECK(p->destination() == f.pt("s1"));
  CHECK(p->position().x == 100);
  CHECK(p->position().y == 100);
  CHECK(p->facing() == 2);
  CHECK(p->currentAsset() == "walkfront");
  CHECK_FALSE(p->moving());
  CHECK(p->delay() == 30);
  CHECK(p->childObject("left_arm")->delay() == 30);
  // the idle children are collected in key order and hidden
  REQUIRE(p->idles().size() == 2);
  CHECK(p->idles()[0].get() == p->childObject("idle_1"));
  CHECK_FALSE(p->childObject("idle_1")->visible());
  CHECK(p->childObject("torso")->visible());
  CHECK(p->childObject("torso")->currentAsset() == "walkfront");
  // standing, her scale is the plain 1 until she first moves
  CHECK(p->perspectiveScale() == 1);
  CHECK(p->numHandsFree() == 2);
}

TEST_CASE("VCharacter: clicks append to the move queue, which is never cleared and pops one point per trip") {
  Fx f;
  auto p = f.makePlayer();
  p->queueMovePoint(f.pt("s2"));
  p->queueMovePoint(f.pt("s3"));
  p->queueMovePoint(f.pt("s2"));  // no de-duplication outside a tutorial
  p->queueMovePoint(nullptr);     // null is ignored
  CHECK(p->queue().size() == 3);
  CHECK(p->isPathEmpty() == false);
  f.step(16);  // standing: the next update pops the front and starts the trip
  CHECK(p->queue().size() == 2);
  CHECK(p->moving());
  CHECK(p->destination() == f.pt("s2"));
  // a click while she walks only appends: the trip goes on
  p->queueMovePoint(f.pt("s4"));
  CHECK(p->queue().size() == 3);
  CHECK(p->destination() == f.pt("s2"));
  CHECK(p->moving());
}

TEST_CASE("VCharacter: a click queued while a tutorial runs is ignored when it repeats the destination or the queue's last") {
  Fx f;
  auto p = f.makePlayer();
  f.host.tutorial = true;
  p->queueMovePoint(f.pt("s2"));       // standing on s1 with the destination s1: a trip
  CHECK(p->queue().size() == 1);
  p->queueMovePoint(f.pt("s2"));       // the queue's last
  CHECK(p->queue().size() == 1);
  p->queueMovePoint(f.pt("s3"));
  CHECK(p->queue().size() == 2);
  f.step(16);
  f.step(16);
  CHECK(p->queue().size() == 1);       // s2 was popped and is her destination
  // not empty: only the last queued entry counts
  p->queueMovePoint(f.pt("s3"));
  CHECK(p->queue().size() == 1);
  // empty queue, on her way to s3 after the pop
  f.steps(40);
  while (p->moving() && p->destination() != f.pt("s3")) f.step(16);
  REQUIRE(p->queue().empty());
  p->queueMovePoint(p->destination());  // she is still on her way (or arrived): standing on it adds, walking to it does not
  CHECK(p->queue().size() == (p->currentNode() == p->destination() ? 1u : 0u));
}

TEST_CASE("VPlayer: a queued point she already stands on arrives at once, inside the update that pops it") {
  Fx f;
  auto p = f.makePlayer();
  auto st = f.makeStation(1, {{"s1", 1}});
  p->queueMovePoint(f.pt("s1"));
  CHECK(f.arrivals.empty());  // a click only appends
  f.step(16);
  CHECK(f.arrivals == std::vector<std::string>{"s1"});
  CHECK_FALSE(p->moving());
  CHECK(p->position().x == 100);
  CHECK(p->queue().empty());
  CHECK(p->nodeTrail().back() == "s1");
  // the same through the graph's route ([P, P]) with the centre table
  f.host.center = true;
  p->queueMovePoint(f.pt("s1"));
  f.step(16);
  CHECK(f.arrivals.size() == 2);
  CHECK_FALSE(p->moving());
}

TEST_CASE("VPlayer: queued trips chain, each arrival told to its station in order, and the facing comes from the last point's entry") {
  Fx f;
  auto p = f.makePlayer();
  f.makeStation(2, {{"s2", 3}});  // back
  f.makeStation(3, {{"s3", 0}});  // left
  p->queueMovePoint(f.pt("s2"));
  p->queueMovePoint(f.pt("s3"));
  p->queueMovePoint(f.pt("s1"));  // no station has s1: no arrival call, the trip still runs
  f.steps(200);
  CHECK(f.arrivals == std::vector<std::string>{"s2", "s3"});
  CHECK(p->position().x == 100);
  CHECK(p->position().y == 100);
  CHECK(p->queue().empty());
  CHECK_FALSE(p->moving());
  CHECK(p->currentNode() == f.pt("s1"));
  // straight lines (no centre table): s1 -> s2 -> s3 -> s1, no intermediate node
  CHECK(p->nodeTrail() == Names{"s2", "s3", "s1"});
}

TEST_CASE("VStation: a click queues the player point nearest to the player, the first of equals, and the arrival sets the facing when the queue is empty") {
  Fx f;
  auto p = f.makePlayer();
  auto st = f.makeStation(4, {{"s4", kFaceRight}, {"s2", kFaceBack}, {"s3", kFaceLeft}});
  // the resolved points are kept in the graph's order, not the table's
  REQUIRE(st->points().size() == 3);
  CHECK(st->points()[0] == f.pt("s2"));
  CHECK(st->facingAt("s4") == kFaceRight);
  CHECK(st->facingAt("s2") == kFaceBack);
  CHECK(st->facingAt("unknown") == kFaceLeft);  // the map's default
  CHECK(st->hasPoint(f.pt("s3")));
  CHECK_FALSE(st->hasPoint(f.pt("s1")));
  st->onClick();
  REQUIRE(p->queue().size() == 1);
  CHECK(p->queue().front() == f.pt("s2"));  // s2 is 100 from s1; s4 is 100 as well, but s2 comes first
  // a click does not move her or clear what is there
  st->onClick();
  CHECK(p->queue().size() == 2);
  for (int i = 0; i < 200 && f.arrivals.size() < 2; ++i) f.step(16);
  CHECK(f.arrivals.size() == 2);
  // the last arrival: the queue was empty, so she turns to the facing of the point (back); front to back is half a turn, which
  // goes round the way she was turning, through left
  CHECK(p->queue().empty());
  bool sawBack = false;
  for (int i = 0; i < 25; ++i) {
    f.step(16);
    sawBack = sawBack || p->facing() == kFaceBack;
  }
  CHECK(sawBack);
  // without a player or without points a click does nothing
  f.host.who.reset();
  st->onClick();
  CHECK(p->queue().empty());
}

TEST_CASE("VStation: names that are not points of the avatar zone are dropped, and a single name falls back to the station's own") {
  Fx f;
  auto st = f.makeStation(1, {{"s1", 1}, {"nowhere", 2}, {"s1", 3}});
  REQUIRE(st->points().size() == 1);
  CHECK(st->facingAt("s1") == 3);  // the later entry for the same point wins in the map
  auto one = std::make_shared<VStation>(f.ctx, 5, kStationEmpty);
  auto t = tbl();
  t->set("playerPoint", "s3").set("stationID", 5.0).set("stationType", 0.0);
  one->loadFromTable(StyleChain(*t, nullptr));
  one->resolvePoints(*f.graph);
  REQUIRE(one->points().size() == 1);
  CHECK(one->points()[0] == f.pt("s3"));
  CHECK(one->facingAt("s3") == kFaceLeft);  // a single name stores no facing
  auto t2 = tbl();
  t2->set("playerPoint", "missing").set("stationID", 6.0).set("stationType", 0.0);
  one->loadFromTable(StyleChain(*t2, nullptr));
  one->resolvePoints(*f.graph);
  CHECK(one->points().empty());  // the fallback is the station's own name ("station00"), which is not a point either
  // a counter finds its customer point in the customers zone
  auto counter = std::make_shared<VStation>(f.ctx, 1, kStationCounter);
  auto t3 = tbl();
  t3->set("stationID", 1.0).set("stationType", 1.0).set("custPoint", "customer01");
  counter->loadFromTable(StyleChain(*t3, nullptr));
  counter->resolvePoints(*f.graph);
  REQUIRE(counter->customerZonePoint());
  CHECK(counter->customerZonePoint()->name == "customer01");
}

TEST_CASE("VCharacter: a step is (speed - 0.25 |dy|) * ms / 10 pixels, the last one lands on the point, and arrival ends the walk") {
  Fx f;
  auto p = f.makePlayer();
  p->queueMovePoint(f.pt("s2"));
  f.step(16);  // the update that pops the point starts the trip; the first step comes with the next one
  CHECK(p->moving());
  CHECK(near(p->position().x, 100.0f));
  f.step(16);
  CHECK(near(p->position().x, 106.4f));  // 4.0 * 16 / 10
  CHECK(near(p->position().y, 100.0f));
  f.steps(14);
  CHECK(near(p->position().x, 100.0f + 6.4f * 15));
  CHECK(p->moving());
  f.step(16);  // 4 pixels left: the step is clamped to land exactly
  CHECK(p->position().x == 200.0f);
  CHECK_FALSE(p->moving());
  // a long frame is not a long step beyond the elapsed cap, and a 0 elapsed (the clamp at 3000 ms) moves nothing
  p->queueMovePoint(f.pt("s3"));
  f.step(16);
  const float y0 = p->position().y;
  CHECK(near(p->position().y, y0));
  f.step(3000);  // GetElapsed is 0 for a step this long
  CHECK(near(p->position().y, y0));
  f.step(100);  // 100 ms downward: (4 - 0.25) * 100 / 10 = 37.5
  CHECK(near(p->position().y, y0 + 37.5f));
}

TEST_CASE("VPlayer: the walk speed is read from the value every step, with its upgrade modifiers") {
  Fx f;
  auto p = f.makePlayer();
  f.world->values().setValue("walkspeed", 8.0f, false);
  p->queueMovePoint(f.pt("s2"));
  f.steps(2);
  CHECK(near(p->position().x, 100.0f + 8.0f * 16 / 10));
  CHECK(p->walkSpeed() == doctest::Approx(8.0f));
}

TEST_CASE("VPlayer: the scale follows the height on the screen, and only while she moves") {
  Fx f;
  auto p = f.makePlayer();
  CHECK(p->perspectiveScale() == 1);
  p->queueMovePoint(f.pt("s4"));  // straight down: 100 to 200
  f.steps(2);
  const float y = p->position().y;
  CHECK(y > 100);
  const float expect = (y - 100.0f) / 100.0f * 0.1f + 1.0f;
  CHECK(near(p->perspectiveScale(), expect));
  CHECK(near(p->scale().x, expect));
  CHECK(near(p->scale().y, expect));
  f.steps(40);
  CHECK(p->position().y == 200.0f);
  CHECK(near(p->perspectiveScale(), 1.1f));
}

namespace {
int facingAfterTurning(Fx& f, TestPlayer& p, float dx, float dy) {
  p.setDirectionVector({dx, dy});
  for (int i = 0; i < 30; ++i) p.updateFacing();
  (void)f;
  return p.facing();
}
}  // namespace

TEST_CASE("VCharacter::UpdateFacing: the heading maps to a facing at 60, 120, -45 and -135 degrees") {
  Fx f;
  auto p = f.makePlayer();
  struct Row { float dx, dy; int facing; };
  const Row rows[] = {
      {1, 0, kFaceRight},  {-1, 0, kFaceLeft},     {0, 1, kFaceFront},  {0, -1, kFaceBack},
      {1, 1, kFaceRight},    // 45 degrees down-right
      {1, 2, kFaceFront},    // 63 degrees
      {-1, 2, kFaceFront},   // 117 degrees
      {-1, 1.5f, kFaceLeft}, // 124 degrees
      {-1, -1.05f, kFaceBack},   // -133 degrees
      {-1, -0.9f, kFaceLeft},    // -138 degrees
      {1, -1.05f, kFaceBack},    // -46 degrees
      {1, -0.9f, kFaceRight},    // -42 degrees
  };
  for (const Row& r : rows) CHECK_MESSAGE(facingAfterTurning(f, *p, r.dx, r.dy) == r.facing, "direction " << r.dx << "," << r.dy);
}

TEST_CASE("VCharacter::UpdateFacing: the heading turns player_turn_speed per update, not per millisecond, the short way round") {
  Fx f;
  auto p = f.makePlayer();
  p->setDirectionVector({1, 0});
  const float pi2 = 1.5707964f;
  p->updateFacing();
  CHECK(near(p->heading(), pi2 - 0.4f));
  CHECK(near(p->previousHeading(), pi2));
  p->updateFacing();
  CHECK(near(p->heading(), pi2 - 0.8f));
  p->updateFacing();
  p->updateFacing();  // 0.3708 left: the last turn is the remainder
  CHECK(near(p->heading(), 0.0f, 1e-4f));
  p->updateFacing();
  CHECK(near(p->heading(), 0.0f));
  // a long frame turns the same: the amount does not depend on the time
  p->setHeading(0.0f, 0.0f);
  p->setDirectionVector({0, 1});
  f.step(800);
  CHECK(near(p->heading(), 0.4f));
  // the short way round: from -3.0 toward +3.0 is a turn of 0.28 through pi, not of 6
  p->setHeading(-3.0f, -3.0f);
  p->setDirectionVector({-1, 0.05f});  // heading about +3.09
  p->updateFacing();
  CHECK(near(p->heading(), -3.4f + 6.2831855f));  // wrapped past -pi
  // and the other way
  p->setHeading(3.0f, 3.0f);
  p->setDirectionVector({-1, -0.05f});  // about -3.09
  p->updateFacing();
  CHECK(near(p->heading(), 3.4f - 6.2831855f));
}

TEST_CASE("VCharacter::UpdateFacing: half a turn goes the way the heading was already turning") {
  Fx f;
  auto p = f.makePlayer();
  p->setDirectionVector({-1, 0});  // pi
  p->setHeading(0.0f, 0.5f);       // came from above 0: it was turning down
  p->updateFacing();
  CHECK(near(p->heading(), 0.4f));
  p->setHeading(0.0f, -0.5f);
  p->updateFacing();
  CHECK(near(p->heading(), -0.4f));
}

TEST_CASE("VPlayer: the avatar switches to the facing's art, flipped for left and back, and the arms follow") {
  Fx f;
  auto p = f.makePlayer();
  p->setFacing(1);
  CHECK(p->currentAsset() == "walkright");
  CHECK_FALSE(p->hflip());
  p->setFacing(0);
  CHECK(p->currentAsset() == "walkright");
  CHECK(p->hflip());
  p->setFacing(3);
  CHECK(p->currentAsset() == "walkback");
  CHECK(p->hflip());  // back is mirrored as well (the base character flips only left)
  CHECK(p->childObject("left_arm")->currentAsset() == "walkback");
  CHECK(p->childObject("torso")->currentAsset() == "walkback");
  p->setFacing(2);
  CHECK(p->currentAsset() == "walkfront");
  CHECK_FALSE(p->hflip());
  // the turn-around countdown: front clears it, any other facing loads the value
  CHECK(p->turnAroundMs() == 0);
  p->setFacing(1);
  CHECK(p->turnAroundMs() == 500);
  // the same facing again switches nothing but the countdown reloads
  p->childObject("torso")->switchAsset("walkfront", "default", "default");
  p->setFacing(1);
  CHECK(p->childObject("torso")->currentAsset() == "walkfront");
}

TEST_CASE("VPlayer: half a second after she stops she faces front again") {
  Fx f;
  auto p = f.makePlayer();
  p->queueMovePoint(f.pt("s2"));
  f.steps(30);
  REQUIRE_FALSE(p->moving());
  CHECK(p->facing() == kFaceRight);
  f.steps(40);  // 640 ms
  CHECK(p->facing() == kFaceFront);
  CHECK(p->turnAroundMs() == 0);
}

TEST_CASE("VPlayer: hand slots - one-hand items fill A then B, a two-hand item needs both, detaching toggles the preference") {
  Fx f;
  auto p = f.makePlayer();
  auto a = VCarriable::create(f.ctx, 1);
  auto b = VCarriable::create(f.ctx, 1);
  auto c = VCarriable::create(f.ctx, 1);
  auto two = VCarriable::create(f.ctx, 2);
  CHECK_FALSE(p->attachObject(nullptr));
  CHECK(p->attachObject(a));
  CHECK(p->numHandsFree() == 1);
  CHECK_FALSE(p->attachObject(two));  // one hand is taken
  CHECK(p->attachObject(b));
  CHECK(p->numHandsFree() == 0);
  CHECK_FALSE(p->attachObject(c));
  CHECK(p->handA() == a);
  CHECK(p->handB() == b);
  // facing front: A is on the right arm at the right hand, B on the left arm; the arms show the carry art for the hands used
  CHECK(a->parent() == p->childObject("right_arm"));
  CHECK(a->anchorName() == "righthand");
  CHECK(b->parent() == p->childObject("left_arm"));
  CHECK(b->anchorName() == "lefthand");
  CHECK(p->childObject("right_arm")->currentState() == "carry");
  CHECK(p->childObject("left_arm")->currentState() == "carry");
  // the held things undo the avatar's perspective scale: at her start (y 100, scale 1) the factor is 1 - (100-... )
  CHECK(near(a->scale().x, (1.0f - 0.0f) - ((100.0f + 100.0f - 100.0f) / 100.0f) * 0.1f));
  // asking for the preferred hand: the preference starts at 2 (slot A); the answer toggles it when the hand is empty
  CHECK(p->preferredHand() == 2);
  auto got = p->detachObject(VPlayer::kHandPreferred);
  CHECK(got == a);
  CHECK(p->preferredHand() == 1);
  CHECK(a->parent() == nullptr);
  CHECK(near(a->scale().x, 1.0f));
  CHECK(p->handA() == nullptr);
  CHECK(p->childObject("right_arm")->currentState() == "default");
  CHECK(p->childObject("left_arm")->currentState() == "carry");
  // hand 0 with the preference on the full hand B, then on the now empty A
  CHECK(p->detachObject(VPlayer::kHandPreferred) == b);
  CHECK(p->detachObject(VPlayer::kHandPreferred) == nullptr);
  // a two-hand item fills both slots and takes the bucket anchor
  CHECK(p->attachObject(two));
  CHECK(p->handA() == two);
  CHECK(p->handB() == two);
  CHECK(two->anchorName() == "bucket");
  CHECK(p->numHandsFree() == 0);
  CHECK(p->detachObject(VPlayer::kHandA) == two);
  CHECK(p->handB() == nullptr);  // detaching it clears both
  CHECK(p->numHandsFree() == 2);
  // nothing to detach: the preference toggles
  const int before = p->preferredHand();
  CHECK(p->detachObject(VPlayer::kHandB) == nullptr);
  CHECK(p->preferredHand() != before);
}

TEST_CASE("VPlayer::detachObject(-1): the hand is unsigned in the original, so nothing is taken and the preferred hand turns") {
  Fx f;
  auto p = f.makePlayer();
  auto a = VCarriable::create(f.ctx, 1);
  auto b = VCarriable::create(f.ctx, 1);
  REQUIRE(p->attachObject(a));
  REQUIRE(p->attachObject(b));
  const int before = p->preferredHand();
  CHECK(p->detachObject(-1) == nullptr);
  CHECK(p->handA() == a);
  CHECK(p->handB() == b);
  CHECK(p->preferredHand() != before);
  CHECK(p->detachObject(-1) == nullptr);
  CHECK(p->preferredHand() == before);
}

TEST_CASE("VPlayer: when the figure is mirrored the arms swap; a missing arm leaves the item on the avatar") {
  Fx f;
  auto p = f.makePlayer();
  auto a = VCarriable::create(f.ctx, 1);
  p->attachObject(a);
  p->setFacing(1);  // right: not mirrored; A on the right arm
  CHECK(a->parent() == p->childObject("right_arm"));
  CHECK(p->childObject("right_arm")->currentState() == "carry");
  CHECK(p->childObject("left_arm")->currentState() == "default");
  p->setFacing(0);  // left: mirrored; A on the left arm at the left hand
  CHECK(a->parent() == p->childObject("left_arm"));
  CHECK(a->anchorName() == "lefthand");
  CHECK(p->childObject("left_arm")->currentState() == "carry");
  CHECK(p->childObject("right_arm")->currentState() == "default");
  CHECK(p->childObject("left_arm_upper")->currentState() == "carry");
  // no arms at all
  Fx g;
  auto q = std::make_shared<TestPlayer>(g.ctx, g.graph);
  q->setHost(&g.host);
  auto t = tbl();
  t->set("assets", walkAssets(false)).set("startZone", "avatar").set("startPoint", "s1");
  q->loadFromTable(StyleChain(*t, nullptr));
  q->init();
  auto item = VCarriable::create(g.ctx, 1);
  CHECK(q->attachObject(item));
  CHECK(item->parent() == q.get());
  CHECK(item->anchorName() == "righthand");
}

TEST_CASE("VPlayer: a held item that the drop check names leaves the hands on the next update") {
  Fx f;
  auto p = f.makePlayer();
  auto a = VCarriable::create(f.ctx, 1);
  p->attachObject(a);
  bool decayed = false;
  p->setDropCheck([&](const VCarriable& c) { return &c == a.get() && decayed; });
  f.step(16);
  CHECK(p->handA() == a);
  decayed = true;
  f.step(16);
  CHECK(p->handA() == nullptr);
}

TEST_CASE("VCharacter idle: the countdown reloads while busy, a stand ends in an idle animation that hides the body, and walking stops it") {
  Fx f;
  auto p = f.makePlayer();
  f.step(16);
  CHECK(p->idleCountdownMs() <= 10000);  // standing from the start: the countdown was 0, so it took the long cooldown
  CHECK(p->idleCountdownMs() > 9900);
  p->queueMovePoint(f.pt("s2"));
  f.step(16);
  CHECK(p->idleCountdownMs() > 2900);  // busy: first_cooldown less this update's time
  CHECK(p->idleCountdownMs() <= 3000);
  f.steps(30);
  REQUIRE_FALSE(p->moving());
  // (the idle assets here are stills, so the idle child stops at once; the show/hide is checked directly)
  p->playRandomIdleAnim();
  CHECK(p->currentIdle() != nullptr);
  CHECK_FALSE(p->selfVisible());
  CHECK_FALSE(p->childObject("torso")->visible());
  CHECK_FALSE(p->childObject("left_arm")->visible());
  p->stopCurrentIdle();
  CHECK(p->currentIdle() == nullptr);
  CHECK(p->selfVisible());
  CHECK(p->childObject("torso")->visible());
  CHECK_FALSE(p->childObject("idle_1")->visible());
  CHECK_FALSE(p->childObject("idle_2")->visible());
  // with a hand full she does not idle, she faces front
  p->setFacing(1);
  p->attachObject(VCarriable::create(f.ctx, 1));
  p->playRandomIdleAnim();
  CHECK(p->currentIdle() == nullptr);
  CHECK(p->selfVisible());
}

TEST_CASE("VCharacter idle: after the first cooldown of standing the idle animation is chosen by the game's random stream") {
  Fx f;
  auto p = f.makePlayer();
  p->queueMovePoint(f.pt("s2"));
  f.steps(40);
  REQUIRE_FALSE(p->moving());
  f.world->values().setValue("player_idle_first_cooldown", 100.0f, false);
  // busy at the start of a trip reloads 100; once standing the countdown runs out within a few frames
  p->queueMovePoint(f.pt("s1"));
  f.steps(2);
  REQUIRE(p->moving());
  script::Rng probe = f.ctx.rng;
  f.steps(40);
  REQUIRE_FALSE(p->moving());
  f.steps(10);
  CHECK(p->idleCountdownMs() > 9000);  // it ran out and chose an idle (a still image ends at once), then reloaded the long wait
  // one draw of the game's stream picked the idle animation
  probe.next32();
  script::Rng now = f.ctx.rng;
  CHECK(probe.next32() == now.next32());
}

TEST_CASE("VCharacter: the walk goes on under a soft pause, and a sprite's pause shifts a running wait") {
  Fx f;
  auto p = f.makePlayer();
  f.mgr.softPause(true);
  p->queueMovePoint(f.pt("s2"));
  f.steps(10);
  CHECK(p->position().x > 150);  // she walks while the manager is soft-paused
  f.steps(20);
  CHECK(p->position().x == 200.0f);
  CHECK(f.arrivals.empty());  // (no station here; the queue was consumed)
  // VSuspendable: the wait does not run down while paused
  f.mgr.softPause(false);
  f.step(16);
  const double t0 = f.now;
  p->suspend(500);
  CHECK(p->suspended());
  f.step(16);
  f.mgr.softPause(true);
  f.steps(10);  // 160 ms of pause
  f.mgr.softPause(false);
  f.step(16);  // the pause ends: the wait goes on from the time it had run before the pause
  f.steps(26);
  CHECK(f.now - t0 > 500);
  CHECK(p->suspended());  // without the shift it would be over by now
  f.steps(14);
  CHECK_FALSE(p->suspended());
}

TEST_CASE("VPlayer: with the centre table she follows the graph's route, without it she walks straight") {
  {
    Fx f;
    auto p = f.makePlayer(true);
    p->queueMovePoint(f.pt("s4"));
    f.steps(200);
    CHECK(p->position().x == 100.0f);
    CHECK(p->position().y == 200.0f);
    CHECK(p->nodeTrail() == Names{"s1", "s2", "s3", "s4", "s4"});  // the route's start node, the nodes between, the end twice
    CHECK(p->currentNode() == f.pt("s4"));
    CHECK(f.graph->pathEmpty());
  }
  {
    // an unreachable destination: the route is [dest, dest] and she walks straight at it
    Fx f;
    auto p = f.makePlayer(true);
    f.graph->disablePoint("avatar", "s2", true);
    p->queueMovePoint(f.pt("s4"));
    f.steps(8);
    CHECK(p->position().x == 100.0f);
    CHECK(p->position().y > 130);
    f.steps(100);
    CHECK(p->nodeTrail() == Names{"s4", "s4"});
    CHECK(p->position().y == 200.0f);
  }
  {
    Fx f;
    auto p = f.makePlayer(false);
    p->queueMovePoint(f.pt("s4"));
    f.steps(8);
    CHECK(p->position().x == 100.0f);  // straight down the left side, never through s2
    CHECK(p->position().y > 130);
    f.steps(100);
    CHECK(p->nodeTrail() == Names{"s4"});
  }
}

TEST_CASE("VPlayer: an arrival handler that takes the avatar out of the scene ends the chain safely") {
  Fx f;
  auto p = f.makePlayer();
  auto st = f.makeStation(2, {{"s2", 2}});
  st->extra = [&] { f.mgr.removeObject(*p); };  // what a level reload does to it
  p->queueMovePoint(f.pt("s2"));
  p->queueMovePoint(f.pt("s3"));
  f.steps(30);
  CHECK(f.arrivals == std::vector<std::string>{"s2"});
  CHECK(p->manager() == nullptr);
  CHECK(p->queue().size() == 1);  // s3 was not started
  CHECK_FALSE(p->moving());
}

TEST_CASE("VPlayer: a route left in the graph is walked by a new avatar that is placed while it stays (the graph outlives a level)") {
  Fx f;
  auto p = f.makePlayer(true);
  p->queueMovePoint(f.pt("s4"));
  f.steps(6);
  REQUIRE(p->moving());
  REQUIRE_FALSE(f.graph->pathEmpty());
  const std::size_t left = f.graph->path().size();
  // the level is loaded again: a new avatar, an empty queue, the old one gone
  f.mgr.removeAllObjects();
  auto q = f.makePlayer(true);
  CHECK(q->queue().empty());
  CHECK(q.get() != p.get());
  // the graph still holds the old route; the new avatar takes its next node at once
  CHECK(f.graph->path().size() < left);
  CHECK(q->moving());
}
