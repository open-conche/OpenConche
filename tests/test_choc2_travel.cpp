// Chocolatier 2: the week mover, MapTravel and its natives, Map's style-fed marker list and SpriteWindow
// (docs/design/choc2-port-and-ledger.md 2.5, 2.6 and section 4).
#include <doctest/doctest.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include "platform/Png.h"

#include "games/Registry.h"
#include "ScriptHarness.h"
#include "games/choc2/ColorMask.h"
#include "games/choc2/Map.h"
#include "games/choc2/TravelSounds.h"
#include "games/choc2/MapTravel.h"
#include "games/common/sprite/PathSprite.h"
#include "games/choc2/SpriteWindow.h"
#include "games/choc2/TravelWeek.h"
#include "gfx/SheetCache.h"
#include "gfx/TextureCache.h"

using namespace choco;
using game::choc2::MapTravel;
using game::choc2::WeekMover;
namespace fs = std::filesystem;

namespace {

// A straight leg of the shipped encoding: doubled end points, 300 px long.
gfx::Curve straight300() { return gfx::Curve::fromPoints({{0, 0}, {0, 0}, {300, 0}, {300, 0}}); }

std::string travelAssets() {
  const fs::path dir = fs::temp_directory_path() / "choco_test_choc2_travel";
  fs::remove_all(dir);
  auto png = [&](const char* name, int w, int h) {
    const fs::path p = dir / name;
    fs::create_directories(p.parent_path());
    std::vector<unsigned char> px(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4, 255);
    REQUIRE(choco::platform::writePng(p.string(), w, h, px.data()));
  };
  fs::create_directories(dir / "ui");
  std::ofstream(dir / "ui/mainloop.lua") << "function Main() while true do WaitForMessage() end end\nreturn Main\n";
  png("image/icon_train.png", 64, 60);
  png("image/traveldot.png", 8, 8);
  png("image/marker.png", 32, 32);
  png("image/sheet.png", 64, 32);
  std::ofstream(dir / "image/sheet.xml") << R"(<sequence><img name="sheet.png"/><framelist>
      <frame regx="16" regy="16" x="0" y="0" w="32" h="32"/><frame regx="16" regy="16" x="32" y="0" w="32" h="32"/></framelist></sequence>)";
  const auto s = dir.u8string();
  return std::string(s.begin(), s.end());
}

struct TravelEnv : test::ScriptEnv {
  TravelEnv() : ScriptEnv(travelAssets()), textures(fs, nullptr), sheets(fs, textures), host(ctx) {
    ctx.module = games::findModule("choc2");
    ctx.module->attach(ctx);
    wm.setTextures(&textures);
    wm.setSheets(&sheets);
    booted = host.boot();
  }
  // Steps the window animations to `*t + ms` in 10 ms ticks (the finest animation here is 10 ms).
  void advance(double* t, double ms) {
    for (double end = *t + ms; *t < end;) {
      *t += 10;
      clock.nowMs = *t;
      wm.setNowMs(*t);
      wm.tickWindowAnimations(*t);
    }
  }
  MapTravel* travel() { return MapTravel::current(wm); }
  gfx::TextureCache textures;
  gfx::SheetCache sheets;
  script::ScriptHost host;
  bool booted = false;
};

}  // namespace

TEST_CASE("WeekMover: a two-week leg moves half the curve per week over 2000 ms and drops a dot per 160 ms of leg time") {
  WeekMover m;
  CHECK_FALSE(m.begun());
  CHECK_FALSE(m.startWeek(0));
  m.begin(straight300(), 2);
  REQUIRE(m.curve().length() == doctest::Approx(300));
  CHECK_FALSE(m.active());
  CHECK(m.fraction() == 0);

  std::vector<gfx::PointF> dots;
  REQUIRE(m.startWeek(100));
  CHECK(m.active());
  CHECK(m.step(1100, &dots));  // 1000 ms in: half of the first week
  CHECK(m.fraction() == doctest::Approx(0.25f));
  CHECK(m.position().x == doctest::Approx(75));
  CHECK(m.legMs() == doctest::Approx(1000));
  CHECK(dots.size() == 6);  // floor(1000 / 160): the first dot at 160 ms, 4% of the 4000 ms leg
  CHECK(dots[0].x == doctest::Approx(12));
  CHECK(dots[1].x == doctest::Approx(24));
  CHECK_FALSE(m.step(2100, &dots));  // the week's 2000 ms are over
  CHECK(m.fraction() == 0.5f);       // exactly the end of the week
  CHECK(m.weeksDone() == 1);
  CHECK(m.position().x == doctest::Approx(150));
  CHECK(m.dots() == 12);  // floor(2000 / 160)
  CHECK(dots.size() == 12);
  CHECK_FALSE(m.step(9000, &dots));  // nothing runs between weeks

  REQUIRE(m.startWeek(5000));
  CHECK(m.fraction() == 0.5f);  // the second week starts where the first ended
  CHECK_FALSE(m.step(5000 + WeekMover::kWeekMs + 50, &dots));
  CHECK(m.fraction() == 1.0f);
  CHECK(m.weeksDone() == 2);
  CHECK(m.dots() == 25);  // floor(4000 / 160): the leg clock runs across the weeks
  CHECK(dots.size() == 25);
  CHECK_FALSE(m.startWeek(20000));  // no third week
  CHECK(m.fraction() == 1.0f);
}

