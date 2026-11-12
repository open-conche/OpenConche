// The Great Chocolate Chase, slice 3 (docs/design/greatcc.md section 6): the widget library (RShrinkText, RDisableButton,
// RTextEditDisable, KSplit9Image, BLayeredWindow, RScrollable) and the menu windows (TMainMenu, TChoosePlayer, TOptions,
// VHelpWindow). Synthetic fixtures only: tests/assets/greatcc/menus and the button images of tests/assets/button.
#include <doctest/doctest.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "gfx/RecordingRenderer.h"
#include "gfx/TextureCache.h"
#include "gfx/text/TextLayout.h"
#include "games/Registry.h"
#include "games/greatcc/GreatCCWorld.h"
#include "games/greatcc/ui/BLayeredWindow.h"
#include "games/greatcc/ui/KSplit9Image.h"
#include "games/greatcc/ui/RDisableButton.h"
#include "games/greatcc/ui/RScrollable.h"
#include "games/greatcc/ui/RShrinkText.h"
#include "games/greatcc/ui/RTextEditDisable.h"
#include "games/greatcc/ui/TChoosePlayer.h"
#include "games/greatcc/ui/TCredits.h"
#include "games/greatcc/ui/TMainMenu.h"
#include "games/greatcc/ui/TOptions.h"
#include "games/greatcc/ui/VHelpWindow.h"
#include "ScriptHarness.h"
#include "sdk/Users.h"
#include "ui/Input.h"
#include "ui/ScriptSink.h"
#include "ui/Style.h"
#include "ui/VolumeSliders.h"
#include "ui/WindowManager.h"
#include "ui/widgets/InputWidgets.h"
#include "ui/widgets/TextWidgets.h"
#include "ui/widgets/Widgets.h"

using namespace choco;
using namespace choco::games::greatcc;

namespace {

const std::string kMenus = std::string(CHOCO_TEST_ASSETS) + "/greatcc/menus";
const std::string kButtonAssets = std::string(CHOCO_TEST_ASSETS) + "/button";

struct Uploader : gfx::TextureUploader {
  void* upload(const std::uint8_t*, int, int) override { return new int(0); }
  void release(void* h) override { delete static_cast<int*>(h); }
};

// Records what the window side hands to the script (button presses and the like).
struct Sink : ui::ScriptSink {
  std::vector<ui::Message> messages;
  void deliver(const ui::Message* m, const script::LuaRef*) override {
    if (m) messages.push_back(*m);
  }
  void runInScript(const script::LuaRef&) override {}
  bool callProtected(const script::LuaRef&) override { return true; }
  void setGlobalString(const char*, std::string_view) override {}
  int pressed(const std::string& name) const {
    int n = 0;
    for (const auto& m : messages) n += m.type == ui::kButtonPress && m.name == name;
    return n;
  }
};

ui::InputEvent mouse(ui::InputEvent::Type t, float x, float y) {
  ui::InputEvent e;
  e.type = t;
  e.x = x;
  e.y = y;
  return e;
}
ui::InputEvent down(float x, float y) { return mouse(ui::InputEvent::Type::MouseDown, x, y); }
ui::InputEvent up(float x, float y) { return mouse(ui::InputEvent::Type::MouseUp, x, y); }
ui::InputEvent move(float x, float y) { return mouse(ui::InputEvent::Type::MouseMove, x, y); }

// A window manager with a modal to build into, a text system and the button textures.
struct Bare {
  Bare() : btnFs(kButtonAssets, ""), cache(btnFs, nullptr) {
    wm.setTextSystem(&ts);
    wm.setTextures(&cache);
    wm.setScriptSink(&sink);
    wm.pushModal("m");
  }
  ~Bare() { wm.destroyAll(); }
  // The DSL's order: init the window into the window under construction, build children, postChildrenInit, pop.
  template <class T>
  T& open(const ui::MapStyle& s) {
    auto w = std::make_unique<T>(wm);
    T& ref = static_cast<T&>(wm.initWindow(std::move(w), s));
    return ref;
  }
  void close(ui::Window& w, const ui::MapStyle& s) {
    w.postChildrenInit(s);
    wm.popCreation();
  }
  template <class T>
  T& make(const ui::MapStyle& s) {
    T& w = open<T>(s);
    close(w, s);
    return w;
  }
  void pump() {
    wm.clearDestroyed();
    wm.dispatchMessages();
  }
  Uploader up;
  gfx::text::TextSystem ts{nullptr, &up};
  assets::AssetFS btnFs;
  gfx::TextureCache cache;
  ui::WindowManager wm;
  Sink sink;
};

ui::MapStyle rectStyle(double x, double y, double w, double h) {
  ui::MapStyle s;
  s.set("x", x).set("y", y).set("w", w).set("h", h);
  return s;
}

ui::MapStyle fontStyle(double x, double y, double w, double h, double size) {
  ui::MapStyle s = rectStyle(x, y, w, h);
  s.set("font.1", std::string("__DEFAULT")).set("font", std::vector<double>{0, size}).set("font.3", std::vector<double>{1, 1, 1, 1});
  return s;
}

}  // namespace

// ================================================================ RShrinkText

