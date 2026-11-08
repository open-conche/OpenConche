#include <doctest/doctest.h>

#include <memory>
#include <set>
#include <string>
#include <vector>

#include "ScriptHarness.h"
#include "audio/Audio.h"
#include "sdk/Users.h"
#include "gfx/RecordingRenderer.h"
#include "gfx/TextureCache.h"
#include "gfx/text/TextLayout.h"
#include "ui/Layout.h"
#include "ui/WindowManager.h"
#include "ui/widgets/InputWidgets.h"
#include "ui/widgets/Widgets.h"

using namespace choco;
using ui::BSGWindow;
using ui::InputEvent;
using ui::SliderWindow;
using ui::TextEditWindow;

namespace {

struct Uploader : gfx::TextureUploader {
  void* upload(const std::uint8_t*, int, int) override { return new int(0); }
  void release(void* h) override { delete static_cast<int*>(h); }
};

// A manager with a text system, one modal to build into, and the button textures for sliders.
struct Env {
  Uploader up;
  gfx::text::TextSystem ts{nullptr, &up};
  assets::AssetFS fs{std::string(CHOCO_TEST_ASSETS) + "/button", ""};
  gfx::TextureCache cache{fs, nullptr};
  ui::WindowManager wm;
  ui::Window* modal = nullptr;
  Env() {
    wm.setTextSystem(&ts);
    wm.setTextures(&cache);
    modal = &wm.pushModal("m");
    modal->setRect({0, 0, 800, 600});
  }
  template <class W>
  W& make(ui::Window& parent, const ui::MapStyle& s) {
    auto w = std::make_unique<W>(wm);
    W* raw = w.get();
    parent.adoptFront(std::move(w));
    raw->init(s);
    return *raw;
  }
  TextEditWindow& edit(ui::MapStyle s, const std::string& label = "") {
    s.set("x", 10.0).set("y", 10.0).set("w", 200.0).set("h", 30.0).set("label", label).set("name", std::string("entry"));
    s.set("font", std::vector<double>{0, 20});
    return make<TextEditWindow>(*modal, s);
  }
};

InputEvent mouse(InputEvent::Type t, double x, double y) {
  InputEvent e;
  e.type = t;
  e.x = static_cast<float>(x);
  e.y = static_cast<float>(y);
  return e;
}
InputEvent textEvent(const std::string& s) {
  InputEvent e;
  e.type = InputEvent::Type::Text;
  e.text = s;
  return e;
}
InputEvent keyEvent(const char* name, bool ctrl = false) {
  InputEvent e;
  e.type = InputEvent::Type::Key;
  e.key = {name, false, ctrl, false};
  return e;
}

// Records what reaches a window as a message.
struct Recorder : ui::Window {
  explicit Recorder(ui::WindowManager& wm) : Window(wm) {}
  bool onMessage(const ui::Message& m) override {
    got.push_back(m.type);
    names.push_back(m.name);
    return true;
  }
  std::vector<int> got;
  std::vector<std::string> names;
};

}  // namespace

// ---------------------------------------------------------------- TextEdit

TEST_CASE("TextEdit: a non-UTF-8 field holds length-1 characters; a utf8 field holds length code points") {
  Env e;
  auto& plain = e.edit({});  // length defaults to 10
  CHECK(plain.maxLength() == 10);
  CHECK(plain.typeText(std::string(12, 'a')) == true);
  CHECK(plain.text() == std::string(9, 'a'));
  CHECK_FALSE(plain.typeCharacter("b"));
  auto& wide = e.edit(ui::MapStyle().set("utf8", 1.0).set("length", 3.0));
  wide.typeText("a\xc3\xa9\xc3\xa9\xc3\xa9");  // a, e-acute x3: the fourth character is refused
  CHECK(wide.text() == "a\xc3\xa9\xc3\xa9");
  wide.typeText("zzz");
  CHECK(wide.text() == "a\xc3\xa9\xc3\xa9");  // still full: 3 characters
  wide.typeCharacter("\b");  // a UTF-8 backspace removes the whole character
  CHECK(wide.text() == "a\xc3\xa9");
}

TEST_CASE("TextEdit: control characters, non-ASCII and the ignore string") {
  Env e;
  auto& a = e.edit({});
  CHECK_FALSE(a.typeCharacter("\x01"));
  CHECK_FALSE(a.typeCharacter("\xc3\xa9"));  // non-ASCII without utf8 or password
  CHECK(a.text().empty());
  auto& pw = e.edit(ui::MapStyle().set("password", 1.0));
  CHECK(pw.typeCharacter("\xc3\xa9"));  // password accepts it
  auto& u = e.edit(ui::MapStyle().set("utf8", 1.0).set("ignore", std::string("x-")));
  CHECK(u.typeText("ax-b"));
  CHECK(u.text() == "ab");  // the ignored characters are swallowed: handled, not inserted
  CHECK(u.typeCharacter("x"));
  CHECK(u.text() == "ab");
}

TEST_CASE("TextEdit: password shows one star per byte and GetLabel returns the real text") {
  Env e;
  auto& pw = e.edit(ui::MapStyle().set("password", 1.0));
  pw.typeText("ab");
  pw.typeCharacter("\xc3\xa9");  // two bytes
  CHECK(pw.displayText() == "****");
  CHECK(pw.label() == "ab\xc3\xa9");
  CHECK(pw.password());
}

TEST_CASE("TextEdit: caret movement, Delete and Backspace work on whole characters") {
  Env e;
  auto& t = e.edit(ui::MapStyle().set("utf8", 1.0), "abc");
  CHECK(t.caretByte() == 3);  // SetText puts the caret at the end
  CHECK(t.editKey("Left"));
  CHECK(t.caretByte() == 2);
  t.typeCharacter("X");
  CHECK(t.text() == "abXc");
  CHECK(t.editKey("Delete"));  // removes the c after the caret
  CHECK(t.text() == "abX");
  t.typeCharacter("\b");
  CHECK(t.text() == "ab");
  CHECK(t.editKey("Right"));
  CHECK(t.typeCharacter("\b"));
  CHECK(t.text() == "a");
  t.setLabel("zz\xc3\xa9");
  CHECK(t.caretByte() == 4);
  t.editKey("Left");
  CHECK(t.caretByte() == 2);  // stepped over the two-byte character
  t.setLabel("");
  CHECK_FALSE(t.typeCharacter("\b"));  // nothing to delete
}