TEST_CASE("WeekMover: a week started while one runs finishes it first; weeks below 1 count as 1") {
  WeekMover m;
  m.begin(straight300(), 3);
  REQUIRE(m.startWeek(0));
  REQUIRE(m.startWeek(500));  // the Lua always waits, but if it does not the first week is not lost
  CHECK(m.weeksDone() == 1);
  CHECK(m.fraction() == doctest::Approx(1.0f / 3));
  m.step(500 + WeekMover::kWeekMs);
  CHECK(m.weeksDone() == 2);
  CHECK(m.fraction() == doctest::Approx(2.0f / 3));

  m.begin(straight300(), 0);
  CHECK(m.weeks() == 1);
}

TEST_CASE("WeekMover: a curve with no length (fewer than 4 points) leaves the vehicle on its first point and drops no dots") {
  WeekMover m;
  m.begin(gfx::Curve::fromPoints({{5, 6}}), 1);
  REQUIRE(m.startWeek(0));
  m.step(WeekMover::kWeekMs);
  CHECK(m.weeksDone() == 1);
  CHECK(m.dots() == 0);
}

TEST_CASE("TravelOneWeek keeps StillTravelling true for 2000 ms; the vehicle ends at curve.at((k+1)/weeks)") {
  TravelEnv e;
  REQUIRE(e.booted);
  REQUIRE(e.run("PushModal('m'); MapTravel{x = 0, y = 0, w = 800, h = 600}()") == 0);
  MapTravel* t = e.travel();
  REQUIRE(t);
  CHECK(t->typeName() == "MapTravel");
  CHECK_FALSE(e.log.contains("[unimplemented-type] MapTravel"));
  REQUIRE(e.run("travelling0 = StillTravelling();"
                "InitiateTravel{weeks = 2, type = 'train', {10, 10}, {10, 10}, {310, 10}, {310, 10}};"
                "travelling1 = StillTravelling()") == 0);
  CHECK(e.lastError.empty());
  CHECK(e.getString("travelling0") == "false");
  CHECK(e.getString("travelling1") == "false");  // started, but no week yet
  CHECK(t->vehicleType() == "train");
  REQUIRE(t->vehicle());
  CHECK_FALSE(t->vehicle()->visible);  // hidden until the first week moves
  CHECK(t->hasVehicleIcon());          // image/icon_train

  double now = 1000;
  e.clock.nowMs = now;
  e.wm.setNowMs(now);
  REQUIRE(e.run("TravelOneWeek('train'); still = StillTravelling()") == 0);
  CHECK(e.getString("still") == "true");
  CHECK(t->vehicle()->visible);
  e.advance(&now, 1000);
  CHECK(t->stillTravelling());
  CHECK(t->mover().fraction() > 0.2f);
  CHECK(t->mover().fraction() < 0.3f);
  e.advance(&now, 1100);  // 2100 ms after the call
  CHECK_FALSE(t->stillTravelling());
  CHECK(t->mover().weeksDone() == 1);
  const gfx::PointF half = t->mover().curve().at(0.5f);
  CHECK(t->vehicle()->pos.x == doctest::Approx(half.x));
  CHECK(t->vehicle()->pos.y == doctest::Approx(half.y));
  CHECK(t->dotSprites() == 12);  // floor(2000 / 160)
  CHECK_FALSE(t->vehicle()->visible);  // drawn only while a week runs
  CHECK(t->vehicle()->hflip);          // the leg runs to the right and the art faces left
  CHECK(t->mover().dots() == t->dotSprites());

  // The second week ends at the end of the curve.
  e.clock.nowMs = now;
  e.wm.setNowMs(now);
  REQUIRE(e.run("TravelOneWeek('train')") == 0);
  e.advance(&now, 2100);
  CHECK_FALSE(t->stillTravelling());
  const gfx::PointF end = t->mover().curve().at(1.0f);
  CHECK(t->vehicle()->pos.x == doctest::Approx(end.x));
  CHECK(t->dotSprites() == 25);
  CHECK(e.getString("still") == "true");
  REQUIRE(e.run("done = StillTravelling()") == 0);
  CHECK(e.getString("done") == "false");
}

