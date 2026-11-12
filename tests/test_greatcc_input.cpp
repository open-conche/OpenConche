// The Great Chocolate Chase, slice 8 (docs/design/greatcc.md section 6): scene input. Synthetic fixtures only: small PNGs built
// here (an opaque square, a ring with a transparent middle, an invisible image), plain textures on probe sprites that log
// every handler, and the manager's mouse calls.
#include <doctest/doctest.h>

#include <cmath>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "assets/AssetFS.h"
#include "games/Registry.h"
#include "games/greatcc/GreatCCWorld.h"
#include "games/greatcc/scene/Effects.h"
#include "games/greatcc/scene/Mat3.h"
#include "games/greatcc/scene/SpriteManager.h"
#include "games/greatcc/scene/TextSprite.h"
#include "games/greatcc/scene/VObject.h"
#include "games/greatcc/scene/VSprite.h"
#include "games/greatcc/scene/VSpriteWindow.h"
#include "gfx/RecordingRenderer.h"
#include "gfx/SheetCache.h"
#include "gfx/TextureCache.h"
#include "platform/Png.h"
#include "ScriptHarness.h"
#include "ui/Style.h"

using namespace choco;
using namespace choco::games::greatcc;
namespace fs = std::filesystem;

namespace {

using Log = std::vector<std::string>;

// A sprite that records every handler it gets, with the point where the handler takes one.
class Probe : public VSprite {
 public:
  Probe(script::ScriptContext& ctx, std::string name, Log& log) : VSprite(ctx, std::move(name)), log_(log) {}
  void onMouseEnter() override { note("Enter"); }
  void onMouseLeave() override { note("Leave"); }
  void onMouseDown(gfx::PointI p) override { note("Down", &p); }
  void onMouseMove(gfx::PointI p) override { note("Move", &p); }
  void onMouseUp(gfx::PointI p) override { note("Up", &p); }
  void onMouseDrag(gfx::PointI p) override { note("Drag", &p); }
  void onClick() override {
    note("Click");
    if (onClickFn) onClickFn();
  }
  void onMouseHover(double ms) override { hoverCalls.push_back(ms); }
  std::function<void()> onClickFn, onEnterFn, onDownFn;
  std::vector<double> hoverCalls;

 private:
  void note(const char* what, const gfx::PointI* p = nullptr) {
    std::string s = name() + ":" + what;
    if (p) s += "(" + std::to_string(p->x) + "," + std::to_string(p->y) + ")";
    log_.push_back(s);
    if (std::string(what) == "Enter" && onEnterFn) onEnterFn();
    if (std::string(what) == "Down" && onDownFn) onDownFn();
  }
  Log& log_;
};

class Spy : public SpriteListener {
 public:
  Spy(std::string tag, Log& log) : tag_(std::move(tag)), log_(log) {}
  void onMouseEnter(VSprite& s) override { log_.push_back(tag_ + ":Enter " + s.name()); }
  void onMouseLeave(VSprite& s) override { log_.push_back(tag_ + ":Leave " + s.name()); }
  void onMouseDown(VSprite& s, gfx::PointI) override { log_.push_back(tag_ + ":Down " + s.name()); }
  void onMouseMove(VSprite& s, gfx::PointI) override { log_.push_back(tag_ + ":Move " + s.name()); }
  void onMouseUp(VSprite& s, gfx::PointI) override { log_.push_back(tag_ + ":Up " + s.name()); }
  void onMouseDrag(VSprite& s, gfx::PointI) override { log_.push_back(tag_ + ":Drag " + s.name()); }
  void onClick(VSprite& s) override { log_.push_back(tag_ + ":Click " + s.name()); }

