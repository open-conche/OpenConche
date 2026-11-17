// Enhancements (docs/design/enhancements.md): the wheel event, the two hooks, the embedded overlay and the switch.
#include <doctest/doctest.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "ScriptHarness.h"
#include "gfx/TextureCache.h"
#include "script/Embedded.h"
#include "script/Enhancements.h"
#include "script/ScriptHost.h"
#include "ui/Input.h"
#include "ui/WindowManager.h"
#include "ui/widgets/InputWidgets.h"
#include "ui/widgets/Widgets.h"

using namespace choco;
using ui::ButtonWindow;
using ui::InputEvent;

namespace {

const char* kPush = "{'image/bt_up','image/bt_down','image/bt_over'}";

std::string buttonLua(const std::string& fields) {
  return "do local t = { typename = 'Button', " + fields + " }; GenericCreate(t); PostCreate(t, 'Button'); PopWindow() end";
}

// Runs a delivered command function the way a command runs (the window side hands it over through the sink), and
// records what was delivered.
struct Sink : ui::ScriptSink {
  lua_State* L = nullptr;
  int commands = 0;
  std::map<std::string, std::string> globals;
  void deliver(const ui::Message*, const script::LuaRef* cmd) override {
    if (!cmd) return;
    ++commands;
    callProtected(*cmd);
  }
  void runInScript(const script::LuaRef& fn) override { callProtected(fn); }
  bool callProtected(const script::LuaRef& fn) override {
    const int top = lua_gettop(L);
    fn.push(L);
    const bool ok = lua_pcall(L, 0, 0, 0) == 0;
    lua_settop(L, top);
    return ok;
  }
  void setGlobalString(const char* n, std::string_view v) override { globals[n] = std::string(v); }
};

struct Plain : ui::Window {
  Plain(ui::WindowManager& wm, const char* n) : Window(wm) { setName(n); }
};

ui::Window& addPlain(ui::Window& parent, const char* name, gfx::RectI r) {
  auto p = std::make_unique<Plain>(parent.manager(), name);
  p->setRect(r);
  return parent.adoptFront(std::move(p));
}

InputEvent wheel(float x, float y, int notches, bool inArea = true) {
  InputEvent e;
  e.type = InputEvent::Type::Wheel;
  e.x = x;
  e.y = y;
  e.wheel = notches;
  e.inArea = inArea;
  return e;
}

// A booted host over the modal fixture with the button textures; `on` sets the `enhancements` switch.
struct Env : test::ScriptEnv {
  explicit Env(bool on)
      : ScriptEnv(std::string(CHOCO_TEST_ASSETS) + "/ui/modal"),
        btnFs(std::string(CHOCO_TEST_ASSETS) + "/button", ""),
        cache(btnFs, nullptr),
        host(ctx) {
    if (on) config.set("enhancements", "1");
    wm.setTextures(&cache);
    sink.L = lua.L();
    wm.setScriptSink(&sink);  // after the host took it
    booted = host.boot();
  }
  ButtonWindow* button(const char* name) { return dynamic_cast<ButtonWindow*>(wm.findInTopModal(name)); }
  void click(float x, float y) {
    InputEvent m;
    m.type = InputEvent::Type::MouseMove;
    m.x = x;
    m.y = y;
    wm.handleInput(m);
    m.type = InputEvent::Type::MouseDown;
    wm.handleInput(m);
    m.type = InputEvent::Type::MouseUp;
    wm.handleInput(m);
  }
  assets::AssetFS btnFs;
  gfx::TextureCache cache;
  script::ScriptHost host;
  Sink sink;
  bool booted = false;
};

}  // namespace

