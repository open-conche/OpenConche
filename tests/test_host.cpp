#include <doctest/doctest.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "ScriptHarness.h"
#include "script/Loaders.h"
#include "script/Messages.h"
#include "script/Stubs.h"
#include "games/choc3/Choc3Api.h"

using namespace choco::script;
using choco::test::ScriptEnv;
namespace ui = choco::ui;

namespace {

std::string hostAssets(const char* scenario) { return std::string(CHOCO_TEST_ASSETS) + "/host/" + scenario; }

// A booted host over one scenario directory (tests/assets/host/<name>/ui/mainloop.lua).
struct HostEnv : ScriptEnv {
  explicit HostEnv(const char* scenario, bool bootNow = true) : ScriptEnv(hostAssets(scenario)), host(ctx) {
    if (bootNow) booted = host.boot();
  }
  ~HostEnv() { /* ScriptHost is destroyed before ScriptEnv members */ }
  // One simulated 60 Hz frame.
  void frame() {
    ++frames;
    host.tick(static_cast<double>(frames) * 1000.0 / 60.0);
  }
  void frames_(int n) {
    for (int i = 0; i < n; ++i) frame();
  }
  ScriptHost host;
  bool booted = false;
  int frames = 0;
};

// A native template argument needs linkage, so this cannot be a function-local struct's member.
void testDeliver(ScriptContext& ctx, std::string name) {
  ui::Message m{3, name, 0};
  ctx.host->deliver(&m, nullptr);
}

}  // namespace

TEST_CASE("host: boot sets the libraries, constants and prelude") {
  HostEnv e("poll");
  REQUIRE(e.booted);
  CHECK(e.isNil("math"));
  CHECK(e.isNil("io"));
  CHECK(e.isNil("os"));
  CHECK(e.isNil("debug"));
  CHECK(e.getString("string") == "table");
  CHECK(e.getString("table") == "table");
  CHECK(e.getString("coroutine") == "table");
  CHECK(e.getNumber("kScreenWidth") == 800);
  CHECK(e.getNumber("kGameHeight") == 600);
  CHECK(e.getNumber("kGameScale") == 1);
  CHECK(e.getString("gVersionString") == "ver ");  // no settings.xml in the test assets
  CHECK(e.getString("LUA_PATH") == "?;../?;../../?");
  CHECK(e.getString("WaitForMessage") == "function");
  CHECK(e.getString("Window") == "function");  // window types come from AddWindowType
  CHECK(e.getString("MapWindow") == "function");
  CHECK(e.wm.windowTypes().size() > 10);
  CHECK(e.getString("DumpTable") == "function");  // host.lua
}

TEST_CASE("host: DebugOut, _ALERT, toparams") {
  HostEnv e("poll");
  REQUIRE_MESSAGE(e.run("DebugOut('LOAD', 12, nil, true, {})") == 0, e.lastError);
  CHECK(e.log.contains("[debug] LOAD 12 nil true <table>"));
  e.run("_ALERT('bad thing\\n')");
  CHECK(e.log.contains("[lua-error] bad thing"));
  e.run("r = toparams(1)");
  CHECK(e.stubs.count("toparams") == 1);
}

TEST_CASE("host: movie splashes are skipped, image splashes still run") {
  HostEnv e("splash");
  REQUIRE(e.booted);
  CHECK(e.getNumber("gAfterMovie") == 0);  // the movie splash returned at once, with no waiting
  CHECK(e.getString("gDone") == "true");
  CHECK(e.host.mainReturned());
  CHECK(e.host.luaErrorCount() == 0);
  CHECK(e.stubs.count("StartMovie") == 1);  // only the graphic-only splash asked for a movie
}

TEST_CASE("host: a timeout resumes Main with nothing") {
  HostEnv e("timeout");
  REQUIRE(e.booted);
  CHECK(e.host.state() == ScriptHost::CoState::WaitingMessage);
  e.frames_(5);
  CHECK(e.isNil("gDone"));  // 83 ms: not yet
  e.frame();
  CHECK(e.getString("gDone") == "true");
  CHECK(e.isNil("gMsg"));
  CHECK(e.getNumber("gEnd") - e.getNumber("gStart") == doctest::Approx(100).epsilon(0.2));
  CHECK(e.host.mainReturned());
  CHECK(e.host.luaErrorCount() == 0);
}

