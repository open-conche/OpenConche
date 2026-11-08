#include <doctest/doctest.h>

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "ScriptHarness.h"
#include "app/ScriptDriver.h"
#include "gfx/TextureCache.h"
#include "script/ScriptHost.h"
#include "ui/Input.h"
#include "ui/WindowManager.h"
#include "ui/widgets/Widgets.h"

using namespace choco;
using ui::ButtonKind;
using ui::ButtonWindow;
using ui::InputEvent;

namespace {

// What the window side hands to the script, recorded.
struct Sink : ui::ScriptSink {
  struct Delivered {
    ui::Message message;
    bool hasMessage = false, hasCommand = false;
  };
  std::vector<Delivered> got;
  std::map<std::string, std::string> globals;
  std::function<void()> onCommand;  // runs while a button command is delivered (to detach the button, say)
  void deliver(const ui::Message* m, const script::LuaRef* cmd) override {
    if (cmd && onCommand) onCommand();
    Delivered d;
    d.hasMessage = m != nullptr;
    if (m) d.message = *m;
    d.hasCommand = cmd != nullptr;
    got.push_back(std::move(d));
  }
  void runInScript(const script::LuaRef&) override {}
  bool callProtected(const script::LuaRef&) override { return true; }
  void setGlobalString(const char* n, std::string_view v) override { globals[n] = std::string(v); }
  int commands() const {
    int n = 0;
    for (const auto& d : got) n += d.hasCommand;
    return n;
  }
  int ofType(int type) const {
    int n = 0;
    for (const auto& d : got) n += d.hasMessage && d.message.type == type;
    return n;
  }
};

InputEvent mouse(InputEvent::Type t, double x, double y) {
  InputEvent e;
  e.type = t;
  e.x = static_cast<float>(x);
  e.y = static_cast<float>(y);
  return e;
}
InputEvent down(float x, float y) { return mouse(InputEvent::Type::MouseDown, x, y); }
InputEvent up(float x, float y) { return mouse(InputEvent::Type::MouseUp, x, y); }
InputEvent move(float x, float y) { return mouse(InputEvent::Type::MouseMove, x, y); }
InputEvent key(const char* name, bool alt = false, bool ctrl = false, bool shift = false) {
  InputEvent e;
  e.type = InputEvent::Type::Key;
  e.key = {name, shift, ctrl, alt};
  return e;
}

// A window that records the events it sees (with window-local points) and answers each with a fixed result.
struct Probe : ui::Window {
  Probe(ui::WindowManager& wm, std::string n, std::vector<std::string>* log, bool handles) : Window(wm), log_(log), handles_(handles) {
    setName(std::move(n));
  }
  bool onMouseDown(int x, int y) override { return note("down", x, y); }
  bool onMouseUp(int x, int y) override { return note("up", x, y); }
  bool onMouseMove(int x, int y) override { return note("move", x, y); }
  void onMouseLeave() override { log_->push_back(name() + ":leave"); }
  void onSetFocus(ui::WindowId old) override {
    log_->push_back(name() + ":focus");
    oldFocus = old;
  }
  bool onKeyDown(const ui::KeyEvent& k) override {
    log_->push_back(name() + ":key " + k.name);
    return handles_;
  }
  bool note(const char* what, int x, int y) {
    log_->push_back(name() + ":" + what + " " + std::to_string(x) + "," + std::to_string(y));
    return handles_;
  }
  void setFocusable() { setFlags(flags() | kFocusableFlag); }
  std::vector<std::string>* log_;
  bool handles_;
  ui::WindowId oldFocus = 0;
};

Probe& addProbe(ui::Window& parent, const char* name, gfx::RectI r, std::vector<std::string>* log, bool handles) {
  auto p = std::make_unique<Probe>(parent.manager(), name, log, handles);
  p->setRect(r);
  return static_cast<Probe&>(parent.adoptFront(std::move(p)));
}

// A booted script host over the Reforged-free modal assets, with the button textures in tests/assets/button.
struct Env : test::ScriptEnv {
  Env()
      : ScriptEnv(std::string(CHOCO_TEST_ASSETS) + "/ui/modal"),
        btnFs(std::string(CHOCO_TEST_ASSETS) + "/button", ""),
        cache(btnFs, nullptr),
        host(ctx) {
    wm.setTextures(&cache);
    wm.setScriptSink(&sink);  // after the host took it: commands are only recorded
    wm.setLogger([this](const std::string& l) { logged.push_back(l); });
    booted = host.boot();
  }
  ButtonWindow* button(const char* name) { return dynamic_cast<ButtonWindow*>(wm.findInTopModal(name)); }
  void pump() {
    wm.clearDestroyed();
    wm.dispatchMessages();
  }
  assets::AssetFS btnFs;
  gfx::TextureCache cache;
  script::ScriptHost host;
  Sink sink;
  std::vector<std::string> logged;
  bool booted = false;
};

const char* kPush = "{'image/bt_up','image/bt_down','image/bt_over'}";
const char* kFour = "{'image/bt_up','image/bt_on','image/bt_over','image/bt_down'}";

// Builds one button in the top modal the way DoWindow does.
std::string buttonLua(const std::string& fields, const std::string& inner = {}) {
  return "do local t = { typename = 'Button', " + fields + " }; GenericCreate(t); " + inner + "PostCreate(t, 'Button'); PopWindow() end";
}

// A Text child named `label`, built inside the button as the Lua Button constructor does.
const char* kLabelChild = "do local x = { typename = 'Text', name = 'label', label = '#Go', x = 0, y = 0, w = 40, h = 20 }; "
                          "GenericCreate(x); PostCreate(x, 'Text'); PopWindow() end; ";

}  // namespace

