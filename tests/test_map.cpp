#include <doctest/doctest.h>
#include "platform/Png.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "ScriptHarness.h"
#include "assets/AssetFS.h"
#include "games/choc3/Choc3Natives.h"
#include "games/common/MapWindow.h"
#include "games/common/PortWindow.h"
#include "games/common/Travel.h"
#include "games/common/World.h"
#include "gfx/RecordingRenderer.h"
#include "gfx/TextureCache.h"
#include "script/LuaStyle.h"
#include "ui/DrawContext.h"
#include "ui/Style.h"
#include "ui/WindowManager.h"

using namespace choco;
using game::Travel;
using game::TravelEvent;
using game::TravelMode;
using game::TravelScaling;
using gfx::PointF;
using sdk::Sprite;
using ui::MapStyle;

namespace {

std::shared_ptr<MapStyle> sub() { return std::make_shared<MapStyle>(); }

std::shared_ptr<MapStyle> port(const std::string &name, double x, double y) {
  auto p = sub();
  p->set("name", name).set("mapx", x).set("mapy", y);
  return p;
}

std::shared_ptr<const gfx::SpriteSheet> fakeSheet(int frames) {
  auto s = std::make_shared<gfx::SpriteSheet>();
  for (int i = 0; i < frames; ++i)
    s->frames.push_back({{0, 0, 10, 10}, {5, 5}, {}});
  return s;
}

// A trip on the 60 Hz simulated clock; fire counts events and optionally
// refuses (owner gone) at `stopAt`.
struct Trip {
  Sprite vehicle{10, "vehicle"};
  std::unique_ptr<Travel> travel;
  std::vector<std::string> modes;
  std::vector<PointF> dots;
  std::vector<float> fired;
  int stopAt = -1;
  double now = 0;

  Trip(std::vector<PointF> pts, std::vector<TravelMode> m,
       std::vector<float> eventTimes, TravelScaling s = {0, 400, 1, 1}) {
    vehicle.sheet = fakeSheet(16);
    std::vector<TravelEvent> ev;
    for (float t : eventTimes)
      ev.push_back({t, {}});
    travel = std::make_unique<Travel>(
        gfx::Curve::fromPoints(std::move(pts)), vehicle, std::move(m),
        std::move(ev), s, now,
        [this](const std::string &n) { modes.push_back(n); });
  }
  Travel::Result frame() {
    now += 1000.0 / 60.0;
    Travel::Hooks h;
    h.setMode = [this](const std::string &n) { modes.push_back(n); };
    h.addDot = [this](PointF p) { dots.push_back(p); };
    h.fire = [this](const TravelEvent &e) {
      fired.push_back(e.timeMs);
      return stopAt < 0 || static_cast<int>(fired.size()) < stopAt;
    };
    return travel->tick(now, h);
  }
  void runTo(double ms) {
    while (now < ms && !travel->finished())
      frame();
  }
};

} // namespace

TEST_CASE("Marker looks by Player.portsAvailable state") {
  CHECK_FALSE(game::markerLook("hidden"));
  auto look = [](const char *s) { return *game::markerLook(s); };
  CHECK(look("open").image == "image/port_visited");
  CHECK_FALSE(look("open").sheet);
  CHECK(look("new").image == "image/port_new.xml");
  CHECK(look("new").animated);
  CHECK(look("factory").image == "image/factory.xml");
  CHECK(look("factory").animated);
  CHECK(look("factory_stall").image == "image/factory_idle.xml");
  CHECK(look("shop").image == "image/icon_shop");
  // Locked, a missing state and anything unknown all get the (still) locked
  // marker.
  for (const char *s : {"locked", "", "what"}) {
    CHECK(look(s).image == "image/port_locked.xml");
    CHECK_FALSE(look(s).animated);
  }
}

TEST_CASE("Ports and the player are read from nested data") {
  MapStyle all;
  auto zurich = port("zurich", 400, 50);
  auto wellington = port("wellington", 780, 400);
  wellington->set("align", std::string("left"))
      .set("popupX", 526.0)
      .set("popupY", 310.0);
  all.set("zurich", zurich).set("wellington", wellington);
  all.set("junk", sub()); // a table without a name is skipped
  const auto ports = game::parsePorts(&all);
  REQUIRE(ports.size() == 2);
  const auto &w = ports[0].name == "wellington" ? ports[0] : ports[1];
  CHECK(w.align == "left");
  CHECK(*w.popupX == 526);
  CHECK(*w.popupY == 310);
  const auto &z = ports[0].name == "zurich" ? ports[0] : ports[1];
  CHECK_FALSE(z.popupX);
  CHECK(z.mapy == 50);

  MapStyle player;
  auto avail = sub();
  avail->set("zurich", std::string("open"))
      .set("wellington", std::string("hidden"));
  player.set("portName", std::string("zurich")).set("portsAvailable", avail);
  const auto pl = game::parsePlayer(&player, ports);
  CHECK(pl.portName == "zurich");
  CHECK(pl.available.at("wellington") == "hidden");
  CHECK(game::parsePorts(nullptr).empty());
}

