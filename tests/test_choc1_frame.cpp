// Chocolatier 1: the Frame window's layout (docs/re/choc1-natives.md
// section 3) and the shared DynamicWindow under the name UpdateDynamicWindow
// (section 4).
#include <doctest/doctest.h>

#include <string>

#include "ScriptHarness.h"
#include "games/Registry.h"
#include "games/choc1/FrameWindow.h"
#include "games/common/DynamicWindow.h"
#include "gfx/RecordingRenderer.h"

using namespace choco;
namespace f1 = choco::game::choc1;
using Image = f1::FramePiece::Image;

namespace {
const f1::FrameSizes kSizes{{20, 24}, {23, 24}, {23, 27},
                            {25, 27}, {34, 12}, {12, 35}};

struct FrameEnv : test::ScriptEnv {
  FrameEnv()
      : ScriptEnv(std::string(CHOCO_TEST_ASSETS) + "/choc2_ledger"), host(ctx) {
    ctx.module = games::findModule("choc1");
    ctx.module->attach(ctx);
    booted = host.boot();
  }
  script::ScriptHost host;
  bool booted = false;
};
} // namespace

TEST_CASE("choc1 Frame layout: exe draw order, tiles unscaled from the corner "
          "width, corners flush to the rect") {
  const auto p = f1::layoutFrame(200, 100, kSizes);
  // UL, 6 top tiles (ceil((200-20)/34)), 3 left, 3 right (ceil((100-24)/35)),
  // UR, LL, 6 bottom (ceil((200-23)/34)), LR.
  REQUIRE(p.size() == 1 + 6 + 3 + 3 + 1 + 1 + 6 + 1);
  std::size_t i = 0;
  CHECK(p[i++] == f1::FramePiece{Image::UL, {0, 0, 20, 24}});
  for (int k = 0; k < 6; ++k)
    CHECK(p[i++] == f1::FramePiece{Image::H,
                                   {20 + 34 * k, 0, 34,
                                    12}}); // the last overruns to x = 224
  for (int k = 0; k < 3; ++k)
    CHECK(p[i++] == f1::FramePiece{Image::V, {0, 24 + 35 * k, 12, 35}});
  for (int k = 0; k < 3; ++k)
    CHECK(p[i++] ==
          f1::FramePiece{Image::V,
                         {188, 24 + 35 * k, 12, 35}}); // right edge flush at W
  CHECK(p[i++] == f1::FramePiece{Image::UR, {177, 0, 23, 24}});
  CHECK(p[i++] == f1::FramePiece{Image::LL, {0, 73, 23, 27}});
  for (int k = 0; k < 6; ++k)
    CHECK(p[i++] ==
          f1::FramePiece{Image::H,
                         {23 + 34 * k, 88, 34, 12}}); // bottom flush at H
  CHECK(p[i++] == f1::FramePiece{Image::LR, {175, 73, 25, 27}});
  CHECK(i == p.size());
}

TEST_CASE("choc1 Frame layout: the fill is (5, 5) to (W-10, H-10), a missing "
          "image leaves its pieces out, a size too small adds none") {
  CHECK(game::choc1::frameFillRect(200, 100) ==
        gfx::RectI{5, 5, 185, 85}); // INFERENCE: right/bottom, not width/height
  CHECK(f1::frameFillRect(8, 8) == gfx::RectI{5, 5, 0, 0});
  f1::FrameSizes none{};
  CHECK(f1::layoutFrame(200, 100, none).empty());
  f1::FrameSizes corners{{20, 24}, {23, 24}, {23, 27}, {25, 27}, {}, {}};
  CHECK(f1::layoutFrame(200, 100, corners).size() == 4);
  CHECK(f1::layoutFrame(10, 10, kSizes).size() ==
        4 + 0 + 0); // narrower than the corners: no edge tile starts inside
}