TEST_CASE("TextEdit: clearinitial clears the placeholder on the first key, a rejected key or a click") {
  Env e;
  auto& a = e.edit(ui::MapStyle().set("clearinitial", 1.0), "Player");
  CHECK(a.initialPending());
  CHECK(a.text() == "Player");
  a.typeText("hi");
  CHECK(a.text() == "hi");
  CHECK_FALSE(a.initialPending());
  auto& b = e.edit(ui::MapStyle().set("clearinitial", 1.0).set("length", 3.0), "Player");
  b.editKey("Left");  // even a cursor key counts
  CHECK(b.text().empty());
  auto& c = e.edit(ui::MapStyle().set("clearinitial", 1.0), "Player");
  CHECK(c.onMouseUp(1, 1));  // the first click clears it
  CHECK(c.text().empty());
  CHECK_FALSE(c.onMouseUp(1, 1));
  auto& d = e.edit(ui::MapStyle(), "Keep");  // without clearinitial nothing is cleared
  d.typeCharacter("!");
  CHECK(d.text() == "Keep!");
}

TEST_CASE("TextEdit: enablewindow shows the named window only while the field has text") {
  Env e;
  auto okOwned = std::make_unique<ui::PlaceholderWindow>(e.wm, "Window");
  ui::Window& ok = e.modal->adoptFront(std::move(okOwned));
  ok.setName("ok");
  ok.setShown(false);  // the script disabled it while the placeholder was showing
  auto& t = e.edit(ui::MapStyle().set("clearinitial", 1.0).set("enablewindow", std::string("ok")), "Name");
  CHECK_FALSE(ok.shown());
  t.typeCharacter("a");  // the first key clears the placeholder, then the field has text
  CHECK(ok.shown());
  t.typeCharacter("b");
  CHECK(ok.shown());
  t.typeCharacter("\b");
  CHECK(ok.shown());  // one character left
  t.typeCharacter("\b");  // the check runs before the removal: length 1 - 1 < 1
  CHECK_FALSE(ok.shown());
  t.typeCharacter("c");
  CHECK(ok.shown());
}

TEST_CASE("TextEdit: typing posts kTextEditChanged and plays the typing sound") {
  Env e;
  std::vector<std::string> sounds;
  e.wm.setSoundHook([&](const std::string& f, bool) { sounds.push_back(f); });
  e.wm.setTypingSound("sfx/sfx_typing_", 4, ".ogg?group=1000");  // Chocolatier 3's profile value
  auto& t = e.edit({});
  t.typeCharacter("a");
  t.typeCharacter("\x01");  // rejected: no message, no sound
  CHECK(e.wm.pendingMessages() == 1);
  REQUIRE(sounds.size() == 1);
  CHECK(sounds[0].rfind("sfx/sfx_typing_0", 0) == 0);
  CHECK(sounds[0].find(".ogg?group=1000") != std::string::npos);
}

TEST_CASE("TextEdit: with no typing sound, typing plays nothing") {
  Env e;
  std::vector<std::string> sounds;
  e.wm.setSoundHook([&](const std::string& f, bool) { sounds.push_back(f); });
  auto& t = e.edit({});
  t.typeCharacter("a");
  t.typeCharacter("b");
  CHECK(e.wm.pendingMessages() == 2);
  CHECK(sounds.empty());
}

TEST_CASE("TextEdit: Chocolatier 1's typing sound names audio/sfx/typing_01..05.ogg") {
  Env e;
  std::vector<std::string> sounds;
  e.wm.setSoundHook([&](const std::string& f, bool) { sounds.push_back(f); });
  e.wm.setTypingSound("audio/sfx/typing_", 5, ".ogg");
  auto& t = e.edit(ui::MapStyle().set("length", 100.0));
  for (int i = 0; i < 40; ++i) t.typeCharacter("a");
  REQUIRE(sounds.size() == 40);
  std::set<std::string> seen;
  for (const auto& s : sounds) {
    CHECK(s.rfind("audio/sfx/typing_0", 0) == 0);
    CHECK(s.size() == std::string("audio/sfx/typing_01.ogg").size());
    CHECK(s[s.size() - 5] >= '1');
    CHECK(s[s.size() - 5] <= '5');
    seen.insert(s);
  }
  CHECK(seen.size() > 1);
}

TEST_CASE("TextEdit: Chocolatier 3's first eight typing sounds are the ones the hard-coded pick gave") {
  Env e;
  std::vector<std::string> sounds;
  e.wm.setSoundHook([&](const std::string& f, bool) { sounds.push_back(f); });
  e.wm.setTypingSound("sfx/sfx_typing_", 4, ".ogg?group=1000");
  auto& t = e.edit({});
  for (int i = 0; i < 8; ++i) t.typeCharacter("a");
  // Captured from the code before the sound moved to the profile: LCG from seed 1, ((seed >> 16) & 3) + 1.
  const std::vector<std::string> expected = {
      "sfx/sfx_typing_03.ogg?group=1000", "sfx/sfx_typing_03.ogg?group=1000", "sfx/sfx_typing_02.ogg?group=1000",
      "sfx/sfx_typing_04.ogg?group=1000", "sfx/sfx_typing_04.ogg?group=1000", "sfx/sfx_typing_04.ogg?group=1000",
      "sfx/sfx_typing_03.ogg?group=1000", "sfx/sfx_typing_04.ogg?group=1000"};
  CHECK(sounds == expected);
}