TEST_CASE("Routes: the default is {A,A,B,B}; a via merges to seven points") {
  const PointF a{10, 10}, m{100, 10}, b{100, 100};
  const auto first = game::straightRoute(a, m);
  CHECK(first == std::vector<PointF>{a, a, m, m});
  const auto merged = game::mergeRoutePoints(first, game::straightRoute(m, b));
  REQUIRE(merged.size() == 7);
  CHECK(merged[3] == m); // the shared waypoint once
  CHECK(merged[6] == b);
  const gfx::Curve c = gfx::Curve::fromPoints(merged);
  CHECK(c.length() == doctest::Approx(90 + 90).epsilon(0.001));
  CHECK(gfx::Curve::fromPoints(first).length() == doctest::Approx(90));
}

TEST_CASE("InitiateTravel arguments are read from the Lua table") {
  test::ScriptEnv env(CHOCO_TEST_ASSETS);
  env.run(R"(
    T = { path = { {0,0},{0,0},{300,0},{300,0} }, type = { {0,"travel_air"}, {0.5,"travel_boat"} },
          events = { {time=1000, action=function() end}, {time=3000, action=function() end} }, ignored = 1 }
    D = { dest = { mapx = 30, mapy = 40 } }
  )");
  lua_State *L = env.lua.L();
  lua_getglobal(L, "T");
  {
    const script::LuaStyle st(L, lua_gettop(L), &env.lua);
    auto r = game::parseTravelRequest(st, {1, 1});
    CHECK(r.curve.controlPoints() == 4);
    CHECK(r.curve.length() == doctest::Approx(300));
    REQUIRE(r.modes.size() == 2);
    CHECK(r.modes[1].fraction == 0.5f);
    CHECK(r.modes[1].name == "travel_boat");
    REQUIRE(r.events.size() == 2);
    CHECK(r.events[1].timeMs == 3000);
    CHECK(r.events[0].action.valid());
  }
  lua_pop(L, 1);
  lua_getglobal(L, "D");
  {
    const script::LuaStyle st(L, lua_gettop(L), &env.lua);
    auto r = game::parseTravelRequest(
        st, {0, 0}); // no path: {from, from, dest, dest}
    CHECK(r.curve.controlPoints() == 4);
    CHECK(r.curve.length() == doctest::Approx(50));
    CHECK(r.modes.empty());
  }
  lua_pop(L, 1);
}

TEST_CASE("Heading frames and perspective scale") {
  auto pose = [](PointF a, PointF b, TravelScaling s = {0, 400, 1, 1}) {
    return game::travelPose(gfx::Curve::fromPoints(game::straightRoute(a, b)),
                            0.5f, 16, s);
  };
  CHECK(pose({0, 100}, {0, 0}).frame == 0);   // up the screen
  CHECK(pose({0, 0}, {100, 0}).frame == 4);   // right
  CHECK(pose({0, 0}, {0, 100}).frame == 8);   // down
  CHECK(pose({100, 0}, {0, 0}).frame == 12);  // left
  CHECK(pose({0, 0}, {100, 100}).frame == 6); // down-right
  // y 200 of 0..400 with scales 0.4..1 is 0.7; no rotation is kept then.
  const auto p = pose({0, 200}, {100, 200}, {0, 400, 0.4f, 1});
  CHECK(p.scale == doctest::Approx(0.7f));
  CHECK(p.rotationDeg == 0);
  // Equal far and near y: no scaling, the rest of the angle stays as a
  // rotation.
  const auto q = pose({0, 0}, {100, 20}, {50, 50, 1, 1});
  CHECK(q.scale == 1);
  CHECK(q.rotationDeg != 0);
}

