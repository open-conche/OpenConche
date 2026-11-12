// Chocolatier 2 trade slice: the DynamicWindow `ontext` hook, the string a
// dialog returns through CloseWindow(v), and PlaySound/LoadSound
// (docs/re/choc2-logo-and-trade.md section 2).
#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "games/Registry.h"
#include "ScriptHarness.h"
#include "audio/Audio.h"
#include "games/choc3/AudioEvents.h"
#include "games/common/DynamicWindow.h"
#include "games/choc2/Choc2AudioNatives.h"
#include "ui/widgets/InputWidgets.h"

using namespace choco;
using game::DynamicWindow;

namespace {

struct TradeEnv : test::ScriptEnv {
  TradeEnv()
      : ScriptEnv(std::string(CHOCO_TEST_ASSETS) + "/choc2_ledger"),
        host(ctx),
        audio(
            [](const std::string& path) -> std::optional<audio::SoundCache::Bytes> {
              loaded.push_back(path);
              std::ifstream f(std::filesystem::path(CHOCO_TEST_ASSETS) / "audio" / "sine_stereo.ogg", std::ios::binary);
              return audio::SoundCache::Bytes{std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
            },
            games::choc3::audioEvents(), [](const std::string&) {}, 1) {
    ctx.module = games::findModule("choc2");
    ctx.module->attach(ctx);
    loaded.clear();
    booted = host.boot();
    ctx.audio = &audio;
  }
  ui::Window* win(const char* name) { return wm.topModal()->find(name); }
  template <class T>
  T* get(const char* name) {
    return dynamic_cast<T*>(win(name));
  }
  // Types into the TextEdit and delivers the messages it posts.
  void type(const char* edit, const std::string& s) {
    auto* t = get<ui::TextEditWindow>(edit);
    REQUIRE(t);
    t->typeText(s);
    wm.dispatchMessages();
  }
  bool fired(const std::string& path) const { return audio.dump().find(path + "=") != std::string::npos; }
  script::ScriptHost host;
  audio::Audio audio;
  bool booted = false;
  static inline std::vector<std::string> loaded;
};

const char* kReceipt =
    "PushModal('t');"
    "TextEdit{name = 'amount', x = 0, y = 0, w = 100, h = 20, length = 7, "
    "label = '', font = {0, 20}}();"
    "hits = 0; last = '';"
    "function gNumberPadCallback() hits = hits + 1; last = GetLabel('amount') "
    "end;"
    "DynamicWindow{x = 0, y = 0, w = 1, h = 1, ontext = true, script = 'if "
    "gNumberPadCallback then gNumberPadCallback() end'}()";

}  // namespace

TEST_CASE(
    "DynamicWindow ontext: the script runs on every text change, not at "
    "creation") {
  TradeEnv e;
  REQUIRE(e.booted);
  REQUIRE(e.run(kReceipt) == 0);
  CHECK(e.lastError.empty());
  CHECK(e.getNumber("hits") == 0);  // building the window is not a text change
  e.type("amount", "1");
  CHECK(e.getNumber("hits") == 1);
  CHECK(e.getString("last") == "1");
  e.type("amount", "25");  // two characters, two messages
  CHECK(e.getNumber("hits") == 3);
  CHECK(e.getString("last") == "125");
  CHECK_FALSE(e.log.contains("not supported yet"));
  // SetLabel on the field is not a user edit: no message, no run (the pad's own
  // presses call the callback themselves).
  REQUIRE(e.run("SetLabel('amount', '7')") == 0);
  e.wm.dispatchMessages();
  CHECK(e.getNumber("hits") == 3);
}

TEST_CASE(
    "DynamicWindow ontext: no run once the window is gone, and a global "
    "that is nil is guarded by the script") {
  TradeEnv e;
  REQUIRE(e.booted);
  REQUIRE(e.run(kReceipt) == 0);
  e.type("amount", "4");
  CHECK(e.getNumber("hits") == 1);
  REQUIRE(e.run("gNumberPadCallback = nil") == 0);
  e.type("amount",
         "5");  // `if gNumberPadCallback then` makes this a no-op, not an error
  CHECK(e.getNumber("hits") == 1);
  CHECK(e.lastError.empty());
  for (const auto& c : e.wm.topModal()->children())
    if (auto* dw = dynamic_cast<DynamicWindow*>(c.get())) {
      CHECK(dw->hasOnText());
      CHECK(dw->textRuns() == 2);
      e.wm.destroyWindow(*dw);
      break;  // destroying it changed the list being walked
    }
  e.run("function gNumberPadCallback() hits = hits + 100 end");
  e.type("amount", "6");
  CHECK(e.getNumber("hits") == 1);
}

TEST_CASE(
    "DynamicWindow ontext: a script that posts another change runs again after, never nested, and "
    "without a script it rebuilds contents") {
  TradeEnv e;
  REQUIRE(e.booted);
  // The script posts another change: it is delivered after the script is done,
  // so it is a further run (bounded by the script's own counter) and never a
  // nested one.
  REQUIRE(e.run("PushModal('t');"
                "TextEdit{name = 'amount', x = 0, y = 0, w = 100, h = 20, length = "
                "7, label = '', font = {0, 20}}();"
                "depth = 0; maxDepth = 0; runs = 0;"
                "DynamicWindow{x = 0, y = 0, w = 1, h = 1, ontext = true, script = "
                "'depth = depth + 1; if depth > maxDepth then maxDepth = depth "
                "end; runs = runs + 1; "
                "if runs < 3 then PostMessage(CreateNamedMessage(kTextEditChanged, "
                "\"amount\")) end; depth = depth - 1'}()") == 0);
  e.type("amount", "1");
  CHECK(e.getNumber("runs") == 3);  // the typed character, then two self-posted
                                    // changes, each after the previous run ended
  CHECK(e.getNumber("maxDepth") == 1);
  CHECK(e.getNumber("depth") == 0);

  TradeEnv f;
  REQUIRE(f.booted);
  REQUIRE(f.run("PushModal('t');"
                "TextEdit{name = 'amount', x = 0, y = 0, w = 100, h = 20, "
                "length = 7, label = '', font = {0, 20}}();"
                "DynamicWindow{name = 'dyn', x = 0, y = 0, w = 100, h = 50, "
                "ontext = true, contents = 'ui/dyn_counter.lua'}()") == 0);
  CHECK(f.getNumber("dynBuilds") == 1);  // the creation build
  f.type("amount", "3");
  CHECK(f.getNumber("dynBuilds") == 2);
  auto* d = f.get<DynamicWindow>("dyn");
  REQUIRE(d);
  CHECK(d->textRuns() == 1);
}

TEST_CASE(
    "CloseWindow(v) hands the dialog the string it was given, and "
    "tonumber works on it") {
  TradeEnv e;
  REQUIRE(e.booted);
  REQUIRE(e.run("PushModal('t'); CloseWindow('42'); gotType = "
                "type(gReturnValue); gotNum = tonumber(gReturnValue) + 1;"
                "CloseWindow(''); emptyNum = tonumber(gReturnValue) or 0;") == 0);
  CHECK(e.getString("gotType") == "string");
  CHECK(e.getNumber("gotNum") == 43);
  CHECK(e.getNumber("emptyNum") == 0);
  // A number passes through as a number (the Lua decides; nothing in the engine
  // coerces).
  REQUIRE(e.run("CloseWindow(7); numType = type(gReturnValue)") == 0);
  CHECK(e.getString("numType") == "number");
}

TEST_CASE(
    "PlaySound: names resolve to sfx/<name>.ogg, a path is kept, misses "
    "and empty names are silent") {
  CHECK(script::choc2SoundPath("buy") == "sfx/buy.ogg");
  CHECK(script::choc2SoundPath("haggle_price_up") == "sfx/haggle_price_up.ogg");
  CHECK(script::choc2SoundPath("sfx/lab_select.ogg") == "sfx/lab_select.ogg");
  CHECK(script::choc2SoundPath("sfx/lab_select") == "sfx/lab_select.ogg");
  CHECK(script::choc2SoundPath("ui_click.ogg") == "sfx/ui_click.ogg");
  CHECK(script::choc2SoundPath("") == "");

  TradeEnv e;
  REQUIRE(e.booted);
  REQUIRE(e.run("PlaySound('buy'); PlaySound('sfx/lab_select.ogg'); "
                "PlaySound('sell'); PlaySound('sell'); PlaySound('')") == 0);
  CHECK(e.lastError.empty());
  CHECK(e.audio.dump().find("sfx/buy.ogg=1") != std::string::npos);
  CHECK(e.audio.dump().find("sfx/sell.ogg=2") != std::string::npos);
  CHECK(e.audio.dump().find("sfx/lab_select.ogg=1") != std::string::npos);
  CHECK_FALSE(e.log.contains("[unimplemented] PlaySound"));
}

TEST_CASE("LoadSound decodes into the cache and makes no sound") {
  TradeEnv e;
  REQUIRE(e.booted);
  const std::size_t before = e.audio.cache().decodedCount();
  REQUIRE(e.run("LoadSound('buy'); LoadSound('buy'); LoadSound('')") == 0);
  CHECK(e.audio.cache().decodedCount() == before + 1);
  CHECK(e.audio.dump().find("audio fired none") != std::string::npos);
  CHECK(std::count(TradeEnv::loaded.begin(), TradeEnv::loaded.end(), "sfx/buy.ogg") == 1);
  REQUIRE(e.run("PlaySound('buy')") == 0);
  CHECK(std::count(TradeEnv::loaded.begin(), TradeEnv::loaded.end(),
                   "sfx/buy.ogg") == 1);  // already decoded
  CHECK(e.fired("sfx/buy.ogg"));
}

TEST_CASE("DynamicWindow ontext: the listener goes when the window is destroyed and swept") {
  TradeEnv e;
  REQUIRE(e.booted);
  REQUIRE(e.run(kReceipt) == 0);
  CHECK(e.wm.messageListenerCount() == 1);
  for (const auto& c : e.wm.topModal()->children())
    if (auto* dw = dynamic_cast<DynamicWindow*>(c.get())) {
      e.wm.destroyWindow(*dw);
      break;  // destroying it changed the list being walked
    }
  e.wm.clearDestroyed();
  CHECK(e.wm.messageListenerCount() == 0);
}