TEST_CASE("TextEdit: the caret shows only while focused and in the visible half of the 800 ms cycle") {
  CHECK_FALSE(TextEditWindow::caretPhaseVisible(0));
  CHECK_FALSE(TextEditWindow::caretPhaseVisible(400));
  CHECK(TextEditWindow::caretPhaseVisible(401));
  CHECK(TextEditWindow::caretPhaseVisible(799));
  CHECK_FALSE(TextEditWindow::caretPhaseVisible(800));
  CHECK(TextEditWindow::caretPhaseVisible(1201));

  Env e;
  gfx::RecordingRenderer rr;
  auto& t = e.edit({}, "ab");
  auto bars = [&] {
    rr.clear();
    ui::DrawContext ctx{rr, {0, 0}, 1.0f};
    ui::drawWindow(*e.modal, ctx);
    int n = 0;
    for (const auto& c : rr.calls())
      if (c.kind == gfx::RecordingRenderer::Call::Kind::FillRect && c.dst.w == 1) ++n;
    return n;
  };
  e.wm.setNowMs(500);
  CHECK(bars() == 0);  // not focused: no caret even in the visible phase
  e.wm.setFocus(t.id());
  CHECK(bars() == 1);
  e.wm.setNowMs(100);
  CHECK(bars() == 0);  // hidden phase
  e.wm.setNowMs(1250);
  CHECK(bars() == 1);
}

TEST_CASE("TextEdit: text events reach only the focused edit, and printable keys are consumed") {
  Env e;
  auto& a = e.edit({});
  auto& b = e.edit({});
  e.wm.setFocus(b.id());
  e.wm.handleInput(textEvent("hi"));
  CHECK(a.text().empty());
  CHECK(b.text() == "hi");
  e.wm.handleInput(keyEvent("Backspace"));
  CHECK(b.text() == "h");
  // A hotkey on the modal does not fire for a printable key while an edit has the focus.
  e.modal->setName("m");
  static_cast<ui::ModalWindow*>(e.modal)->addHotkey("q", "nothing");
  e.wm.handleInput(keyEvent("Q"));
  CHECK(e.wm.pendingMessages() >= 1);
  e.wm.dispatchMessages();
  CHECK(b.text() == "h");
}

// ---------------------------------------------------------------- Slider

namespace {
ui::MapStyle sliderStyle(double x, double y, double w, double h) {
  ui::MapStyle s;
  s.set("x", x).set("y", y).set("w", w).set("h", h).set("name", std::string("vol"));
  s.set("railtop", std::string("image/bt_up")).set("railmid", std::string("image/bt_up")).set("railbot", std::string("image/bt_up"));
  s.set("sliderimage", std::string("image/bt_down")).set("sliderrollimage", std::string("image/bt_over"));
  return s;
}
}  // namespace

TEST_CASE("Slider: orientation, value clamping and the knob position") {
  Env e;
  auto& h = e.make<SliderWindow>(*e.modal, sliderStyle(0, 0, 200, 0));
  CHECK(h.horizontal());
  auto& v = e.make<SliderWindow>(*e.modal, sliderStyle(0, 0, 0, 200));
  CHECK_FALSE(v.horizontal());
  h.setValue(2.5f, true);
  CHECK(h.value() == 1.0f);
  h.setValue(-1, true);
  CHECK(h.value() == 0.0f);
  h.setValue(std::nanf(""), true);
  CHECK(h.value() == 1.0f);  // NaN becomes 1
  h.setValue(0.5f, true);
  const gfx::RectI k = h.knobRect();
  CHECK(k.x == static_cast<int>((h.rect().w - k.w) * 0.5f));
  CHECK(h.rect().h >= k.h);
  v.setValue(1.0f, true);
  CHECK(v.knobRect().y == v.rect().h - v.knobRect().h);
}

TEST_CASE("Slider: the knob image is loaded and its position and the dump follow the value") {
  Env e;
  auto& h = e.make<SliderWindow>(*e.modal, sliderStyle(0, 0, 200, 0));
  h.setValue(0.25f, true);
  const gfx::RectI k = h.knobRect();
  CHECK(k.w > 0);
  CHECK(k.h > 0);
  CHECK(k.x == static_cast<int>((h.rect().w - k.w) * 0.25f));
  const std::string d = h.describe();
  CHECK(d.find("knob=" + std::to_string(k.x) + "," + std::to_string(k.y) + "," + std::to_string(k.w) + "," + std::to_string(k.h)) !=
        std::string::npos);
  CHECK(d.find("noknob") == std::string::npos);
  h.setValue(0.75f, true);
  CHECK(h.knobRect().x > k.x);
  ui::MapStyle none;
  none.set("x", 0.0).set("y", 0.0).set("w", 200.0).set("h", 0.0).set("name", std::string("bare"));
  auto& bare = e.make<SliderWindow>(*e.modal, none);
  CHECK(bare.describe().find("noknob") != std::string::npos);
}

TEST_CASE("Slider: dragging the knob sets the value from the pointer and sends the messages to the parent") {
  Env e;
  auto rec = std::make_unique<Recorder>(e.wm);
  Recorder& parent = *rec;
  parent.setRect({100, 50, 400, 100});
  e.modal->adoptFront(std::move(rec));
  auto& s = e.make<SliderWindow>(parent, sliderStyle(20, 10, 200, 0));
  s.setValue(0.5f, true);
  CHECK(parent.got.empty());  // quiet
  const gfx::RectI k = s.knobRect();
  const float kx = 100 + 20 + k.x + k.w / 2.0f, ky = 50 + 10 + k.y + k.h / 2.0f;
  e.wm.handleInput(mouse(InputEvent::Type::MouseMove, kx, ky));
  e.wm.handleInput(mouse(InputEvent::Type::MouseDown, kx, ky));
  CHECK(s.dragging());
  CHECK(e.wm.isMouseListener(s.id()));
  CHECK(parent.got.empty());  // pressing the knob changes nothing
  // Drag far outside the rail on the right: captured, clamped to 1.
  e.wm.handleInput(mouse(InputEvent::Type::MouseMove, 100 + 20 + 50, ky + 300));
  CHECK(s.value() == doctest::Approx(50.0 / 200.0));
  CHECK(parent.got == std::vector<int>{ui::kSliderValChanged});
  CHECK(parent.names[0] == "vol");
  e.wm.handleInput(mouse(InputEvent::Type::MouseMove, 100 + 20 + 900, ky));
  CHECK(s.value() == 1.0f);
  e.wm.handleInput(mouse(InputEvent::Type::MouseUp, 100 + 20 + 900, ky));
  CHECK_FALSE(s.dragging());
  CHECK(parent.got.back() == ui::kSliderMouseUp);
  CHECK_FALSE(e.wm.isMouseListener(s.id()));
  const std::size_t n = parent.got.size();
  // A press on the rail beside the knob posts a page message and moves nothing.
  s.setValue(0.5f, true);
  const gfx::RectI k2 = s.knobRect();
  e.wm.handleInput(mouse(InputEvent::Type::MouseDown, 100 + 20 + k2.x + k2.w + 5, ky));
  CHECK(parent.got.back() == ui::kSliderPageDown);
  e.wm.handleInput(mouse(InputEvent::Type::MouseUp, 100 + 20 + k2.x + k2.w + 5, ky));
  e.wm.handleInput(mouse(InputEvent::Type::MouseDown, 100 + 20 + 2, ky));
  CHECK(parent.got.back() == ui::kSliderPageUp);
  CHECK(parent.got.size() == n + 2);
  CHECK(s.value() == 0.5f);
}