TEST_CASE("A dialog over the map pauses a week mid-way and the train goes on smoothly afterwards") {
  TravelEnv e;
  REQUIRE(e.booted);
  REQUIRE(e.run("PushModal('m'); MapTravel{x = 0, y = 0, w = 800, h = 600}();"
                "InitiateTravel{weeks = 1, type = 'train', {0, 0}, {0, 0}, {300, 0}, {300, 0}}") == 0);
  MapTravel* t = e.travel();
  REQUIRE(t);
  double now = 1000;
  e.clock.nowMs = now;
  e.wm.setNowMs(now);
  REQUIRE(e.run("TravelOneWeek('train')") == 0);
  e.advance(&now, 1000);  // half the week
  const float half = t->mover().fraction();
  CHECK(half == doctest::Approx(0.5f).epsilon(0.05));
  const int dotsHalf = t->mover().dots();

  e.wm.topModal()->clock.pause(now);  // the ledger's Pause or Recipes dialog covers the map
  e.advance(&now, 5000);
  CHECK(t->mover().fraction() == half);  // frozen, not running on the platform clock
  CHECK(t->mover().dots() == dotsHalf);
  e.wm.topModal()->clock.start(now);

  e.advance(&now, 500);
  CHECK(t->stillTravelling());
  CHECK(t->mover().fraction() > half);
  CHECK(t->mover().fraction() < 0.8f);  // a smooth step, not a jump to the end of the week
  e.advance(&now, 600);
  CHECK_FALSE(t->stillTravelling());
  CHECK(t->mover().fraction() == 1.0f);
  CHECK(t->dotSprites() == 12);
}

TEST_CASE("StillTravelling is false and the other travel natives only log when there is no MapTravel") {
  TravelEnv e;
  REQUIRE(e.booted);
  REQUIRE(e.run("PushModal('m'); r = StillTravelling(); TravelOneWeek('ship'); TravelOneWeek('ship');"
                "InitiateTravel{weeks = 1, type = 'ship', {0, 0}, {0, 0}, {5, 5}, {5, 5}}") == 0);
  CHECK(e.lastError.empty());
  CHECK(e.getString("r") == "false");
  CHECK(e.log.contains("[native-warning] TravelOneWeek: there is no MapTravel"));
  CHECK(e.log.contains("[native-warning] InitiateTravel: there is no MapTravel"));
  // Once per run: the second TravelOneWeek does not log again.
  CHECK(e.log.count("[native-warning] TravelOneWeek") == 1);
}

TEST_CASE("MapTravel: TravelOneWeek before InitiateTravel, a leg with too few points, and a missing vehicle image are logged") {
  TravelEnv e;
  REQUIRE(e.booted);
  REQUIRE(e.run("PushModal('m'); MapTravel{x = 0, y = 0, w = 800, h = 600}();"
                "TravelOneWeek('ship'); InitiateTravel{weeks = 1, type = 'ship', {0, 0}, {1, 1}}") == 0);
  CHECK(e.log.contains("[choc2] TravelOneWeek: no leg was started"));
  CHECK(e.log.contains("[choc2] InitiateTravel: a leg needs at least 4 control points, got 2"));
  CHECK_FALSE(e.travel()->stillTravelling());
  REQUIRE(e.run("InitiateTravel{weeks = 1, type = 'canoe', {0, 0}, {0, 0}, {5, 5}, {5, 5}}") == 0);
  CHECK(e.log.contains("no vehicle image 'image/icon_canoe'"));
  CHECK_FALSE(e.travel()->hasVehicleIcon());
  CHECK(e.travel()->vehicleType() == "canoe");
}