TEST_CASE("enhance: the switch off installs nothing") {
  Env e(false);
  REQUIRE(e.booted);
  CHECK(e.log.contains("[enhancements] off"));
  CHECK(e.isNil("Enhance"));
  CHECK(e.isNil("EnhanceClose"));
  CHECK(e.isNil("EnhanceWindowExists"));
  CHECK(e.isNil("EnhanceHasButtonGraphic"));
  CHECK(e.isNil("EnhanceButtonGraphic"));
  CHECK(e.isNil("EnhanceSetText"));
  CHECK_FALSE(e.wm.hasWheelHook());
  CHECK_FALSE(static_cast<bool>(e.wm.commandHook()));
  // A wheel is dropped: nothing under it is touched and no focus or hover changes.
  const ui::WindowId focus = e.wm.focus(), hover = e.wm.hover();
  e.wm.handleInput(wheel(100, 100, 3));
  CHECK(e.wm.focus() == focus);
  CHECK(e.wm.hover() == hover);
  // A press goes through untouched.
  REQUIRE(e.run("PushModal('mb')") == 0);
  REQUIRE(e.run(buttonLua(std::string("name = 'ok', close = true, graphics = ") + kPush + ", command = function() gRan = 1 end")) == 0);
  e.click(5, 5);
  CHECK(e.sink.commands == 1);
  CHECK(e.getNumber("gRan") == 1);
  CHECK(e.sink.globals["gReturnValue"] == "ok");  // the original press set it
  CHECK(e.wm.pendingMessages() == 2);             // the close request and kButtonPress
  CHECK(e.wm.closePending(e.wm.topModal()->id()));
}

TEST_CASE("enhance: any value but 1 is off") {
  for (const char* v : {"", "0", "yes"}) {
    test::ScriptEnv probe(std::string(CHOCO_TEST_ASSETS) + "/ui/modal");
    probe.config.set("enhancements", v);
    script::ScriptHost host(probe.ctx);
    REQUIRE(host.boot());
    CHECK(probe.isNil("Enhance"));
    CHECK(probe.log.contains("[enhancements] off"));
  }
}

TEST_CASE("enhance: the switch on loads the overlay, the natives and the hooks") {
  Env e(true);
  REQUIRE(e.booted);
  CHECK(e.log.contains("[enhancements] on (choc3)"));
  CHECK(e.getString("Enhance") == "table");
  CHECK(e.getString("EnhanceClose") == "function");
  CHECK(e.getString("EnhanceWindowExists") == "function");
  CHECK(e.getString("EnhanceHasButtonGraphic") == "function");
  CHECK(e.getString("EnhanceButtonGraphic") == "function");
  CHECK(e.getString("EnhanceSetText") == "function");
  CHECK(e.wm.hasWheelHook());
  CHECK(static_cast<bool>(e.wm.commandHook()));
  // The default handlers do nothing.
  e.wm.handleInput(wheel(100, 100, 1));
  REQUIRE(e.run("assert(Enhance.onWheel(1, {}) == nil and Enhance.beforeCommand('a', nil, false) == nil)") == 0);
}

TEST_CASE("enhance: embedded overlays by game id") {
  CHECK_FALSE(script::embeddedEnhanceShared().empty());
  CHECK_FALSE(script::embeddedEnhanceFor("choc1").empty());
  CHECK_FALSE(script::embeddedEnhanceFor("choc2").empty());
  CHECK_FALSE(script::embeddedEnhanceFor("choc3").empty());  // Reforged shares the module id
  CHECK(script::embeddedEnhanceFor("greatcc").empty());
  CHECK(script::embeddedEnhanceFor("").empty());
}

TEST_CASE("enhance: helpers") {
  Env e(true);
  REQUIRE(e.booted);
  REQUIRE_MESSAGE(e.run("assert(Enhance.has({'a','b'}, 'b') and not Enhance.has({'a'}, 'c') and not Enhance.has(nil, 'c'))\n"
                        "assert(Enhance.number('12') == 12 and Enhance.number('x') == 0 and Enhance.number(nil) == 0)\n"
                        "assert(Enhance.clamp(5, 0, 3) == 3 and Enhance.clamp(-1, 0, 3) == 0 and Enhance.clamp(2, 0, 3) == 2)") == 0,
                  e.lastError);
}

