#include <doctest/doctest.h>

#include "app/ScriptDriver.h"

using namespace choco::app;

namespace {
struct Recorder : choco::platform::InputInjector {
  std::string log;
  void injectMouseMove(float x, float y) override { log += "M" + std::to_string(int(x)) + "," + std::to_string(int(y)) + ";"; }
  void injectMouseButton(float, float, bool down) override { log += down ? "D;" : "U;"; }
  void injectWindowMouseMove(float x, float y) override { log += "WM" + std::to_string(int(x)) + "," + std::to_string(int(y)) + ";"; }
  void injectWindowMouseButton(float, float, bool down) override { log += down ? "WD;" : "WU;"; }
  bool injectKey(const std::string& k) override {
    if (k == "bogus") return false;
    log += "K" + k + ";";
    return true;
  }
  void injectText(const std::string& t) override { log += "T" + t + ";"; }
  void injectQuit() override { log += "Q;"; }
  void injectResize(int w, int h) override { log += "R" + std::to_string(w) + "x" + std::to_string(h) + ";"; }
};
}  // namespace

TEST_CASE("parse valid script") {
  auto p = parseScript("# comment\nclick 400 300\n\nwait 250  # trailing\nkey alt+Return\nscreenshot out dir/a.png\nexpect-window Main\nquit\n"
                       "click-window 1280 720\nscreenshot-logical l.png\n");
  CHECK(p.errors.empty());
  REQUIRE(p.commands.size() == 8);
  CHECK(p.commands[6].kind == ScriptCommand::Kind::ClickWindow);
  CHECK(p.commands[6].x == 1280);
  CHECK(p.commands[7].kind == ScriptCommand::Kind::ScreenshotLogical);
  CHECK(p.commands[0].kind == ScriptCommand::Kind::Click);
  CHECK(p.commands[0].x == 400);
  CHECK(p.commands[1].ms == 250);
  CHECK(p.commands[2].text == "alt+Return");
  CHECK(p.commands[3].text == "out dir/a.png");
  CHECK(p.commands[4].text == "Main");
}

TEST_CASE("parse errors carry line numbers") {
  auto p = parseScript("click 1\nfrobnicate\nwait\nquit now\n");
  REQUIRE(p.errors.size() == 4);
  CHECK(p.errors[1].find("line 2") == 0);
  CHECK(p.errors[1].find("frobnicate") != std::string::npos);
}

TEST_CASE("driver honours simulated waits") {
  Recorder rec;
  auto p = parseScript("click 5 6\nwait 100\nkey Return\nquit\n");
  ScriptDriver d(p.commands, rec);
  d.update(0);
  CHECK(rec.log == "M5,6;D;U;");
  d.update(50);
  CHECK(rec.log == "M5,6;D;U;");
  d.update(100);
  CHECK(rec.log == "M5,6;D;U;KReturn;");  // a key ends the update so its effects land before the next command
  CHECK_FALSE(d.finished());
  d.update(116);
  CHECK(rec.log == "M5,6;D;U;KReturn;");  // quit stops the app directly, not through a window-close event
  CHECK(d.quitRequested());
  CHECK(d.finished());
}

TEST_CASE("a trailing wait keeps the script running") {
  Recorder rec;
  auto p = parseScript("click 1 2\nwait 100\n");
  ScriptDriver d(p.commands, rec);
  d.update(0);
  CHECK_FALSE(d.finished());
  d.update(99);
  CHECK_FALSE(d.finished());
  d.update(100);
  CHECK(d.finished());
}

TEST_CASE("window coordinates bypass the logical mapping") {
  Recorder rec;
  auto p = parseScript("click-window 10 20\n");
  ScriptDriver d(p.commands, rec);
  d.update(0);
  CHECK(rec.log == "WM10,20;WD;WU;");
}

TEST_CASE("resize W H: whole window pixels, ends the frame") {
  auto p = parseScript("resize 1600 1000\nresize 800\nresize 800 600 1\nresize 0 600\nresize 800.5 600\nresize a b\n");
  CHECK(p.commands.size() == 1);
  CHECK(p.errors.size() == 5);
  Recorder rec;
  auto q = parseScript("resize 1600 1000\nclick-window 1 2\n");
  ScriptDriver d(q.commands, rec);
  d.update(0);
  CHECK(rec.log == "R1600x1000;");  // the frame ends: the click waits for the next one
  d.update(16);
  CHECK(rec.log == "R1600x1000;WM1,2;WD;WU;");
}

TEST_CASE("key names are validated when the script is parsed") {
  auto p = parseScript("key Return\nkey alt+Return\nkey F11\nkey Escape\nkey bogus\nkey wibble+Return\n");
  CHECK(p.commands.size() == 4);
  REQUIRE(p.errors.size() == 2);
  CHECK(p.errors[0].find("line 5") == 0);
  CHECK(p.errors[0].find("bogus") != std::string::npos);
  CHECK(p.errors[1].find("line 6") == 0);
}