 private:
  std::string tag_;
  Log& log_;
};

struct InputAssets {
  InputAssets() {
    dir = fs::temp_directory_path() / "choco_greatcc_input";
    fs::remove_all(dir);
    fs::create_directories(dir);
    auto write = [&](const char* name, int w, int h, const std::function<unsigned char(int, int)>& alpha) {
      std::vector<unsigned char> px(static_cast<std::size_t>(w * h * 4), 255);
      for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) px[static_cast<std::size_t>((y * w + x) * 4 + 3)] = alpha(x, y);
      REQUIRE(platform::writePng((dir / name).string(), w, h, px.data()));
    };
    write("sq.png", 20, 20, [](int, int) { return 255; });
    write("ring.png", 20, 20, [](int x, int y) { return (x >= 6 && x < 14 && y >= 6 && y < 14) ? 0 : 255; });
    write("ghost.png", 20, 20, [](int, int) { return 0; });
    write("dim.png", 20, 20, [](int, int) { return 1; });
    write("half.png", 20, 20, [](int x, int) { return x >= 10 ? 0 : 255; });
    afs = std::make_unique<assets::AssetFS>(dir.string(), "");
    textures = std::make_unique<gfx::TextureCache>(*afs, nullptr);
    sheets = std::make_unique<gfx::SheetCache>(*afs, *textures);
  }
  fs::path dir;
  std::unique_ptr<assets::AssetFS> afs;
  std::unique_ptr<gfx::TextureCache> textures;
  std::unique_ptr<gfx::SheetCache> sheets;
};

struct Input : InputAssets, test::ScriptEnv {
  Input() : InputAssets(), ScriptEnv(std::string(CHOCO_TEST_ASSETS) + "/greatcc/state", scratch()) {
    ctx.module = games::findModule("greatcc");
    ctx.module->attach(ctx);
    wm.setSheets(sheets.get());
    wm.setTextures(textures.get());
    wm.pushModal("m");
  }
  static std::string scratch() {
    const fs::path p = fs::temp_directory_path() / "choco_greatcc_input_user";
    std::error_code ec;
    fs::remove_all(p, ec);
    fs::create_directories(p, ec);
    return p.string();
  }
  // A probe showing `image`, centred on (x, y).
  std::shared_ptr<Probe> probe(const std::string& name, const char* image, float x, float y) {
    auto s = std::make_shared<Probe>(ctx, name, log);
    s->setPlainTexture(textures->load(image));
    s->setPosition({x, y});
    return s;
  }
  // The pointer moves and one frame passes: the hit test runs.
  void move(int x, int y) {
    mgr.onMouseMove({x, y});
    frame();
  }
  void frame(double dt = 16) {
    now += dt;
    mgr.setNow(now);
    mgr.updateObjects();
  }
  Log take() { return std::exchange(log, {}); }
  SpriteManager mgr{ctx};
  Log log;
  double now = 1000;
};

}  // namespace

TEST_CASE("Mat3 inverse maps a transformed point back, a singular matrix has none") {
  const Mat3 m = Mat3::rotationAbout(3, 4, 0.7f) * Mat3::scaling(2, 3) * Mat3::translation(5, -2);
  const gfx::PointF p = m.apply({7, 1});
  gfx::PointF back;
  REQUIRE(m.applyInverse(p, back));
  CHECK(std::fabs(back.x - 7) < 1e-3f);
  CHECK(std::fabs(back.y - 1) < 1e-3f);
  CHECK_FALSE(Mat3::scaling(0, 1).applyInverse({1, 1}, back));
}

TEST_CASE("input: the hit test walks the top level back to front by the drawn order; the first valid hit is the hover target") {
  Input e;
  auto lo = e.probe("lo", "sq", 100, 100);
  auto hi = e.probe("hi", "sq", 105, 100);
  hi->setDepth(5);
  e.mgr.addObject(hi);  // added first, but drawn (and hit) last
  e.mgr.addObject(lo);
  gfx::RecordingRenderer rr;
  e.mgr.draw(rr, {0, 0});
  e.move(102, 100);
  CHECK(e.mgr.hover() == hi.get());
  e.take();
  // where only `lo` reaches, it gets the event
  e.move(91, 100);
  CHECK(e.mgr.hover() == lo.get());
  // no sprite under the point: nothing is hovered
  e.move(300, 300);
  CHECK(e.mgr.hover() == nullptr);
  // a sprite with `mouse` off is skipped, so the one behind it is hit
  hi->setMouse(false);
  e.move(102, 100);
  CHECK(e.mgr.hover() == lo.get());
  // an invisible one too
  hi->setMouse(true);
  hi->setVisible(false);
  e.mgr.recalcMouse();
  e.frame();
  CHECK(e.mgr.hover() == lo.get());
}