TEST_CASE("Travel: fraction, trail dots, vehicle modes and events on the "
          "simulated clock") {
  // 360 px straight north, 3 s long; an event at 1 s and the arrival at 3 s.
  Trip trip({{100, 400}, {100, 400}, {100, 40}, {100, 40}},
            {{0, "a"}, {0.5f, "b"}}, {1000, 3000});
  CHECK(trip.modes ==
        std::vector<std::string>{"a"}); // the first mode applies at once
  CHECK_FALSE(trip.vehicle.visible);
  CHECK(trip.travel->durationMs() == 3000);

  trip.runTo(1500);
  CHECK(trip.vehicle.visible);
  CHECK(trip.travel->fraction() == doctest::Approx(0.5).epsilon(0.03));
  CHECK(trip.travel->dots() ==
        static_cast<int>(std::floor(trip.travel->fraction() * 360)) / 12);
  CHECK(trip.dots.size() == static_cast<std::size_t>(trip.travel->dots()));
  CHECK(trip.dots[0] == PointF{100, 400});
  CHECK(trip.dots[1].y == doctest::Approx(388));
  CHECK(trip.vehicle.frame == 0);
  CHECK(trip.fired == std::vector<float>{1000});
  CHECK(trip.modes.size() == 2); // switched to "b" at 0.5
  CHECK(trip.modes[1] == "b");

  trip.runTo(10000);
  CHECK(trip.travel->finished());
  CHECK(trip.fired == std::vector<float>{1000, 3000});
  CHECK(trip.travel->fraction() == 1.0f);
  CHECK(trip.vehicle.pos == PointF{100, 40});
  CHECK(trip.travel->dots() == 360 / 12);
  CHECK(trip.travel->tick(trip.now + 1000, {}) ==
        Travel::Result::Finished); // a finished trip stays finished
}

TEST_CASE("Travel: events fire in list order and the first future one stops "
          "the scan; the last tick fires the rest") {
  Trip trip({{0, 0}, {0, 0}, {300, 0}, {300, 0}}, {},
            {2000, 500, 3000}); // unsorted: 500 waits behind 2000
  trip.runTo(1000);
  CHECK(trip.fired.empty());
  trip.runTo(2100);
  CHECK(trip.fired == std::vector<float>{2000, 500});
  trip.runTo(10000);
  CHECK(trip.fired == std::vector<float>{2000, 500, 3000});
  CHECK(trip.modes == std::vector<std::string>{
                          "travel_air"}); // no types: the default vehicle
}

TEST_CASE("Travel: pause and restart discard the paused time") {
  Trip trip({{0, 0}, {0, 0}, {300, 0}, {300, 0}}, {}, {3000});
  trip.runTo(1000);
  const double t = trip.travel->timeMs();
  trip.travel->pause();
  CHECK(trip.travel->paused());
  for (int i = 0; i < 300; ++i)
    trip.frame(); // 5 s with no progress
  CHECK(trip.travel->timeMs() == t);
  trip.travel->restart(trip.now);
  CHECK_FALSE(trip.travel->paused());
  trip.runTo(trip.now + 100);
  CHECK(trip.travel->timeMs() - t < 150);
  CHECK(trip.travel->timeMs() - t > 50);
  // Pausing and restarting inside an event, as a tick event does, loses
  // nothing.
  const double before = trip.travel->timeMs();
  trip.travel->pause();
  trip.travel->restart(trip.now);
  trip.frame();
  trip.frame();
  trip.frame();
  const double gained = trip.travel->timeMs() - before;
  CHECK(gained >
        20); // time keeps counting from the restart, not from the pause
  CHECK(gained < 70);
}

TEST_CASE(
    "Travel: an event that reports the owner gone stops the loop at once") {
  Trip trip({{0, 0}, {0, 0}, {300, 0}, {300, 0}}, {}, {100, 100, 100});
  trip.stopAt = 2;
  Travel::Result r = Travel::Result::Running;
  while (r == Travel::Result::Running)
    r = trip.frame();
  CHECK(r == Travel::Result::OwnerGone);
  CHECK(trip.fired.size() == 2); // the third event never ran
  CHECK(trip.travel->eventsFired() == 2);
}

