#include <doctest/doctest.h>

#include <filesystem>

#include "ScriptHarness.h"
#include "games/choc3/Choc3Natives.h"
#include "script/LuaStyle.h"
#include "script/Messages.h"
#include "script/ScriptHost.h"
#include "ui/WindowManager.h"

using namespace choco;
using choco::test::ScriptEnv;

namespace {

// Records what the window side hands to the script.
struct FakeSink : ui::ScriptSink {
  std::vector<ui::Message> got;
  void deliver(const ui::Message* m, const script::LuaRef*) override {
    if (m) got.push_back(*m);
  }
  void runInScript(const script::LuaRef&) override {}
  bool callProtected(const script::LuaRef&) override { return true; }
  void setGlobalString(const char*, std::string_view) override {}
};

ui::Window& add(ui::Window& parent, const std::string& name, const char* type = "Window") {
  auto w = std::make_unique<ui::PlaceholderWindow>(parent.manager(), type);
  w->setName(name);
  return parent.adoptFront(std::move(w));
}

std::string uiAssets(const char* scenario) { return std::string(CHOCO_TEST_ASSETS) + "/ui/" + scenario; }

bool haveReal() { return std::filesystem::is_directory(CHOCO_REFORGED_ASSETS); }

}  // namespace

TEST_CASE("ui tree: the first child is frontmost and ownership moves with adopt/orphan") {
  ui::WindowManager wm;
  ui::Window& root = *wm.screen();
  ui::Window& a = add(root, "a");
  ui::Window& b = add(root, "b");
  CHECK(root.children().size() == 2);
  CHECK(root.children()[0].get() == &b);  // added later = in front
  CHECK(root.children()[1].get() == &a);
  CHECK(a.parent() == &root);
  ui::WindowId aid = a.id();
  CHECK(wm.window(aid) == &a);
  auto owned = root.orphan(a);
  REQUIRE(owned);
  CHECK(a.parent() == nullptr);
  CHECK(root.children().size() == 1);
  CHECK(root.orphan(a) == nullptr);  // no longer a child
  owned.reset();
  CHECK(wm.window(aid) == nullptr);
}

TEST_CASE("ui tree: find is pre-order, self first, first duplicate wins") {
  ui::WindowManager wm;
  ui::Window& root = *wm.screen();
  ui::Window& p = add(root, "p");
  ui::Window& deepDup = add(add(p, "inner"), "x");  // front-to-back: p's children are [inner] only
  ui::Window& q = add(root, "q");
  ui::Window& qx = add(q, "x");
  // root children are [q, p]; pre-order visits q's subtree before p's.
  CHECK(root.find("x") == &qx);
  CHECK(q.find("x") == &qx);
  CHECK(p.find("x") == &deepDup);
  CHECK(root.find("q") == &q);
  ui::Window& selfNamed = add(q, "q");  // a window matches itself before its children
  (void)selfNamed;
  CHECK(root.find("q") == &q);
  CHECK(root.find("nothing") == nullptr);
  CHECK(root.find("") == nullptr);
}

TEST_CASE("ui tree: ids resolve to null after clearDestroyed, never reused") {
  ui::WindowManager wm;
  ui::Window& root = *wm.screen();
  ui::Window& a = add(root, "a");
  ui::Window& child = add(a, "child");
  ui::WindowId aid = a.id(), cid = child.id();
  wm.destroyWindow(a);
  CHECK(root.children().empty());
  CHECK(wm.destroyedCount() == 1);
  CHECK(wm.window(aid) == nullptr);  // gone from lookups at once
  CHECK(wm.window(cid) == nullptr);  // and so is everything under it
  wm.clearDestroyed();
  CHECK(wm.destroyedCount() == 0);
  CHECK(wm.window(aid) == nullptr);
  ui::Window& b = add(root, "b");
  CHECK(b.id() > cid);
}