TEST_CASE("input: Enter, Leave and Move arrive in the original order with the original points") {
  Input e;
  auto a = e.probe("a", "sq", 100, 100);
  auto b = e.probe("b", "sq", 140, 100);
  e.mgr.addObject(a);
  e.mgr.addObject(b);
  e.move(95, 103);
  // Enter and Leave carry (0,0); Move is relative to the sprite's own position (truncated), not the parent's origin
  CHECK(e.take() == Log{"a:Enter", "a:Move(-5,3)"});
  e.move(97, 104);
  CHECK(e.take() == Log{"a:Move(-3,4)"});  // the same target: only a Move
  e.move(135, 104);
  CHECK(e.take() == Log{"a:Leave", "b:Enter", "b:Move(-5,4)"});
  e.move(500, 500);
  CHECK(e.take() == Log{"b:Leave"});
  CHECK(e.mgr.hover() == nullptr);
  e.move(501, 500);
  CHECK(e.take().empty());
  // the flags follow the handlers: set after Enter, cleared after Leave
  e.move(100, 100);
  CHECK(a->hovered());
  e.move(500, 500);
  CHECK_FALSE(a->hovered());
}

TEST_CASE("input: a mouse move only queues the hit test, and answers whether a target exists now") {
  Input e;
  auto a = e.probe("a", "sq", 100, 100);
  e.mgr.addObject(a);
  CHECK_FALSE(e.mgr.onMouseMove({100, 100}));  // the target appears at the next update
  CHECK(e.log.empty());
  e.frame();
  CHECK(e.mgr.hover() == a.get());
  CHECK(e.mgr.onMouseMove({600, 500}));  // still on the old target until the update runs
  e.frame();
  CHECK_FALSE(e.mgr.onMouseMove({601, 500}));
}

TEST_CASE("input: Down captures, a move off the sprite drags it (hovered turns false), Up always ends the capture") {
  Input e;
  auto a = e.probe("a", "sq", 100, 100);
  auto b = e.probe("b", "sq", 200, 100);
  e.mgr.addObject(a);
  e.mgr.addObject(b);
  CHECK_FALSE(e.mgr.onMouseDown({100, 100}));  // nothing hovered yet
  e.move(100, 100);
  e.take();
  CHECK(e.mgr.onMouseDown({100, 100}));
  CHECK(e.mgr.pressedSprite() == a.get());
  CHECK(a->pressed());
  CHECK(e.take() == Log{"a:Down(0,0)"});
  // while captured a move is a Drag, nothing else; the hit test is not run, so no Leave and no Enter on b
  CHECK(e.mgr.onMouseMove({205, 100}));
  CHECK_FALSE(a->hovered());
  CHECK(e.take() == Log{"a:Drag(105,0)"});
  e.frame();
  CHECK(e.take().empty());
  CHECK(e.mgr.onMouseMove({102, 101}));
  CHECK(a->hovered());
  CHECK(e.take() == Log{"a:Drag(2,1)"});
  // Up goes to the captured sprite wherever the pointer is; not clickable: no Click
  CHECK(e.mgr.onMouseUp({300, 300}));
  CHECK(e.take() == Log{"a:Up(200,200)"});
  CHECK(e.mgr.pressedSprite() == nullptr);
  CHECK_FALSE(a->pressed());
  CHECK_FALSE(e.mgr.onMouseUp({300, 300}));  // nothing captured now
  CHECK(e.take().empty());
}