TEST_CASE("Slider: horizontal rail pieces are source-flipped, the knob is upright (TSlider's FUN_0043bdc0)") {
  // FUN_0043bdc0 maps the vertical rail art with the matrix [[0,1],[1,0]]: the image's y axis becomes the rail's
  // length (+x) and its x axis becomes +y, so the art's LEFT edge ends up at the TOP of the piece. A plain -90
  // rotation maps the x axis to -y and puts it at the bottom, which lifted Chocolatier 1's rails 12 of 24 pixels -
  // straight through the labels' text. The flip restores the original placement; the knob never rotates.
  {
    Env e;
    e.make<SliderWindow>(*e.modal, sliderStyle(0, 0, 200, 0));
    gfx::RecordingRenderer rr;
    e.wm.draw(rr);
    int pieces = 0, flipped = 0, knob = 0;
    for (const auto& c : rr.calls()) {
      if (c.kind != gfx::RecordingRenderer::Call::Kind::DrawTexture) continue;
      if (c.rotation == 0.0f) {
        ++knob;
        CHECK_FALSE(c.hflip);
      } else {
        CHECK(c.rotation == doctest::Approx(-90.0f));
        ++pieces;
        flipped += c.hflip;
      }
    }
    CHECK(pieces == 3);  // railtop, railmid, railbot
    CHECK(flipped == 3);
    CHECK(knob == 1);
  }
  {
    Env e;
    e.make<SliderWindow>(*e.modal, sliderStyle(220, 0, 0, 200));
    gfx::RecordingRenderer rr;
    e.wm.draw(rr);
    for (const auto& c : rr.calls())
      if (c.kind == gfx::RecordingRenderer::Call::Kind::DrawTexture) {
        CHECK(c.rotation == 0.0f);  // the vertical slider draws as is
        CHECK_FALSE(c.hflip);
      }
  }
}

// ---------------------------------------------------------------- BSGWindow

TEST_CASE("BSGWindow: the colour's alpha is the window's alpha and reaches the children, the default 0 included") {
  Env e;
  ui::MapStyle none;
  auto& plain = e.make<BSGWindow>(*e.modal, none);
  plain.postChildrenInit(none);
  CHECK(plain.alpha() == 0.0f);  // the fill is invisible by default
  CHECK_FALSE(plain.swallowsMouse());
  auto& kid = e.make<ui::BitmapWindow>(plain, none);
  plain.postChildrenInit(none);
  CHECK(kid.alpha() == 0.0f);  // SetAlpha(0, true) runs for the default colour too: the children start hidden
  CHECK(plain.onMouseDown(0, 0) == false);

  auto& framed = e.make<BSGWindow>(*e.modal, none);
  auto& child = e.make<ui::BitmapWindow>(framed, none);
  ui::MapStyle coloured;
  coloured.set("color", std::vector<double>{0.1, 0.2, 0.3, 0.5}).set("swallowmouse", 1.0);
  framed.postChildrenInit(coloured);
  CHECK(framed.alpha() == doctest::Approx(0.5f));
  CHECK(child.alpha() == doctest::Approx(0.5f));  // SetAlpha(a, true) reaches the children that take fades
  CHECK(framed.color().b == doctest::Approx(0.3f));
  CHECK(framed.onMouseDown(0, 0));  // swallowed
  CHECK(framed.onMouseMove(0, 0));
}

// ---------------------------------------------------------------- Popup placement

TEST_CASE("Rollover popups are kept inside the screen with a 4 pixel margin") {
  using ui::PopupWindow;
  CHECK(PopupWindow::reposition(100, 100, 50, 50, 800, 600) == std::pair<int, int>{100, 100});
  // Overflowing the right edge: moved left by the overflow and down 32 pixels.
  CHECK(PopupWindow::reposition(780, 100, 50, 50, 800, 600) == std::pair<int, int>{746, 132});
  // Overflowing the bottom: moved up by the overflow.
  CHECK(PopupWindow::reposition(100, 580, 50, 50, 800, 600) == std::pair<int, int>{100, 546});
}

// ---------------------------------------------------------------- with the script host

namespace {

// A booted host over tests/assets/ui/modal (Main has a dialog open and waits), with the real host as the script sink.
struct HostEnv : test::ScriptEnv {
  HostEnv()
      : ScriptEnv(std::string(CHOCO_TEST_ASSETS) + "/ui/modal"), btnFs(std::string(CHOCO_TEST_ASSETS) + "/button", ""),
        cache(btnFs, nullptr), host(ctx) {
    wm.setTextures(&cache);
    booted = host.boot();
  }
  void pump() {
    wm.clearDestroyed();
    wm.dispatchMessages();
  }
  InputEvent move(float x, float y) { return mouse(InputEvent::Type::MouseMove, x, y); }
  assets::AssetFS btnFs;
  gfx::TextureCache cache;
  script::ScriptHost host;
  bool booted = false;
};

const char* kRollover =
    "do local t = { typename = 'Rollover', name = 'hot', x = 300, y = 300, w = 40, h = 40, xOffset = 5, yOffset = 6, "
    "contents = \"gBuilt = (gBuilt or 0) + 1; local x = { typename = 'Text', name = 'tip', label = '#tip', x = 0, y = 0, w = 60, h = 20 }; "
    "GenericCreate(x); PostCreate(x, 'Text'); PopWindow()\", command = function() gClicked = (gClicked or 0) + 1 end }; "
    "GenericCreate(t); PostCreate(t, 'Rollover'); PopWindow() end";

}  // namespace