TEST_CASE("choc1 Frame: registered, fill in `color` unless hollow, a missing "
          "image is logged once and the fill still draws") {
  using Kind = gfx::RecordingRenderer::Call::Kind;
  FrameEnv e;
  REQUIRE(e.booted);
  CHECK_FALSE(e.log.contains("[unimplemented-type] Frame"));
  REQUIRE(e.run("PushModal('t'); Frame{name='a', x=10, y=20, w=100, h=60, "
                "color={1,0,0,1}}();"
                "Frame{name='b', x=0, y=0, w=50, h=50, hollow=true}()") == 0);
  auto *a = dynamic_cast<f1::FrameWindow *>(e.wm.topModal()->find("a"));
  auto *b = dynamic_cast<f1::FrameWindow *>(e.wm.topModal()->find("b"));
  REQUIRE((a && b));
  CHECK(b->hollow());
  CHECK(a->imagesLoaded() == 0); // this asset folder has no image/frame*
  gfx::RecordingRenderer rr;
  e.wm.draw(rr);
  int fills = 0;
  for (const auto &c : rr.calls())
    if (c.kind == Kind::FillRect) {
      ++fills;
      CHECK(c.dst.x == doctest::Approx(15)); // 10 + 5
      CHECK(c.dst.w == doctest::Approx(85));
      CHECK(c.color.r == doctest::Approx(1));
    }
  CHECK(fills == 1); // the hollow one draws no fill
  CHECK(rr.count(Kind::DrawTexture) == 0);
  CHECK(e.log.count("[choc1] Frame: image/frameul not found") ==
        1); // two frames, one line
}

TEST_CASE("choc1 UpdateDynamicWindow rebuilds every DynamicWindow of that name "
          "in the whole modal chain, replacing the children") {
  FrameEnv e;
  REQUIRE(e.booted);
  CHECK_FALSE(e.log.contains("[unimplemented-type] DynamicWindow"));
  CHECK_FALSE(e.log.contains("UpdateDynamicWindow"));
  REQUIRE(e.run("PushModal('low'); DynamicWindow{name='tabs', x=0, y=0, w=100, "
                "h=50, contents='ui/dyn_counter.lua'}();"
                "DynamicWindow{name='other', x=0, y=60, w=100, h=50, "
                "contents='ui/dyn_counter.lua'}();"
                "PushModal('high'); DynamicWindow{name='tabs', x=0, y=0, "
                "w=100, h=50, contents='ui/dyn_counter.lua'}()") == 0);
  CHECK(e.getNumber("dynBuilds") == 3);
  const auto &modals = e.wm.modals();
  auto *lowTabs = dynamic_cast<game::DynamicWindow *>(
      modals[modals.size() - 2]->find("tabs"));
  auto *highTabs =
      dynamic_cast<game::DynamicWindow *>(modals.back()->find("tabs"));
  REQUIRE((lowTabs && highTabs));
  const ui::WindowId oldChild = lowTabs->children().front()->id();
  REQUIRE(e.run("UpdateDynamicWindow('tabs')") == 0);
  CHECK(e.getNumber("dynBuilds") == 5); // both `tabs`, not `other`
  CHECK(lowTabs->builds() == 2);
  CHECK(highTabs->builds() == 2);
  CHECK(lowTabs->children().size() == 1); // replaced, not appended
  CHECK(highTabs->children().size() == 1);
  CHECK(e.wm.window(oldChild) == nullptr);
  CHECK(e.run("UpdateDynamicWindow('nothing')") == 0);
  CHECK(e.getNumber("dynBuilds") == 5);
}

TEST_CASE("choc1 DynamicWindow ontext: a text change rebuilds the contents, "
          "and its log lines carry the choc1 tag") {
  FrameEnv e;
  REQUIRE(e.booted);
  REQUIRE(e.run("PushModal('t'); DynamicWindow{name='r', x=0, y=0, w=100, "
                "h=50, ontext=true, contents='ui/dyn_counter.lua'}();"
                "DynamicWindow{name='plain', x=0, y=60, w=100, h=50, "
                "contents='ui/dyn_counter.lua'}()") == 0);
  auto *r = dynamic_cast<game::DynamicWindow *>(e.wm.topModal()->find("r"));
  REQUIRE(r);
  CHECK(e.getNumber("dynBuilds") == 2);
  r->onTextChanged();
  CHECK(e.getNumber("dynBuilds") == 3);
  CHECK(r->children().size() == 1);
  CHECK(r->textRuns() == 1);
}