TEST_CASE("MapWindow: markers, labels, rollovers, ring, clouds and the single "
          "travel") {
  ui::WindowManager wm;
  auto &modal = wm.pushModal("map");
  modal.setRect({0, 0, 800, 600});
  auto map = std::make_unique<game::MapWindow>(wm, game::MapEnv{});
  game::MapWindow *m = map.get();
  modal.adoptFront(std::move(map));
  MapStyle style;
  style.set("x", 0.0)
      .set("y", 0.0)
      .set("w", 800.0)
      .set("h", 600.0)
      .set("background", std::string("image/worldmap"));
  style.set("cloudTime", 60000.0).set("cloudY", 370.0);
  m->init(style);

  std::vector<game::PortInfo> ports;
  for (auto [n, x, y] : {std::tuple{"zurich", 400, 50},
                         {"bali", 600, 300},
                         {"secret", 100, 100},
                         {"tokyo", 760, 200}}) {
    game::PortInfo p;
    p.name = n, p.mapx = static_cast<float>(x), p.mapy = static_cast<float>(y);
    ports.push_back(p);
  }
  game::PlayerInfo player;
  player.portName = "zurich";
  player.available = {{"zurich", "open"},
                      {"bali", "new"},
                      {"secret", "hidden"},
                      {"tokyo", "locked"}};
  m->collectPortData(player, ports);

  CHECK(wm.countSprites("marker:") == 3); // hidden ports get nothing
  CHECK(wm.countSprites("marker:secret") == 0);
  CHECK(wm.countSprites("label:") == 3);
  CHECK(wm.countSprites("ring") >= 1);
  REQUIRE(m->currentPort());
  CHECK(m->currentPort()->pos == PointF{400, 50});
  // Labels: right of the marker, left for mapx >= 750.
  CHECK(wm.dumpWindows().find("label:zurich layer=3 pos=410.0,50.0") !=
        std::string::npos);
  CHECK(wm.dumpWindows().find("label:tokyo layer=3 pos=750.0,200.0") !=
        std::string::npos);
  // One 34 px Rollover per visible port, with the Lua strings.
  int rollovers = 0;
  for (const auto &c : m->children())
    if (c->typeName() == "Rollover") {
      ++rollovers;
      CHECK(c->rect().w == 34);
      CHECK(c->rect().h == 34);
    }
  CHECK(rollovers == 3);
  CHECK(wm.dumpWindows().find(
            "contents=\"bali:MapRolloverPopup()\" click=\"bali:OnClick()\"") !=
        std::string::npos);

  // Clouds drift 30/60000 of the width per animation call (no random phase
  // without an Rng).
  CHECK(m->cloudX() == 0);
  for (int i = 0; i < 100; ++i)
    m->onAnimate();
  CHECK(m->cloudX() ==
        doctest::Approx(100 * 30.0 / 60000 * 800).epsilon(0.001));

  // Pause and resume with no travel are no-ops; the second InitiateTravel is
  // ignored.
  m->pauseTravel();
  m->resumeTravel(0);
  game::TravelRequest req;
  req.curve =
      gfx::Curve::fromPoints(game::straightRoute({400, 50}, {600, 300}));
  req.events.push_back({1000, {}});
  CHECK(m->initiateTravel(std::move(req), 0));
  CHECK(wm.countSprites("vehicle") == 0); // hidden until the first tick
  CHECK_FALSE(m->initiateTravel({}, 0));
  m->tickTravel(40);
  CHECK(wm.countSprites("vehicle") == 1);
  CHECK(wm.dumpWindows().find("travel t=") != std::string::npos);
  m->pauseTravel();
  CHECK(m->travel()->paused());
}

namespace {
// Runs an event the way the host does, except that it also swaps the modal: the
// map is queued for destruction inside it.
struct SwapSink : ui::ScriptSink {
  explicit SwapSink(ui::WindowManager &w) : wm(w) {}
  void deliver(const ui::Message *, const script::LuaRef *) override {}
  void runInScript(const script::LuaRef &) override {
    ++runs;
    wm.clearModalChildren();
  }
  bool callProtected(const script::LuaRef &) override { return true; }
  void setGlobalString(const char *, std::string_view) override {}
  ui::WindowManager &wm;
  int runs = 0;
};

std::unique_ptr<game::MapWindow> plainMap(ui::WindowManager &wm,
                                          game::MapEnv env) {
  auto map = std::make_unique<game::MapWindow>(wm, env);
  MapStyle style;
  style.set("x", 0.0)
      .set("y", 0.0)
      .set("w", 800.0)
      .set("h", 600.0)
      .set("cloudTime", 60000.0)
      .set("cloudY", 370.0);
  map->init(style);
  return map;
}
} // namespace