TEST_CASE("Rollover: hovering builds the popup from `contents`, fades it in over 125 ms, and leaving removes it") {
  HostEnv e;
  REQUIRE(e.booted);
  REQUIRE(e.run(kRollover) == 0);
  auto* hot = dynamic_cast<ui::RolloverWindow*>(e.wm.findInTopModal("hot"));
  REQUIRE(hot);
  CHECK(hot->contents().find("gBuilt") == 0);
  CHECK(hot->hasCommand());
  CHECK(e.wm.findInTopModal("tip") == nullptr);

  e.wm.tickTransitions(1000);
  e.wm.handleInput(e.move(310, 310));
  ui::Window* tip = e.wm.findInTopModal("tip");
  REQUIRE(tip);
  CHECK(e.getNumber("gBuilt") == 1);
  ui::Window* popup = tip->parent();
  REQUIRE(popup);
  CHECK(popup->typeName() == "PopupWindow");
  CHECK(popup->parent() == e.wm.topModal());  // a child of the top modal
  CHECK(e.wm.isMouseListener(popup->id()));
  CHECK(e.wm.transitionCount() == 1);
  CHECK(popup->alpha() == 0.0f);
  CHECK(tip->alpha() == 0.0f);
  CHECK(popup->rect().w >= 60);  // fitted to its child
  // The pointer was at 310,310 with an offset of 5,6: the popup's corner is there.
  CHECK(popup->rect().x == 315);
  CHECK(popup->rect().y == 316);

  e.wm.tickTransitions(1062);  // half way
  CHECK(tip->alpha() == doctest::Approx(0.5f).epsilon(0.05));
  e.wm.tickTransitions(1125);
  CHECK(tip->alpha() == doctest::Approx(1.0f));
  CHECK(e.wm.transitionCount() == 0);

  // The pointer stays inside the Rollover's rectangle: the popup stays.
  e.wm.handleInput(e.move(320, 320));
  e.pump();
  CHECK(e.wm.findInTopModal("tip") == tip);
  CHECK(e.getNumber("gBuilt") == 1);
  // Leaving removes it and its listener.
  e.wm.handleInput(e.move(600, 100));
  e.pump();
  CHECK(e.wm.findInTopModal("tip") == nullptr);
  CHECK_FALSE(e.wm.isMouseListener(popup->id()) );
}

TEST_CASE("Rollover with the Chocolatier 2 popup fader: 50 ms steps 0.4, 0.8, 1; Rectangles end at 0.8, Text at 1") {
  HostEnv e;
  REQUIRE(e.booted);
  e.wm.setLegacyPopupFader(true);
  REQUIRE(e.run(
      "do local t = { typename = 'Rollover', name = 'hot', x = 300, y = 300, w = 40, h = 40, "
      "contents = \"local r = { typename = 'Rectangle', name = 'bg', label = '', x = 0, y = 0, w = 60, h = 20, alpha = 1 }; "
      "GenericCreate(r); PostCreate(r, 'Rectangle'); PopWindow(); "
      "local x = { typename = 'Text', name = 'tip', label = '#tip', x = 0, y = 0, w = 60, h = 20 }; "
      "GenericCreate(x); PostCreate(x, 'Text'); PopWindow()\" }; "
      "GenericCreate(t); PostCreate(t, 'Rollover'); PopWindow() end") == 0);
  e.wm.tickTransitions(1000);
  e.wm.handleInput(e.move(310, 310));
  ui::Window* tip = e.wm.findInTopModal("tip");
  ui::Window* bg = e.wm.findInTopModal("bg");
  REQUIRE(tip);
  REQUIRE(bg);
  CHECK(tip->alpha() == 0.0f);
  CHECK(bg->alpha() == 0.0f);
  e.wm.tickTransitions(1049);  // no step yet
  CHECK(tip->alpha() == 0.0f);
  e.wm.tickTransitions(1050);
  CHECK(tip->alpha() == doctest::Approx(0.4f));
  CHECK(bg->alpha() == doctest::Approx(0.32f));
  e.wm.tickTransitions(1110);
  CHECK(tip->alpha() == doctest::Approx(0.8f));
  CHECK(bg->alpha() == doctest::Approx(0.64f));
  e.wm.tickTransitions(1125);
  CHECK(tip->alpha() == doctest::Approx(1.0f));
  CHECK(bg->alpha() == doctest::Approx(0.8f));
  CHECK(e.wm.transitionCount() == 0);
}

TEST_CASE("Rollover: a click on the popup runs the command and closes it; hovering again rebuilds it") {
  HostEnv e;
  REQUIRE(e.booted);
  REQUIRE(e.run(kRollover) == 0);
  e.wm.handleInput(e.move(310, 310));
  REQUIRE(e.wm.findInTopModal("tip"));
  e.wm.handleInput(mouse(InputEvent::Type::MouseDown, 320, 320));
  e.wm.handleInput(mouse(InputEvent::Type::MouseUp, 320, 320));
  e.pump();
  CHECK(e.getNumber("gClicked") == 1);
  CHECK(e.wm.findInTopModal("tip") == nullptr);
  e.wm.handleInput(e.move(500, 500));
  e.wm.handleInput(e.move(310, 310));
  CHECK(e.wm.findInTopModal("tip") != nullptr);
  CHECK(e.getNumber("gBuilt") == 2);
}

TEST_CASE("Rollover: the popup has no fill; a release without a press on it closes it without the command") {
  HostEnv e;
  REQUIRE(e.booted);
  REQUIRE(e.run(kRollover) == 0);
  e.wm.handleInput(e.move(310, 310));
  ui::Window* tip = e.wm.findInTopModal("tip");
  REQUIRE(tip);
  CHECK(tip->parent()->detail().find("nofill") != std::string::npos);
  e.wm.handleInput(mouse(InputEvent::Type::MouseUp, 320, 320));  // no press began on the popup
  e.pump();
  CHECK(e.getNumber("gClicked") == 0);
  CHECK(e.wm.findInTopModal("tip") == nullptr);
}

