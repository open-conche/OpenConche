#include <doctest/doctest.h>

#include <memory>
#include <set>

#include "gfx/RecordingRenderer.h"
#include "gfx/text/TextLayout.h"
#include "ui/Layout.h"
#include "ui/WindowManager.h"
#include "ui/widgets/TextWidgets.h"

using namespace choco;

namespace {

struct Uploader : gfx::TextureUploader {
  void* upload(const std::uint8_t*, int, int) override { return new int(0); }
  void release(void* h) override { delete static_cast<int*>(h); }
};

struct Env {
  Uploader up;
  gfx::text::TextSystem ts{nullptr, &up};
  ui::WindowManager wm;
  Env() {
    wm.setTextSystem(&ts);
    wm.setLabelLookup([](const std::string& k) { return k == "story_mode" ? std::string("Story Mode") : std::string("#####"); });
  }
  ui::Window& make(ui::Window& parent, const std::string& type, const ui::MapStyle& s) {
    ui::Window& w = parent.adoptFront(wm.createWindow(type));
    w.init(s);
    return w;
  }
};

ui::MapStyle textStyle(double x, double y, double w, double h, const std::string& label) {
  ui::MapStyle s;
  s.set("x", x).set("y", y).set("w", w).set("h", h).set("label", label);
  s.set("font.1", std::string("__DEFAULT")).set("font", std::vector<double>{0, 20}).set("font.3", std::vector<double>{1, 0.5, 0.25, 1});
  return s;
}

}  // namespace

TEST_CASE("resolveLabel: '#' is literal, digits and dots are literal, anything else is a string key") {
  auto lookup = [](const std::string& k) { return k == "play" ? std::string("Play!") : std::string("#####"); };
  CHECK(ui::resolveLabel("#Hello World", lookup) == "Hello World");
  CHECK(ui::resolveLabel("##x", lookup) == "#x");
  CHECK(ui::resolveLabel("#", lookup) == "");
  CHECK(ui::resolveLabel("play", lookup) == "Play!");
  CHECK(ui::resolveLabel("missing_key", lookup) == "#####");
  CHECK(ui::resolveLabel("12.50", lookup) == "12.50");
  CHECK(ui::resolveLabel("", lookup) == "");
  CHECK(ui::resolveLabel("play", {}) == "play");  // no string table: literal
}

TEST_CASE("readTextStyle reads font, colour, flags and padding with the defaults") {
  ui::MapStyle s;
  s.set("font.1", std::string("fonts/x.mvec")).set("font", std::vector<double>{0, 17.9}).set("font.3", std::vector<double>{0.1, 0.2, 0.3, 0.4});
  s.set("flags", 5.0).set("padding", 2.0);
  gfx::text::FontCache fonts(nullptr);
  auto ts = ui::readTextStyle(s, &fonts);
  CHECK(ts.size == 17);  // truncated
  CHECK(ts.color.g == doctest::Approx(0.2));
  CHECK(ts.color.a == doctest::Approx(0.4));
  CHECK(ts.flags == (gfx::text::kHAlignCenter + gfx::text::kVAlignCenter));
  CHECK(ts.padding == 2);
  CHECK(ts.font != nullptr);

  ui::MapStyle d;  // nothing set: 16 px (the TText default), white, flags from defflags
  d.set("defflags", 9.0);
  auto td = ui::readTextStyle(d, &fonts);
  CHECK(td.size == 16);
  CHECK(td.color.r == 1);
  CHECK(td.flags == (gfx::text::kHAlignCenter + gfx::text::kVAlignBottom));
  ui::MapStyle z;
  z.set("font", std::vector<double>{0, 0});
  CHECK(ui::readTextStyle(z, nullptr).size == 12);
}

TEST_CASE("Text windows resolve, store and return their label") {
  Env e;
  ui::Window& root = *e.wm.topModal();
  auto& a = e.make(root, "Text", textStyle(0, 0, 200, 40, "#Literal"));
  auto& b = e.make(root, "Text", textStyle(0, 0, 200, 40, "story_mode"));
  auto& c = e.make(root, "Text", textStyle(0, 0, 200, 40, "nope"));
  CHECK(a.label() == "Literal");
  CHECK(b.label() == "Story Mode");
  CHECK(c.label() == "#####");
  CHECK(a.hasLabel());
  a.setLabel("story_mode");  // SetLabel does no lookup
  CHECK(a.label() == "story_mode");
}