TEST_CASE("RShrinkText: the loop needs strictly smaller bounds in both axes, one pixel per step, and stops at zero") {
  // Text bounds are 4 px wide and 2 px high per pixel of line height.
  const auto measure = [](int lh) { return gfx::RectI{0, 0, 4 * lh, 2 * lh}; };
  bool zero = true;
  // 100 wide: 4*24 = 96 < 100 fits at once.
  CHECK(shrinkLineHeight(24, 100, 100, measure, &zero) == 24);
  CHECK_FALSE(zero);
  // 96 wide: 4*24 = 96 does not fit (<=), 4*23 = 92 does. An exact fit shrinks.
  CHECK(shrinkLineHeight(24, 96, 100, measure) == 23);
  // The height decides alike: 48 high means 2*24 = 48 does not fit; 2*23 = 46 does.
  CHECK(shrinkLineHeight(24, 1000, 48, measure) == 23);
  CHECK(shrinkLineHeight(24, 1000, 49, measure) == 24);
  // Both must be strictly smaller: width fits at 20 (80 < 81) but 2*20 = 40 <= 40 does not, so it goes on to 19.
  CHECK(shrinkLineHeight(24, 81, 40, measure) == 19);
  // Nothing fits: it ends at 0 and says so.
  CHECK(shrinkLineHeight(24, 3, 3, measure, &zero) == 0);
  CHECK(zero);
  // No maximum (a one-entry font table): the size is left alone and the measure is not asked.
  int asked = 0;
  CHECK(shrinkLineHeight(0, 10, 10, [&](int) { ++asked; return gfx::RectI{0, 0, 99, 99}; }) == 0);
  CHECK(asked == 0);
}

TEST_CASE("RShrinkText: SetLabel restarts from the font size; the style's own label is not shrunk") {
  Bare b;
  ui::MapStyle s = fontStyle(0, 0, 60, 24, 20);
  s.set("label", std::string("#This label is far too long for sixty pixels"));
  RShrinkText& t = b.make<RShrinkText>(s);
  CHECK(t.maxSize() == 20);
  CHECK(t.currentSize() == 20);  // the initial label is laid out as it is
  CHECK_FALSE(t.textFits());
  t.setLabel("This label is far too long for sixty pixels");
  CHECK(t.currentSize() < 20);
  CHECK(t.currentSize() > 0);
  CHECK(t.textFits());
  CHECK(t.textStyle().size == doctest::Approx(t.currentSize()));
  // One size down from the answer would not have been needed: the answer is the largest that fits.
  t.setTextSize(static_cast<float>(t.currentSize() + 1));
  CHECK_FALSE(t.textFits());
  // A short label starts again from the full size.
  t.setLabel("Hi");
  CHECK(t.currentSize() == 20);
  CHECK(t.textFits());
  // A font table of one entry has no maximum: SetLabel never shrinks.
  ui::MapStyle plain = rectStyle(0, 0, 60, 24);
  plain.set("font.1", std::string("__DEFAULT"));
  RShrinkText& p = b.make<RShrinkText>(plain);
  CHECK(p.maxSize() == 0);
  p.setLabel("This label is far too long for sixty pixels");
  CHECK(p.currentSize() == 0);
}

// ================================================================ RDisableButton

namespace {
// A 100x40 push button at (10,10) with a probe behind it that records any click that falls through.
struct ButtonRig {
  Bare b;
  RDisableButton* btn = nullptr;
  struct Behind : ui::Window {
    explicit Behind(ui::WindowManager& wm) : Window(wm) {}
    bool onMouseDown(int, int) override { ++downs; return true; }
    bool onMouseUp(int, int) override { ++ups; return true; }
    int downs = 0, ups = 0;
  }* behind = nullptr;
  explicit ButtonRig(bool disabled = false) {
    auto back = std::make_unique<Behind>(b.wm);
    behind = back.get();
    b.wm.creationTop()->adoptFront(std::move(back));
    behind->setRect({0, 0, 300, 300});
    ui::MapStyle s = rectStyle(10, 10, 100, 40);
    s.set("name", std::string("ok"));
    if (disabled) s.set("disabled", 1.0);
    btn = &b.make<RDisableButton>(s);
  }
};
}  // namespace

TEST_CASE("RDisableButton: enabled, the layers follow the mouse; disabled, layer 3 and nothing runs") {
  ButtonRig r;
  RDisableButton& btn = *r.btn;
  CHECK(btn.currentLayer() == 0);
  CHECK_FALSE(btn.disabled());
  r.b.wm.handleInput(move(50, 30));
  CHECK(btn.currentLayer() == 2);  // hover
  r.b.wm.handleInput(down(50, 30));
  CHECK(btn.currentLayer() == 1);  // pressed
  r.b.wm.handleInput(up(50, 30));
  r.b.pump();
  CHECK(btn.currentLayer() == 2);  // after the click it is still hovered
  CHECK(r.b.sink.pressed("ok") == 1);
  r.b.wm.handleInput(move(250, 250));
  CHECK(btn.currentLayer() == 0);
  CHECK(r.behind->downs == 0);

  // Disabled: layer 3 whatever the mouse does, the press is swallowed (the window behind sees nothing) and nothing is sent.
  btn.toggleState(true);
  CHECK(btn.disabled());
  CHECK(btn.currentLayer() == 3);
  r.b.wm.handleInput(move(50, 30));
  CHECK(btn.currentLayer() == 3);
  r.b.wm.handleInput(down(50, 30));
  CHECK(btn.currentLayer() == 3);
  r.b.wm.handleInput(up(50, 30));
  r.b.pump();
  CHECK(btn.currentLayer() == 3);
  CHECK(r.b.sink.pressed("ok") == 1);  // still the one from before
  CHECK(r.behind->downs == 0);
  CHECK(r.behind->ups == 0);
  CHECK_FALSE(btn.doCommand());  // Enter on a disabled default button does nothing, nor sends a message
  r.b.pump();
  CHECK(r.b.sink.pressed("ok") == 1);
  r.b.wm.handleInput(move(250, 250));
  CHECK(btn.currentLayer() == 3);

  // Enabled again: the layer of the current state comes back and the button works.
  btn.toggleState(false);
  CHECK(btn.currentLayer() == 0);
  r.b.wm.handleInput(move(50, 30));
  r.b.wm.handleInput(down(50, 30));
  r.b.wm.handleInput(up(50, 30));
  r.b.pump();
  CHECK(r.b.sink.pressed("ok") == 2);
}