TEST_CASE("input: Up on a clickable sprite sends Click at once, between its own Up and the listeners'; no inside test") {
  Input e;
  auto a = e.probe("a", "sq", 100, 100);
  a->setClickable(true);
  a->addListener(std::make_shared<Spy>("L", e.log));
  e.mgr.addObject(a);
  e.frame();  // the listener joins at this update
  e.move(100, 100);
  e.take();
  e.mgr.onMouseDown({100, 100});
  e.mgr.onMouseMove({400, 400});
  e.take();
  e.mgr.onMouseUp({400, 400});  // released far away: still a click
  CHECK(e.take() == Log{"a:Up(300,300)", "a:Click", "L:Click a", "L:Up a"});
}

TEST_CASE("input: listeners hear every mouse event after the sprite's own handler; dead ones and late ones do not") {
  Input e;
  auto a = e.probe("a", "sq", 100, 100);
  e.mgr.addObject(a);
  auto spy = std::make_shared<Spy>("S", e.log);
  a->addListener(spy);
  CHECK(a->listenerCount() == 0);
  e.move(100, 100);  // the update that runs the hit test also joins the listener, after the events
  CHECK(e.take() == Log{"a:Enter", "a:Move(0,0)"});
  e.move(101, 100);
  CHECK(e.take() == Log{"a:Move(1,0)", "S:Move a"});
  e.mgr.onMouseDown({101, 100});
  e.mgr.onMouseMove({102, 100});
  e.mgr.onMouseUp({102, 100});
  CHECK(e.take() == Log{"a:Down(1,0)", "S:Down a", "a:Drag(2,0)", "S:Drag a", "a:Up(2,0)", "S:Up a"});
  e.move(500, 500);
  CHECK(e.take() == Log{"a:Leave", "S:Leave a"});
  spy->dead = true;
  e.move(100, 100);
  CHECK(e.take() == Log{"a:Enter", "a:Move(0,0)"});
  // a listener added by a handler waits for the next update; adding twice keeps one
  auto spy2 = std::make_shared<Spy>("T", e.log);
  a->addListener(spy2);
  a->addListener(spy2);
  e.frame();
  CHECK(a->listenerCount() == 1);
  // the last call in a frame wins: add then remove leaves it out, remove then add keeps it
  auto spy3 = std::make_shared<Spy>("U", e.log);
  a->addListener(spy3);
  a->removeListener(spy3);
  e.frame();
  CHECK(a->listenerCount() == 1);
  a->removeListener(spy2);
  a->addListener(spy2);
  e.frame();
  CHECK(a->listenerCount() == 1);
}

TEST_CASE("input: a leaf hands the event to its container, climbing while the result is a leaf; other children get it") {
  Input e;
  auto box = e.probe("box", "sq", 100, 100);
  auto kid = e.probe("kid", "sq", 0, 0);
  auto grand = e.probe("grand", "sq", 0, 0);
  box->addChild(kid);
  kid->addChild(grand);
  e.mgr.addObject(box);
  e.move(100, 100);
  CHECK(e.mgr.hover() == grand.get());  // the innermost child is above its parents
  grand->setLeaf(true);
  e.mgr.recalcMouse();
  e.frame();
  CHECK(e.mgr.hover() == kid.get());  // a leaf: its container gets the event
  kid->setLeaf(true);
  e.mgr.recalcMouse();
  e.frame();
  CHECK(e.mgr.hover() == box.get());  // and the container is a leaf too: one more level up
  box->setLeaf(true);  // a top-level leaf has nowhere to go: it keeps the event
  e.mgr.recalcMouse();
  e.frame();
  CHECK(e.mgr.hover() == box.get());
  // `leaf` also lets a sprite with `mouse` off take part in hit testing; `mouse` off alone does not
  auto quiet = e.probe("quiet", "sq", 300, 300);
  quiet->setMouse(false);
  e.mgr.addObject(quiet);
  e.move(300, 300);
  CHECK(e.mgr.hover() == nullptr);
  quiet->setLeaf(true);
  e.mgr.recalcMouse();
  CHECK(quiet->hitTest({300, 300}, DrawSpec{}, 1, 0));
  quiet->setLeaf(false);
  CHECK_FALSE(quiet->hitTest({300, 300}, DrawSpec{}, 1, 0));
  // a top-level sprite with `mouse` off is never asked, its children with it; below the top level a `mouse` off container is
  // transparent to the hit test and its children can be hit
  auto lid = e.probe("lid", "sq", 0, 0);
  quiet->addChild(lid);
  e.move(300, 301);
  CHECK(e.mgr.hover() == nullptr);
  e.mgr.removeObject(*quiet);
  auto holder = e.probe("holder", "sq", 300, 300);
  holder->addChild(quiet);
  quiet->setPosition({0, 0});
  e.mgr.addObject(holder);
  e.move(300, 301);
  CHECK(e.mgr.hover() == lid.get());
}