TEST_CASE("ui modals: push, depth, pop by name and by id, popping everything above") {
  ui::WindowManager wm;
  CHECK(wm.modalDepth() == 1);  // the Screen
  auto& m1 = wm.pushModal("ui/one.lua");
  auto& m2 = wm.pushModal("ui/two.lua");
  auto& m3 = wm.pushModal("ui/three.lua");
  CHECK(wm.modalDepth() == 4);
  CHECK(m2.parent() == &m1);
  CHECK(m3.parent() == &m2);
  CHECK(m1.parent() == wm.screen());
  CHECK(m3.rect().w == 800);  // a zero-width modal takes its parent's size
  CHECK(wm.focus() == m3.id());
  CHECK(wm.creationTop() == &m3);
  CHECK(ui::WindowManager::idString(255) == "      ff");

  CHECK_FALSE(wm.popModal("ui/nothing.lua"));
  CHECK(wm.modalDepth() == 4);
  CHECK(wm.popModal("ui/two.lua"));  // pops three, then two
  CHECK(wm.modalDepth() == 2);
  CHECK(wm.topModal() == &m1);
  CHECK(wm.focus() == m1.id());
  CHECK(wm.destroyedCount() == 2);

  auto& m4 = wm.pushModal("ui/four.lua");
  auto& m5 = wm.pushModal("ui/five.lua");
  std::string id4 = ui::WindowManager::idString(m4.id());
  CHECK(wm.popModal(id4));  // by id string
  CHECK(wm.modalDepth() == 2);
  (void)m5;
  CHECK_FALSE(wm.popModal(""));
  CHECK_FALSE(wm.popModal("   "));
  wm.popModal("ui/one.lua");
  CHECK(wm.modalDepth() == 1);
  CHECK_FALSE(wm.popModal(ui::WindowManager::idString(wm.screen()->id())));  // the Screen is never popped
  CHECK(wm.modalDepth() == 1);
}

TEST_CASE("ui modals: kModalClosed is posted one event after the pop, in pop order") {
  ui::WindowManager wm;
  FakeSink sink;
  wm.setScriptSink(&sink);
  auto& m1 = wm.pushModal("a");
  auto& m2 = wm.pushModal("b");
  std::string id1 = ui::WindowManager::idString(m1.id()), id2 = ui::WindowManager::idString(m2.id());
  wm.popModal("a");
  CHECK(wm.pendingMessages() == 0);  // nothing at the pop itself
  wm.dispatchMessages();
  CHECK(sink.got.empty());
  wm.clearDestroyed();  // the windows die here and post
  CHECK(wm.pendingMessages() == 2);
  CHECK(sink.got.empty());  // still not delivered
  wm.dispatchMessages();
  REQUIRE(sink.got.size() == 2);
  CHECK(sink.got[0].type == ui::kModalClosed);
  CHECK(sink.got[0].name == id2);  // the top one was popped first
  CHECK(sink.got[1].name == id1);
  // A destroyed child posts nothing.
  add(*wm.screen(), "k");
  wm.destroyWindow(*wm.screen()->children().front());
  wm.clearDestroyed();
  CHECK(wm.pendingMessages() == 0);
}

TEST_CASE("ui dispatch: the modal consumes kCloseWindow, the script gets the rest") {
  ui::WindowManager wm;
  FakeSink sink;
  wm.setScriptSink(&sink);
  auto& m = wm.pushModal("dlg");
  std::string id = ui::WindowManager::idString(m.id());

  wm.post({ui::kCloseWindow, "", 0});  // empty name: swallowed, nothing popped
  wm.post({ui::kButtonPress, "ok", 0});
  wm.post({ui::kCloseWindow, id, 0});
  wm.post({1000, "x", 0});
  wm.dispatchMessages();
  CHECK(wm.modalDepth() == 1);  // the third message popped it
  REQUIRE(sink.got.size() == 2);
  CHECK(sink.got[0].type == ui::kButtonPress);
  CHECK(sink.got[1].name == "x");
}