TEST_CASE("RDisableButton: the `disabled` key starts it disabled; EnableWindow-style resets keep the disabled look") {
  ButtonRig r(true);
  CHECK(r.btn->disabled());
  CHECK(r.btn->currentLayer() == 3);
  r.btn->resetState();  // what EnableWindow does: back to the normal image, which for a disabled button is layer 3
  CHECK(r.btn->currentLayer() == 3);
}

// ================================================================ RTextEditDisable

TEST_CASE("RTextEditDisable: HandleKeyHit's rule as a truth table") {
  struct Row {
    const char* text;
    int type, ch;
    bool handled, disabled;
  };
  const Row rows[] = {
      {"", 0, 'a', true, false},    // the first letter of a name, accepted: enabled although the text so far is empty
      {"", 0, 'a', false, true},    // empty and the key not accepted
      {"", 0, ' ', true, true},     // a space into an empty field
      {"", 1, 8, true, true},       // Backspace on nothing
      {"A", 1, 8, true, true},      // Backspace that is about to empty the field
      {"AB", 1, 8, true, false},    // Backspace that leaves a letter
      {"A", 0, 'b', true, false},   // another letter
      {"A", 0, 'b', false, false},  // a rejected key with text in the field changes nothing
      {"  ", 0, 'x', true, false},  // blank text counts as empty, but the key was accepted
      {"  ", 1, 8, true, true},
      {" ", 0, ' ', true, true},    // a second space on a blank field
      {" A ", 1, 8, true, true},    // trimmed to one character
      {" AB ", 1, 8, true, false},
  };
  for (const Row& r : rows) {
    INFO("text '" << r.text << "' type " << r.type << " ch " << r.ch << " handled " << r.handled);
    CHECK(textEditDisablesButton(r.text, r.type, r.ch, r.handled) == r.disabled);
  }
}

namespace {
struct NameRig {
  Bare b;
  RDisableButton* ok = nullptr;
  RTextEditDisable* edit = nullptr;
  explicit NameRig(const std::string& initial) {
    ui::MapStyle bs = rectStyle(0, 0, 100, 40);
    bs.set("name", std::string("ok"));
    ok = &b.make<RDisableButton>(bs);
    ui::MapStyle es = fontStyle(0, 100, 200, 30, 20);
    es.set("name", std::string("name")).set("label", initial).set("length", 20.0).set("disable", std::string("ok"));
    edit = &b.make<RTextEditDisable>(es);
  }
};
}  // namespace

TEST_CASE("RTextEditDisable: the initial label enables the button only above one character") {
  CHECK(NameRig("").ok->disabled());
  CHECK(NameRig("A").ok->disabled());
  CHECK(NameRig(" A ").ok->disabled());
  CHECK_FALSE(NameRig("AB").ok->disabled());
  CHECK(NameRig("A").ok->currentLayer() == 3);
}

TEST_CASE("RTextEditDisable: keys are judged with the text from before the edit") {
  NameRig r("");
  REQUIRE(r.ok->disabled());
  // The first letter: judged on the empty field, but the edit accepted it, so the button comes on at once.
  r.edit->typeCharacter("A");
  CHECK(r.edit->text() == "A");
  CHECK_FALSE(r.ok->disabled());
  // Backspace on a single letter: judged on "A" (about to empty it), so the button goes off as the letter goes.
  r.edit->typeCharacter("\b");
  CHECK(r.edit->text().empty());
  CHECK(r.ok->disabled());
  CHECK(r.ok->currentLayer() == 3);
  // A space into the empty field is judged on "" and disables; the following letter enables again.
  r.edit->typeCharacter(" ");
  CHECK(r.ok->disabled());
  r.edit->typeCharacter("B");
  CHECK_FALSE(r.ok->disabled());
  // Two letters then Backspace: still a letter left, enabled; a second Backspace judges "B" and disables.
  r.edit->setLabel("");
  r.edit->typeText("AB");
  r.edit->typeCharacter("\b");
  CHECK(r.edit->text() == "A");
  CHECK_FALSE(r.ok->disabled());
  r.edit->typeCharacter("\b");
  CHECK(r.ok->disabled());
  // An insert the edit refuses (length limit, 20 letters: 19 fit) is not accepted, but with text in the field it stays enabled.
  r.edit->setLabel("ABCDEFGHIJKLMNOPQRS");
  r.edit->evaluateKey(0, 'x', true);
  CHECK_FALSE(r.ok->disabled());
  CHECK_FALSE(r.edit->typeCharacter("T"));
  CHECK_FALSE(r.ok->disabled());
}

// ================================================================ KSplit9Image

TEST_CASE("KSplit9Image: the nine source and destination rectangles") {
  // The help dialog: insets x1=30 y1=32 x2=37 y2=40, a 515x380 window, a 100x100 texture.
  const auto p = split9Pieces(100, 100, 515, 380, 30, 37, 32, 40);
  using R = gfx::RectI;
  const Split9Piece expect[9] = {
      {R{0, 0, 30, 32}, R{0, 0, 30, 32}},          // top-left, native size
      {R{30, 0, 33, 32}, R{30, 0, 448, 32}},       // top: texture x 30..63, window x 30..478
      {R{63, 0, 37, 32}, R{478, 0, 37, 32}},       // top-right
      {R{0, 32, 30, 28}, R{0, 32, 30, 308}},       // left: texture y 32..60, window y 32..340
      {R{30, 32, 33, 28}, R{30, 32, 448, 308}},    // centre
      {R{63, 32, 37, 28}, R{478, 32, 37, 308}},    // right
      {R{0, 60, 30, 40}, R{0, 340, 30, 40}},       // bottom-left
      {R{30, 60, 33, 40}, R{30, 340, 448, 40}},    // bottom
      {R{63, 60, 37, 40}, R{478, 340, 37, 40}},    // bottom-right
  };
  for (int i = 0; i < 9; ++i) {
    INFO("piece " << i);
    CHECK(p[static_cast<std::size_t>(i)] == expect[i]);
  }
  // Symmetric insets on a square: the corners keep their size, the pieces tile the window exactly.
  const auto q = split9Pieces(64, 64, 200, 120, 20, 20, 20, 20);
  CHECK(q[4].src == R{20, 20, 24, 24});
  CHECK(q[4].dst == R{20, 20, 160, 80});
  long area = 0;
  for (const auto& piece : q) area += static_cast<long>(piece.dst.w) * piece.dst.h;
  CHECK(area == 200L * 120L);
  // Insets that overlap give negative middles (nothing is clamped).
  CHECK(split9Pieces(64, 64, 30, 30, 20, 20, 20, 20)[1].dst.w < 0);
}