TEST_CASE("driver stops for screenshots and fails on unknown keys") {
  Recorder rec;
  auto p = parseScript("screenshot a.png\nkey Return\n");
  CHECK(p.errors.empty());
  ScriptDriver d(p.commands, rec);
  d.update(0);
  REQUIRE(d.pendingScreenshot());
  CHECK(*d.pendingScreenshot() == "a.png");
  d.update(16);
  CHECK(rec.log.empty());
  d.screenshotDone();
  d.update(32);
  CHECK(rec.log == "KReturn;");

  // The parser rejects unknown keys, but the driver still fails cleanly if the injector refuses one.
  Recorder rec2;
  ScriptCommand bad;
  bad.kind = ScriptCommand::Kind::Key;
  bad.text = "bogus";
  ScriptDriver d2({bad}, rec2);
  d2.update(0);
  CHECK(d2.failed());
}

namespace {
struct FakeWindows : choco::ui::WindowQuery {
  bool shown = false;
  bool isShown(std::string_view) const override { return shown; }
};
}  // namespace

TEST_CASE("expect-window: optional timeout, waits for the window, fails on timeout") {
  auto p = parseScript("expect-window ui/mainmenu.lua 60000\nexpect-window two words\nexpect-window plain\n");
  REQUIRE(p.errors.empty());
  REQUIRE(p.commands.size() == 3);
  CHECK(p.commands[0].text == "ui/mainmenu.lua");
  CHECK(p.commands[0].ms == 60000);
  CHECK(p.commands[1].text == "two words");  // a non-numeric tail is part of the name
  CHECK(p.commands[2].ms == 5000);           // default timeout

  Recorder rec;
  FakeWindows win;
  {  // appears in time
    auto s = parseScript("expect-window w 1000\nclick 1 2\nquit\n");
    ScriptDriver d(s.commands, rec);
    d.setWindowQuery(&win);
    d.update(0);
    CHECK(rec.log.empty());  // still waiting
    CHECK_FALSE(d.finished());
    d.update(500);
    CHECK(rec.log.empty());
    win.shown = true;
    d.update(516);
    CHECK(rec.log == "M1,2;D;U;");
    CHECK_FALSE(d.failed());
    CHECK(d.quitRequested());
  }
  {  // never appears
    win.shown = false;
    auto s = parseScript("expect-window w 100\nquit\n");
    ScriptDriver d(s.commands, rec);
    d.setWindowQuery(&win);
    d.update(0);
    d.update(99);
    CHECK_FALSE(d.failed());
    d.update(100);
    CHECK(d.failed());
    CHECK_FALSE(d.quitRequested());
  }
  {  // already there: no wait
    win.shown = true;
    auto s = parseScript("expect-window w\nquit\n");
    ScriptDriver d(s.commands, rec);
    d.setWindowQuery(&win);
    d.update(0);
    CHECK(d.quitRequested());
  }
}

TEST_CASE("expect-window: no script host and a stopped app are failures") {
  Recorder rec;
  {
    auto s = parseScript("expect-window w\nquit\n");
    ScriptDriver d(s.commands, rec);  // no window query
    d.update(0);
    CHECK(d.failed());
  }
  FakeWindows win;
  auto s = parseScript("expect-window w 1000\nquit\n");
  ScriptDriver d(s.commands, rec);
  d.setWindowQuery(&win);
  d.update(0);
  CHECK(d.expectPending());
  d.failIfPending("test");
  CHECK(d.failed());
  CHECK_FALSE(d.expectPending());
}

TEST_CASE("type and lua commands: the rest of the line is the text; type injects it, lua needs a runner") {
  auto p = parseScript("type Hello  World\nlua DisplayDialog { 'ui/x.lua' }\ntype\nkey a\n");
  REQUIRE(p.commands.size() == 3);
  CHECK(p.errors.size() == 1);  // a bare `type` has nothing to type
  CHECK(p.commands[0].kind == ScriptCommand::Kind::Type);
  CHECK(p.commands[0].text == "Hello  World");
  CHECK(p.commands[1].kind == ScriptCommand::Kind::Lua);
  CHECK(p.commands[1].text == "DisplayDialog { 'ui/x.lua' }");
  Recorder r;
  ScriptDriver d(p.commands, r);
  std::string ran;
  d.setLuaRunner([&](const std::string& code) { ran = code; });
  d.update(0);
  CHECK(r.log == "THello  World;");  // a type ends the frame
  d.update(16);
  CHECK(ran == "DisplayDialog { 'ui/x.lua' }");
  d.update(32);
  CHECK(r.log == "THello  World;Ka;");
  CHECK_FALSE(d.failed());
  Recorder r2;
  ScriptDriver d2(parseScript("lua x = 1\n").commands, r2);
  d2.update(0);
  CHECK(d2.failed());  // no runner: the host is missing
}