TEST_CASE("input: children at layer >= 0 are tried back to front, then the sprite's own image, then the children below 0") {
  Input e;
  auto top = e.probe("top", "sq", 100, 100);
  auto front = e.probe("front", "sq", 0, 0);
  auto back = e.probe("back", "sq", 0, 0);
  auto lower = e.probe("lower", "sq", 0, 0);
  front->setLayer(2);
  back->setLayer(1);
  lower->setLayer(-1);
  top->addChild(back);
  top->addChild(front);
  top->addChild(lower);
  e.mgr.addObject(top);
  e.move(100, 100);
  CHECK(e.mgr.hover() == front.get());  // the highest layer first
  front->setVisible(false);
  e.mgr.recalcMouse();
  e.frame();
  CHECK(e.mgr.hover() == back.get());
  back->setVisible(false);
  e.mgr.recalcMouse();
  e.frame();
  CHECK(e.mgr.hover() == top.get());  // then the sprite itself, before the child below layer 0
  top->setSelfVisible(false);
  e.mgr.recalcMouse();
  e.frame();
  CHECK(e.mgr.hover() == lower.get());
}

TEST_CASE("input: the hit is pixel accurate with threshold 1 (alpha as drawn), the rectangle alone with a negative one") {
  Input e;
  auto ring = e.probe("ring", "ring", 100, 100);
  auto floor = e.probe("floor", "sq", 100, 100);
  floor->setDepth(-1);
  e.mgr.addObject(ring);
  e.mgr.addObject(floor);
  gfx::RecordingRenderer rr;
  e.mgr.draw(rr, {0, 0});
  e.move(92, 100);  // on the ring's opaque edge
  CHECK(e.mgr.hover() == ring.get());
  e.move(100, 100);  // the transparent middle: the sprite behind is hit
  CHECK(e.mgr.hover() == floor.get());
  CHECK(ring->hitTest({100, 100}, DrawSpec{}, -1, 0));  // the rectangle alone
  CHECK_FALSE(ring->hitTest({100, 100}, DrawSpec{}, 1, 0));
  CHECK_FALSE(ring->hitTest({50, 50}, DrawSpec{}, -1, 0));  // outside the rectangle
  // alpha 0 anywhere is never a hit; alpha 1 of 255 is (threshold 1)
  auto ghost = e.probe("ghost", "ghost", 300, 100);
  auto dim = e.probe("dim", "dim", 340, 100);
  e.mgr.addObject(ghost);
  e.mgr.addObject(dim);
  e.move(300, 100);
  CHECK(e.mgr.hover() == nullptr);
  e.move(340, 100);
  CHECK(e.mgr.hover() == dim.get());
  // the sprite's own alpha and tint count (what is drawn): a fully faded sprite cannot be hit
  dim->setAlpha(0);
  e.mgr.recalcMouse();
  e.frame();
  CHECK(e.mgr.hover() == nullptr);
}