TEST_CASE("host: a nil timeout polls every frame") {
  HostEnv e("poll");
  REQUIRE(e.booted);
  CHECK(e.getNumber("gCount") == 0);
  e.frames_(10);
  CHECK(e.getNumber("gCount") == 10);
  CHECK(e.host.state() == ScriptHost::CoState::WaitingMessage);
}

TEST_CASE("host: Pause(100) lasts 6 to 7 frames at 60 Hz") {
  HostEnv e("pause");
  REQUIRE(e.booted);
  int n = 0;
  while (e.isNil("gDone") && n < 30) {
    e.frame();
    ++n;
  }
  CHECK(n >= 6);
  CHECK(n <= 7);
  CHECK(e.getNumber("gEnd") - e.getNumber("gStart") >= 100);
}

TEST_CASE("host: gCommandQueue drains on a plain resume") {
  HostEnv e("queue");
  REQUIRE(e.booted);
  CHECK(e.getNumber("gRan") == 0);
  e.frame();
  CHECK(e.getNumber("gRan") == 1);
  CHECK(e.getString("gAfter") == "ran");
}

TEST_CASE("host: an error in Main is logged and does not stop the loop") {
  HostEnv e("error");
  REQUIRE(e.booted);
  e.frame();
  CHECK(e.host.state() == ScriptHost::CoState::Dead);
  CHECK(e.host.luaErrorCount() == 1);
  CHECK_FALSE(e.host.mainReturned());
  CHECK(e.log.contains("[lua-error] "));
  CHECK(e.log.contains("boom"));
  auto resumes = e.host.resumeCount();
  e.frames_(30);  // the frame loop carries on; Main is not restarted
  CHECK(e.host.resumeCount() == resumes);
  CHECK(e.host.luaErrorCount() == 1);
  ui::Message m{3, "x", 0};
  e.host.deliver(&m, nullptr);
  e.host.deliver(&m, nullptr);
  CHECK(e.log.count("[script] Main is not running") == 1);  // logged once
  e.run("nq = table.getn(gMessageQueue)");
  CHECK(e.getNumber("nq") == 2);  // a dead Main still queues, as the original does
  // Code is still run with pcall when Main is dead.
  e.host.runString("gZ = 5");
  CHECK(e.getNumber("gZ") == 5);
  e.host.runString("error('late')");
  CHECK(e.host.luaErrorCount() == 2);
}

TEST_CASE("host: a message wakes Main with (message, command)") {
  HostEnv e("deliver");
  REQUIRE(e.booted);
  e.frames_(3);
  CHECK(e.isNil("gKind"));  // polled with no message so far
  ui::Message m{3, "hello", 0};
  auto cmd = e.compile("gCmd = 'ran'; return 5");
  e.host.deliver(&m, &cmd);
  CHECK(e.getString("gKind") == "userdata");
  CHECK(e.getNumber("gType") == 3);
  CHECK(e.getString("gName") == "hello");
  CHECK(e.isNil("gFoo"));  // only type and name are exposed
  CHECK(e.getString("gCmd") == "ran");
  CHECK(e.getNumber("gYieldResult") == 5);
}

TEST_CASE("host: a command with no message arrives as kCommandOnly") {
  HostEnv e("deliver");
  REQUIRE(e.booted);
  auto cmd = e.compile("gCmd = 1");
  e.host.deliver(nullptr, &cmd);
  CHECK(e.getNumber("gType") == ui::kCommandOnly);
  CHECK(e.getString("gName") == "");
  CHECK(e.getNumber("gCmd") == 1);
}

TEST_CASE("host: delivery while Main is running queues on the Lua tables") {
  HostEnv e("queued", false);
  bind<testDeliver>(e.lua.L(), e.ctx, "TestDeliver");
  REQUIRE(e.host.boot());
  e.frames_(5);  // Pause(10) in Main ends
  CHECK(e.getNumber("gQueued") == 2);
  CHECK(e.getNumber("gType") == 3);
  CHECK(e.getString("gName") == "q2");  // popped from the end
  CHECK(e.getString("gName2") == "q1");
  CHECK(e.getNumber("gLeft") == 0);
  CHECK(e.getString("gDone") == "true");
}

TEST_CASE("host: a plain yield drops the message and pcalls the command") {
  HostEnv e("yield");
  REQUIRE(e.booted);
  CHECK(e.host.state() == ScriptHost::CoState::Yielded);
  ui::Message m{3, "dropped", 0};
  auto cmd = e.compile("gCmd = 'ran'");
  e.host.deliver(&m, &cmd);
  CHECK(e.getString("gCmd") == "ran");
  CHECK(e.host.state() == ScriptHost::CoState::Yielded);
  CHECK(e.isNil("gAfter"));
  e.frame();  // resumed with no values on the next tick
  CHECK(e.getString("gAfter") == "true");
  CHECK(e.host.state() == ScriptHost::CoState::WaitingMessage);
}