TEST_CASE("KSplit9Image: the window draws its nine pieces from the size it was built with") {
  Bare b;
  ui::MapStyle s = rectStyle(5, 7, 100, 60);
  s.set("image", std::string("image/bt_up")).set("x1", 10.0).set("x2", 12.0).set("y1", 5.0).set("y2", 6.0);
  KSplit9Image& w = b.make<KSplit9Image>(s);
  CHECK(w.rect().w == 100);
  CHECK(w.rect().h == 60);
  REQUIRE(w.imageLoaded());
  // The texture is 40x20.
  CHECK(w.pieces()[4].src == gfx::RectI{10, 5, 18, 9});
  CHECK(w.pieces()[4].dst == gfx::RectI{10, 5, 78, 49});
  CHECK(w.innerRect() == gfx::RectI{10, 5, 88, 54});

  gfx::RecordingRenderer rr;
  b.wm.draw(rr);
  std::vector<gfx::RecordingRenderer::Call> draws;
  for (const auto& c : rr.calls())
    if (c.kind == gfx::RecordingRenderer::Call::Kind::DrawTexture) draws.push_back(c);
  REQUIRE(draws.size() == 9);
  for (std::size_t i = 0; i < 9; ++i) {
    const Split9Piece& piece = w.pieces()[i];
    CHECK(draws[i].src.x == piece.src.x);
    CHECK(draws[i].src.w == piece.src.w);
    CHECK(draws[i].dst.x == piece.dst.x + 5);  // plus the window's own position
    CHECK(draws[i].dst.y == piece.dst.y + 7);
    CHECK(draws[i].dst.w == piece.dst.w);
    CHECK(draws[i].dst.h == piece.dst.h);
  }
  // A later resize does not change the pieces.
  w.setSize(200, 200);
  CHECK(w.pieces()[4].dst == gfx::RectI{10, 5, 78, 49});
}

TEST_CASE("KSplit9Image: w and h are the raw numbers; no image draws nothing") {
  Bare b;
  ui::MapStyle s = rectStyle(0, 0, 90, 30);
  KSplit9Image& w = b.make<KSplit9Image>(s);
  CHECK_FALSE(w.imageLoaded());
  CHECK(w.rect().w == 90);
  gfx::RecordingRenderer rr;
  b.wm.draw(rr);
  CHECK(rr.count(gfx::RecordingRenderer::Call::Kind::DrawTexture) == 0);
}

// ================================================================ BLayeredWindow

TEST_CASE("BLayeredWindow: three layers, layer 0 shown once the children exist") {
  Bare b;
  ui::MapStyle s = rectStyle(0, 0, 100, 100);
  BLayeredWindow& w = b.open<BLayeredWindow>(s);
  CHECK(w.layerCount() == 3);
  // Lua: SelectLayer(1) then a child; SelectLayer(2) then a child; SelectLayer(0), a child.
  ui::Window* kids[3] = {};
  for (int layer : {1, 2, 0}) {
    w.setCurrentLayer(layer);
    kids[layer] = &b.make<ui::BitmapWindow>(rectStyle(0, 0, 10, 10));
  }
  w.setCurrentLayer(2);  // the Lua leaves some other layer selected
  b.close(w, s);
  CHECK(w.currentLayer() == 0);
  CHECK(kids[0]->shown());
  CHECK_FALSE(kids[1]->shown());
  CHECK_FALSE(kids[2]->shown());
  w.setCurrentLayer(1);
  CHECK(kids[1]->shown());
  CHECK_FALSE(kids[0]->shown());
}

// ================================================================ RScrollable

namespace {
struct ScrollRig {
  Bare b;
  RScrollable* sc = nullptr;
  ui::Window* tall = nullptr;
  ui::Window* kept = nullptr;
  ui::SliderWindow* slider = nullptr;
  ScrollRig(double tick, bool vertical, double contentH, const std::vector<std::string>& except = {"pinned"}) {
    ui::MapStyle s = rectStyle(10, 10, 100, 50);
    s.set("tick", tick).set("vscroll", vertical ? 1.0 : 0.0);
    auto list = std::make_shared<ui::MapStyle>();
    for (const auto& e : except) list->append(e);
    s.set("except", list);
    sc = &b.open<RScrollable>(s);
    // Children in the DSL order: a slider, a pinned window (kept), a content window.
    ui::MapStyle ss = rectStyle(90, 0, 10, 50);
    slider = &b.make<ui::SliderWindow>(ss);
    ui::MapStyle ps = rectStyle(0, 0, 20, 20);
    ps.set("name", std::string("pinned"));
    kept = &b.make<ui::BitmapWindow>(ps);
    ui::MapStyle ts = vertical ? rectStyle(0, 0, 80, contentH) : rectStyle(0, 0, contentH, 40);
    ts.set("name", std::string("content"));
    tall = &b.make<ui::BitmapWindow>(ts);
    tall->setSize(vertical ? 80 : static_cast<int>(contentH), vertical ? static_cast<int>(contentH) : 40);
    b.close(*sc, s);
  }
};
}  // namespace