TEST_CASE("Map reads the ports, routes and current style keys into a marker list") {
  TravelEnv e;
  REQUIRE(e.booted);
  REQUIRE(e.run("AppendStyle{ports = {a = {name = 'alpha', mapx = 10, mapy = 20}, b = {name = 'beta', mapx = 30, mapy = 40},"
                " junk = 5}, routes = {beta = {totalWeeks = 2}, gamma = {totalWeeks = 1}}, current = 'alpha'}();"
                "PushModal('m'); Map{x = 0, y = 0, w = 800, h = 600}()") == 0);
  CHECK(e.lastError.empty());
  CHECK_FALSE(e.log.contains("[unimplemented-type] Map"));
  game::choc2::Map* map = nullptr;
  for (const auto& c : e.wm.topModal()->children())
    if (auto* m = dynamic_cast<game::choc2::Map*>(c.get())) map = m;
  REQUIRE(map);
  CHECK(map->typeName() == "Map");
  CHECK(map->markers().size() == 2);  // the non-table entry is skipped
  REQUIRE(map->marker("beta"));
  CHECK(map->marker("beta")->mapx == 30);
  CHECK(map->marker("beta")->mapy == 40);
  CHECK(map->marker("nowhere") == nullptr);
  CHECK(map->current() == "alpha");
  CHECK(map->routeCount() == 2);
  CHECK(map->detail().find("ports=2 routes=2 current=alpha") != std::string::npos);
}

TEST_CASE("Sprite places an image by its registration point; a sheet uses the frame's, scale and rotation apply") {
  TravelEnv e;
  REQUIRE(e.booted);
  REQUIRE(e.run("PushModal('m'); Sprite{x = 100, y = 50, image = 'image/marker'}();"
                "Sprite{x = 200, y = 80, image = 'image/sheet.xml', scale = 2, rotation = 30, tint = {1, 0, 0, 1}}();"
                "Sprite{x = 7, y = 9, image = 'image/missing'}();"
                "AnimationWindow{x = 0, y = 0, w = 800, h = 600, fps = 30}()") == 0);
  CHECK(e.lastError.empty());
  CHECK_FALSE(e.log.contains("[unimplemented-type] Sprite"));
  CHECK_FALSE(e.log.contains("[unimplemented-type] AnimationWindow"));
  std::vector<game::choc2::SpriteWindow*> sprites;
  game::choc2::AnimationWindow* anim = nullptr;
  for (const auto& c : e.wm.topModal()->children()) {
    if (auto* s = dynamic_cast<game::choc2::SpriteWindow*>(c.get())) sprites.push_back(s);
    if (auto* a = dynamic_cast<game::choc2::AnimationWindow*>(c.get())) anim = a;
  }
  REQUIRE(sprites.size() == 3);
  REQUIRE(anim);
  CHECK(anim->fps() == 30);
  // Children come back to front; find each by its image.
  auto byImage = [&](const char* image) -> game::choc2::SpriteWindow* {
    for (auto* s : sprites)
      if (s->detail().find(image) != std::string::npos) return s;
    return nullptr;
  };
  game::choc2::SpriteWindow* marker = byImage("image/marker");
  game::choc2::SpriteWindow* sheet = byImage("sheet.xml");
  game::choc2::SpriteWindow* missing = byImage("missing");
  REQUIRE((marker && sheet && missing));
  // A 32 x 32 image centred on (100, 50).
  CHECK(marker->rect().x == 84);
  CHECK(marker->rect().y == 34);
  CHECK(marker->rect().w == 32);
  CHECK(marker->rect().h == 32);
  // A 32 x 32 frame with registration point (16, 16) at (200, 80), scaled by 2.
  CHECK(sheet->rect().x == 168);
  CHECK(sheet->rect().y == 48);
  CHECK(sheet->rect().w == 64);
  CHECK(sheet->sprite()->rotationDeg == 30);
  CHECK(sheet->sprite()->tint.g == 0);
  CHECK(sheet->sprite()->tint.r == 1);
  CHECK(sheet->detail().find("noimage") == std::string::npos);
  // No such image: an empty rectangle at the point, flagged in the dump.
  CHECK(missing->rect().w == 0);
  CHECK(missing->detail().find("noimage") != std::string::npos);
}