TEST_CASE("host: code is injected into a waiting Main and run by pcall otherwise") {
  HostEnv e("inject");
  REQUIRE(e.booted);
  auto fn = e.compile("gInjected = 42");
  e.host.runInScript(fn);
  CHECK(e.getNumber("gInjected") == 42);
  CHECK(e.host.state() == ScriptHost::CoState::WaitingMessage);
  e.host.runString("AskQuit()");  // what a close request runs
  CHECK(e.getNumber("gAsked") == 1);
  CHECK(e.host.state() == ScriptHost::CoState::WaitingMessage);
  e.host.runString("syntax error here");
  CHECK(e.host.luaErrorCount() == 1);
  CHECK(e.host.state() == ScriptHost::CoState::WaitingMessage);
  e.frames_(100);  // timeouts keep resuming Main, which keeps waiting
  CHECK(e.host.state() == ScriptHost::CoState::WaitingMessage);
}

TEST_CASE("host: callProtected logs errors and keeps the main state clean") {
  HostEnv e("inject");
  REQUIRE(e.booted);
  auto bad = e.compile("error('onend failed')");
  auto good = e.compile("gOk = 1");
  CHECK_FALSE(e.host.callProtected(bad));
  CHECK(e.host.callProtected(good));
  CHECK(e.getNumber("gOk") == 1);
  CHECK(e.log.contains("onend failed"));
  CHECK(lua_gettop(e.lua.L()) == 0);
}

TEST_CASE("host: mainloop.lua that returns no function, or fails to load") {
  {
    HostEnv e("nomain");
    CHECK(e.booted);
    CHECK(e.getString("gLoaded") == "true");
    CHECK(e.host.state() == ScriptHost::CoState::Dead);
    e.frames_(3);
  }
  {
    HostEnv e("syntaxerr");
    CHECK_FALSE(e.booted);  // a mainloop.lua that cannot be loaded is an engine failure
  }
}

TEST_CASE("host: setGlobalString") {
  HostEnv e("poll");
  e.host.setGlobalString("gReturnValue", "yes");
  CHECK(e.getString("gReturnValue") == "yes");
}

TEST_CASE("host: messages expose only type and name") {
  HostEnv e("poll");
  lua_State* L = e.lua.L();
  pushMessage(L, ui::Message{8, "n", 0});
  lua_setglobal(L, "gMsg");
  e.run("t, n, o = gMsg.type, gMsg.name, gMsg.other");
  CHECK(e.getNumber("t") == 8);
  CHECK(e.getString("n") == "n");
  CHECK(e.isNil("o"));
}