TEST_CASE("RScrollable: content moves into an interior window; the slider and the named windows stay") {
  ScrollRig r(10, true, 200);
  ui::Window* interior = r.sc->interior();
  REQUIRE(interior);
  CHECK(interior->parent() == r.sc);
  CHECK(r.tall->parent() == interior);
  CHECK(r.kept->parent() == r.sc);
  CHECK(r.slider->parent() == r.sc);
  CHECK(r.sc->slider() == r.slider);
  CHECK(interior->rect().h == 200);  // fitted to its children
  // The interior is the rearmost child, so the slider and the kept window draw over it.
  CHECK(r.sc->children().back().get() == interior);
  CHECK(r.sc->children().size() == 3);
}

TEST_CASE("RScrollable: SetInteriorPosition clamps, moves the slider, and places the interior") {
  ScrollRig r(10, true, 200);  // view 50 high, content 200 high: 150 scrollable
  r.sc->setInteriorPosition(0.5f);
  CHECK(r.sc->position() == doctest::Approx(0.5f));
  CHECK(r.sc->interior()->rect().y == -75);
  CHECK(r.slider->value() == doctest::Approx(0.5f));
  r.sc->setInteriorPosition(2.0f);  // clamped to 1: the interior's bottom meets the view's
  CHECK(r.sc->position() == 1.0f);
  CHECK(r.sc->interior()->rect().y == -150);
  r.sc->setInteriorPosition(-1.0f);
  CHECK(r.sc->position() == 0.0f);
  CHECK(r.sc->interior()->rect().y == 0);
  r.sc->setInteriorPosition(0.3333f);
  CHECK(r.sc->interior()->rect().y == -static_cast<int>(150 * 0.3333f));  // truncated toward zero, then negated
}

TEST_CASE("RScrollable: ScrollForward and ScrollBack move one tick; the slider's message moves the interior") {
  ScrollRig r(15, true, 200);
  r.sc->scrollForward();
  CHECK(r.sc->position() == doctest::Approx(15.0f / 150.0f));
  CHECK(r.sc->interior()->rect().y == -15);
  r.sc->scrollForward();
  r.sc->scrollBack();
  CHECK(r.sc->interior()->rect().y == -15);
  for (int i = 0; i < 20; ++i) r.sc->scrollForward();
  CHECK(r.sc->position() == 1.0f);
  // A value-changed message from the slider (type 9) drives the interior and is consumed.
  r.slider->setValue(0.2f, true);
  CHECK(r.sc->onMessage({ui::kSliderValChanged, "", 0}));
  CHECK(r.sc->interior()->rect().y == -30);
  CHECK_FALSE(r.sc->onMessage({ui::kButtonPress, "x", 0}));
}

TEST_CASE("RScrollable: content shorter than the view resets to zero; horizontal scrolls on x; no tick scrolls nowhere") {
  ScrollRig shortContent(10, true, 30);
  shortContent.sc->setInteriorPosition(0.7f);
  CHECK(shortContent.sc->position() == 0.0f);
  CHECK(shortContent.sc->interior()->rect().y == 0);
  shortContent.sc->scrollForward();  // no division by zero: nothing happens
  CHECK(shortContent.sc->interior()->rect().y == 0);

  ScrollRig side(10, false, 300);  // 100 wide view, 300 wide content
  side.sc->setInteriorPosition(0.5f);
  CHECK(side.sc->interior()->rect().x == -100);
  CHECK(side.sc->interior()->rect().y == 0);

  ScrollRig noTick(0, true, 200);
  noTick.sc->scrollForward();
  CHECK(noTick.sc->position() == 0.0f);
}

// ================================================================ the menu windows (a booted greatcc script host)

namespace {

struct MenuEnv : test::ScriptEnv {
  MenuEnv() : ScriptEnv(kMenus), btnFs(kButtonAssets, ""), cache(btnFs, nullptr), host(ctx) {
    ctx.module = games::findModule("greatcc");
    ctx.module->attach(ctx);
    wm.setTextSystem(&ts);
    wm.setTextures(&cache);
    booted = host.boot();
    // The harness's user folder is shared by the test run: start from a store with no players.
    if (ctx.users)
      while (ctx.users->count() > 0) ctx.users->remove(0);
  }
  ~MenuEnv() { wm.destroyAll(); }
  ui::Window& open(const std::string& type, const ui::MapStyle& s) {
    return wm.initWindow(wm.createWindow(type), s);
  }
  void close(ui::Window& w, const ui::MapStyle& s) {
    w.postChildrenInit(s);
    wm.popCreation();
  }
  ui::Window& make(const std::string& type, const ui::MapStyle& s) {
    ui::Window& w = open(type, s);
    close(w, s);
    return w;
  }
  Uploader up;
  gfx::text::TextSystem ts{nullptr, &up};
  assets::AssetFS btnFs;
  gfx::TextureCache cache;
  script::ScriptHost host;
  bool booted = false;
};

ui::MapStyle named(ui::MapStyle s, const std::string& name) {
  s.set("name", name);
  return s;
}

}  // namespace