TEST_CASE("input: a pixel test follows the flips and scale of the sprite") {
  Input e;
  // an asymmetric image: only the left half is opaque
  auto h = e.probe("h", "half", 100, 100);
  e.mgr.addObject(h);
  CHECK(h->hitTest({93, 100}, DrawSpec{}, 1, 0));  // left of the middle
  CHECK_FALSE(h->hitTest({107, 100}, DrawSpec{}, 1, 0));
  h->setFlip(true, false);  // mirrored: the opaque half is on the right
  CHECK_FALSE(h->hitTest({93, 100}, DrawSpec{}, 1, 0));
  CHECK(h->hitTest({107, 100}, DrawSpec{}, 1, 0));
  h->setFlip(false, false);
  h->setScale(2, 2);  // 40 wide: the opaque half is x in [80, 100)
  CHECK(h->hitTest({85, 100}, DrawSpec{}, 1, 0));
  CHECK_FALSE(h->hitTest({105, 100}, DrawSpec{}, 1, 0));
  // an inherited alpha from the parent counts as well
  DrawSpec faded;
  faded.alpha = 0;
  CHECK_FALSE(h->hitTest({85, 100}, faded, 1, 0));
}

TEST_CASE("input: lockdown and the exclusive set decide which sprites are valid; an invalid hit lets the next one behind answer") {
  Input e;
  auto a = e.probe("a", "sq", 100, 100);
  auto b = e.probe("b", "sq", 100, 100);
  b->setDepth(1);
  e.mgr.addObject(a);
  e.mgr.addObject(b);
  gfx::RecordingRenderer rr;
  e.mgr.draw(rr, {0, 0});
  e.move(100, 100);
  CHECK(e.mgr.hover() == b.get());
  CHECK(e.mgr.isValidSprite(a.get()));
  CHECK_FALSE(e.mgr.isValidSprite(nullptr));
  // an exclusive set: only its members
  e.mgr.addExclusiveSprite(a);
  CHECK(e.mgr.mouseDirty());
  e.frame();
  CHECK(e.mgr.hover() == a.get());  // b was hit first but is not valid: the walk goes on to a
  CHECK_FALSE(e.mgr.isValidSprite(b.get()));
  // lockdown with an empty set: nobody
  e.mgr.clearExclusiveSprites();
  CHECK_FALSE(e.mgr.mouseDirty());  // clearing does not queue a hit test
  e.mgr.setLockDown(true);
  CHECK_FALSE(e.mgr.isValidSprite(a.get()));
  e.mgr.recalcMouse();
  e.frame();
  CHECK(e.mgr.hover() == nullptr);
  // lockdown with a member: only it
  e.mgr.addExclusiveSprite(b);
  e.frame();
  CHECK(e.mgr.hover() == b.get());
  // a null entry keeps the set non-empty: nobody is valid (the map's arrival trail)
  e.mgr.setLockDown(false);
  e.mgr.clearExclusiveSprites();
  e.mgr.addExclusiveSprite(nullptr);
  CHECK(e.mgr.exclusiveCount() == 1);
  CHECK_FALSE(e.mgr.isValidSprite(a.get()));
  e.frame();
  CHECK(e.mgr.hover() == nullptr);
  e.mgr.clearExclusiveSprites();
  e.mgr.recalcMouse();
  e.frame();
  CHECK(e.mgr.hover() == b.get());
}

TEST_CASE("input: Down and Up check validity again: a sprite locked out after the hit does not get the press or the release") {
  Input e;
  auto a = e.probe("a", "sq", 100, 100);
  a->setClickable(true);
  e.mgr.addObject(a);
  e.move(100, 100);
  e.take();
  e.mgr.setLockDown(true);  // after the hover target was chosen, before the press
  CHECK_FALSE(e.mgr.onMouseDown({100, 100}));
  CHECK(e.take().empty());
  e.mgr.setLockDown(false);
  CHECK(e.mgr.onMouseDown({100, 100}));
  e.take();
  e.mgr.setLockDown(true);  // locked while pressed: the release is dropped and the capture ends
  CHECK_FALSE(e.mgr.onMouseUp({100, 100}));
  CHECK(e.take().empty());
  CHECK(e.mgr.pressedSprite() == nullptr);
}