TEST_CASE("enhance: the wheel hook sees every shown window under the pointer, front to back, parent after children") {
  Env e(true);
  REQUIRE(e.booted);
  REQUIRE(e.run("PushModal('mb')") == 0);
  ui::Window& modal = *e.wm.topModal();
  ui::Window& outer = addPlain(modal, "outer", {100, 100, 200, 200});
  addPlain(outer, "inner", {10, 10, 50, 50});
  addPlain(outer, "front", {20, 20, 50, 50});     // adopted last: frontmost
  addPlain(outer, "away", {150, 150, 20, 20});    // does not hold the pointer
  addPlain(outer, "", {0, 0, 200, 200});          // unnamed: left out of the names
  ui::Window& hid = addPlain(outer, "hid", {0, 0, 200, 200});
  hid.setShown(false);
  addPlain(hid, "hidkid", {0, 0, 10, 10});        // under a hidden window
  REQUIRE(e.run("gNames = nil; gNotches = nil\n"
                "function Enhance.onWheel(n, names) gNames = table.concat(names, ','); gNotches = n; return true end") == 0);
  e.wm.handleInput(wheel(135, 135, 2));  // outer-relative (35, 35): inside inner (10..60) and front (20..70)
  CHECK(e.getString("gNames") == "front,inner,outer,mb");
  CHECK(e.getNumber("gNotches") == 2);
  e.wm.handleInput(wheel(410, 410, -1));  // outside outer
  CHECK(e.getString("gNames") == "mb");
  CHECK(e.getNumber("gNotches") == -1);
  // The letterbox bars: dropped.
  REQUIRE(e.run("gNames = 'untouched'") == 0);
  e.wm.handleInput(wheel(-10, 300, 1, false));
  CHECK(e.getString("gNames") == "untouched");
  // A window of a modal below the top is not offered.
  REQUIRE(e.run("PushModal('upper')") == 0);
  e.wm.handleInput(wheel(135, 135, 1));
  CHECK(e.getString("gNames") == "upper");
}

TEST_CASE("enhance: the input filter sees the wheel and can consume it") {
  Env e(true);
  REQUIRE(e.booted);
  REQUIRE(e.run("gCalls = 0; function Enhance.onWheel() gCalls = gCalls + 1 return true end") == 0);
  int seen = 0;
  bool eat = true;
  e.wm.setInputFilter([&](const InputEvent& ev) {
    seen += ev.type == InputEvent::Type::Wheel;
    return eat;
  });
  e.wm.handleInput(wheel(10, 10, 1));
  CHECK(seen == 1);
  CHECK(e.getNumber("gCalls") == 0);
  eat = false;
  e.wm.handleInput(wheel(10, 10, 1));
  CHECK(e.getNumber("gCalls") == 1);
}

TEST_CASE("enhance: a string result presses that button once per notch, after the call returns") {
  Env e(true);
  REQUIRE(e.booted);
  REQUIRE(e.run("PushModal('mb')") == 0);
  REQUIRE(e.run(buttonLua(std::string("name = 'down', x = 0, y = 0, graphics = ") + kPush + ", command = function() gDown = (gDown or 0) + 1 end")) == 0);
  REQUIRE(e.run(buttonLua(std::string("name = 'up', x = 100, y = 0, graphics = ") + kPush + ", command = function() gUp = (gUp or 0) + 1 end")) == 0);
  REQUIRE(e.run("function Enhance.onWheel(n, names)\n"
                "  gDuring = gDown or 0\n"
                "  if n > 0 then return 'up' end\n"
                "  if n < -5 then return 'missing' end\n"
                "  return 'down'\n"
                "end") == 0);
  e.wm.handleInput(wheel(300, 300, 3));
  CHECK(e.getNumber("gUp") == 3);
  CHECK(e.getNumber("gDuring") == 0);
  e.wm.handleInput(wheel(300, 300, -2));
  CHECK(e.getNumber("gDown") == 2);
  e.wm.handleInput(wheel(300, 300, -9));  // no such button: a quiet no-op
  CHECK(e.getNumber("gDown") == 2);
  // A hidden button, or one under a hidden parent, is not pressed.
  e.button("down")->setShown(false);
  e.wm.handleInput(wheel(300, 300, -1));
  CHECK(e.getNumber("gDown") == 2);
}

TEST_CASE("enhance: true, false and nil results press nothing") {
  Env e(true);
  REQUIRE(e.booted);
  REQUIRE(e.run("PushModal('mb')") == 0);
  REQUIRE(e.run(buttonLua(std::string("name = 'b', graphics = ") + kPush + ", command = function() gB = (gB or 0) + 1 end")) == 0);
  for (const char* r : {"true", "false", "nil"}) {
    REQUIRE(e.run(std::string("function Enhance.onWheel() return ") + r + " end") == 0);
    e.wm.handleInput(wheel(300, 300, 1));
  }
  CHECK(e.getNumber("gB") == 0);
}