TEST_CASE("Rollover: a failing `contents` leaves the creation stack where it was") {
  HostEnv e;
  REQUIRE(e.booted);
  REQUIRE(e.run("do local t = { typename = 'Rollover', name = 'bad', x = 300, y = 300, w = 40, h = 40, "
                "contents = \"local x = { typename = 'Window', name = 'half', w = 5, h = 5 }; GenericCreate(x); error('boom')\" }; "
                "GenericCreate(t); PostCreate(t, 'Rollover'); PopWindow() end") == 0);
  const std::size_t depth = e.wm.creationDepth();
  e.wm.handleInput(e.move(310, 310));
  CHECK(e.wm.creationDepth() == depth);
}

TEST_CASE("Options: sliders start at the user's volume prefs and SaveVolumes writes them back") {
  HostEnv e;
  REQUIRE(e.booted);
  REQUIRE(e.ctx.users);
  e.ctx.users->setPref("soundlevel", "321");
  e.ctx.users->setPref("musiclevel", "0");
  // The harness's user dir is shared: a user made by a Chocolatier 2 test copied 750 here, so state the ambient level.
  e.ctx.users->setPref("ambientlevel", "500");
  REQUIRE(e.run("do local o = { typename = 'OptionsWindow', name = 'opts', x = 0, y = 0, w = 300, h = 100 }; GenericCreate(o); "
                "for _, n in ipairs({'sfxlevelslider', 'musiclevelslider', 'ambientlevelslider'}) do "
                "local s = { typename = 'Slider', name = n, x = 0, y = 0, w = 100, h = 10 }; GenericCreate(s); PostCreate(s, 'Slider'); PopWindow() end "
                "PostCreate(o, 'OptionsWindow'); PopWindow() end") == 0);
  auto* sfx = dynamic_cast<SliderWindow*>(e.wm.findInTopModal("sfxlevelslider"));
  auto* mus = dynamic_cast<SliderWindow*>(e.wm.findInTopModal("musiclevelslider"));
  auto* amb = dynamic_cast<SliderWindow*>(e.wm.findInTopModal("ambientlevelslider"));
  REQUIRE((sfx && mus && amb));
  CHECK(sfx->value() == doctest::Approx(0.321f));
  CHECK(mus->value() == doctest::Approx(0.0f));
  CHECK(amb->value() == doctest::Approx(0.5f));
  sfx->setValue(0.8f, true);
  REQUIRE(e.run("SaveVolumes()") == 0);
  CHECK(e.ctx.users->pref("soundlevel") == "800");
  CHECK(e.ctx.users->pref("musiclevel") == "0");
  CHECK(e.ctx.users->pref("ambientlevel") == "500");
}

TEST_CASE("Options: moving a slider changes the audio level live and SaveVolumes keeps it") {
  HostEnv e;
  REQUIRE(e.booted);
  choco::audio::Audio audio([](const std::string&) { return std::optional<choco::audio::SoundCache::Bytes>(); }, choco::audio::EventTable::empty(), [](const std::string&) {});
  e.ctx.audio = &audio;
  e.ctx.users->setPref("soundlevel", "750");  // the user store outlives the tests in this file
  e.ctx.users->setPref("musiclevel", "650");
  e.wm.setVolumeHook([&](const std::string& slider, float v) {
    if (slider == "musiclevelslider") audio.setLevels(audio.sfxLevel(), v, audio.envLevel(), audio.muted());
  });
  REQUIRE(e.run("do local o = { typename = 'OptionsWindow', name = 'opts', x = 0, y = 0, w = 300, h = 100 }; GenericCreate(o); "
                "for _, n in ipairs({'sfxlevelslider', 'musiclevelslider', 'ambientlevelslider'}) do "
                "local s = { typename = 'Slider', name = n, x = 0, y = 0, w = 100, h = 10 }; GenericCreate(s); PostCreate(s, 'Slider'); PopWindow() end "
                "PostCreate(o, 'OptionsWindow'); PopWindow() end") == 0);
  auto* mus = dynamic_cast<SliderWindow*>(e.wm.findInTopModal("musiclevelslider"));
  REQUIRE(mus);
  mus->setValue(0.3f, true);
  ui::Window* opts = e.wm.findInTopModal("opts");
  REQUIRE(opts);
  e.wm.sendMessage({ui::kSliderValChanged, "musiclevelslider", opts->id()});  // what a drag of a slider with a knob sends
  e.pump();
  CHECK(audio.musicLevel() == doctest::Approx(0.3f));
  CHECK(audio.sfxLevel() == doctest::Approx(0.75f));
  REQUIRE(e.run("SaveVolumes()") == 0);
  CHECK(e.ctx.users->pref("musiclevel") == "300");
  CHECK(audio.musicLevel() == doctest::Approx(0.3f));  // applied again from the stored pref
  CHECK(audio.sfxLevel() == doctest::Approx(0.75f));
  e.ctx.audio = nullptr;
}

TEST_CASE("FillWindow destroys the window's children and runs the script into it") {
  HostEnv e;
  REQUIRE(e.booted);
  REQUIRE(e.run("do local t = { typename = 'Window', name = 'target', x = 0, y = 0, w = 100, h = 50 }; GenericCreate(t); "
                "local c = { typename = 'Text', name = 'old', label = '#old', w = 10, h = 10 }; GenericCreate(c); PostCreate(c, 'Text'); PopWindow(); "
                "PostCreate(t, 'Window'); PopWindow() end") == 0);
  REQUIRE(e.wm.findInTopModal("old"));
  REQUIRE(e.run("FillWindow('target', 'ui/fill.lua'); FillWindow('nobody', 'ui/fill.lua')") == 0);
  e.pump();
  ui::Window* target = e.wm.findInTopModal("target");
  REQUIRE(target);
  CHECK(e.wm.findInTopModal("old") == nullptr);
  REQUIRE(target->find("filled"));
  CHECK(target->children().size() == 1);
  CHECK(target->find("filled")->label() == "filled");
  CHECK(e.wm.creationTop() == e.wm.topModal());  // the creation stack is back where it was
}