TEST_CASE("host: stubs return safe values, log once and count") {
  HostEnv e("stubs");
  REQUIRE(e.booted);
  CHECK(e.getString("gStr") == "");
  CHECK(e.getNumber("gNum") == 0);
  CHECK(e.getString("gBool") == "false");
  CHECK(e.stubs.count("GetLayerImage") == 2);
  CHECK(e.log.count("[unimplemented] GetLayerImage") == 1);
  REQUIRE_MESSAGE(e.run("function nvals(...) return arg.n end") == 0, e.lastError);
  // Ret::None returns no values; Ret::Nil returns one nil.
  REQUIRE_MESSAGE(e.run("n_none = nvals(LoadIngredients()); n_nil = nvals(toparams(1)); t_nil = tostring((toparams(1)))") == 0,
                  e.lastError);
  CHECK(e.getNumber("n_none") == 0);
  CHECK(e.getNumber("n_nil") == 1);
  CHECK(e.getString("t_nil") == "nil");
  // Ret::Table: no row in the tables uses it today, so register one.
  static const StubRow tableRows[] = {{"TableStub", Ret::Table, ""}};
  registerStubRows(e.lua.L(), e.ctx, tableRows, 1);
  REQUIRE_MESSAGE(e.run("tt = TableStub(); n_tab = nvals(TableStub()); empty = (next(tt) == nil)") == 0, e.lastError);
  CHECK(e.getString("tt") == "table");
  CHECK(e.getNumber("n_tab") == 1);
  CHECK(e.getString("empty") == "true");
  CHECK(e.stubs.render().find("GetLayerImage\t2\n") != std::string::npos);
  // Most frequent first.
  auto text = e.stubs.render();
  CHECK(text.substr(0, text.find('\t')) == "GetLayerImage");

  auto path = std::filesystem::temp_directory_path() / "choco_unimpl_test.txt";
  REQUIRE(e.stubs.writeFile(path.string()));
  std::ifstream f(path);
  std::stringstream ss;
  ss << f.rdbuf();
  CHECK(ss.str() == text);
  f.close();  // Windows refuses to remove an open file
  std::filesystem::remove(path);

  // Every generated stub returns exactly what its row says.
  std::size_t nRows = 0;
  const StubRow* rows = stubRows(nRows);
  for (std::size_t i = 0; i < nRows; ++i) {
    std::string code = std::string("rc = nvals(") + rows[i].name + "()); rt = type((" + rows[i].name + "()))";
    if (rows[i].ret == Ret::None) code = std::string("rc = nvals(") + rows[i].name + "())";
    if (std::string(rows[i].name) == "HandleGetMessage") continue;  // real native, yields
    // Natives that replaced their stub are tested where they live (test_ui.cpp); called bare they would act on state.
    if (std::find(e.ctx.nativeNames.begin(), e.ctx.nativeNames.end(), rows[i].name) != e.ctx.nativeNames.end()) continue;
    if (std::string(rows[i].name) == "GetTimer" || std::string(rows[i].name) == "CurrentTime" ||
        std::string(rows[i].name) == "RandRange" || std::string(rows[i].name) == "DebugOut" ||
        std::string(rows[i].name) == "_ALERT")
      continue;  // replaced by real natives
    REQUIRE_MESSAGE(e.run(code) == 0, rows[i].name << ": " << e.lastError);
    switch (rows[i].ret) {
      case Ret::None: CHECK_MESSAGE(e.getNumber("rc") == 0, rows[i].name); break;
      case Ret::Nil: CHECK_MESSAGE(e.getString("rt") == "nil", rows[i].name); break;
      case Ret::String: CHECK_MESSAGE(e.getString("rt") == "string", rows[i].name); break;
      case Ret::Number: CHECK_MESSAGE(e.getString("rt") == "number", rows[i].name); break;
      case Ret::Bool: CHECK_MESSAGE(e.getString("rt") == "boolean", rows[i].name); break;
      case Ret::Table: CHECK_MESSAGE(e.getString("rt") == "table", rows[i].name); break;
    }
    if (rows[i].ret != Ret::None) CHECK_MESSAGE(e.getNumber("rc") == 1, rows[i].name);
  }
}

TEST_CASE("host: every real native name is in the API tables or the allowlist") {
  HostEnv e("poll");
  REQUIRE(e.booted);
  const char* allow[] = {"AddWindowType", "dofile", "loadfile", "require"};
  REQUIRE(e.ctx.nativeNames.size() > 5);
  for (const auto& n : e.ctx.nativeNames) {
    bool ok = isApiName(n);
    for (auto a : allow) ok = ok || n == a;
    CHECK_MESSAGE(ok, "native '" << n << "' is not in api-globals.tsv, api-windows.tsv or the allowlist");
  }
  // And the generated stub table is non-trivial and covers the natives the prelude calls.
  std::size_t n = 0;
  stubRows(n);
  CHECK(n > 100);
}

TEST_CASE("host: the RNG is deterministic per seed") {
  Rng a(1), b(1), c(2);
  bool differs = false;
  for (int i = 0; i < 20; ++i) {
    int x = a.range(1, 6), y = b.range(1, 6), z = c.range(1, 6);
    CHECK(x == y);
    CHECK(x >= 1);
    CHECK(x <= 6);
    differs = differs || x != z;
  }
  CHECK(differs);
  CHECK(a.range(5, 3) == 5);
  CHECK(Rng(7).range(4, 4) == 4);

  HostEnv e("poll");
  e.run("a = RandRange(1, 100); b = RandRange(1, 100)");
  HostEnv f("poll");
  f.run("a = RandRange(1, 100); b = RandRange(1, 100)");
  CHECK(e.getNumber("a") == f.getNumber("a"));
  CHECK(e.getNumber("b") == f.getNumber("b"));
  e.clock.nowMs = 1234.9;
  e.run("t = GetTimer(); u = CurrentTime()");
  CHECK(e.getNumber("t") == 1234);
  CHECK(e.getNumber("u") == 1234);
}

// ---- loaders ----