TEST_CASE("enhance: a Lua error in a hook is logged once per text and counts as not handled") {
  Env e(true);
  REQUIRE(e.booted);
  REQUIRE(e.run("PushModal('mb')") == 0);
  REQUIRE(e.run(buttonLua(std::string("name = 'ok', close = true, graphics = ") + kPush + ", command = function() gRan = (gRan or 0) + 1 end")) == 0);
  REQUIRE(e.run("function Enhance.onWheel() error('wheel boom') end\n"
                "function Enhance.beforeCommand() error('command boom') end") == 0);
  for (int i = 0; i < 3; ++i) e.wm.handleInput(wheel(300, 300, 1));
  int wheelLines = 0, cmdLines = 0;
  for (const auto& l : e.log.lines()) {
    wheelLines += l.find("wheel boom") != std::string::npos;
    cmdLines += l.find("command boom") != std::string::npos;
  }
  CHECK(wheelLines == 1);
  e.click(5, 5);
  e.click(5, 5);
  CHECK(e.getNumber("gRan") == 2);  // the press went ahead as the original
  for (const auto& l : e.log.lines()) cmdLines += l.find("command boom") != std::string::npos;
  CHECK(cmdLines == 1);
}

TEST_CASE("enhance: the command hook leaves a press alone on nil, replaces it with a function") {
  Env e(true);
  REQUIRE(e.booted);
  REQUIRE(e.run("PushModal('mb')") == 0);
  REQUIRE(e.run(buttonLua(std::string("name = 'ok', close = true, graphics = ") + kPush + ", command = function() gOrig = (gOrig or 0) + 1 end")) == 0);
  REQUIRE(e.run(buttonLua(std::string("name = 'plain', x = 100, graphics = ") + kPush)) == 0);  // no command, no close
  REQUIRE(e.run("gArgs = ''\n"
                "function Enhance.beforeCommand(name, original, closes)\n"
                "  gArgs = gArgs .. name .. ':' .. type(original) .. ':' .. tostring(closes) .. ';'\n"
                "  return nil\n"
                "end") == 0);
  // nil: the press is exactly as it was: the command, the close request and kButtonPress.
  e.click(5, 5);
  CHECK(e.getNumber("gOrig") == 1);
  CHECK(e.sink.globals["gReturnValue"] == "ok");
  CHECK(e.wm.pendingMessages() == 2);
  CHECK(e.getString("gArgs") == "ok:function:true;");
}

TEST_CASE("enhance: a replacement runs where a command runs and decides whether to call the original and close") {
  Env e(true);
  REQUIRE(e.booted);
  REQUIRE(e.run("PushModal('mb')") == 0);
  REQUIRE(e.run(buttonLua(std::string("name = 'ok', close = true, graphics = ") + kPush + ", command = function() gOrig = (gOrig or 0) + 1 end")) == 0);
  // A replacement that does nothing: no command, no close request, no gReturnValue; kButtonPress still posts.
  REQUIRE(e.run("function Enhance.beforeCommand(name, original, closes) return function() gRep = (gRep or 0) + 1 end end") == 0);
  e.click(5, 5);
  CHECK(e.getNumber("gRep") == 1);
  CHECK(e.isNil("gOrig"));
  CHECK(e.sink.commands == 1);
  CHECK(e.sink.globals.count("gReturnValue") == 0);
  CHECK(e.wm.pendingMessages() == 1);
  e.wm.dispatchMessages();
  // A replacement that calls the original and closes with Enhance.close.
  REQUIRE(e.run("function Enhance.beforeCommand(name, original, closes)\n"
                "  return function() original() Enhance.close(name) end\n"
                "end") == 0);
  e.click(5, 5);
  CHECK(e.getNumber("gOrig") == 1);
  CHECK(e.getString("gReturnValue") == "ok");  // EnhanceClose sets it through the host
  CHECK(e.wm.pendingMessages() == 2);          // the close request and kButtonPress
}