TEST_CASE("Text rebuilds its layout when the label changes") {
  Env e;
  auto& w = static_cast<ui::TextWindow&>(e.make(*e.wm.topModal(), "Text", textStyle(0, 0, 400, 40, "#AB")));
  const int n1 = static_cast<int>(w.layout().glyphPositions().size());
  w.setLabel("ABCDEF");
  CHECK(static_cast<int>(w.layout().glyphPositions().size()) > n1);
  const int h1 = w.layout().bounds().h;
  CHECK(h1 > 0);
}

TEST_CASE("Text draws glyph geometry with the window alpha, and nothing for an empty label") {
  Env e;
  auto& w = e.make(*e.wm.topModal(), "Text", textStyle(10, 10, 200, 40, "#Hi"));
  auto& empty = e.make(*e.wm.topModal(), "Text", textStyle(10, 60, 200, 40, ""));
  gfx::RecordingRenderer r;
  ui::DrawContext ctx{r, {10, 10}, 1, {}};
  empty.draw(ctx);
  CHECK(r.calls().empty());
  w.draw(ctx);
  CHECK_FALSE(r.calls().empty());
}

TEST_CASE("TightText forces 800x600 then shrinks to the text plus pad, and a parent's FitToChildren sees it") {
  Env e;
  ui::Window& root = *e.wm.topModal();
  ui::MapStyle gs;
  gs.set("x", 0.0).set("y", 0.0).set("w", 10.0).set("h", 10.0);
  ui::Window& group = e.make(root, "Window", gs);

  ui::MapStyle ts = textStyle(30, 40, 800, 600, "#Tight text");
  auto& t = static_cast<ui::TightTextWindow&>(e.make(group, "TightText", ts));
  CHECK(t.rect().w == 800);
  CHECK(t.rect().h == 600);
  CHECK(t.pad() == 3);

  const gfx::RectI bounds = t.layout().bounds();
  t.postChildrenInit(ts);
  CHECK(t.rect().w == bounds.w + 3);
  CHECK(t.rect().h == bounds.h);
  CHECK(t.rect().w < 800);
  CHECK(t.rect().h > 0);

  ui::fitToChildren(group);
  CHECK(group.rect().w == 30 + t.rect().w);
  CHECK(group.rect().h == 40 + t.rect().h);

  ts.set("pad", 10.0);
  auto& t2 = static_cast<ui::TightTextWindow&>(e.make(group, "TightText", ts));
  t2.postChildrenInit(ts);
  CHECK(t2.rect().w == t.rect().w + 7);
}

TEST_CASE("Text start line: line 3 draws from the third line, as the text of lines 3 on would") {
  Env e;
  const std::string all = "#AAAA\nBBBB\nCCCC\nDDDD\nEEEE\nFFFF\nGGGG\nHHHH";
  const std::string tail = "#CCCC\nDDDD\nEEEE\nFFFF\nGGGG\nHHHH";
  auto& scrolled = static_cast<ui::TextWindow&>(e.make(*e.wm.topModal(), "Text", textStyle(10, 10, 200, 80, all)));
  auto& plain = static_cast<ui::TextWindow&>(e.make(*e.wm.topModal(), "Text", textStyle(10, 10, 200, 80, tail)));
  auto vertices = [](ui::TextWindow& w) {
    gfx::RecordingRenderer r;
    ui::DrawContext ctx{r, {10, 10}, 1, {}};
    w.draw(ctx);
    std::vector<gfx::Vertex> v;
    for (const auto& c : r.calls()) v.insert(v.end(), c.vertices.begin(), c.vertices.end());
    return v;
  };
  const auto top = vertices(scrolled);
  scrolled.setStartLine(2);
  const auto from3 = vertices(scrolled);
  const auto want = vertices(plain);
  REQUIRE_FALSE(want.empty());
  CHECK(from3.size() == want.size());
  CHECK(from3.front().uv.x != top.front().uv.x);  // not the first line
  for (std::size_t i = 0; i < std::min(from3.size(), want.size()); ++i) {
    CHECK(from3[i].pos.x == doctest::Approx(want[i].pos.x));
    CHECK(from3[i].pos.y == doctest::Approx(want[i].pos.y));
    CHECK(from3[i].uv.x == doctest::Approx(want[i].uv.x));
  }
  // A start line past the end is clamped, not an empty box.
  scrolled.setStartLine(99);
  CHECK_FALSE(vertices(scrolled).empty());
}