TEST_CASE("SetLayeredLabel sets label0..label3 on their layers and leaves the layer where it was") {
  HostEnv e;
  REQUIRE(e.booted);
  std::string inner;
  for (int i = 0; i < 4; ++i)
    inner += "DoSelectLayer(" + std::to_string(i) + "); do local x = { typename = 'Text', name = 'label" + std::to_string(i) +
             "', label = '#x', w = 40, h = 20 }; GenericCreate(x); PostCreate(x, 'Text'); PopWindow() end; ";
  REQUIRE(e.run("do local t = { typename = 'Button', name = 'lb', graphics = {'image/bt_up','image/bt_down','image/bt_over'} }; "
                "GenericCreate(t); " + inner + "DoSelectLayer(-1); PostCreate(t, 'Button'); PopWindow() end") == 0);
  auto* b = dynamic_cast<ui::ButtonWindow*>(e.wm.findInTopModal("lb"));
  REQUIRE(b);
  const int before = b->currentLayer();
  REQUIRE(e.run("SetLayeredLabel('lb', 'Quest'); SetLayeredLabel('missing', 'x')") == 0);
  for (int i = 0; i < 4; ++i) CHECK(b->find("label" + std::to_string(i))->label() == "Quest");
  CHECK(b->currentLayer() == before);
  // Layer 0 is showing: its label is visible, layer 1's is not.
  CHECK(b->find("label0")->shown());
  CHECK_FALSE(b->find("label1")->shown());
}

TEST_CASE("Text labels resolve through the SDK string table with empty arguments") {
  HostEnv e;
  REQUIRE(e.booted);
  e.ctx.strings.loadRows({{"greet", "Hi %1%!"}, {"bad", "x %missing% y"}, {"plain", "Plain"}}, assets::StringTable::Layer::Sdk);
  REQUIRE(e.run("local function mk(n, l) local x = { typename = 'Text', name = n, label = l, w = 40, h = 20 }; GenericCreate(x); "
                "PostCreate(x, 'Text'); PopWindow() end; mk('a', 'greet'); mk('b', 'bad'); mk('c', 'plain'); mk('d', '#literal')") == 0);
  CHECK(e.wm.findInTopModal("a")->label() == "Hi !");  // %1% is the empty argument
  CHECK(e.wm.findInTopModal("b")->label() == "#####");  // a missing nested key spoils the whole string
  CHECK(e.wm.findInTopModal("c")->label() == "Plain");
  CHECK(e.wm.findInTopModal("d")->label() == "literal");
}

TEST_CASE("TextEdit: onkey runs once per character, in the script, and the label is literal") {
  HostEnv e;
  REQUIRE(e.booted);
  REQUIRE(e.run("do local t = { typename = 'TextEdit', name = 'ed', x = 0, y = 0, w = 100, h = 20, label = 'story_mode', utf8 = true, "
                "length = 20, onkey = function() gKeys = (gKeys or 0) + 1 end }; GenericCreate(t); PostCreate(t, 'TextEdit'); PopWindow() end") == 0);
  auto* ed = dynamic_cast<TextEditWindow*>(e.wm.findInTopModal("ed"));
  REQUIRE(ed);
  CHECK(ed->text() == "story_mode");  // no string-table lookup
  e.wm.setFocus(ed->id());
  e.wm.handleInput(textEvent("ab"));
  CHECK(ed->text() == "story_modeab");
  CHECK(e.getNumber("gKeys") == 2);
  e.wm.handleInput(keyEvent("Backspace"));
  CHECK(e.getNumber("gKeys") == 3);
  REQUIRE(e.run("gLabel = GetLabel('ed'); SetLabel('ed', 'new'); gAfter = GetLabel('ed')") == 0);
  CHECK(e.getString("gLabel") == "story_modea");
  CHECK(e.getString("gAfter") == "new");
}

TEST_CASE("TextEdit: Enter and Tab reach the edit first, clear a placeholder, run onkey and still bubble") {
  HostEnv e;
  REQUIRE(e.booted);
  REQUIRE(e.run("do local t = { typename = 'TextEdit', name = 'ed', x = 0, y = 0, w = 100, h = 20, label = 'hint', "
                "clearinitial = true, onkey = function() gKeys = (gKeys or 0) + 1 end }; GenericCreate(t); PostCreate(t, 'TextEdit'); PopWindow() end") == 0);
  auto* ed = dynamic_cast<TextEditWindow*>(e.wm.findInTopModal("ed"));
  REQUIRE(ed);
  CHECK(ed->initialPending());
  ui::KeyEvent k;
  k.name = "Tab";
  CHECK_FALSE(ed->onChar(k));  // not consumed
  CHECK_FALSE(ed->initialPending());
  CHECK(e.getNumber("gKeys") == 1);
  k.name = "Return";
  CHECK_FALSE(ed->onChar(k));
  CHECK(e.getNumber("gKeys") == 2);
  k.name = "Escape";
  CHECK_FALSE(ed->onChar(k));
  CHECK(e.getNumber("gKeys") == 3);
  CHECK(ed->wantsTextInput());
  gfx::RectI area;
  e.wm.setFocus(ed->id());
  CHECK(e.wm.textInputWanted(area));
  CHECK(area.w == 100);
}

// ---------------------------------------------------------------- Mouse fall-through to the sibling behind