TEST_CASE("input: the hover handler gets the milliseconds on the target, reset by a change of target") {
  Input e;
  auto a = e.probe("a", "sq", 100, 100);
  auto b = e.probe("b", "sq", 200, 100);
  e.mgr.addObject(a);
  e.mgr.addObject(b);
  e.move(100, 100);  // the very first update counts no elapsed time
  CHECK(e.mgr.hoverMs() == doctest::Approx(0));
  e.frame(50);
  e.frame(50);
  REQUIRE(a->hoverCalls.size() >= 2);
  CHECK(a->hoverCalls.back() == doctest::Approx(100));
  CHECK(e.mgr.hoverMs() == doctest::Approx(100));
  e.move(200, 100);
  CHECK(e.mgr.hover() == b.get());
  CHECK(e.mgr.hoverMs() == doctest::Approx(16));  // the change reset it, then this frame's elapsed was added
  CHECK(b->hoverCalls.back() == doctest::Approx(16));
}

TEST_CASE("input: removing or re-texturing the sprite from its own handlers is safe, in every event") {
  Input e;
  auto a = e.probe("a", "sq", 100, 100);
  a->setClickable(true);
  e.mgr.addObject(a);
  // Enter: remove itself from the manager
  a->onEnterFn = [&] { e.mgr.removeObject(*a); };
  e.move(100, 100);
  CHECK(e.mgr.objects().empty());
  e.frame();  // the next hit test finds nothing and tells the old target it left
  e.take();
  CHECK(e.mgr.hover() == nullptr);
  a->onEnterFn = nullptr;
  // Click: remove itself, queue its own removal, swap its image, and drop every object
  e.mgr.addObject(a);
  e.move(100, 100);
  a->onClickFn = [&] {
    a->setPlainTexture(e.textures->load("ring"));
    a->scriptRemove();
    e.mgr.removeObject(*a);
    e.mgr.removeAllObjects();
  };
  e.mgr.onMouseDown({100, 100});
  CHECK(e.mgr.onMouseUp({100, 100}));
  CHECK(e.mgr.objects().empty());
  CHECK(e.mgr.hover() == nullptr);
  CHECK(e.mgr.pressedSprite() == nullptr);
  e.frame();
  e.frame();
  // Down: the handler throws away the whole manager contents; the capture that follows must not touch freed state
  a->onClickFn = nullptr;
  auto b = e.probe("b", "sq", 100, 100);
  e.mgr.addObject(b);
  e.move(100, 100);
  b->onDownFn = [&] { e.mgr.removeAllObjects(); };
  e.mgr.onMouseDown({100, 100});
  e.mgr.onMouseMove({101, 101});
  e.mgr.onMouseUp({101, 101});
  e.frame();
  // a listener that removes its own sprite while the event is being delivered
  struct Remover : SpriteListener {
    explicit Remover(SpriteManager& m) : mgr(m) {}
    void onMouseEnter(VSprite& s) override { mgr.removeObject(s); }
    SpriteManager& mgr;
  };
  auto c = e.probe("c", "sq", 100, 100);
  c->addListener(std::make_shared<Remover>(e.mgr));
  e.mgr.addObject(c);
  e.frame();
  e.move(500, 500);
  e.move(100, 100);
  e.move(500, 500);
  e.frame();
  CHECK(e.mgr.objects().empty());
}

TEST_CASE("input: a handler that moves the pointer's sprite out from under it, or hides it, leaves a consistent hover state") {
  Input e;
  auto a = e.probe("a", "sq", 100, 100);
  e.mgr.addObject(a);
  e.move(100, 100);
  a->setVisible(false);
  e.mgr.recalcMouse();
  e.frame();
  CHECK(e.mgr.hover() == nullptr);
  CHECK(e.take().back() == "a:Leave");
}