namespace {
struct LoaderEnv : ScriptEnv {
  LoaderEnv() : ScriptEnv(std::string(CHOCO_TEST_ASSETS) + "/loaders") {
    registerLoaders(lua.L(), ctx);
    registerLogging(lua.L(), ctx);
  }
};
}  // namespace

TEST_CASE("loaders: dofile always re-runs, case-insensitively, with multiple results") {
  LoaderEnv e;
  REQUIRE_MESSAGE(e.run("gLoadsA = nil; dofile('mod_a.lua'); dofile('MOD_A.LUA'); dofile('assets/mod_a.lua')") == 0, e.lastError);
  CHECK(e.getNumber("gLoadsA") == 3);
  e.run("function nvals(...) return arg.n end; n = nvals(dofile('multi.lua'))");
  CHECK(e.getNumber("n") == 3);
  CHECK(e.run("dofile('missing.lua')") != 0);
  CHECK(e.lastError.find("cannot open missing.lua") != std::string::npos);
}

TEST_CASE("loaders: loadfile returns the chunk or nil and a message") {
  LoaderEnv e;
  e.run("f = loadfile('multi.lua'); a, b = f()");
  CHECK(e.getNumber("a") == 1);
  e.run("f, msg = loadfile('missing.lua')");
  CHECK(e.isNil("f"));
  CHECK(e.getString("msg") == "cannot open missing.lua");
  e.run("f, msg = loadfile('broken.lua')");
  CHECK(e.isNil("f"));
  CHECK(e.getString("msg").find("loaders") == std::string::npos);  // chunk name is the asset name
  CHECK(e.getString("msg").find("broken.lua:1:") != std::string::npos);
}

TEST_CASE("loaders: require caches in _LOADED, sets _REQUIREDNAME and searches LUA_PATH") {
  LoaderEnv e;
  e.run("LUA_PATH = '?;../?;../../?'");
  e.run("gLoadsA = nil; m1 = require('mod_a.lua'); m2 = require('mod_a.lua')");
  CHECK(e.getNumber("gLoadsA") == 1);
  e.run("same = (m1 == m2); nm = m1.requiredName; r = _REQUIREDNAME");
  CHECK(e.getString("same") == "true");
  CHECK(e.getString("nm") == "mod_a.lua");
  CHECK(e.isNil("r"));  // restored afterwards
  e.run("c = _LOADED['mod_a.lua'] == m1");
  CHECK(e.getString("c") == "true");

  e.run("x = require('multi.lua')");  // first result only, cached
  CHECK(e.getNumber("x") == 1);

  // A module that returns nothing is cached as true.
  e.run("LUA_PATH = 'lib/?.lua;?'; v = require('modb'); w = require('modb')");
  CHECK(e.getString("v") == "true");
  CHECK(e.getNumber("gLoadsB") == 1);

  // A missing package reports the stock message with the path.
  CHECK(e.run("require('nomod')") != 0);
  CHECK(e.lastError.find("could not load package `nomod' from path `lib/?.lua;?'") != std::string::npos);
  // A package with a syntax error reports the stock "error loading package".
  e.run("LUA_PATH = '?'");
  CHECK(e.run("require('broken.lua')") != 0);
  CHECK(e.lastError.find("error loading package `broken.lua' (") != std::string::npos);
}

TEST_CASE("loaders: require retries a bare name with .lua, keyed by the original name, and logs it") {
  LoaderEnv e;
  e.run("LUA_PATH = '?;../?;../../?'");
  e.run("gLoadsC = nil; m1 = require('modc'); m2 = require('modc')");
  CHECK(e.getNumber("gLoadsC") == 1);  // the second call is a _LOADED hit
  e.run("nm = m1.requiredName; same = (m1 == m2); keyed = (_LOADED['modc'] == m1); other = _LOADED['modc.lua']");
  CHECK(e.getString("nm") == "modc");  // _REQUIREDNAME is the original name
  CHECK(e.getString("same") == "true");
  CHECK(e.getString("keyed") == "true");
  CHECK(e.isNil("other"));
  CHECK(e.log.count("[script] require 'modc' resolved as 'modc.lua'") == 1);
  // A name that exists nowhere still fails with the original message.
  CHECK(e.run("require('nomod')") != 0);
  CHECK(e.lastError.find("could not load package `nomod' from path `?;../?;../../?'") != std::string::npos);
  // An exact hit does not use the fallback.
  e.run("require('mod_a.lua')");
  CHECK(e.log.count("[script] require") == 1);
}