TEST_CASE("TMainMenu: welcome text, hidden debug and moregames buttons, default focus, scoped SetWelcomeText") {
  MenuEnv e;
  REQUIRE(e.booted);
  ui::MapStyle ms = rectStyle(0, 0, 800, 600);
  ms.set("fit", 1.0);
  ui::Window& menu = e.open("TMainMenu", ms);
  CHECK(std::string(menu.typeName()) == "TMainMenu");
  CHECK_FALSE(e.isNil("SetWelcomeText"));
  e.make("Text", named(fontStyle(0, 0, 200, 30, 20), "txt_welcome"));
  e.make("Text", named(fontStyle(0, 0, 200, 30, 20), "txt_welcome_shadow"));
  e.make("RShrinkText", named(fontStyle(0, 40, 250, 30, 25), "txt_user"));
  e.make("RShrinkText", named(fontStyle(0, 40, 250, 30, 25), "txt_user_shadow"));
  e.make("Button", named(rectStyle(0, 300, 100, 40), "mainmenu_debug_button"));
  e.make("Button", named(rectStyle(0, 400, 100, 40), "moregames"));
  e.make("Button", named(rectStyle(0, 500, 100, 40), "mainmenu_quit"));
  e.close(menu, ms);

  CHECK(menu.find("txt_welcome")->label() == "Welcome!");  // no user: "!" and no name
  CHECK(menu.find("txt_welcome_shadow")->label() == "Welcome!");
  CHECK(menu.find("txt_user")->label().empty());
  CHECK_FALSE(menu.find("mainmenu_debug_button")->shown());
  CHECK_FALSE(menu.find("moregames")->shown());  // xsell/xsell.lua does not exist in the fixture
  CHECK(menu.find("mainmenu_quit")->shown());
  CHECK(e.wm.topModal()->defaultFocus == menu.id());

  // A user exists: "Welcome," and "<name>!"; SetWelcomeText from Lua refreshes after a change.
  REQUIRE(e.ctx.users->create("Zed") >= 0);
  REQUIRE(e.run("SetWelcomeText()") == 0);
  CHECK(menu.find("txt_welcome")->label() == "Welcome,");
  CHECK(menu.find("txt_user")->label() == "Zed!");
  CHECK(menu.find("txt_user_shadow")->label() == "Zed!");
  e.wm.destroyAll();
  CHECK(e.isNil("SetWelcomeText"));  // nil again once the menu is gone
}

TEST_CASE("TChoosePlayer: the Lua line it runs, its four natives, and the name fields") {
  CHECK(updatePlayerChoicesLine(1, 3, {"Ann", "Bo", "Cy"}) == "UpdatePlayerChoices( 1, 3, {'Ann','Bo','Cy',} )");
  CHECK(updatePlayerChoicesLine(-1, 0, {}) == "UpdatePlayerChoices( -1, 0, {} )");
  CHECK(updatePlayerChoicesLine(0, 1, {"O'Neil\\x"}) == "UpdatePlayerChoices( 0, 1, {'O\\'Neil\\\\x',} )");
  std::vector<std::string> twelve(12, "n");
  CHECK(updatePlayerChoicesLine(0, 12, twelve).find("{'n','n','n','n','n','n','n','n','n','n',}") != std::string::npos);

  MenuEnv e;
  REQUIRE(e.booted);
  REQUIRE(e.ctx.users->create("Ann") >= 0);
  REQUIRE(e.ctx.users->create("Bo") >= 0);
  REQUIRE(e.run("UpdatePlayerChoices = function(cur, n, t) gCur = cur; gN = n; gFirst = t[1]; gSecond = t[2] end") == 0);
  ui::MapStyle ps = rectStyle(0, 0, 400, 300);
  ui::Window& cp = e.open("TChoosePlayer", ps);
  for (int i = 0; i < 2; ++i) {
    ui::MapStyle bs = named(rectStyle(0, 40.0 * i, 200, 33), "chooseplayer_button_" + std::to_string(i));
    bs.set("type", 2.0);
    ui::Window& btn = e.open("Button", bs);
    e.make("RShrinkText", named(fontStyle(30, 0, 150, 33, 18), "namefield"));
    e.close(btn, bs);
  }
  e.close(cp, ps);
  // PostChildrenInit ran SetPlayerChoices: the current user is the one just created (1), of 2.
  CHECK(e.getNumber("gCur") == 1);
  CHECK(e.getNumber("gN") == 2);
  CHECK(e.getString("gFirst") == "Ann");
  CHECK(e.getString("gSecond") == "Bo");

  REQUIRE(e.run("SetNameField('chooseplayer_button_1', 'Bobby')") == 0);
  CHECK(cp.find("chooseplayer_button_1")->find("namefield")->label() == "Bobby");
  CHECK(cp.find("chooseplayer_button_0")->find("namefield")->label().empty());
  REQUIRE(e.run("SetNameField('nobutton', 'x')") == 0);  // an unknown button does nothing

  // No button is on: index 0. Switching the second on selects it; the name is the store's.
  REQUIRE(e.run("i = GetSelectedUserIndex(); n = GetSelectedUserName()") == 0);
  CHECK(e.getNumber("i") == 0);
  CHECK(e.getString("n") == "Ann");
  dynamic_cast<ui::ButtonWindow*>(cp.find("chooseplayer_button_1"))->setOn(true);
  REQUIRE(e.run("i = GetSelectedUserIndex(); n = GetSelectedUserName()") == 0);
  CHECK(e.getNumber("i") == 1);
  CHECK(e.getString("n") == "Bo");

  e.wm.destroyAll();
  CHECK(e.isNil("SetPlayerChoices"));
  CHECK(e.isNil("GetSelectedUserIndex"));
  CHECK(e.isNil("GetSelectedUserName"));
  CHECK(e.isNil("SetNameField"));
}