TEST_CASE("buttons: the StateUpdate layer table for push, toggle and radio") {
  using B = ButtonWindow;
  const int tableOff[4] = {0, 1, 2, -1}, tableOn[4] = {1, 0, 3, -1};
  for (int s = 0; s < 4; ++s) {
    CHECK(B::layerFor(ButtonKind::Push, s, false) == (s == 3 ? -1 : s));
    CHECK(B::layerFor(ButtonKind::Push, s, true) == (s == 3 ? -1 : s));  // `on` means nothing to a push button
    for (ButtonKind k : {ButtonKind::Toggle, ButtonKind::Radio}) {
      CHECK(B::layerFor(k, s, false) == tableOff[s]);
      CHECK(B::layerFor(k, s, true) == tableOn[s]);
    }
  }
}

TEST_CASE("keys: NormalizeKeyString and the hotkey string of an event") {
  CHECK(ui::normalizeKeyString("alt-s") == "alt-S");
  CHECK(ui::normalizeKeyString("x") == "X");
  CHECK(ui::normalizeKeyString("Shift-Ctrl-q") == "shift-ctrl-Q");
  CHECK(ui::normalizeKeyString("control-a") == "ctrl-A");
  CHECK(ui::normalizeKeyString("@Escape") == "@escape");  // "@name" keeps its text and skips the modifier parse
  CHECK(ui::hotkeyOf(ui::KeyEvent{"S", false, false, true}, true) == "alt-S");
  CHECK(ui::hotkeyOf(ui::KeyEvent{"S", false, false, true}, false) == "S");
  CHECK(ui::hotkeyOf(ui::KeyEvent{"Return", false, false, true}, true) == "@return");
  CHECK(ui::keyChar(ui::KeyEvent{"A"}) == 'a');
  CHECK(ui::keyChar(ui::KeyEvent{"Escape"}) == 27);
}

TEST_CASE("input: hit testing goes front to back, skips hidden windows, falls through to the sibling behind, then the parent") {
  ui::WindowManager wm;
  Sink sink;
  wm.setScriptSink(&sink);
  std::vector<std::string> log;
  ui::Window& modal = wm.pushModal("m");
  Probe& parent = addProbe(modal, "P", {100, 100, 400, 300}, &log, false);
  Probe& back = addProbe(parent, "A", {10, 10, 100, 100}, &log, false);
  Probe& front = addProbe(parent, "B", {50, 50, 100, 100}, &log, false);
  Probe& hidden = addProbe(parent, "H", {0, 0, 300, 300}, &log, true);
  hidden.setShown(false);
  Probe& inner = addProbe(front, "BC", {0, 0, 20, 20}, &log, false);
  (void)back, (void)inner;
  CHECK(parent.children()[0].get() == &hidden);  // the last one added is frontmost

  // Screen (170,170) is parent-local (70,70): inside B (50..150) and A (10..110); BC covers B-local (0..20)^2 only.
  wm.handleInput(down(170, 170));
  CHECK(log == std::vector<std::string>{"B:down 20,20", "A:down 60,60", "P:down 70,70"});  // front, behind, then parent
  log.clear();

  // Over BC (screen 160,160 = B-local 10,10): the deepest first, then B, then the sibling behind B, then the parent.
  wm.handleInput(down(160, 160));
  CHECK(log == std::vector<std::string>{"BC:down 10,10", "B:down 10,10", "A:down 50,50", "P:down 60,60"});
  log.clear();

  // Outside A and B but inside P: only the parent. A mouse down nobody handles posts nothing.
  wm.handleInput(down(400, 350));
  CHECK(log == std::vector<std::string>{"P:down 300,250"});
  wm.dispatchMessages();
  CHECK(sink.got.empty());

  // A handler stops the search; an unhandled mouse up reaches the modal and posts kPressAnyKey "mouse:up".
  front.handles_ = true;
  log.clear();
  wm.handleInput(up(170, 170));
  CHECK(log == std::vector<std::string>{"B:up 20,20"});
  front.handles_ = false;
  wm.handleInput(up(400, 350));
  wm.dispatchMessages();
  REQUIRE(sink.got.size() == 1);
  CHECK(sink.got[0].message.type == ui::kPressAnyKey);
  CHECK(sink.got[0].message.name == "mouse:up");
}

TEST_CASE("input: overlaymouse silences kPressAnyKey; keys post raw:<char>") {
  ui::WindowManager wm;
  Sink sink;
  wm.setScriptSink(&sink);
  wm.pushModal("m");
  bool overlay = true;
  wm.setOverlayMouseQuery([&] { return overlay; });
  wm.handleInput(up(5, 5));
  wm.handleInput(key("A"));
  wm.dispatchMessages();
  CHECK(sink.got.empty());
  overlay = false;
  wm.handleInput(up(5, 5));
  wm.handleInput(key("A"));
  wm.dispatchMessages();
  REQUIRE(sink.got.size() == 2);
  CHECK(sink.got[0].message.name == "mouse:up");
  CHECK(sink.got[1].message.type == ui::kPressAnyKey);
  CHECK(sink.got[1].message.name == "raw:a");
  // A mouse event with the pointer in the letterbox bars is ignored, a right click too.
  sink.got.clear();
  InputEvent bar = up(5, 5);
  bar.inArea = false;
  InputEvent right = up(5, 5);
  right.button = 3;
  wm.handleInput(down(5, 5));
  wm.handleInput(right);
  wm.dispatchMessages();
  CHECK(sink.got.size() == 0);
  wm.handleInput(bar);  // an up outside the picture still counts as an up
  wm.dispatchMessages();
  CHECK(sink.got.size() == 1);
}