TEST_CASE("ui dispatch: handlers can post and those run in the same pass; kQuitNow exits; guard stops runaways") {
  ui::WindowManager wm;
  struct Reposter : ui::ScriptSink {
    ui::WindowManager* wm = nullptr;
    int n = 0;
    void deliver(const ui::Message*, const script::LuaRef*) override {
      ++n;
      if (n < 3) wm->post({ui::kGeneric, "again", 0});
    }
    void runInScript(const script::LuaRef&) override {}
    bool callProtected(const script::LuaRef&) override { return true; }
    void setGlobalString(const char*, std::string_view) override {}
  } sink;
  sink.wm = &wm;
  wm.setScriptSink(&sink);
  wm.post({ui::kGeneric, "first", 0});
  wm.dispatchMessages();
  CHECK(sink.n == 3);
  CHECK_FALSE(wm.quitRequested());
  wm.post({ui::kQuitNow, "Quit", 0});
  wm.dispatchMessages();
  CHECK(wm.quitRequested());

  struct Looper : ui::ScriptSink {
    ui::WindowManager* wm = nullptr;
    int n = 0;
    void deliver(const ui::Message*, const script::LuaRef*) override {
      ++n;
      wm->post({ui::kGeneric, "loop", 0});
    }
    void runInScript(const script::LuaRef&) override {}
    bool callProtected(const script::LuaRef&) override { return true; }
    void setGlobalString(const char*, std::string_view) override {}
  } loop;
  ui::WindowManager wm2;
  loop.wm = &wm2;
  wm2.setScriptSink(&loop);
  wm2.post({ui::kGeneric, "loop", 0});
  wm2.dispatchMessages();
  CHECK(loop.n == ui::WindowManager::kMaxDispatch);
  CHECK(wm2.takeDispatchOverflow());
}

TEST_CASE("ui query: expect-window sees modal script paths, names, and hidden windows") {
  ui::WindowManager wm;
  auto& m = wm.pushModal("ui/x.lua");
  m.setName("renamed");  // MakeDialog{name=} renames the modal; the script path still matches
  auto& child = add(m, "c");
  CHECK(wm.isShown("ui/x.lua"));
  CHECK(wm.isShown("renamed"));
  CHECK(wm.isShown("c"));
  child.setShown(false);
  CHECK_FALSE(wm.isShown("c"));
  child.setShown(true);
  add(child, "g");
  child.setShown(false);
  CHECK_FALSE(wm.isShown("g"));  // an ancestor is hidden
  CHECK_FALSE(wm.isShown("missing"));
  CHECK(wm.dump().find("script=\"ui/x.lua\"") != std::string::npos);
}

// ---- natives through the real prelude ----

TEST_CASE("natives: a dialog builds placeholders, GetLabel/SetLabel/EnableWindow/DestroyWindow work, DoModal returns") {
  struct Env : ScriptEnv {
    Env() : ScriptEnv(uiAssets("modal")), host(ctx) { booted = host.boot(); }
    void pump() {
      wm.clearDestroyed();
      wm.dispatchMessages();
    }
    void frame() {
      ++frames;
      host.tick(frames * 1000.0 / 60.0);
      pump();
    }
    script::ScriptHost host;
    bool booted = false;
    int frames = 0;
  } e;
  REQUIRE(e.booted);
  e.frame();
  CHECK(e.host.luaErrorCount() == 0);
  CHECK(e.wm.modalDepth() == 2);
  CHECK(e.getNumber("gBuiltDepth") == 2);
  CHECK(e.wm.isShown("ui/dlg.lua"));
  CHECK(e.wm.topModal()->name() == "dlg");
  CHECK(e.wm.topModal()->scriptPath() == "ui/dlg.lua");

  // Later siblings are in front: Button, other, box.
  const auto& kids = e.wm.topModal()->children();
  REQUIRE(kids.size() == 3);
  CHECK(kids[0]->name() == "ok");
  CHECK(kids[1]->name() == "other");
  CHECK(kids[2]->name() == "box");
  CHECK(kids[2]->rect() == gfx::RectI{10, 20, 100, 50});
  CHECK(kids[1]->rect().x == 0);  // kCenter is not interpreted yet
  CHECK_FALSE(e.log.contains("[unimplemented-type] Window"));  // Window is a real plain built-in

  // t1 appears twice in box; the later (front) one is found first.
  REQUIRE(e.run("lbl = GetLabel('t1'); okl = GetLabel('ok'); miss = GetLabel('nope'); nl = GetLabel('box')") == 0);
  CHECK(e.getString("lbl") == "dup");
  CHECK(e.getString("okl") == "");  // a button's label is its child Text named "label"; this one has none
  CHECK(e.getString("miss") == "nope_is_not_found");
  CHECK(e.getString("nl") == "box_is_not_a_TText_window_or_TButton");
  REQUIRE(e.run("SetLabel('t1', 'changed'); SetLabel('box', 'ignored'); SetLabel('zzz', 'x'); lbl2 = GetLabel('t1')") == 0);
  CHECK(e.getString("lbl2") == "changed");
  CHECK(e.wm.findInTopModal("box")->find("t1")->label() == "changed");

  REQUIRE(e.run("en1 = EnableWindow('box', false); en2 = EnableWindow('zzz', true)") == 0);
  CHECK(e.getString("en1") == "true");
  CHECK(e.getString("en2") == "false");
  CHECK_FALSE(e.wm.isShown("t1"));
  e.run("EnableWindow('box', true)");
  CHECK(e.wm.isShown("t1"));

  REQUIRE(e.run("DestroyWindow('other')") == 0);
  CHECK(e.wm.topModal()->children().size() == 2);
  CHECK(e.wm.destroyedCount() == 1);
  e.pump();
  CHECK(e.wm.destroyedCount() == 0);
  REQUIRE(e.run("SetFocus('box')") == 0);  // recorded only
  CHECK(e.wm.focus() == e.wm.findInTopModal("box")->id());

  // Closing: depth drops at the dispatch, Main hears about it one event later.
  CHECK(e.isNil("gDone"));
  REQUIRE(e.run("CloseWindow('yes')") == 0);
  e.wm.dispatchMessages();
  CHECK(e.wm.modalDepth() == 1);
  CHECK(e.isNil("gDone"));
  CHECK(e.wm.pendingMessages() == 0);
  e.wm.clearDestroyed();
  CHECK(e.wm.pendingMessages() == 1);
  CHECK(e.isNil("gDone"));
  e.wm.dispatchMessages();
  CHECK(e.getString("gDone") == "true");
  CHECK(e.getString("gResult") == "yes");
  CHECK(e.host.luaErrorCount() == 0);
}