TEST_CASE("input: adding or removing sprites and children queues the hit test; clearing the exclusive set does not") {
  Input e;
  auto a = e.probe("a", "sq", 100, 100);
  CHECK_FALSE(e.mgr.mouseDirty());
  e.mgr.addObject(a);
  CHECK(e.mgr.mouseDirty());
  e.frame();
  CHECK_FALSE(e.mgr.mouseDirty());
  auto k = e.probe("k", "sq", 0, 0);
  a->addChild(k);
  CHECK(e.mgr.mouseDirty());
  e.frame();
  a->removeChild(*k);
  CHECK(e.mgr.mouseDirty());
  e.frame();
  e.mgr.removeObject(*a);
  CHECK(e.mgr.mouseDirty());
  e.frame();
  e.mgr.addExclusiveSprite(a);
  CHECK(e.mgr.mouseDirty());
}

TEST_CASE("input: a sprite window forwards the mouse to its manager and answers whether it consumed the event") {
  Input e;
  auto s = std::make_shared<ui::MapStyle>();
  auto w = std::make_unique<VSpriteWindow>(e.wm, e.ctx);
  auto& win = static_cast<VSpriteWindow&>(e.wm.initWindow(std::move(w), *s));
  win.postChildrenInit(*s);
  e.wm.popCreation();
  auto a = e.probe("a", "sq", 100, 100);
  win.sprites().addObject(a);
  CHECK_FALSE(win.onMouseMove(100, 100));  // the hit test has not run yet
  CHECK_FALSE(win.onMouseDown(100, 100));
  e.wm.setNowMs(1000);
  win.onAnimate();
  e.wm.setNowMs(1016);
  win.onAnimate();
  CHECK(win.sprites().hover() == a.get());
  CHECK(win.onMouseMove(101, 100));
  CHECK(win.onMouseDown(101, 100));
  CHECK(win.onMouseUp(101, 100));
  CHECK_FALSE(win.onMouseUp(101, 100));  // nothing captured any more
  // a miss falls through: the window reports it did not take the event
  win.onMouseMove(600, 500);
  e.wm.setNowMs(1032);
  win.onAnimate();
  CHECK_FALSE(win.onMouseMove(601, 500));
  CHECK_FALSE(win.onMouseDown(601, 500));
}

TEST_CASE("sprite copy: layer, depth, mouse flags, draw state and position carry over; a nameless copy is named") {
  Input e;
  auto a = e.probe("src", "sq", 12, 34);
  a->setLayer(3);
  a->setDepth(-4);
  a->setLeaf(true);
  a->setMouse(false);
  a->setClickable(true);
  a->setAlpha(0.25f);
  a->setScale(2, 3);
  a->setFlip(true, false);
  a->setBlend(2);
  auto copy = std::make_shared<VSprite>(e.ctx, "");
  a->copyTo(*copy);
  CHECK(copy->name() == "src(copy 01)");
  CHECK(copy->layer() == 3);
  CHECK(copy->depth() == -4);
  CHECK(copy->leaf());
  CHECK_FALSE(copy->mouse());
  CHECK(copy->clickable());
  CHECK(copy->alpha() == doctest::Approx(0.25f));
  CHECK(copy->scale().y == doctest::Approx(3));
  CHECK(copy->hflip());
  CHECK(copy->blend() == 2);
  CHECK(copy->position().x == doctest::Approx(12));
  auto second = std::make_shared<VSprite>(e.ctx, "");
  a->copyTo(*second);
  CHECK(second->name() == "src(copy 02)");
  auto named = std::make_shared<VSprite>(e.ctx, "mine");
  a->copyTo(*named);
  CHECK(named->name() == "mine");
}

TEST_CASE("sprite rectangles: the sprite's own visible flag is not read, a child's is; a hidden self shows only its children") {
  Input e;
  auto a = e.probe("a", "sq", 100, 100);
  auto k = e.probe("k", "sq", 30, 0);
  a->addChild(k);
  a->setVisible(false);
  const gfx::RectF r = a->getRect(DrawSpec{}, -1);
  CHECK(r.w == doctest::Approx(50));  // 90..140: the sprite's own box still counts, and the child's
  k->setVisible(false);
  CHECK(a->getRect(DrawSpec{}, -1).w == doctest::Approx(20));
  a->setSelfVisible(false);
  CHECK(a->getRect(DrawSpec{}, -1).w <= 0);
}