TEST_CASE("enhance: the command hook is not consulted for a button with neither command nor close") {
  Env e(true);
  REQUIRE(e.booted);
  REQUIRE(e.run("PushModal('mb')") == 0);
  REQUIRE(e.run(buttonLua(std::string("name = 'plain', graphics = ") + kPush)) == 0);
  REQUIRE(e.run("gCalled = 0; function Enhance.beforeCommand() gCalled = gCalled + 1 end") == 0);
  e.click(5, 5);
  CHECK(e.getNumber("gCalled") == 0);
}

TEST_CASE("enhance: EnhanceWindowExists looks at the top modal") {
  Env e(true);
  REQUIRE(e.booted);
  REQUIRE(e.run("PushModal('mb')") == 0);
  REQUIRE(e.run(buttonLua(std::string("name = 'go', graphics = ") + kPush + ", command = function() gGo = 1 end")) == 0);
  REQUIRE(e.run(buttonLua(std::string("name = 'nocmd', x = 100, graphics = ") + kPush)) == 0);
  REQUIRE_MESSAGE(e.run("assert(EnhanceWindowExists('go') and EnhanceWindowExists('nocmd') and not EnhanceWindowExists('nope'))") == 0,
                  e.lastError);
  e.button("go")->setShown(false);
  REQUIRE(e.run("assert(not EnhanceWindowExists('go'))") == 0);
  REQUIRE(e.run("PushModal('upper')") == 0);
  REQUIRE_MESSAGE(e.run("assert(not EnhanceWindowExists('nocmd'))") == 0, e.lastError);
}

TEST_CASE("enhance: EnhanceHasButtonGraphic sees a shown button by its idle graphic, EnhanceButtonGraphic names the pressed one") {
  Env e(true);
  REQUIRE(e.booted);
  REQUIRE(e.run("PushModal('mb')") == 0);
  REQUIRE(e.run(buttonLua(std::string("name = 'go', graphics = ") + kPush + ", command = function() end")) == 0);
  REQUIRE_MESSAGE(e.run("assert(EnhanceHasButtonGraphic('image/bt_up') and not EnhanceHasButtonGraphic('image/bt_down'))\n"
                        "assert(not EnhanceHasButtonGraphic('image/none') and EnhanceButtonGraphic() == nil)") == 0,
                  e.lastError);
  REQUIRE(e.run("function Enhance.beforeCommand(name, original, closes) gSeen = EnhanceButtonGraphic() end") == 0);
  e.click(5, 5);
  CHECK(e.getString("gSeen") == "image/bt_up");
  REQUIRE(e.run("assert(EnhanceButtonGraphic() == nil)") == 0);
  e.button("go")->setShown(false);
  REQUIRE(e.run("assert(not EnhanceHasButtonGraphic('image/bt_up'))") == 0);
}

TEST_CASE("enhance: EnhanceSetText sets a TextEdit as typing would, running its onkey") {
  Env e(true);
  REQUIRE(e.booted);
  REQUIRE(e.run("PushModal('mb')") == 0);
  REQUIRE_MESSAGE(e.run("gKeys = 0\n"
                        "do local t = { typename = 'TextEdit', name = 'count', x = 0, y = 0, w = 100, h = 20, length = 7, label = '0',\n"
                        "  clearinitial = true, ignore = kNumbersOnly, onkey = function() gKeys = gKeys + 1 gSeen = GetLabel('count') end }\n"
                        "  GenericCreate(t); PostCreate(t, 'TextEdit'); PopWindow() end") == 0,
                  e.lastError);
  auto* edit = dynamic_cast<ui::TextEditWindow*>(e.wm.findInTopModal("count"));
  REQUIRE(edit);
  const int before = e.wm.pendingMessages();
  REQUIRE_MESSAGE(e.run("assert(EnhanceSetText('count', '42') == true)") == 0, e.lastError);
  CHECK(edit->text() == "42");
  CHECK(e.getNumber("gKeys") == 1);
  CHECK(e.getString("gSeen") == "42");
  CHECK(e.wm.pendingMessages() == before + 1);  // kTextEditChanged
  CHECK_FALSE(edit->initialPending());
  // Plain SetLabel does not run onkey; and the pending initial text is not cleared again by the next key.
  REQUIRE(e.run("SetLabel('count', '7')") == 0);
  CHECK(e.getNumber("gKeys") == 1);
  REQUIRE(e.run("assert(EnhanceSetText('count', '8'))") == 0);
  CHECK(edit->text() == "8");
  CHECK(e.getNumber("gKeys") == 2);
  // No such field, not a TextEdit, or a missing argument: false, nothing changes.
  REQUIRE_MESSAGE(e.run(buttonLua(std::string("name = 'b', graphics = ") + kPush)) == 0, e.lastError);
  REQUIRE(e.run("assert(EnhanceSetText('nope', '1') == false and EnhanceSetText('b', '1') == false and EnhanceSetText('count') == false)") == 0);
  CHECK(e.getNumber("gKeys") == 2);
}

