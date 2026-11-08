#include <doctest/doctest.h>
#include "platform/Png.h"

#include <filesystem>
#include <vector>

#include "ScriptHarness.h"
#include "app/ScriptDriver.h"
#include "assets/AssetFS.h"
#include "gfx/RecordingRenderer.h"
#include "gfx/TextureCache.h"
#include "script/LuaStyle.h"
#include "script/ScriptHost.h"
#include "sdk/Sprite.h"
#include "ui/Style.h"

using namespace choco;
using sdk::Sprite;
using sdk::SpriteSpec;
namespace fs = std::filesystem;

namespace {

struct Pics {
  Pics() {
    dir = fs::temp_directory_path() / "choco_sprite_pics";
    fs::remove_all(dir);
    fs::create_directories(dir / "image");
    for (const char *n : {"a", "b", "c", "d", "e", "m"}) {
      std::vector<unsigned char> px(static_cast<std::size_t>(20) * 10 * 4, 255);
      REQUIRE(choco::platform::writePng(
                  (dir / "image" / (std::string(n) + ".png")).string(), 20, 10, px.data()));
    }
    afs = std::make_unique<assets::AssetFS>(dir.string(), "");
    cache = std::make_unique<gfx::TextureCache>(*afs, nullptr);
  }
  std::shared_ptr<gfx::Texture> tex(const char *n) {
    return cache->load(std::string("image/") + n);
  }
  std::unique_ptr<Sprite> sprite(const char *n, int layer, float x = 0,
                                 float y = 0) {
    auto s = std::make_unique<Sprite>(layer, n);
    s->texture = tex(n);
    s->pos = {x, y};
    return s;
  }
  fs::path dir;
  std::unique_ptr<assets::AssetFS> afs;
  std::unique_ptr<gfx::TextureCache> cache;
};

std::vector<std::string> drawOrder(const Sprite &root,
                                   gfx::RecordingRenderer &r) {
  r.clear();
  root.draw(r, SpriteSpec{});
  std::vector<std::string> names;
  for (const auto &c : r.calls())
    if (c.kind == gfx::RecordingRenderer::Call::Kind::DrawTexture) {
      std::string p = c.texture->path(); // "image/x.png"
      names.push_back(p.substr(6, p.size() - 10));
    }
  return names;
}

} // namespace

TEST_CASE("Sprite draw order: negative-layer children first, then the parent's "
          "own texture, then the rest, stably") {
  Pics pics;
  auto root = pics.sprite("a", 0);
  root->addChild(pics.sprite("b", 5));
  root->addChild(pics.sprite("c", -1));
  root->addChild(pics.sprite("d", -1));
  root->addChild(pics.sprite("e", 5));
  gfx::RecordingRenderer r;
  CHECK(drawOrder(*root, r) ==
        std::vector<std::string>{"c", "d", "a", "b", "e"});

  // Lower layers come before higher ones whatever the insertion order, and
  // equal layers keep it.
  auto top = pics.sprite("a", 0);
  top->addChild(pics.sprite("e", 30));
  top->addChild(pics.sprite("b", 10));
  top->addChild(pics.sprite("d", 30));
  top->addChild(pics.sprite("c", 10));
  CHECK(drawOrder(*top, r) ==
        std::vector<std::string>{"a", "b", "c", "e", "d"});

  // setLayer re-sorts only when asked, so a sprite can change layer
  // mid-iteration.
  Sprite *e = top->children().back()->debugName == "d"
                  ? top->children()[2].get()
                  : nullptr;
  REQUIRE(e);
  CHECK(e->debugName == "e");
  e->setLayer(-2);
  CHECK(top->children()[2]->debugName == "e");
  top->resortDirty();
  CHECK(top->children()[0]->debugName == "e");
  CHECK(drawOrder(*top, r) ==
        std::vector<std::string>{"e", "a", "b", "c", "d"});
}