TEST_CASE("VHelpWindow: SetPage clamps, shows the layer, sets the title and counter and the buttons' state") {
  MenuEnv e;
  REQUIRE(e.booted);
  // The title text lives outside the help window, in the modal, as in help.lua.
  e.make("Text", named(fontStyle(0, 0, 200, 30, 20), "txt_title"));
  ui::MapStyle hs = rectStyle(0, 0, 500, 300);
  VHelpWindow& help = static_cast<VHelpWindow&>(e.open("VHelpWindow", hs));
  CHECK(help.layerCount() == 10);
  help.setCurrentLayer(-1);  // `SelectLayer(-1)` for the shared children
  e.make("Text", named(fontStyle(400, 10, 40, 18, 15), "txt_page"));
  ui::MapStyle back = named(rectStyle(0, 200, 100, 40), "btn_back");
  e.make("RDisableButton", back);
  ui::MapStyle next = named(rectStyle(300, 200, 100, 40), "btn_next");
  e.make("RDisableButton", next);
  for (int layer : {0, 1, 9}) {
    help.setCurrentLayer(layer);
    e.make("Text", named(fontStyle(0, 0, 100, 20, 15), "page" + std::to_string(layer)));
  }
  help.setCurrentLayer(-1);
  e.close(help, hs);
  auto* nextBtn = dynamic_cast<RDisableButton*>(e.wm.findInTopModal("btn_next"));
  auto* backBtn = dynamic_cast<RDisableButton*>(e.wm.findInTopModal("btn_back"));
  REQUIRE(nextBtn);
  REQUIRE(backBtn);

  REQUIRE(e.run("SetPage(0)") == 0);
  CHECK(help.page() == 0);
  CHECK(help.currentLayer() == 0);
  CHECK(e.wm.findInTopModal("txt_title")->label() == "Page Zero");
  CHECK(e.wm.findInTopModal("txt_page")->label() == "1/10");
  CHECK(backBtn->disabled());
  CHECK_FALSE(nextBtn->disabled());
  CHECK(e.wm.findInTopModal("page0")->shown());
  CHECK_FALSE(e.wm.findInTopModal("page1")->shown());
  CHECK(nextBtn->shown());  // the shared buttons show on every layer

  REQUIRE(e.run("SetPage(1)") == 0);
  CHECK(e.wm.findInTopModal("txt_title")->label() == "Page One");
  CHECK(e.wm.findInTopModal("txt_page")->label() == "2/10");
  CHECK_FALSE(backBtn->disabled());
  CHECK(e.wm.findInTopModal("page1")->shown());

  // Out of range clamps; the last page disables Next.
  REQUIRE(e.run("SetPage(99)") == 0);
  CHECK(help.page() == 9);
  CHECK(e.wm.findInTopModal("txt_title")->label() == "Page Nine");
  CHECK(e.wm.findInTopModal("txt_page")->label() == "10/10");
  CHECK(nextBtn->disabled());
  CHECK_FALSE(backBtn->disabled());
  REQUIRE(e.run("SetPage(-4)") == 0);
  CHECK(help.page() == 0);

  // The buttons' presses step a page; the message is not consumed.
  CHECK_FALSE(help.onMessage({ui::kButtonPress, "btn_next", 0}));
  CHECK(help.page() == 1);
  CHECK_FALSE(help.onMessage({ui::kButtonPress, "btn_back", 0}));
  CHECK(help.page() == 0);
  CHECK_FALSE(help.onMessage({ui::kButtonPress, "btn_back", 0}));  // clamped at the first page
  CHECK(help.page() == 0);
  CHECK_FALSE(help.onMessage({ui::kButtonPress, "other", 0}));
  CHECK(help.page() == 0);
  e.wm.destroyAll();
  CHECK(e.isNil("SetPage"));
}

TEST_CASE("the two GREATCC sliders are volume sliders with a default of 750 per mille each") {
  const ui::VolumeSlider* sfx = ui::findVolumeSlider("options_sfxlevel_slider");
  const ui::VolumeSlider* mus = ui::findVolumeSlider("options_musiclevel_slider");
  REQUIRE(sfx);
  REQUIRE(mus);
  CHECK(std::string(sfx->pref) == "soundlevel");
  CHECK(std::string(mus->pref) == "musiclevel");
  CHECK(sfx->defaultPerMille == 750);
  CHECK(mus->defaultPerMille == 750);
  CHECK(sfx->channel == 0);
  CHECK(mus->channel == 1);
}