TEST_CASE("enhance: a close button is pressed once however many notches arrive") {
  Env e(true);
  REQUIRE(e.booted);
  REQUIRE(e.run("PushModal('mb')") == 0);
  REQUIRE(e.run(buttonLua(std::string("name = 'ok', close = true, graphics = ") + kPush + ", command = function() gRan = (gRan or 0) + 1 end")) == 0);
  REQUIRE(e.run("function Enhance.onWheel() return 'ok' end") == 0);
  e.wm.handleInput(wheel(300, 300, 3));
  CHECK(e.getNumber("gRan") == 1);
  CHECK(e.wm.pendingMessages() == 2);
}

TEST_CASE("enhance: the notch loop stops when the press changed the top modal") {
  Env e(true);
  REQUIRE(e.booted);
  REQUIRE(e.run("PushModal('mb')") == 0);
  REQUIRE(e.run(buttonLua(std::string("name = 'more', graphics = ") + kPush +
                          ", command = function() gMore = (gMore or 0) + 1; PushModal('next') end")) == 0);
  REQUIRE(e.run("function Enhance.onWheel() return 'more' end") == 0);
  e.wm.handleInput(wheel(300, 300, 4));
  CHECK(e.getNumber("gMore") == 1);
}

TEST_CASE("enhance: the wheel finds the children of a modal smaller than they are") {
  Env e(true);
  REQUIRE(e.booted);
  REQUIRE(e.run("PushModal('mb')") == 0);
  ui::Window& modal = *e.wm.topModal();
  modal.setRect({0, 0, 50, 50});
  ui::Window& big = addPlain(modal, "big", {0, 0, 400, 400});
  addPlain(big, "far", {200, 200, 50, 50});
  REQUIRE(e.run("gNames = nil; function Enhance.onWheel(n, names) gNames = table.concat(names, ','); return true end") == 0);
  e.wm.handleInput(wheel(220, 220, 1));  // outside the modal's own rect, inside its child
  CHECK(e.getString("gNames") == "far,big,mb");
}

TEST_CASE("enhance: destroying the host removes both hooks") {
  test::ScriptEnv probe(std::string(CHOCO_TEST_ASSETS) + "/ui/modal");
  probe.config.set("enhancements", "1");
  {
    script::ScriptHost host(probe.ctx);
    REQUIRE(host.boot());
    CHECK(probe.wm.hasWheelHook());
    CHECK(static_cast<bool>(probe.wm.commandHook()));
  }
  CHECK_FALSE(probe.wm.hasWheelHook());
  CHECK_FALSE(static_cast<bool>(probe.wm.commandHook()));
}

TEST_CASE("enhance: every embedded overlay compiles and runs cleanly for its game") {
  for (const char* id : {"choc1", "choc2", "choc3"}) {
    CAPTURE(id);
    test::ScriptEnv probe(std::string(CHOCO_TEST_ASSETS) + "/ui/modal");
    script::loadOverlay(probe.ctx, id, script::embeddedEnhanceShared(), script::embeddedEnhanceFor(id));
    CHECK(probe.log.contains(std::string("[enhancements] on (") + id + ")"));
    CHECK_FALSE(probe.log.contains("[enhancements] off"));
    for (const auto& l : probe.log.lines()) CHECK_MESSAGE(l.find("[engine-error]") == std::string::npos, l);
    CHECK(probe.getString("Enhance") == "table");
    CHECK(probe.wm.hasWheelHook());
    CHECK(static_cast<bool>(probe.wm.commandHook()));
    // The handlers feature-detect: with none of the game's windows or globals present they return nil.
    REQUIRE_MESSAGE(probe.run("assert(not Enhance.onWheel(1, {}) and not Enhance.onWheel(-1, {'x'}))\n"
                              "assert(Enhance.beforeCommand('x', nil, false) == nil)") == 0,
                    probe.lastError);
  }
}