TEST_CASE("Sprite draw: anchors, transforms, alpha and visibility") {
  Pics pics;
  auto root = pics.sprite("a", 0, 100, 50); // 20x10, centre anchor
  root->alpha = 0.5f;
  root->scale = 2;
  root->tint = {1, 0.5f, 0.25f, 1};
  auto &child = root->addChild(pics.sprite("b", 1, 10, 0));
  child.topLeft = true;
  child.alpha = 0.5f;
  auto &hidden = root->addChild(pics.sprite("c", 2));
  hidden.visible = false;
  hidden.addChild(pics.sprite("d", 0));
  auto &gone = root->addChild(pics.sprite("e", 3));
  gone.removed = true;

  gfx::RecordingRenderer r;
  root->draw(r, SpriteSpec{{10, 20}, 1, 0, 1});
  REQUIRE(r.count(gfx::RecordingRenderer::Call::Kind::DrawTexture) == 2);
  const auto &a = r.calls()[0];
  CHECK(a.dst == gfx::RectF{10 + 100 - 20, 20 + 50 - 10, 40,
                            20}); // pos - anchor*scale, size*scale
  CHECK(a.color.a == doctest::Approx(0.5f));
  CHECK(a.color.g == doctest::Approx(0.5f));
  const auto &b = r.calls()[1];
  // The child sits at (10,0) in the parent's scaled space and is anchored at
  // its own top-left corner.
  CHECK(b.dst.x == doctest::Approx(10 + 100 + 10 * 2));
  CHECK(b.dst.y == doctest::Approx(20 + 50));
  CHECK(b.dst.w == doctest::Approx(40));
  CHECK(b.color.a == doctest::Approx(0.25f)); // alpha multiplies down the tree
  CHECK(b.color.g == doctest::Approx(1.0f));  // tint does not

  // Sprite hit: left/top inclusive, right/bottom exclusive.
  Sprite s(0, "h");
  s.texture = pics.tex("a");
  s.topLeft = true;
  s.pos = {10, 10};
  CHECK(s.hit({10, 10}));
  CHECK(s.hit({29.9f, 19.9f}));
  CHECK_FALSE(s.hit({30, 15}));
  CHECK_FALSE(s.hit({15, 20}));
  CHECK_FALSE(s.hit({9.9f, 15}));
  s.visible = false;
  CHECK_FALSE(s.hit({15, 15}));
}

TEST_CASE("Sprite hitOpaque (Chocolatier 2): alpha >= 192 at the point, "
          "children included, scale and mirror honoured") {
  Pics pics;
  // 20x10: columns 0..9 transparent, columns 10..19 opaque except row 9, which
  // has alpha 191.
  std::vector<unsigned char> px(static_cast<std::size_t>(20) * 10 * 4, 0);
  for (int y = 0; y < 10; ++y)
    for (int x = 10; x < 20; ++x) {
      unsigned char *p =
          &px[(static_cast<std::size_t>(y) * 20 + static_cast<std::size_t>(x)) *
              4];
      p[0] = p[1] = p[2] = 255;
      p[3] = y == 9 ? 191 : 255;
    }
  REQUIRE(choco::platform::writePng((pics.dir / "image" / "m.png").string(), 20, 10, px.data()));
  Sprite s(0, "m");
  s.texture = pics.tex("m");
  s.topLeft = true;
  s.pos = {100, 100};
  const gfx::TextureCache &tc = *pics.cache;
  CHECK(s.hit(
      {102, 102})); // the plain rectangle test hits the transparent corner...
  CHECK_FALSE(s.hitOpaque({102, 102}, tc, 192)); // ...the alpha test does not
  CHECK(s.hitOpaque({115, 102}, tc, 192));       // opaque pixel
  CHECK_FALSE(
      s.hitOpaque({115, 109}, tc, 192)); // alpha 191 is below the threshold
  CHECK(s.hitOpaque({115, 109}, tc, 191));
  CHECK_FALSE(s.hitOpaque({130, 102}, tc, 192)); // outside the rectangle
  s.alpha = 0.7f; // a sprite drawn below alpha 0.75 can never be hit
  CHECK_FALSE(s.hitOpaque({115, 102}, tc, 192));
  s.alpha = 1;
  s.hflip = true; // mirrored about the anchor (as Sprite::draw): the image
                  // spans x 80..100 and its opaque half is on the left
  CHECK(s.hitOpaque({82, 102}, tc, 192));
  CHECK_FALSE(s.hitOpaque({95, 102}, tc, 192));
  CHECK_FALSE(s.hitOpaque({115, 102}, tc, 192));
  s.hflip = false;
  s.scale = 2; // the image is 40x20 from (100, 100): the opaque half starts at
               // x = 120
  CHECK_FALSE(s.hitOpaque({115, 102}, tc, 192));
  CHECK(s.hitOpaque({125, 102}, tc, 192));
  s.scale = 1;
  // A child's opaque pixels count even where the parent is transparent, and
  // widen the area outside the parent's rectangle.
  auto &child = s.addChild(pics.sprite(
      "a", 1, 0, 0)); // 20x10 opaque white, centred on the parent's position
  CHECK(s.hitOpaque({95, 100}, tc, 192));
  child.visible = false;
  CHECK_FALSE(s.hitOpaque({95, 100}, tc, 192));
}