TEST_CASE("input: a captured window gets mouse events before the hit test; hover follows the handler") {
  ui::WindowManager wm;
  std::vector<std::string> log;
  ui::Window& modal = wm.pushModal("m");
  Probe& a = addProbe(modal, "A", {0, 0, 100, 100}, &log, true);
  Probe& b = addProbe(modal, "B", {200, 0, 100, 100}, &log, true);
  wm.handleInput(move(10, 10));
  CHECK(wm.hover() == a.id());
  wm.handleInput(move(210, 10));  // hover moves: the old one hears about it
  CHECK(wm.hover() == b.id());
  CHECK(log == std::vector<std::string>{"A:move 10,10", "B:move 10,10", "A:leave"});
  log.clear();
  // Capture A: its handler now sees an event far outside it, before anything under the pointer.
  wm.addMouseListener(a.id());
  wm.handleInput(down(250, 50));
  CHECK(log == std::vector<std::string>{"A:down 250,50"});
  wm.removeMouseListener(a.id());
  log.clear();
  // A listener that does not handle the event lets it continue down the normal path.
  a.handles_ = false;
  wm.addMouseListener(a.id());
  wm.handleInput(down(250, 50));
  CHECK(log == std::vector<std::string>{"A:down 250,50", "B:down 50,50"});
  // A modal push cancels capture and hover.
  log.clear();
  wm.pushModal("m2");
  CHECK(log == std::vector<std::string>{"B:leave", "A:leave"});  // the hovered window, then the captured one
  CHECK(wm.hover() == 0);
  CHECK_FALSE(wm.isMouseListener(a.id()));
}

TEST_CASE("input: a mouse down focuses the nearest focusable ancestor; SetFocus calls onSetFocus") {
  Env e;
  REQUIRE(e.booted);
  std::vector<std::string> log;
  REQUIRE(e.run("PushModal('f')") == 0);
  ui::Window& modal = *e.wm.topModal();
  Probe& pane = addProbe(modal, "pane", {0, 0, 200, 200}, &log, false);
  pane.setFocusable();
  Probe& leaf = addProbe(pane, "leaf", {10, 10, 50, 50}, &log, false);
  (void)leaf;
  e.wm.handleInput(down(20, 20));
  CHECK(e.wm.focus() == pane.id());
  CHECK(log == std::vector<std::string>{"pane:focus", "leaf:down 10,10", "pane:down 20,20"});  // focus first
  e.wm.handleInput(down(500, 500));  // nothing there: the modal itself is focusable
  CHECK(e.wm.focus() == modal.id());
  log.clear();
  REQUIRE(e.run("SetFocus('pane')") == 0);  // RealSetFocus looks under the creation stack top (the modal)
  CHECK(e.wm.focus() == pane.id());
  CHECK(pane.oldFocus == modal.id());
  REQUIRE(e.run("SetFocus('nothing')") == 0);
  CHECK(e.wm.focus() == 0);  // a missing name clears focus
  // Keys with no focus go to the top modal; with a focus they bubble from it.
  log.clear();
  e.wm.setFocus(pane.id());
  e.wm.handleInput(key("Q"));
  CHECK(log == std::vector<std::string>{"pane:focus", "pane:key Q"});
}

TEST_CASE("Button: images become layers, sizes follow the image and scale, and the mouse moves between layers") {
  Env e;
  REQUIRE(e.booted);
  REQUIRE(e.run("PushModal('mb')") == 0);
  REQUIRE(e.run(buttonLua(std::string("name = 'b', x = 100, y = 50, graphics = ") + kPush + ", scale = 2")) == 0);
  ButtonWindow* b = e.button("b");
  REQUIRE(b);
  CHECK(b->rect() == gfx::RectI{100, 50, 80, 40});  // 40x20 images at scale 2
  REQUIRE(b->children().size() == 3);
  CHECK(b->currentLayer() == 0);
  auto shownImages = [&] {
    std::string s;
    for (int i = 0; i < 3; ++i) s += b->find("image" + std::to_string(i))->shown() ? '1' : '0';
    return s;
  };
  CHECK(shownImages() == "100");
  CHECK(b->layerMembers(2).size() == 1);
  e.wm.handleInput(move(110, 60));
  CHECK(b->mouseState() == ButtonWindow::kOver);
  CHECK(shownImages() == "001");
  e.wm.handleInput(down(110, 60));
  CHECK(b->mouseState() == ButtonWindow::kDown);
  CHECK(shownImages() == "010");
  CHECK(e.wm.isMouseListener(b->id()));
  e.wm.handleInput(move(500, 500));  // dragging off cancels the press
  CHECK(b->mouseState() == ButtonWindow::kNormal);
  CHECK(shownImages() == "100");
  CHECK_FALSE(e.wm.isMouseListener(b->id()));
  e.wm.handleInput(up(500, 500));
  e.pump();
  CHECK(e.sink.commands() == 0);
  CHECK(e.sink.ofType(ui::kButtonPress) == 0);
  e.sink.got.clear();  // the cancelled press ended with an up that nothing handled: a kPressAnyKey
  // Hover again, then click: the command is delivered synchronously, kButtonPress follows through the queue.
  REQUIRE(e.run("SetCommand('b', function() end)") == 0);
  e.wm.handleInput(move(110, 60));
  e.wm.handleInput(down(110, 60));
  e.wm.handleInput(up(110, 60));
  CHECK(e.sink.commands() == 1);  // before any dispatch
  REQUIRE(e.sink.got.size() == 1);
  CHECK(e.sink.got[0].message.type == ui::kCommandOnly);
  CHECK(e.sink.got[0].message.name == "LuaAction:b");
  e.pump();
  CHECK(e.sink.ofType(ui::kButtonPress) == 1);
  CHECK(e.sink.got.back().message.name == "b");
  CHECK(b->mouseState() == ButtonWindow::kOver);
  CHECK(shownImages() == "001");
  CHECK(e.sink.ofType(ui::kPressAnyKey) == 0);  // a handled click posts no "any key"
}