TEST_CASE("enhance: a failing overlay boots as off") {
  const char* bad[] = {"error('boom')", "this is not lua ("};
  for (const char* gameCode : bad) {
    CAPTURE(gameCode);
    test::ScriptEnv probe(std::string(CHOCO_TEST_ASSETS) + "/ui/modal");
    script::loadOverlay(probe.ctx, "choc3", script::embeddedEnhanceShared(), gameCode);
    CHECK(probe.log.contains("[enhancements] enhance_choc3.lua"));
    CHECK(probe.log.contains("[enhancements] off"));
    CHECK_FALSE(probe.log.contains("[enhancements] on"));
    CHECK(probe.isNil("Enhance"));
    for (const char* n : {"EnhanceClose", "EnhanceWindowExists", "EnhanceHasButtonGraphic", "EnhanceButtonGraphic", "EnhanceSetText"})
      CHECK_MESSAGE(probe.isNil(n), n);
    CHECK_FALSE(probe.wm.hasWheelHook());
    CHECK_FALSE(static_cast<bool>(probe.wm.commandHook()));
  }
  // The shared file failing is the same.
  test::ScriptEnv probe(std::string(CHOCO_TEST_ASSETS) + "/ui/modal");
  script::loadOverlay(probe.ctx, "choc3", "error('shared boom')", script::embeddedEnhanceFor("choc3"));
  CHECK(probe.log.contains("[enhancements] enhance.lua"));
  CHECK(probe.isNil("Enhance"));
  CHECK_FALSE(probe.wm.hasWheelHook());
}

namespace {

// The real host as the script sink (Main is a coroutine waiting for messages), over the button textures.
struct CoEnv : test::ScriptEnv {
  CoEnv()
      : ScriptEnv(std::string(CHOCO_TEST_ASSETS) + "/enhance/main"),
        btnFs(std::string(CHOCO_TEST_ASSETS) + "/button", ""),
        cache(btnFs, nullptr),
        host(ctx) {
    config.set("enhancements", "1");
    wm.setTextures(&cache);
    booted = host.boot();
  }
  void click(float x, float y) {
    InputEvent m;
    m.type = InputEvent::Type::MouseMove;
    m.x = x;
    m.y = y;
    wm.handleInput(m);
    m.type = InputEvent::Type::MouseDown;
    wm.handleInput(m);
    m.type = InputEvent::Type::MouseUp;
    wm.handleInput(m);
  }
  assets::AssetFS btnFs;
  gfx::TextureCache cache;
  script::ScriptHost host;
  bool booted = false;
};

}  // namespace

TEST_CASE("enhance: a replacement resumed into Main may open a modal") {
  CoEnv e;
  REQUIRE(e.booted);
  for (int i = 1; i <= 3; ++i) e.host.tick(i * 17.0);
  REQUIRE(e.host.state() == script::ScriptHost::CoState::WaitingMessage);
  REQUIRE(e.run("PushModal('mb')") == 0);
  REQUIRE(e.run(buttonLua(std::string("name = 'ok', close = true, graphics = ") + kPush + ", command = function() gOrig = 1 end")) == 0);
  REQUIRE(e.run("function Enhance.beforeCommand(name, original, closes)\n"
                "  return function() PushModal('inner'); gOpened = 1 end\n"
                "end") == 0);
  const ui::WindowId before = e.wm.topModal()->id();
  e.click(5, 5);
  CHECK(e.getNumber("gOpened") == 1);
  CHECK(e.isNil("gOrig"));
  CHECK(e.wm.topModal()->id() != before);
  CHECK(e.host.state() == script::ScriptHost::CoState::WaitingMessage);
}