TEST_CASE("Sprite sheet frames draw the frame rectangle anchored at its "
          "registration point") {
  Pics pics;
  auto sheet = std::make_shared<gfx::SpriteSheet>();
  sheet->image = pics.tex("a");
  sheet->frames = {{{0, 0, 10, 10}, {2, 3}, {}}, {{10, 0, 8, 6}, {4, 5}, {}}};
  Sprite s(0, "anim");
  s.sheet = sheet;
  s.pos = {50, 60};
  s.frame = 1;
  gfx::RecordingRenderer r;
  s.draw(r, SpriteSpec{});
  REQUIRE(r.calls().size() == 1);
  CHECK(r.calls()[0].src == gfx::RectF{10, 0, 8, 6});
  CHECK(r.calls()[0].dst == gfx::RectF{46, 55, 8, 6});
  CHECK(s.rect() == gfx::RectF{46, 55, 8, 6});
  s.frame = 99; // clamped
  CHECK(s.rect() == gfx::RectF{46, 55, 8, 6});
}

TEST_CASE("Sprite counting and dump lines") {
  Pics pics;
  auto root = pics.sprite("a", 0);
  root->debugName = "bg";
  auto &b1 = root->addChild(pics.sprite("b", 770));
  b1.debugName = "building:zur_market";
  auto &b2 = root->addChild(pics.sprite("c", 780));
  b2.debugName = "building:zur_hall";
  auto &hl = b2.addChild(pics.sprite("d", -1));
  hl.debugName = "highlight:zur_hall";
  hl.visible = false;
  CHECK(root->countVisible("building:") == 2);
  CHECK(root->countVisible("highlight:") == 0);
  hl.visible = true;
  CHECK(root->countVisible("highlight:") == 1);
  CHECK(root->countVisible("") == 4);
  b2.visible = false; // hides its subtree
  CHECK(root->countVisible("building:") == 1);
  CHECK(root->countVisible("highlight:") == 0);
  b2.visible = true;
  b1.removed = true;
  CHECK(root->countVisible("building:") == 1);

  b1.pos = {12.34f, -0.04f};
  b1.alpha = 0.25f;
  CHECK(b1.dumpLine() ==
        "sprite building:zur_market layer=770 pos=12.3,0.0 scale=1.0 frame=0/1 "
        "alpha=0.2 tint=1.0,1.0,1.0,1.0 removed");
  hl.visible = false;
  std::string out;
  root->dump(out, 1);
  CHECK(out.find("  sprite bg layer=0") == 0);
  CHECK(out.find("      sprite highlight:zur_hall layer=-1 pos=0.0,0.0 "
                 "scale=1.0 frame=0/1 alpha=1.0 tint=1.0,1.0,1.0,1.0 hidden") !=
        std::string::npos);
}

TEST_CASE("Nested style views on a MapStyle") {
  auto port = std::make_shared<ui::MapStyle>();
  port->set("name", std::string("zurich")).set("x", 12.0);
  auto sprites = std::make_shared<ui::MapStyle>();
  auto sp = std::make_shared<ui::MapStyle>();
  sp->set("image", std::string("wave"));
  sprites->append(sp).append(1.5).append(std::string("s"));
  port->set("sprites", sprites);
  ui::MapStyle top;
  top.set("port", port);
  auto p = top.table("port");
  REQUIRE(p);
  CHECK(p->string("name") == "zurich");
  auto s = p->table("sprites");
  REQUIRE(s);
  CHECK(s->size() == 3);
  CHECK(s->element(1)->string("image") == "wave");
  CHECK(s->element(2) == nullptr); // not a table
  CHECK(s->numberAt(2) == 1.5);
  CHECK(s->stringAt(3) == "s");
  CHECK_FALSE(s->numberAt(3).has_value());
  CHECK(top.table("missing") == nullptr);
  CHECK(top.table("port")->table("name") == nullptr);
  int n = 0;
  s->forEachTable([&](const ui::Style &t) {
    ++n;
    CHECK(t.string("image") == "wave");
  });
  CHECK(n == 1);
}

namespace {

struct Env : test::ScriptEnv {
  Env() : ScriptEnv(std::string(CHOCO_TEST_ASSETS) + "/ui/modal"), host(ctx) {
    booted = host.boot();
  }
  script::ScriptHost host;
  bool booted = false;
};

} // namespace