TEST_CASE("Button: an explicit style w/h stands above the images (TButton::Init fixes the size before the fit)") {
  Env e;
  REQUIRE(e.booted);
  REQUIRE(e.run("PushModal('s')") == 0);
  // ui/options.lua's checkbox buttons: FixSize runs while the rect is still empty (FUN_00421d20 at the top of
  // TButton::Init), so the explicit w=120/h=26 lands; the image layers then fit grow-only (FUN_00422280 takes the
  // current size into GetLargest) and cannot shrink it. Before this was read, the fit ran first and the 26x13
  // scaled checkbox bitmaps swallowed the 120px hit area - and with it a Text at x=26 with w=kMax, which
  // resolved to 0 and never drew.
  REQUIRE(e.run(buttonLua(std::string("name = 'cb', x = 0, y = 0, w = 120, h = 26, scale = 0.65, graphics = ") +
                          kFour,
                          "do local x = { typename = 'Text', name = 'lbl', label = '#Full Screen', x = 26, y = 0, "
                          "w = 160000, h = 160000 }; GenericCreate(x); PostCreate(x, 'Text'); PopWindow() end; ")) == 0);
  ButtonWindow* b = e.button("cb");
  REQUIRE(b);
  CHECK(b->rect() == gfx::RectI{0, 0, 120, 26});
  CHECK(b->find("image0")->rect() == gfx::RectI{0, 0, 26, 13});  // 40x20 art at scale 0.65
  ui::Window* lbl = b->find("lbl");
  REQUIRE(lbl);
  CHECK(lbl->rect() == gfx::RectI{26, 0, 94, 26});  // kMax fills the parent: (120-26)x26
}

TEST_CASE("Button: an empty graphics entry carries the previous image forward into its layer") {
  Env e;
  REQUIRE(e.run("PushModal('mb')") == 0);
  // TButton::Init keeps a "current image" that only a non-empty entry replaces, and adopts it into every layer it visits,
  // so an empty entry re-adopts the previous image (one shared window, not a copy).
  REQUIRE(e.run(buttonLua("name = 'one', x = 100, y = 50, graphics = {'image/bt_up'}")) == 0);
  REQUIRE(e.run(buttonLua("name = 'two', x = 300, y = 50, graphics = {'image/bt_up','image/bt_down'}")) == 0);
  REQUIRE(e.run(buttonLua("name = 'rad', x = 100, y = 200, type = 2, on = true, "
                          "graphics = {'image/bt_up','image/bt_on','image/bt_over'}")) == 0);
  REQUIRE(e.run(buttonLua("name = 'lead', x = 300, y = 200, graphics = {'','image/bt_down'}")) == 0);
  REQUIRE(e.run(buttonLua("name = 'kid', x = 100, y = 350, graphics = {'image/bt_up'}",
                          "DoSelectLayer(2); GenericCreate({ typename = 'Window', name = 'extra' }); PopWindow(); ")) == 0);
  // No images at all: the per-layer behaviour is kept (label-only hit areas).
  REQUIRE(e.run(buttonLua("name = 'none', x = 300, y = 350, w = 40, h = 20, graphics = {}")) == 0);
  auto shown = [&](ButtonWindow* b, int n) {
    std::string s;
    for (int i = 0; i < n; ++i) {
      ui::Window* im = b->find("image" + std::to_string(i));
      s += im && im->shown() ? '1' : '0';
    }
    return s;
  };
  ButtonWindow* one = e.button("one");
  ButtonWindow* two = e.button("two");
  ButtonWindow* rad = e.button("rad");
  ButtonWindow* lead = e.button("lead");
  ButtonWindow* kid = e.button("kid");
  ButtonWindow* none = e.button("none");
  REQUIRE((one && two && rad && lead && kid && none));
  // One image: on every layer.
  CHECK(one->children().size() == 1);
  for (int l = 0; l < 3; ++l) CHECK(one->layerMembers(l).size() == 1);
  CHECK(shown(one, 1) == "1");
  e.wm.handleInput(move(110, 60));
  CHECK(one->mouseState() == ButtonWindow::kOver);
  CHECK(shown(one, 1) == "1");
  e.wm.handleInput(down(110, 60));
  CHECK(shown(one, 1) == "1");
  e.wm.handleInput(move(500, 500));
  e.wm.handleInput(up(500, 500));
  // {a, b}: hover shows b (carried into layer 2).
  e.wm.handleInput(move(310, 60));
  CHECK(two->mouseState() == ButtonWindow::kOver);
  CHECK(shown(two, 2) == "01");
  e.wm.handleInput(down(310, 60));
  CHECK(shown(two, 2) == "01");
  e.wm.handleInput(move(500, 500));
  e.wm.handleInput(up(500, 500));
  // Radio with three images: over-on (layer 3) shows image 2.
  CHECK(rad->on());
  e.wm.handleInput(move(110, 210));
  CHECK(rad->mouseState() == ButtonWindow::kOver);
  CHECK(shown(rad, 3) == "001");
  e.wm.handleInput(move(500, 500));
  // A leading empty entry: nothing on layer 0, and layer 2 holds b.
  CHECK(lead->layerMembers(0).empty());
  CHECK(lead->layerMembers(2).size() == 1);
  e.wm.handleInput(move(310, 210));
  CHECK(lead->mouseState() == ButtonWindow::kOver);
  CHECK(shown(lead, 2) == "01");
  e.wm.handleInput(move(500, 500));
  // A child added with SelectLayer(2) shows on hover together with the carried image.
  CHECK(shown(kid, 1) == "1");
  CHECK_FALSE(kid->find("extra")->shown());
  e.wm.handleInput(move(110, 360));
  CHECK(kid->mouseState() == ButtonWindow::kOver);
  CHECK(shown(kid, 1) == "1");
  CHECK(kid->find("extra")->shown());
  e.wm.handleInput(move(500, 500));
  e.wm.handleInput(move(310, 360));
  CHECK(none->mouseState() == ButtonWindow::kOver);
  CHECK(none->currentLayer() == 2);  // untouched: nothing to carry
}