TEST_CASE("MapWindow: focus loss stops the clouds, gain restarts them and "
          "re-bases a running travel") {
  ui::WindowManager wm;
  auto &modal = wm.pushModal("map");
  modal.setRect({0, 0, 800, 600});
  auto map = std::make_unique<game::MapWindow>(wm, game::MapEnv{});
  game::MapWindow *m = map.get();
  modal.adoptFront(std::move(map));
  MapStyle style;
  style.set("x", 0.0)
      .set("y", 0.0)
      .set("w", 800.0)
      .set("h", 600.0)
      .set("background", std::string("image/worldmap"));
  m->init(style);
  game::PlayerInfo player;
  player.portName = "zurich";
  game::PortInfo zurich;
  zurich.name = "zurich", zurich.mapx = 400, zurich.mapy = 50;
  m->collectPortData(player, {zurich});
  wm.startWindowAnimation(m->id(), game::MapWindow::kCloudStepMs);
  REQUIRE(wm.windowAnimationCount() == 1);

  game::TravelRequest req;
  req.curve =
      gfx::Curve::fromPoints(game::straightRoute({400, 50}, {600, 300}));
  req.events.push_back({100000, {}});
  REQUIRE(m->initiateTravel(std::move(req), 0));
  m->tickTravel(1000);
  const double before = m->travel()->timeMs();

  m->setForeground(false, 2000);
  CHECK(wm.windowAnimationCount() == 0);
  m->setForeground(true, 60000);
  CHECK(wm.windowAnimationCount() == 1);
  m->tickTravel(61000);
  CHECK(m->travel()->timeMs() - before < 3000); // the minute away is not flown

  // A paused travel stays paused on gain.
  m->pauseTravel();
  m->setForeground(true, 70000);
  CHECK(m->travel()->paused());
}

TEST_CASE("MapWindow: an event that swaps the modal ends the travel through "
          "the real window lookup") {
  test::ScriptEnv env(CHOCO_TEST_ASSETS);
  SwapSink sink(env.wm);
  env.wm.setScriptSink(&sink);
  auto &modal = env.wm.pushModal("map");
  modal.setRect({0, 0, 800, 600});
  auto owned = plainMap(env.wm, {});
  game::MapWindow *m = owned.get();
  modal.adoptFront(std::move(owned));
  m->collectPortData({}, {});

  game::TravelRequest req;
  req.curve = gfx::Curve::fromPoints(game::straightRoute({0, 0}, {300, 0}));
  for (int i = 0; i < 3; ++i)
    req.events.push_back(
        {100, env.compile("x = 1")}); // three events due at the same time
  REQUIRE(m->initiateTravel(std::move(req), 0));
  for (double t = 20; t <= 400 && !m->isDestroyed(); t += 20)
    m->tickTravel(t);
  CHECK(sink.runs == 1); // the second and third events never ran
  CHECK(m->isDestroyed());
  CHECK(env.wm.window(m->id()) == nullptr);
  env.wm.setScriptSink(nullptr);
  env.wm.clearDestroyed();
}

TEST_CASE("Travel: a multi-week trip fires one event per week at (i-1)*3000+1 "
          "even when each pauses and restarts") {
  // Port:GetRoute events for a 3 week trip: the tick events at 1, 3001 and 6001
  // ms, and the arrival at 9000.
  Trip trip({{0, 0}, {0, 0}, {900, 0}, {900, 0}}, {}, {1, 3001, 6001, 9000});
  std::vector<double> at;
  while (!trip.travel->finished() && trip.now < 20000) {
    trip.now += 1000.0 / 60.0;
    Travel::Hooks h;
    h.fire = [&](const TravelEvent &e) {
      trip.fired.push_back(e.timeMs);
      at.push_back(trip.travel->timeMs());
      trip.travel->pause(); // PauseTravel(); TickSim(1); ResumeTravel()
      trip.travel->restart(trip.now);
      return true;
    };
    trip.travel->tick(trip.now, h);
  }
  CHECK(trip.travel->finished());
  CHECK(trip.fired == std::vector<float>{1, 3001, 6001, 9000});
  REQUIRE(at.size() == 4);
  for (std::size_t i = 0; i < 4; ++i) {
    CHECK(at[i] >= trip.fired[i]);
    CHECK(at[i] < trip.fired[i] + 100); // fired on the first tick after its
                                        // time, none skipped or doubled
  }
  CHECK(trip.travel->durationMs() == 9000);
}