TEST_CASE(
    "Nested style views on a Lua table, through LuaStyle and withGlobal") {
  Env e;
  REQUIRE(e.booted);
  REQUIRE(e.run("T = { name = 'zurich', n = 5, ports = { a = { x = 1, y = 2 }, "
                "b = { x = 3, y = 4 }, skip = 7 }, "
                "list = { 10, 'two', { k = 'v' }, 40 }, empty = {}, "
                "sprites = setmetatable({}, { __index = function(t, k) if k == "
                "'meta' then return { z = 9 } end end }) }; "
                "gCurrentDefaultStyle = { fallback = 'dflt', parent = nil }") ==
          0);
  int calls = 0;
  e.host.withGlobal("T", [&](const ui::Style *t) {
    ++calls;
    REQUIRE(t);
    CHECK(t->string("name") == "zurich");
    CHECK(t->string("fallback") ==
          "dflt"); // the default-style chain applies at the top level...
    auto ports = t->table("ports");
    REQUIRE(ports);
    CHECK_FALSE(
        ports->string("fallback").has_value()); // ...and not in sub-tables
    int tables = 0;
    double sumX = 0;
    ports->forEachTable([&](const ui::Style &p) {
      ++tables;
      sumX += p.numberOr("x", 0);
    });
    CHECK(tables == 2); // the number 7 is skipped
    CHECK(sumX == 4);
    auto list = t->table("list");
    REQUIRE(list);
    CHECK(list->size() == 4);
    CHECK(list->numberAt(1) == 10);
    CHECK(list->stringAt(2) == "two");
    CHECK(list->stringAt(1) == "10");
    CHECK(list->element(3)->string("k") == "v");
    CHECK(list->element(1) == nullptr);
    CHECK(list->element(9) == nullptr);
    CHECK(t->table("empty")->size() == 0);
    CHECK(t->table("name") == nullptr);
    CHECK(t->table("nothing") == nullptr);
    // Metatables apply to sub-tables.
    auto sprites = t->table("sprites");
    REQUIRE(sprites);
    CHECK(sprites->table("meta")->numberOr("z", 0) == 9);
    // Sub-views nest and unwind in order, leaving the stack as it was.
    {
      auto a = ports->table("a");
      REQUIRE(a);
      CHECK(a->number("y") == 2);
    }
  });
  CHECK(calls == 1);
  const int top = lua_gettop(e.lua.L());
  e.host.withGlobal("name_not_a_table", [&](const ui::Style *t) {
    CHECK(t == nullptr);
    ++calls;
  });
  e.host.withGlobal("T", [&](const ui::Style *t) {
    CHECK(t != nullptr);
    ++calls;
  });
  CHECK(calls == 3);
  CHECK(lua_gettop(e.lua.L()) == top);
}

namespace {

struct NullInput : platform::InputInjector {
  void injectMouseMove(float, float) override {}
  void injectMouseButton(float, float, bool) override {}
  void injectWindowMouseMove(float, float) override {}
  void injectWindowMouseButton(float, float, bool) override {}
  bool injectKey(const std::string &) override { return true; }
  void injectText(const std::string &) override {}
  void injectQuit() override {}
};

struct FakeWindows : ui::WindowQuery {
  bool isShown(std::string_view) const override { return false; }
  int countSprites(std::string_view prefix) const override {
    return prefix == "dot" ? dots : 0;
  }
  std::string dumpWindows() const override { return "[windows] fake\n"; }
  int dots = 0;
};

} // namespace

TEST_CASE("Harness: expect-sprite parses, waits for the count and times out; "
          "dump prints the window dump") {
  auto p = app::parseScript(
      "expect-sprite dot\nexpect-sprite vehicle 1\nexpect-sprite dot 5 "
      "2000\ndump\nexpect-sprite\nexpect-sprite a 1 2 3\ndump x\n");
  REQUIRE(p.commands.size() == 4);
  CHECK(p.errors.size() == 3);
  CHECK(p.commands[0].text == "dot");
  CHECK(p.commands[0].count == 1);
  CHECK(p.commands[0].ms == 5000);
  CHECK(p.commands[2].count == 5);
  CHECK(p.commands[2].ms == 2000);
  CHECK(p.commands[3].kind == app::ScriptCommand::Kind::Dump);

  NullInput in;
  FakeWindows w;
  auto parsed = app::parseScript("expect-sprite dot 3 1000\ndump\n");
  app::ScriptDriver d(parsed.commands, in);
  d.setWindowQuery(&w);
  d.update(0);
  CHECK(d.expectPending());
  CHECK_FALSE(d.finished());
  w.dots = 2;
  d.update(100);
  CHECK(d.expectPending());
  w.dots = 3;
  d.update(200);
  CHECK_FALSE(d.expectPending());
  d.update(300);
  CHECK(d.finished());
  CHECK_FALSE(d.failed());

  auto failing = app::parseScript("expect-sprite dot 3 500\n");
  app::ScriptDriver f(failing.commands, in);
  w.dots = 0;
  f.setWindowQuery(&w);
  f.update(0);
  f.update(400);
  CHECK_FALSE(f.failed());
  f.update(500);
  CHECK(f.failed());
  CHECK_FALSE(f.expectPending());
}