TEST_CASE("TOptions: sliders from the prefs, fills as wide as the value, checkboxes from the state, the tutorials flag") {
  CHECK(TOptions::fillWidth(300, 0.5f) == 150);
  CHECK(TOptions::fillWidth(301, 0.75f) == 225);  // truncated
  CHECK(TOptions::fillWidth(301, 0.0f) == 0);
  CHECK(TOptions::fillWidth(301, 1.0f) == 301);

  MenuEnv e;
  REQUIRE(e.booted);
  REQUIRE(e.ctx.users->create("Ann") >= 0);
  e.ctx.users->setPref("soundlevel", "500");
  e.ctx.users->setPref("musiclevel", "300");
  e.ctx.users->setPref("soundoff", "1");
  ui::MapStyle os = rectStyle(0, 0, 600, 400);
  ui::Window& opt = e.open("TOptions", os);
  ui::MapStyle sfx = named(rectStyle(0, 10, 300, 36), "options_sfxlevel_slider");
  ui::MapStyle mus = named(rectStyle(0, 60, 300, 36), "options_musiclevel_slider");
  e.make("Slider", sfx);
  e.make("Slider", mus);
  ui::MapStyle fillS = named(rectStyle(0, 10, 100, 20), "sound_fill");
  fillS.set("image", std::string("image/bt_up"));  // 40 x 20
  e.make("Bitmap", fillS);
  ui::MapStyle fillM = named(rectStyle(0, 60, 100, 20), "music_fill");
  fillM.set("image", std::string("image/bt_up"));
  e.make("Bitmap", fillM);
  for (const char* n : {"options_fullscreen_checkbox", "options_muteall_checkbox", "options_tutorials_checkbox"}) {
    ui::MapStyle bs = named(rectStyle(0, 200, 60, 30), n);
    bs.set("type", 1.0);
    e.make("Button", bs);
  }
  e.close(opt, os);

  auto* sfxSlider = dynamic_cast<ui::SliderWindow*>(opt.find("options_sfxlevel_slider"));
  auto* musSlider = dynamic_cast<ui::SliderWindow*>(opt.find("options_musiclevel_slider"));
  REQUIRE(sfxSlider);
  REQUIRE(musSlider);
  CHECK(sfxSlider->value() == doctest::Approx(0.5f));
  CHECK(musSlider->value() == doctest::Approx(0.3f));
  CHECK(opt.find("sound_fill")->rect().w == 20);  // 40 * 0.5
  CHECK(opt.find("sound_fill")->rect().h == 20);
  CHECK(opt.find("music_fill")->rect().w == 12);  // 40 * 0.3, truncated
  CHECK_FALSE(dynamic_cast<ui::ButtonWindow*>(opt.find("options_fullscreen_checkbox"))->on());
  CHECK(dynamic_cast<ui::ButtonWindow*>(opt.find("options_muteall_checkbox"))->on());  // soundoff = 1
  auto* tut = dynamic_cast<ui::ButtonWindow*>(opt.find("options_tutorials_checkbox"));
  CHECK(tut->on());  // tutorials default to shown

  // A slider change resizes its fill and applies live (the message is handled).
  sfxSlider->setValue(0.25f, true);
  CHECK(opt.onMessage({ui::kSliderValChanged, "options_sfxlevel_slider", 0}));
  CHECK(opt.find("sound_fill")->rect().w == 10);
  CHECK(opt.find("music_fill")->rect().w == 12);

  // The tutorials checkbox: its press flips the flag Progress persists.
  auto* world = worldOf(e.ctx);
  REQUIRE(world);
  CHECK(world->progress().showTutorials());
  CHECK_FALSE(opt.onMessage({ui::kButtonPress, "options_tutorials_checkbox", 0}));
  CHECK_FALSE(world->progress().showTutorials());
  CHECK_FALSE(opt.onMessage({ui::kButtonPress, "options_tutorials_checkbox", 0}));
  CHECK(world->progress().showTutorials());
  CHECK_FALSE(opt.onMessage({ui::kButtonPress, "options_muteall_checkbox", 0}));
  CHECK(world->progress().showTutorials());
}

TEST_CASE("the shared SaveVolumes stores GREATCC's two sliders as per mille of the current user") {
  MenuEnv e;
  REQUIRE(e.booted);
  REQUIRE(e.ctx.users->create("Ann") >= 0);
  ui::MapStyle os = rectStyle(0, 0, 600, 400);
  ui::Window& opt = e.open("TOptions", os);
  e.make("Slider", named(rectStyle(0, 10, 300, 36), "options_sfxlevel_slider"));
  e.make("Slider", named(rectStyle(0, 60, 300, 36), "options_musiclevel_slider"));
  e.close(opt, os);
  dynamic_cast<ui::SliderWindow*>(opt.find("options_sfxlevel_slider"))->setValue(0.4f, true);
  dynamic_cast<ui::SliderWindow*>(opt.find("options_musiclevel_slider"))->setValue(1.0f, true);
  REQUIRE(e.run("SaveVolumes()") == 0);
  CHECK(e.ctx.users->pref("soundlevel") == "400");
  CHECK(e.ctx.users->pref("musiclevel") == "1000");
}

TEST_CASE("TCredits: sends itself behind its siblings and fills the version text from `app_version` and the version config") {
  MenuEnv e;
  REQUIRE(e.booted);
  e.config.set("version", "3.1");
  ui::MapStyle box = rectStyle(0, 0, 400, 300);
  ui::Window& screen = e.open("RWindow", box);
  ui::MapStyle frame = rectStyle(0, 0, 400, 300);
  ui::Window& art = e.make("Bitmap", frame);
  ui::MapStyle vs = named(fontStyle(0, 200, 400, 20, 16), "credits_version");
  e.make("Text", vs);
  ui::MapStyle cs = rectStyle(0, 40, 400, 150);
  cs.set("file", std::string("credits/credits.txt"));  // not in the fixture: the scroller stays empty, the rest still works
  ui::Window& credits = e.make("TCredits", cs);
  e.close(screen, box);
  CHECK(std::string(credits.typeName()) == "TCredits");
  CHECK(screen.children().back().get() == &credits);  // SetWindowDepth(0): the last child is drawn first
  CHECK(screen.children().front().get() != &credits);
  (void)art;
  CHECK(screen.find("credits_version")->label() == "Version 3.1");
}

TEST_CASE("Window::sendToBack and bringToFront reorder a window among its siblings") {
  Bare b;
  ui::Window& a = b.make<ui::BitmapWindow>(rectStyle(0, 0, 10, 10));
  ui::Window& c = b.make<ui::BitmapWindow>(rectStyle(0, 0, 10, 10));
  ui::Window& d = b.make<ui::BitmapWindow>(rectStyle(0, 0, 10, 10));
  ui::Window* parent = a.parent();
  REQUIRE(parent);
  auto order = [&] {
    std::vector<ui::Window*> v;
    for (const auto& k : parent->children()) v.push_back(k.get());
    return v;
  };
  CHECK(order() == std::vector<ui::Window*>{&d, &c, &a});  // the last one made is in front
  d.sendToBack();
  CHECK(order() == std::vector<ui::Window*>{&c, &a, &d});
  d.bringToFront();
  CHECK(order() == std::vector<ui::Window*>{&d, &c, &a});
  c.bringToFront();
  c.sendToBack();
  CHECK(order() == std::vector<ui::Window*>{&d, &a, &c});
}