TEST_CASE("natives: PushModal/GetWindowDepth/DoPopModal/ClearModalChildren/PostMessageToParent from Lua") {
  struct Env : ScriptEnv {
    Env() : ScriptEnv(uiAssets("modal")), host(ctx) { booted = host.boot(); }
    script::ScriptHost host;
    bool booted = false;
  } e;
  REQUIRE(e.booted);
  REQUIRE(e.run("a = PushModal('first'); b = PushModal('second'); d = GetWindowDepth()") == 0);
  CHECK(e.getNumber("d") == 4);  // Screen, the dialog Main opened, and two more
  CHECK(e.getString("a").size() >= 8);
  REQUIRE(e.run("DoPopModal(a); d2 = GetWindowDepth()") == 0);
  CHECK(e.getNumber("d2") == 2);
  REQUIRE(e.run("PushModal('m'); Window{name='kid'}(); ClearModalChildren()") == 0);
  CHECK(e.wm.topModal()->rect() == gfx::RectI{0, 0, 0, 0});
  CHECK(e.wm.topModal()->children().empty());
  // A message to the parent targets the modal below the top.
  e.wm.dispatchMessages();
  REQUIRE(e.run("m = CreateNamedMessage(kButtonPress, 'hello'); PostMessageToParent(m); PostMessage(m); PostMessage(5)") == 0);
  CHECK(e.wm.pendingMessages() == 2);
  CHECK(e.log.contains("[lua-arg] PostMessage arg 1: expected a message"));
}