TEST_CASE("Button: a mask maps by proportion, so a scaled button hits where its picture is lit") {
  Env e;
  REQUIRE(e.booted);
  REQUIRE(e.run("PushModal('mb')") == 0);
  // The mask is 20x10 with the left half lit; the button is 40x20 scaled by 2 = 80x40 (not the mask's size).
  REQUIRE(e.run(buttonLua(std::string("name = 'b', x = 100, y = 100, scale = 2, mask = 'image/bt_mask', graphics = ") + kPush +
                          ", command = function() end")) == 0);
  ButtonWindow* b = e.button("b");
  REQUIRE(b);
  CHECK(b->rect().w == 80);
  CHECK(b->hitsPoint(5, 5));
  CHECK(b->hitsPoint(39, 39));
  CHECK_FALSE(b->hitsPoint(40, 5));  // right half: dark mask
  CHECK_FALSE(b->hitsPoint(75, 35));
  CHECK_FALSE(b->hitsPoint(80, 5));  // outside the rectangle
  CHECK_FALSE(b->hitsPoint(-1, 5));
  // Lit side: a click presses. Dark side: the press is swallowed (no fall-through, no kPressAnyKey) and starts nothing.
  e.wm.handleInput(down(150, 110));
  e.wm.handleInput(up(150, 110));
  CHECK(e.sink.commands() == 0);
  CHECK(e.wm.hover() == 0);
  e.pump();
  CHECK(e.sink.ofType(ui::kPressAnyKey) == 0);
  CHECK(b->mouseState() == ButtonWindow::kNormal);
  e.wm.handleInput(down(110, 110));
  e.wm.handleInput(up(110, 110));
  CHECK(e.sink.commands() == 1);
  // A dark pixel is "R, G and B all <= 19": the threshold is 19.
  gfx::HitMask m(2, 1, {19, 19, 19, 19, 20, 19});
  CHECK_FALSE(m.hit(0, 0));
  CHECK(m.hit(1, 0));
}

TEST_CASE("Button: EnableWindow hides it from hit testing and drops its capture") {
  Env e;
  REQUIRE(e.booted);
  REQUIRE(e.run("PushModal('mb')") == 0);
  REQUIRE(e.run(buttonLua(std::string("name = 'b', graphics = ") + kPush + ", command = function() end")) == 0);
  ButtonWindow* b = e.button("b");
  REQUIRE(b);
  e.wm.handleInput(down(5, 5));
  CHECK(b->mouseState() == ButtonWindow::kDown);
  REQUIRE(e.run("EnableWindow('b', false)") == 0);
  CHECK(b->mouseState() == ButtonWindow::kNormal);
  CHECK_FALSE(e.wm.isMouseListener(b->id()));
  e.wm.handleInput(up(5, 5));
  e.wm.handleInput(down(5, 5));
  e.wm.handleInput(up(5, 5));
  e.pump();
  CHECK(e.sink.commands() == 0);
  CHECK(e.sink.ofType(ui::kPressAnyKey) == 2);  // ups that nothing handled
  REQUIRE(e.run("EnableWindow('b', true)") == 0);
  e.wm.handleInput(down(5, 5));
  e.wm.handleInput(up(5, 5));
  CHECK(e.sink.commands() == 1);
}

TEST_CASE("Button: close posts a close request for the modal and sets gReturnValue; the message names the button") {
  Env e;
  REQUIRE(e.booted);
  REQUIRE(e.run("PushModal('dlg')") == 0);
  REQUIRE(e.run(buttonLua(std::string("name = 'ok', close = true, graphics = ") + kPush)) == 0);  // no command at all
  const int depth = e.wm.modalDepth();  // the host's own modal is below
  e.wm.handleInput(down(5, 5));
  e.wm.handleInput(up(5, 5));
  CHECK(e.sink.globals["gReturnValue"] == "ok");
  CHECK(e.sink.commands() == 0);
  e.pump();  // kCloseWindow reaches the modal, which pops itself; kButtonPress goes to the script
  CHECK(e.wm.modalDepth() == depth - 1);
  CHECK(e.sink.ofType(ui::kButtonPress) == 1);
}