TEST_CASE("MapWindow: the cloud strip is centred on cloudY and drawn in three "
          "tiles") {
  namespace fs = std::filesystem;
  const fs::path dir = fs::temp_directory_path() / "choco_map_clouds";
  fs::remove_all(dir);
  fs::create_directories(dir / "image");
  auto png = [&](const char *name, int w, int h) {
    std::vector<unsigned char> px(static_cast<std::size_t>(w) * h * 4, 255);
    REQUIRE(choco::platform::writePng((dir / "image" / name).string(), w, h, px.data()));
  };
  png("worldmap_clouds.png", 800, 172);
  png("worldmap.png", 800, 600);
  assets::AssetFS afs(dir.string(), "");
  gfx::TextureCache textures(afs, nullptr);

  ui::WindowManager wm;
  wm.setTextures(&textures);
  auto &modal = wm.pushModal("map");
  modal.setRect({0, 0, 800, 600});
  auto map = std::make_unique<game::MapWindow>(wm, game::MapEnv{});
  game::MapWindow *m = map.get();
  modal.adoptFront(std::move(map));
  MapStyle style;
  style.set("x", 0.0)
      .set("y", 0.0)
      .set("w", 800.0)
      .set("h", 600.0)
      .set("background", std::string("image/worldmap"));
  style.set("cloudTime", 60000.0).set("cloudY", 370.0);
  m->init(style);
  m->collectPortData({}, {});

  gfx::RecordingRenderer r;
  ui::drawWindow(*m, ui::DrawContext{r, {0, 0}, 1});
  float top = 1e9f, bottom = -1e9f;
  int tiles = 0;
  for (const auto &c : r.calls())
    if (c.kind == gfx::RecordingRenderer::Call::Kind::DrawTexture &&
        c.texture &&
        c.texture->path().find("worldmap_clouds") != std::string::npos) {
      ++tiles;
      top = std::min(top, c.dst.y);
      bottom = std::max(bottom, c.dst.y + c.dst.h);
    }
  CHECK(tiles == 3);
  CHECK(top == doctest::Approx(284)); // 370 - 172 / 2
  CHECK(bottom == doctest::Approx(456));
  wm.clearDestroyed();
}

TEST_CASE("World: one tick steps the port, the map's travel and the ledger; a "
          "gone window is skipped") {
  test::ScriptEnv env(CHOCO_TEST_ASSETS);
  auto &modal = env.wm.pushModal("screen");
  modal.setRect({0, 0, 800, 600});
  game::World &world = *game::worldOf(env.ctx);
  CHECK(world.port() == nullptr);
  CHECK(world.map() == nullptr);
  world.tick(10); // nothing built: harmless

  auto portOwned =
      std::make_unique<game::PortWindow>(env.wm, env.rng, env.log, &world);
  game::PortWindow *port = portOwned.get();
  ui::MapStyle pstyle;
  pstyle.set("w", 800.0).set("h", 600.0);
  env.wm.initWindow(std::move(portOwned), pstyle);
  port->postChildrenInit(pstyle);
  CHECK(world.port() == port);

  game::MapEnv menv;
  menv.world = &world;
  auto mapOwned = plainMap(env.wm, menv);
  game::MapWindow *map = mapOwned.get();
  modal.adoptFront(std::move(mapOwned));
  MapStyle mstyle;
  map->postChildrenInit(mstyle);
  CHECK(world.map() == map);
  game::TravelRequest req;
  req.curve = gfx::Curve::fromPoints(game::straightRoute({0, 0}, {300, 0}));
  req.events.push_back({5000, {}});
  REQUIRE(map->initiateTravel(std::move(req), 0));

  env.wm.setNowMs(0);
  world.tick(40);
  CHECK(map->travel()->timeMs() == doctest::Approx(40));
  CHECK(port->spriteRoot()->tint.r ==
        doctest::Approx(0.25)); // the port's first 50 ms step is not due yet
  world.tick(80);
  CHECK(map->travel()->timeMs() == doctest::Approx(80));
  CHECK(port->spriteRoot()->tint.r > 0.25f); // it was, by 80

  env.wm.destroyWindow(*map);
  CHECK(world.map() == nullptr);
  world.tick(120); // no map: no crash, the port still runs
  env.wm.clearDestroyed();
}

TEST_CASE("Travel natives are no-ops without a map") {
  test::ScriptEnv env(CHOCO_TEST_ASSETS);
  script::registerMapNatives(env.lua.L(), env.ctx);
  CHECK(env.run("PauseTravel() ResumeTravel() InitiateTravel{ path = {}, "
                "events = {} }") == 0);
  CHECK(env.log.contains("InitiateTravel: there is no map window"));
  std::string err;
  CHECK(script::evalLuaCondition(env.ctx, "1 + 1 == 2", err));
  CHECK_FALSE(script::evalLuaCondition(env.ctx, "nosuch.field", err));
  CHECK_FALSE(err.empty());
}