namespace {

// Counts the mouse events that reach it and handles them.
struct Hot : ui::Window {
  explicit Hot(ui::WindowManager& wm) : Window(wm) {}
  bool onMouseMove(int, int) override { return ++moves, true; }
  bool onMouseDown(int, int) override { return ++downs, true; }
  int moves = 0, downs = 0;
};

// The port ledger's shape (docs/re/choc2-bottom-hud-clicks.md): a `quest` window at (235,53) holds a hot area at local
// (44,62,497,39), which on screen lies at (279,405)-(776,444) under the tab row. In front of it sits the ledger art at
// (258,100) with a tab-like hot child at (1,3,141,36); the art itself handles nothing.
struct LedgerShape {
  Env e;
  ui::Window* ledger = nullptr;
  Hot* rollover = nullptr;
  Hot* tab = nullptr;
  LedgerShape() {
    auto l = std::make_unique<ui::Window>(e.wm);
    ledger = l.get();
    e.modal->adoptFront(std::move(l));
    ledger->setRect({0, 290, 800, 310});
    auto quest = std::make_unique<ui::Window>(e.wm);
    quest->setRect({235, 53, 587, 129});
    auto r = std::make_unique<Hot>(e.wm);
    rollover = r.get();
    r->setRect({44, 62, 497, 39});
    quest->adoptFront(std::move(r));
    ledger->adoptFront(std::move(quest));
    auto art = std::make_unique<ui::Window>(e.wm);  // added last = in front of the quest window
    art->setRect({258, 100, 540, 208});
    auto t = std::make_unique<Hot>(e.wm);
    tab = t.get();
    t->setRect({1, 3, 141, 36});
    art->adoptFront(std::move(t));
    ledger->adoptFront(std::move(art));
  }
};

}  // namespace

TEST_CASE("Fall-through: Chocolatier 3 translates the point into the sibling behind, so a hidden hot area is reached") {
  LedgerShape s;
  CHECK_FALSE(s.e.wm.legacyFallThroughPoint());
  s.e.wm.handleInput(mouse(InputEvent::Type::MouseMove, 402, 411));  // a gap beside the tab: the art handles nothing
  CHECK(s.rollover->moves == 1);
  CHECK(s.tab->moves == 0);
}

TEST_CASE("Fall-through: the legacy rule keeps the point in the parent's space, so the hidden hot area is not reached") {
  LedgerShape s;
  s.e.wm.setLegacyFallThroughPoint(true);
  s.e.wm.handleInput(mouse(InputEvent::Type::MouseMove, 402, 411));
  s.e.wm.handleInput(mouse(InputEvent::Type::MouseDown, 402, 411));
  CHECK(s.rollover->moves == 0);
  CHECK(s.rollover->downs == 0);
  // The tab hovers and takes the first press at once.
  s.e.wm.handleInput(mouse(InputEvent::Type::MouseMove, 346, 411));
  CHECK(s.tab->moves == 1);
  s.e.wm.handleInput(mouse(InputEvent::Type::MouseDown, 346, 411));
  CHECK(s.tab->downs == 1);
}

namespace {
// A Rollover without xOffset, with a sound and an `area` of two rects (client coordinates: left, top, right, bottom).
const char* kRolloverArea =
    "do local t = { typename = 'Rollover', name = 'hot', x = 300, y = 300, w = 40, h = 40, rolloverSound = 'ding', "
    "area = { {0, 0, 10, 40}, {30, 0, 40, 10} }, "
    "contents = \"local x = { typename = 'Text', name = 'tip', label = '#tip', x = 0, y = 0, w = 60, h = 20 }; "
    "GenericCreate(x); PostCreate(x, 'Text'); PopWindow()\" }; "
    "GenericCreate(t); PostCreate(t, 'Rollover'); PopWindow() end";
}  // namespace

TEST_CASE("Rollover (Chocolatier 2): xOffset defaults to 20, the rollover sound plays on open, `area` limits the hot zone") {
  HostEnv e;
  REQUIRE(e.booted);
  e.wm.setLegacyRollover(true);
  e.wm.setSoundNameResolver([](const std::string& n) { return "sfx/" + n + ".ogg"; });  // the game module's rule
  std::vector<std::string> sounds;
  e.wm.setSoundHook([&](const std::string& f, bool) { sounds.push_back(f); });
  REQUIRE(e.run(kRolloverArea) == 0);
  auto* hot = dynamic_cast<ui::RolloverWindow*>(e.wm.findInTopModal("hot"));
  REQUIRE(hot);
  REQUIRE(hot->area().size() == 2);
  CHECK(hot->area()[1] == gfx::RectI{30, 0, 40, 10});
  e.wm.tickTransitions(1000);
  // Inside the rect but outside both areas: nothing opens.
  e.wm.handleInput(e.move(315, 320));
  CHECK(e.wm.findInTopModal("tip") == nullptr);
  CHECK(sounds.empty());
  // Inside the first area (client 5,20): the popup opens at pointer + 20 and the sound plays (sfx/ prefix, .ogg suffix).
  e.wm.handleInput(e.move(305, 320));
  ui::Window* tip = e.wm.findInTopModal("tip");
  REQUIRE(tip);
  CHECK(tip->parent()->rect().x == 325);
  REQUIRE(sounds.size() == 1);
  CHECK(sounds[0] == "sfx/ding.ogg");
}

TEST_CASE("Rollover area test: left <= x < right, top <= y < bottom; no area means anywhere") {
  const std::vector<gfx::RectI> area{{0, 0, 10, 40}, {30, 0, 40, 10}};
  CHECK(ui::RolloverWindow::inArea({}, 99, 99));
  CHECK(ui::RolloverWindow::inArea(area, 0, 0));
  CHECK_FALSE(ui::RolloverWindow::inArea(area, 10, 5));  // right edge excluded
  CHECK(ui::RolloverWindow::inArea(area, 9, 39));
  CHECK_FALSE(ui::RolloverWindow::inArea(area, 9, 40));  // bottom edge excluded
  CHECK(ui::RolloverWindow::inArea(area, 35, 9));
  CHECK_FALSE(ui::RolloverWindow::inArea(area, 35, 10));
}

TEST_CASE("Rollover (Chocolatier 3 default): xOffset 0, no sound, no area") {
  HostEnv e;
  REQUIRE(e.booted);
  std::vector<std::string> sounds;
  e.wm.setSoundHook([&](const std::string& f, bool) { sounds.push_back(f); });
  REQUIRE(e.run(kRolloverArea) == 0);
  auto* hot = dynamic_cast<ui::RolloverWindow*>(e.wm.findInTopModal("hot"));
  REQUIRE(hot);
  CHECK(hot->area().empty());
  e.wm.tickTransitions(1000);
  e.wm.handleInput(e.move(315, 320));
  ui::Window* tip = e.wm.findInTopModal("tip");
  REQUIRE(tip);
  CHECK(tip->parent()->rect().x == 315);
  CHECK(sounds.empty());
}