TEST_CASE("Button: toggles and radios keep their state and show the toggle layers") {
  Env e;
  REQUIRE(e.booted);
  REQUIRE(e.run("PushModal('t')") == 0);
  REQUIRE(e.run(buttonLua(std::string("name = 'chk', type = kToggle, x = 0, y = 0, graphics = ") + kFour)) == 0);
  ButtonWindow* chk = e.button("chk");
  REQUIRE(chk);
  CHECK(chk->kind() == ButtonKind::Toggle);
  CHECK(chk->layerMembers(3).size() == 1);  // four images
  CHECK_FALSE(chk->on());
  CHECK(chk->currentLayer() == 0);
  e.wm.handleInput(move(5, 5));
  CHECK(chk->currentLayer() == 2);  // over, off
  e.wm.handleInput(down(5, 5));
  CHECK(chk->currentLayer() == 1);  // down while off shows the "on" picture
  e.wm.handleInput(up(5, 5));
  CHECK(chk->on());
  CHECK(chk->currentLayer() == 3);  // over, on
  e.wm.handleInput(move(500, 500));
  CHECK(chk->currentLayer() == 1);  // normal, on
  e.wm.handleInput(move(5, 5));
  e.wm.handleInput(down(5, 5));
  CHECK(chk->currentLayer() == 0);  // down while on shows the "off" picture
  e.wm.handleInput(up(5, 5));
  CHECK_FALSE(chk->on());
  // The natives.
  REQUIRE(e.run("gA = GetButtonToggleState('chk'); SetButtonToggleState('chk', true); gB = GetButtonToggleState('chk'); "
                "gC = GetButtonToggleState('missing')") == 0);
  CHECK(e.getString("gA") == "false");
  CHECK(e.getString("gB") == "true");
  CHECK(e.getString("gC") == "false");
  CHECK(chk->on());
}

TEST_CASE("Button: radio buttons turn their group's others off; beginGroup starts a new group") {
  Env e;
  REQUIRE(e.booted);
  REQUIRE(e.run("PushModal('r')") == 0);
  // Created in order r1 r2 | r3 r4 (r1 and r3 begin groups); the button-less windows are not part of any group.
  const char* names[] = {"r1", "r2", "r3", "r4"};
  for (int i = 0; i < 4; ++i) {
    std::string f = std::string("name = '") + names[i] + "', type = kRadio, x = " + std::to_string(i * 50) + ", graphics = " + kFour;
    if (i == 0 || i == 2) f += ", beginGroup = 1";
    REQUIRE(e.run(buttonLua(f)) == 0);
  }
  REQUIRE(e.run("SetButtonToggleState('r1', true); SetButtonToggleState('r3', true)") == 0);
  CHECK(e.button("r1")->on());
  CHECK(e.button("r3")->on());
  REQUIRE(e.run("SetButtonToggleState('r2', true)") == 0);
  CHECK_FALSE(e.button("r1")->on());  // same group as r2
  CHECK(e.button("r2")->on());
  CHECK(e.button("r3")->on());  // the other group is untouched
  REQUIRE(e.run("SetButtonToggleState('r4', true)") == 0);
  CHECK_FALSE(e.button("r3")->on());
  CHECK(e.button("r2")->on());
  // Clicking a radio that is on keeps it on (a radio never toggles off by itself).
  e.wm.handleInput(down(55, 5));
  e.wm.handleInput(up(55, 5));
  CHECK(e.button("r2")->on());
}

TEST_CASE("Button: SelectLayer puts later children on a layer, and a button's labels follow the state") {
  Env e;
  REQUIRE(e.booted);
  REQUIRE(e.run("PushModal('l')") == 0);
  REQUIRE(e.run("do local t = { typename = 'Button', name = 'b', graphics = " + std::string(kPush) +
                " }; GenericCreate(t); DoSelectLayer(kAllLayers); GenericCreate({ typename = 'Window', name = 'all' }); PopWindow(); "
                "DoSelectLayer(0); GenericCreate({ typename = 'Window', name = 'n' }); PopWindow(); "
                "DoSelectLayer(1); GenericCreate({ typename = 'Window', name = 'd' }); PopWindow(); "
                "DoSelectLayer(2); GenericCreate({ typename = 'Window', name = 'o' }); PopWindow(); "
                "PostCreate(t, 'Button'); PopWindow() end") == 0);
  ButtonWindow* b = e.button("b");
  REQUIRE(b);
  auto shown = [&] {
    std::string s;
    for (const char* n : {"all", "n", "d", "o"}) s += b->find(n)->shown() ? '1' : '0';
    return s;
  };
  CHECK(shown() == "1100");  // PostCreate shows layer 0: the all-layers child and the normal one
  e.wm.handleInput(move(5, 5));
  CHECK(shown() == "1001");
  e.wm.handleInput(down(5, 5));
  CHECK(shown() == "1010");
  // DoSelectLayer on a window that is not layered does nothing.
  REQUIRE(e.run("PushModal('plain'); DoSelectLayer(2)") == 0);
  CHECK(e.log.count("[lua-arg]") == 0);
}