TEST_CASE("Choc2 map vehicle: mirrored by the tangent rule, 40 ms tick") {
  using game::PathSprite;
  CHECK(MapTravel::kTickMs == 40);
  CHECK(PathSprite::mirrorForTangent({1, 0}));         // moving right
  CHECK_FALSE(PathSprite::mirrorForTangent({-1, 0}));  // moving left
  CHECK_FALSE(PathSprite::mirrorForTangent({0, 1}));   // straight down
  CHECK(PathSprite::mirrorForTangent({0, -1}));        // straight up
  CHECK(PathSprite::mirrorForTangent({0.6f, 0.8f}));
  CHECK_FALSE(PathSprite::mirrorForTangent({-0.6f, -0.8f}));
}

TEST_CASE("Choc2 travel sounds: ambient per type, one-shot only when the type changes and never for raft, hiker, camel, canoe") {
  using game::choc2::travelAudio;
  auto t = travelAudio("train", "");
  CHECK(t.ambient == std::optional<std::string>("sfx/saloon_loop.ogg"));
  CHECK(t.oneShot == "train");
  CHECK(t.remember == "train");
  t = travelAudio("train", "train");  // the second week of the same type: ambience again, no one-shot
  CHECK(t.ambient == std::optional<std::string>("sfx/saloon_loop.ogg"));
  CHECK(t.oneShot.empty());
  CHECK(travelAudio("ship", "train").ambient == std::optional<std::string>("sfx/seaside_loop.ogg"));
  CHECK(travelAudio("ship", "train").oneShot == "ship");
  CHECK(travelAudio("raft", "ship").ambient == std::optional<std::string>("sfx/seaside_loop.ogg"));
  CHECK(travelAudio("hiker", "").ambient == std::optional<std::string>("sfx/farm_loop.ogg"));
  CHECK(travelAudio("camel", "").ambient == std::optional<std::string>("sfx/farm_loop.ogg"));
  CHECK(travelAudio("canoe", "").ambient == std::optional<std::string>("sfx/remote_loop.ogg"));
  for (const char* quiet : {"raft", "hiker", "camel", "canoe"}) {
    const auto q = travelAudio(quiet, "");
    CHECK(q.oneShot.empty());
    CHECK(q.remember == quiet);  // the type is still stored; only its one-shot is suppressed
  }
  const auto air = travelAudio("airplane", "train");
  REQUIRE(air.ambient.has_value());
  CHECK(air.ambient->empty());  // silence
  CHECK(air.oneShot == "airplane");
  const auto boat = travelAudio("boat", "");
  CHECK_FALSE(boat.ambient.has_value());  // unchanged
  CHECK(boat.oneShot == "boat");
}

TEST_CASE("Choc2 colour mask: RGB of the picture, alpha = the maximum of the visited masks") {
  gfx::ImageData colour;
  colour.width = 4, colour.height = 1;
  colour.rgba = {10, 20, 30, 255, 10, 20, 30, 255, 10, 20, 30, 255, 10, 20, 30, 255};
  auto mask = [](std::initializer_list<int> alphas) {
    gfx::ImageData m;
    m.width = static_cast<int>(alphas.size()), m.height = 1;
    for (int a : alphas) m.rgba.insert(m.rgba.end(), {255, 255, 255, static_cast<std::uint8_t>(a)});
    return m;
  };
  const gfx::ImageData a = mask({255, 100, 0, 0}), b = mask({0, 200, 0, 50});
  game::choc2::composeColorMask(colour, {&a, &b});
  CHECK(colour.rgba[3] == 255);
  CHECK(colour.rgba[7] == 200);  // overlapping masks: the maximum
  CHECK(colour.rgba[11] == 0);
  CHECK(colour.rgba[15] == 50);
  CHECK(colour.rgba[0] == 10);  // RGB untouched
  CHECK(colour.rgba[2] == 30);
  game::choc2::composeColorMask(colour, {});  // no visited port: fully transparent
  CHECK(colour.rgba[3] == 0);
  // A smaller mask is aligned at the top-left and leaves the rest transparent.
  const gfx::ImageData small = mask({77});
  game::choc2::composeColorMask(colour, {&small});
  CHECK(colour.rgba[3] == 77);
  CHECK(colour.rgba[7] == 0);
}