TEST_CASE("style: own table, then gCurrentDefaultStyle and its parent chain, never the table's own parent") {
  ScriptEnv e(uiAssets("modal"));
  lua_State* L = e.lua.L();
  REQUIRE(e.run(R"(
    grand = { fromgrand = 7, shared = "grand" }
    gCurrentDefaultStyle = { fromdef = "d", shared = "def", parent = grand }
    own = { x = 5, shared = "own", parent = { fromparent = 1 } }
    num = { w = "42", s = 12 }
  )") == 0);
  auto get = [&](const char* tbl) { lua_getglobal(L, tbl); return script::LuaStyle(L, lua_gettop(L)); };
  {
    lua_getglobal(L, "own");
    script::LuaStyle st(L, lua_gettop(L));
    CHECK(st.numberOr("x", -1) == 5);
    CHECK(st.stringOr("shared") == "own");
    CHECK(st.stringOr("fromdef") == "d");  // default style
    CHECK(st.numberOr("fromgrand", -1) == 7);  // default style's parent
    CHECK_FALSE(st.number("fromparent").has_value());  // the window table's own parent is not followed
    CHECK_FALSE(st.string("none").has_value());
    lua_pop(L, 1);
  }
  {
    lua_getglobal(L, "num");
    script::LuaStyle st(L, lua_gettop(L));
    CHECK(st.numberOr("w", -1) == 42);  // numeric strings convert
    CHECK(st.stringOr("s") == "12");
    CHECK_FALSE(st.number("shared").has_value());  // "def" is not numeric
    lua_pop(L, 1);
  }
  // A style that is its own ancestor (AppendStyle twice) terminates.
  REQUIRE(e.run("loop = {}; loop.parent = loop; gCurrentDefaultStyle = loop") == 0);
  lua_getglobal(L, "own");
  script::LuaStyle st(L, lua_gettop(L));
  CHECK_FALSE(st.string("nokey").has_value());
  lua_pop(L, 1);
  // A raising metamethod does not escape.
  REQUIRE(e.run("bad = setmetatable({}, { __index = function() error('boom') end }); gCurrentDefaultStyle = {}") == 0);
  lua_getglobal(L, "bad");
  script::LuaStyle st2(L, lua_gettop(L));
  CHECK_FALSE(st2.string("k").has_value());
  lua_pop(L, 1);
  (void)get;
  CHECK(lua_gettop(L) == 0);
}

// ---- content natives on the real data ----

TEST_CASE("content natives: counts on the Reforged data") {
  if (!haveReal()) {
    MESSAGE("SKIPPED: " CHOCO_REFORGED_ASSETS " not present");
    return;
  }
  ScriptEnv e(CHOCO_REFORGED_ASSETS);
  script::registerContentNatives(e.lua.L(), e.ctx);
  script::registerContentLoaderNatives(e.lua.L(), e.ctx);
  REQUIRE(e.run(R"(
    nIng, nProd = 0, 0
    function CreateIngredient(t)
      nIng = nIng + 1
      if nIng == 1 then firstName = t.name; firstType = type(t.price_low) end
    end
    function CreateProductFromXML(t)
      nProd = nProd + 1
      if nProd == 1 then prodCode = t.code end
    end
    LoadIngredients()
    LoadProducts()
    ports = {} LoadPortFileList(ports)
    quests = {} LoadQuestFileList(quests)
    nPorts, nQuests = table.getn(ports), table.getn(quests)
    firstPort = ports[1]
  )") == 0);
  CHECK(e.getNumber("nIng") == 100);
  CHECK(e.getNumber("nProd") == 72);
  CHECK(e.getNumber("nPorts") == 20);
  CHECK(e.getNumber("nQuests") == 5);
  CHECK(e.getString("firstType") == "string");  // every value is a string
  CHECK(e.getString("firstName") != "<nil>");
  CHECK(e.getString("prodCode") != "<nil>");
  CHECK(e.getString("firstPort").find("ports/") == 0);
  CHECK(e.log.contains("[content] LoadPortFileList: 20 files"));
}

TEST_CASE("content natives: a missing Create function is logged once, errors in it are logged") {
  if (!haveReal()) return;
  ScriptEnv e(CHOCO_REFORGED_ASSETS);
  script::registerContentNatives(e.lua.L(), e.ctx);
  script::registerContentLoaderNatives(e.lua.L(), e.ctx);
  REQUIRE(e.run("LoadIngredients(); LoadIngredients()") == 0);
  CHECK(e.log.count("[content] CreateIngredient is not defined") == 1);
  REQUIRE(e.run("function CreateProductFromXML(t) error('bad row') end LoadProducts()") == 0);
  CHECK(e.log.contains("[lua-error]"));
}

TEST_CASE("config natives: GetConfig, IsEnabled, CheckConfig") {
  ScriptEnv e(uiAssets("modal"));
  script::registerContentNatives(e.lua.L(), e.ctx);
  e.config.set("hiscorelocal", "1");
  e.config.set("cheatmode", "0");
  e.config.set("name", "x");
  REQUIRE(e.run(R"(
    a = GetConfig('name', 'dflt'); b = GetConfig('none', 'dflt'); c = GetConfig('none')
    d = IsEnabled('hiscorelocal'); f = IsEnabled('cheatmode'); g = IsEnabled('zzz')
    h = CheckConfig('local'); i = CheckConfig('dev'); j = CheckConfig('debug'); k = CheckConfig('xsell')
  )") == 0);
  CHECK(e.getString("a") == "x");
  CHECK(e.getString("b") == "dflt");
  CHECK(e.getString("c") == "");
  CHECK(e.getString("d") == "true");
  CHECK(e.getString("f") == "false");
  CHECK(e.getString("g") == "false");
  CHECK(e.getString("h") == "true");
  CHECK(e.getString("i") == "false");
  CHECK(e.getString("j") == "false");  // not an allowed key
  CHECK(e.getString("k") == "false");  // no xsell/xsell.lua in the test assets
  e.config.override("cheatmode", "1");
  e.config.set("cheatmode", "0");  // settings.xml cannot undo a command-line override
  REQUIRE(e.run("i2 = CheckConfig('dev')") == 0);
  CHECK(e.getString("i2") == "true");
}

TEST_CASE("string natives: bsg* and _GetString on real rows") {
  if (!haveReal()) return;
  ScriptEnv e(CHOCO_REFORGED_ASSETS);
  script::registerContentNatives(e.lua.L(), e.ctx);
  REQUIRE(e.run(R"(
    before = bsgStringExists('copyright')
    bsgLoadStringFile('strings.xml')
    bsgLoadStringFile('no_such_file.xml')
    after = bsgStringExists('copyright')
    copy = bsgGetString('copyright')
    welcome = bsgGetString('welcome')
    pct = bsgGetString('b12')
    lit = bsgGetString('#literal text')
    missing = bsgGetString('this_key_does_not_exist')
  )") == 0);
  // _GetString reads the SDK table, which the host fills from strings.xml at boot.
  e.ctx.strings.loadFile(e.fs, "strings.xml", assets::StringTable::Layer::Sdk);
  REQUIRE(e.run(R"(
    g1 = _GetString('welcome', 'Bob', '', '', '', '')
    g2 = _GetString('#raw', '', '', '', '', '')
    g3 = _GetString('trip_time', '3', '', '', '', '')
  )") == 0);
  CHECK(e.getString("before") == "false");
  CHECK(e.getString("after") == "true");
  // The row is the publisher's notice line; the test checks its shape (prefix, holder), not the notice itself.
  CHECK(e.getString("copy").rfind("Copyright", 0) == 0);
  CHECK(e.getString("copy").find("PlayFirst") != std::string::npos);
  CHECK(e.getString("welcome") == "Welcome back, -->1<--!");  // %1% is the Lua side's to fill
  CHECK(e.getString("pct") == "Special Reserve 66% African Cacao");
  CHECK(e.getString("lit") == "literal text");
  CHECK(e.getString("missing") == "#####");
  CHECK(e.getString("g1") == "Welcome back, Bob!");
  CHECK(e.getString("g2") == "raw");
  CHECK(e.getString("g3") == "This trip will take 3 weeks");
}

TEST_CASE("natives: SwapToModal moves the modal's script path; a script error is attributed to the modal") {
  struct Env : ScriptEnv {
    explicit Env(const char* sc) : ScriptEnv(uiAssets(sc)), host(ctx) { booted = host.boot(); }
    script::ScriptHost host;
    bool booted = false;
  };
  {
    Env e("swap");
    REQUIRE(e.booted);
    CHECK(e.wm.isShown("ui/first.lua"));
    CHECK_FALSE(e.wm.isShown("ui/second.lua"));
    REQUIRE(e.run("SwapToModal('ui/second.lua')") == 0);
    CHECK(e.wm.modalDepth() == 2);  // same modal, no push
    CHECK(e.wm.topModal()->scriptPath() == "ui/second.lua");
    CHECK(e.wm.isShown("ui/second.lua"));
    CHECK(e.wm.isShown("s1"));
    CHECK_FALSE(e.wm.isShown("f1"));
    CHECK_FALSE(e.wm.isShown("ui/first.lua"));
    CHECK(e.wm.dump().find("script=\"ui/second.lua\"") != std::string::npos);
    CHECK_FALSE(e.wm.hasScriptError("ui/second.lua"));
  }
  {
    Env e("swaperr");
    REQUIRE(e.booted);
    CHECK(e.host.luaErrorCount() == 1);
    CHECK(e.wm.hasScriptError("ui/first.lua"));
  }
}

TEST_CASE("string natives: bsgStringExists checks only the BSG map, in a booted host") {
  if (!haveReal()) return;
  struct Env : ScriptEnv {
    Env() : ScriptEnv(CHOCO_REFORGED_ASSETS), host(ctx) { booted = host.boot(); }
    script::ScriptHost host;
    bool booted = false;
  } e;
  REQUIRE(e.booted);  // strings.xml is in the SDK layer now
  REQUIRE(e.run("a = bsgStringExists('copyright'); b = bsgGetString('copyright'); bsgLoadStringFile('strings.xml'); c = bsgStringExists('copyright')") == 0);
  CHECK(e.getString("a") == "false");
  CHECK(e.getString("b") != "#####");  // the lookup still falls back to the SDK table
  CHECK(e.getString("c") == "true");
  CHECK(e.ctx.strings.has("copyright"));
}

TEST_CASE("ui dispatch: a message to a non-modal window climbs to its modal; a close to the modal below pops both") {
  ui::WindowManager wm;
  FakeSink sink;
  wm.setScriptSink(&sink);
  struct Eater : ui::PlaceholderWindow {
    using PlaceholderWindow::PlaceholderWindow;
    int seen = 0;
    bool consume = false;
    bool onMessage(const ui::Message&) override { ++seen; return consume; }
  };
  auto& m1 = wm.pushModal("one");
  auto& m2 = wm.pushModal("two");
  auto outer = std::make_unique<Eater>(wm, "Window");
  Eater& o = *outer;
  m2.adoptFront(std::move(outer));
  auto inner = std::make_unique<Eater>(wm, "Window");
  Eater& i = *inner;
  o.adoptFront(std::move(inner));

  wm.post({ui::kButtonPress, "b", i.id()});  // nobody consumes: inner, outer, modal, then the script
  wm.dispatchMessages();
  CHECK(i.seen == 1);
  CHECK(o.seen == 1);
  REQUIRE(sink.got.size() == 1);
  o.consume = true;
  wm.post({ui::kButtonPress, "b", i.id()});  // the outer window consumes it
  wm.dispatchMessages();
  CHECK(i.seen == 2);
  CHECK(o.seen == 2);
  CHECK(sink.got.size() == 1);
  // A message aimed at m1 never reaches windows in m2 (they are not its ancestors).
  wm.post({ui::kButtonPress, "c", m1.id()});
  wm.dispatchMessages();
  CHECK(o.seen == 2);
  CHECK(sink.got.size() == 2);

  // A close addressed to the modal below, as PostMessageToParent does, pops it and the one above.
  wm.post({ui::kCloseWindow, ui::WindowManager::idString(m1.id()), m1.id()});
  wm.dispatchMessages();
  CHECK(wm.modalDepth() == 1);
}

TEST_CASE("natives: PostMessageToParent targets the modal below the top; PostCreate hits the creation-stack top") {
  struct Env : ScriptEnv {
    Env() : ScriptEnv(uiAssets("modal")), host(ctx) { booted = host.boot(); }
    script::ScriptHost host;
    bool booted = false;
  } e;
  REQUIRE(e.booted);
  // Main's dialog is the top modal; its parent is the Screen. Close the dialog through its parent.
  std::string id = ui::WindowManager::idString(e.wm.topModal()->id());
  REQUIRE(e.run("PushModal('over')") == 0);
  std::string overId = ui::WindowManager::idString(e.wm.topModal()->id());
  REQUIRE(e.run("m = CreateNamedMessage(kCloseWindow, '" + id + "'); PostMessageToParent(m)") == 0);
  e.wm.dispatchMessages();  // target = the dialog below 'over'; the close pops the dialog and 'over'
  CHECK(e.wm.modalDepth() == 1);
  (void)overId;

  // PostCreate calls postChildrenInit on the creation stack top, not on the table's window.
  REQUIRE(e.run("PushModal('pc')") == 0);
  REQUIRE(e.run("GenericCreate({ typename = 'Window', name = 'outerw' })") == 0);
  REQUIRE(e.run("GenericCreate({ typename = 'Window', name = 'innerw' })") == 0);
  auto* outer = static_cast<ui::PlaceholderWindow*>(e.wm.findInTopModal("outerw"));
  auto* inner = static_cast<ui::PlaceholderWindow*>(e.wm.findInTopModal("innerw"));
  REQUIRE(outer);
  REQUIRE(inner);
  REQUIRE(e.run("PostCreate({}, 'Window')") == 0);
  CHECK(inner->postInits == 1);  // innerw is the creation top
  CHECK(outer->postInits == 0);
  REQUIRE(e.run("PopWindow(); PostCreate({}, 'Window')") == 0);
  CHECK(outer->postInits == 1);
  CHECK(inner->postInits == 1);
}