TEST_CASE("Button hotkeys, Enter and Escape: RegisterHotkey presses the button; default and cancel; kDefaultAction") {
  Env e;
  REQUIRE(e.booted);
  REQUIRE(e.run("PushModal('h')") == 0);
  REQUIRE(e.run(buttonLua(std::string("name = 'save', x = 0, graphics = ") + kPush + ", command = function() end")) == 0);
  REQUIRE(e.run(buttonLua(std::string("name = 'ok', x = 100, default = true, graphics = ") + kPush + ", command = function() end")) == 0);
  REQUIRE(e.run(buttonLua(std::string("name = 'no', x = 200, cancel = true, graphics = ") + kPush + ", command = function() end")) == 0);
  REQUIRE(e.run("RegisterHotkey('alt-s', 'save'); RegisterHotkey('q', 'ghost'); RegisterHotkey('', 'save'); "
                "RegisterHotkey('F1', nil)") == 0);
  CHECK(e.wm.topModal()->hotkeys().size() == 2);  // alt-S and Q; the empty key and the missing button were ignored

  e.wm.handleInput(key("S", /*alt=*/true));
  CHECK(e.sink.commands() == 1);
  e.wm.handleInput(key("S"));  // plain s: no such hotkey, and it is a plain key press for the script
  CHECK(e.sink.commands() == 1);
  e.pump();
  CHECK(e.sink.ofType(ui::kButtonPress) == 1);
  CHECK(e.sink.ofType(ui::kPressAnyKey) == 1);  // the plain "s"

  e.wm.handleInput(key("Q"));  // registered for a button that does not exist
  REQUIRE(!e.logged.empty());
  CHECK(e.logged.back() == "ERROR: Hot key button not found. Button:'ghost'");
  CHECK(e.sink.commands() == 1);

  // The pressed button stays attached, so kDefaultAction follows its command at once (not queued).
  auto lastCommand = [&] {
    for (auto it = e.sink.got.rbegin(); it != e.sink.got.rend(); ++it)
      if (it->hasCommand) return it->message.name;
    return std::string();
  };
  const int defaults = e.sink.ofType(ui::kDefaultAction);
  e.wm.handleInput(key("Return"));
  CHECK(e.sink.commands() == 2);
  CHECK(lastCommand() == "LuaAction:ok");
  CHECK(e.sink.ofType(ui::kDefaultAction) == defaults + 1);
  CHECK(e.wm.pendingMessages() >= 1);  // only the kButtonPress is queued
  e.wm.handleInput(key("Escape"));
  CHECK(e.sink.commands() == 3);
  CHECK(lastCommand() == "LuaAction:no");

  // Without default and cancel buttons, Enter sends kDefaultAction instead.
  REQUIRE(e.run("EnableWindow('ok', false); EnableWindow('no', false)") == 0);
  e.pump();
  e.sink.got.clear();
  e.wm.handleInput(key("Return"));
  e.pump();
  CHECK(e.sink.commands() == 0);
  CHECK(e.sink.ofType(ui::kDefaultAction) == 1);

  // TModalWindow::PressButton presses a hidden hotkey button too.
  REQUIRE(e.run("EnableWindow('save', false)") == 0);
  e.wm.handleInput(key("S", true));
  CHECK(e.sink.commands() == 1);
}

TEST_CASE("natives: SetCommand replaces the command, StripKeyFromLabel splits the key out") {
  Env e;
  REQUIRE(e.booted);
  REQUIRE(e.run("PushModal('c')") == 0);
  REQUIRE(e.run(buttonLua(std::string("name = 'b', graphics = ") + kPush, kLabelChild)) == 0);
  ButtonWindow* b = e.button("b");
  REQUIRE(b);
  CHECK_FALSE(b->hasCommand());
  REQUIRE(e.run("SetCommand('b', function() end); SetCommand('nobody', function() end)") == 0);
  CHECK(b->hasCommand());
  REQUIRE(e.run("gL, gK = StripKeyFromLabel('Sa&ve'); gL2, gK2 = StripKeyFromLabel('Plain'); gL3, gK3 = StripKeyFromLabel('End&')") == 0);
  CHECK(e.getString("gL") == "Save");
  CHECK(e.getString("gK") == "v");
  CHECK(e.getString("gL2") == "Plain");
  CHECK(e.getString("gK2") == "");
  CHECK(e.getString("gL3") == "End");
  CHECK(e.getString("gK3") == "");
  // GetLabel / SetLabel on a button read and write its child Text named `label`.
  REQUIRE(e.run("gBefore = GetLabel('b'); SetLabel('b', 'Went'); gGot = GetLabel('b')") == 0);
  CHECK(e.getString("gBefore") == "Go");
  CHECK(e.getString("gGot") == "Went");
  // Without a label child, GetLabel is "" whatever the style's label key says.
  REQUIRE(e.run(buttonLua(std::string("name = 'nolabel', label = 'key_only', graphics = ") + kPush)) == 0);
  REQUIRE(e.run("gNone = GetLabel('nolabel')") == 0);
  CHECK(e.getString("gNone") == "");
}

TEST_CASE("script harness: expect-label parses a window and the rest of the line, and WindowManager answers it") {
  auto p = app::parseScript("expect-label ok  Hello there\nexpect-label\n");
  REQUIRE(p.commands.size() == 1);
  CHECK(p.commands[0].kind == app::ScriptCommand::Kind::ExpectLabel);
  CHECK(p.commands[0].text == "ok");
  CHECK(p.commands[0].text2 == "Hello there");
  CHECK(p.errors.size() == 1);
  Env e;
  REQUIRE(e.booted);
  REQUIRE(e.run("PushModal('ml')") == 0);
  REQUIRE(e.run(buttonLua(std::string("name = 'b', label = 'Go', graphics = ") + kPush, kLabelChild)) == 0);
  std::string got;
  CHECK(e.wm.labelOf("b", got));
  CHECK(got == "Go");
  CHECK_FALSE(e.wm.labelOf("ml", got));  // a modal has no label
  CHECK_FALSE(e.wm.labelOf("nothing", got));
}