TEST_CASE("loaders: require without LUA_PATH uses the stock default and never the environment") {
  LoaderEnv e;
  e.run("LUA_PATH = nil");
  e.run("gLoadsA = nil; require('mod_a')");  // default '?;?.lua' finds mod_a.lua with the second template
  CHECK(e.getNumber("gLoadsA") == 1);
}

TEST_CASE("loaders: a first line starting with # is skipped and line numbers are kept") {
  LoaderEnv e;
  CHECK(e.run("dofile('shebang.lua')") != 0);
  CHECK(e.lastError.find("shebang.lua:3: line three") != std::string::npos);
}

TEST_CASE("loaders: yielding across dofile gives the stock 5.0 error") {
  LoaderEnv e;
  e.run("co = coroutine.create(function() dofile('yielder.lua') end); ok, err = coroutine.resume(co); ok = tostring(ok)");
  CHECK(e.getString("ok") == "false");
  CHECK(e.getString("err").find("attempt to yield across metamethod/C-call boundary") != std::string::npos);
}

TEST_CASE("lua: the game's `continue` statement") {
  LoaderEnv e;
  e.run("r = dofile('loops.lua')");
  // for: 1,3,5; while: 11,12,14,15,16; repeat: 21,23; ipairs: 31,33; closures: 10+20+30
  CHECK(e.getString("r") == "1,3,5,11,12,14,15,16,21,23,31,33,60");
  CHECK(e.run("continue") != 0);
  CHECK(e.run("x = 1\nif x then continue end") != 0);  // outside a loop
  CHECK(e.lastError.find("no loop to continue") != std::string::npos);
}

TEST_CASE("host: a yield that Lua refuses inside pcall does not confuse the state") {
  HostEnv e("pcallyield");
  REQUIRE(e.booted);
  // The error was caught inside Main, which went on to a real wait.
  CHECK(e.getString("gCaught") == "true");
  CHECK(e.getString("gOk") == "false");
  CHECK(e.host.state() == ScriptHost::CoState::WaitingMessage);
  CHECK(e.host.luaErrorCount() == 0);
  e.frames_(70);  // the 1000 ms wait times out normally
  CHECK(e.host.mainReturned());
}

TEST_CASE("host: code injected into a plain-yield suspension runs at once and Main stays suspended") {
  HostEnv e("yieldinject");
  REQUIRE(e.booted);
  CHECK(e.host.state() == ScriptHost::CoState::Yielded);
  auto fn = e.compile("gRan = 1");
  e.host.runInScript(fn);  // used to be pushed into a yield whose caller discards it: silently dropped
  CHECK(e.host.luaErrorCount() == 0);
  CHECK(e.getNumber("gRan") == 1);
  CHECK(e.host.state() == ScriptHost::CoState::Yielded);
  CHECK(e.isNil("gAfter"));
}

TEST_CASE("lua: hexadecimal numerals follow the game's lexer") {
  LoaderEnv e;
  REQUIRE_MESSAGE(e.run("a, b, c, d = 0x20AC, 0xff, 0xFfFf, 0x0") == 0, e.lastError);
  CHECK(e.getNumber("a") == 0x20AC);
  CHECK(e.getNumber("b") == 255);
  CHECK(e.getNumber("c") == 65535);
  CHECK(e.getNumber("d") == 0);
  REQUIRE(e.run("e, f = 0xFFFFFFFF, 0x80000000") == 0);
  CHECK(e.getNumber("e") == 4294967295.0);  // unsigned 32-bit
  CHECK(e.getNumber("f") == 2147483648.0);
  // Eight characters at most: the ninth hex digit is a separate token (here, a syntax error).
  CHECK(e.run("g = 0x123456789") != 0);
  REQUIRE(e.run("g = 0x12345678") == 0);
  CHECK(e.getNumber("g") == 0x12345678);
  // Only a lowercase x starts one; there are no fractions or exponents; ordinary numbers are untouched.
  CHECK(e.run("h = 0X10") != 0);
  REQUIRE(e.run("i, j, k, l = 0, 0.5, 10e2, .25") == 0);
  CHECK(e.getNumber("i") == 0);
  CHECK(e.getNumber("j") == 0.5);
  CHECK(e.getNumber("k") == 1000);
  CHECK(e.getNumber("l") == 0.25);
  REQUIRE(e.run("m = {[0x80] = 0x20AC}; n = m[128]") == 0);
  CHECK(e.getNumber("n") == 0x20AC);
}