TEST_CASE("Button: a hovered button listens for the mouse, so a modal push or pop sends it OnMouseLeave") {
  Env e;
  REQUIRE(e.booted);
  REQUIRE(e.run("PushModal('hv')") == 0);
  REQUIRE(e.run(buttonLua(std::string("name = 'b', x = 100, y = 100, graphics = ") + kPush + ", command = function() end")) == 0);
  ButtonWindow* b = e.button("b");
  REQUIRE(b);
  e.wm.handleInput(move(110, 110));
  CHECK(b->mouseState() == ButtonWindow::kOver);
  CHECK(e.wm.isMouseListener(b->id()));
  e.wm.pushModal("above");  // a dialog opens over it
  CHECK(b->mouseState() == ButtonWindow::kNormal);
  CHECK(b->currentLayer() == 0);
  CHECK_FALSE(e.wm.isMouseListener(b->id()));
  // The same when the hovered window is not a listener: cancelMouseState tells it first.
  e.wm.popModal("above");
  e.pump();
  e.wm.handleInput(move(110, 110));
  CHECK(b->mouseState() == ButtonWindow::kOver);
  e.wm.popModal("hv");
  CHECK(b->mouseState() == ButtonWindow::kNormal);
}

TEST_CASE("Button: a toggle flips after its command, and only if the button is still attached") {
  Env e;
  REQUIRE(e.booted);
  REQUIRE(e.run("PushModal('tg')") == 0);
  REQUIRE(e.run(buttonLua(std::string("name = 'chk', type = kToggle, x = 0, y = 0, graphics = ") + kFour + ", command = function() end")) == 0);
  ButtonWindow* chk = e.button("chk");
  REQUIRE(chk);
  bool onDuringCommand = true;
  e.sink.onCommand = [&] { onDuringCommand = chk->on(); };
  e.wm.handleInput(move(5, 5));
  e.wm.handleInput(down(5, 5));
  e.wm.handleInput(up(5, 5));
  CHECK_FALSE(onDuringCommand);  // the state is still the old one while the command runs
  CHECK(chk->on());
  // A command that closes the dialog leaves the toggle as it was.
  e.sink.onCommand = [&] { e.wm.popModal("tg"); };
  e.wm.handleInput(move(5, 5));
  e.wm.handleInput(down(5, 5));
  e.wm.handleInput(up(5, 5));
  CHECK(chk->on());  // not flipped back
  CHECK(chk->mouseState() == ButtonWindow::kNormal);  // it was told the mouse left
}

TEST_CASE("Button: radios with no beginGroup before them clear nothing") {
  Env e;
  REQUIRE(e.booted);
  REQUIRE(e.run("PushModal('rg')") == 0);
  for (int i = 0; i < 2; ++i)
    REQUIRE(e.run(buttonLua(std::string("name = 'q") + std::to_string(i) + "', type = kRadio, x = " + std::to_string(i * 50) + ", graphics = " + kFour)) == 0);
  REQUIRE(e.run("SetButtonToggleState('q0', true); SetButtonToggleState('q1', true)") == 0);
  CHECK(e.button("q0")->on());
  CHECK(e.button("q1")->on());
}

TEST_CASE("Escape: the last enabled cancel wins; completing the pair ends only that sibling list") {
  Env e;
  REQUIRE(e.booted);
  REQUIRE(e.run("PushModal('h')") == 0);
  const std::string btn = std::string(", graphics = ") + kPush + ", command = function() end";
  // Children are adopted at the front, so everything is added in reverse. The tree reads:
  //   box { c1 (cancel), d (default), c2 (cancel) }, c_outer (cancel)
  // Inside box the pair is complete after d, so c2 is skipped; the walk returns to the top list, where c_outer is a
  // later sibling of the container and still replaces c1.
  REQUIRE(e.run(buttonLua("name = 'c_outer', x = 0, cancel = true" + btn)) == 0);
  REQUIRE(e.run("do local t = { typename = 'Window', name = 'box', x = 0, y = 100, w = 400, h = 60 }; GenericCreate(t); " +
                buttonLua("name = 'c2', x = 200, cancel = true" + btn) + " " + buttonLua("name = 'd', x = 100, default = true" + btn) +
                " " + buttonLua("name = 'c1', x = 0, cancel = true" + btn) + " PostCreate(t, 'Window'); PopWindow() end") == 0);
  auto lastCommand = [&] {
    for (auto it = e.sink.got.rbegin(); it != e.sink.got.rend(); ++it)
      if (it->hasCommand) return it->message.name;
    return std::string();
  };
  const auto& kids = e.wm.topModal()->children();
  REQUIRE(kids.size() == 2);
  CHECK(kids[0]->name() == "box");
  e.wm.handleInput(key("Escape"));
  CHECK(lastCommand() == "LuaAction:c_outer");  // not c1 (first wins) and not c2 (past the end of its list)
  REQUIRE(e.run("EnableWindow('c_outer', false)") == 0);
  e.wm.handleInput(key("Escape"));
  CHECK(lastCommand() == "LuaAction:c1");  // c2 comes after the completing button and is never reached
  e.wm.handleInput(key("Return"));
  CHECK(lastCommand() == "LuaAction:d");
}
