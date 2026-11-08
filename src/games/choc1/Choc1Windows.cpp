#include "games/choc1/Choc1Windows.h"

#include "games/choc1/Choc1AudioNatives.h"
#include "games/choc1/Choc1World.h"
#include "script/ScriptHost.h"
#include "ui/WindowManager.h"
#include "ui/widgets/TextWidgets.h"
#include "ui/widgets/Widgets.h"

namespace choco::script {

namespace {

void walk(ui::Window& w, const std::string& name, const std::function<void(ui::Window&)>& fn) {
  if (w.name() == name) fn(w);
  for (const auto& c : w.children()) walk(*c, name, fn);
}

// UtilEnableWindow / UtilDisableWindow (exe 0041de40 / 0041ded0): the first window of that name gets the enabled bit set or
// cleared. That bit is also what hides a window and its subtree. Unlike the SDK's EnableWindow a disabled button keeps its
// state (no reset).
void nativeUtilEnableWindow(ScriptContext& ctx, std::string name) {
  if (ui::Window* w = ctx.wm.findInChain(name)) w->setShown(true);
}
void nativeUtilDisableWindow(ScriptContext& ctx, std::string name) {
  if (ui::Window* w = ctx.wm.findInChain(name)) w->setShown(false);
}

// UtilSetButtonOn / UtilSetLedgerTab (one function, exe 0041ee10): every Button of that name goes toggle-on. The ledger tabs are
// plain buttons in Chocolatier 1's Lua, so the same lookup serves the port's ledger.
void nativeUtilSetButtonOn(ScriptContext& ctx, std::string name) {
  forEachNamedInChain(ctx.wm, name, [](ui::Window& w) {
    if (auto* b = dynamic_cast<ui::ButtonWindow*>(&w)) b->setOn(true);
  });
}

// UtilSetGlobalString (0041ef70): the label of every Text of that name. The text is shown as given (no string-table lookup).
void nativeUtilSetGlobalString(ScriptContext& ctx, std::string name, std::string text) {
  forEachNamedInChain(ctx.wm, name, [&](ui::Window& w) {
    if (dynamic_cast<ui::TextWindow*>(&w)) w.setLabel(text);
  });
}

// UtilSetTextColor (0041ec50): every Text of that name takes colour (r, g, b, 1.0); the components default to 1.0.
void nativeUtilSetTextColor(ScriptContext& ctx, std::string name, TableArg t) {
  float c[3] = {1, 1, 1};
  if (t.valid()) {
    for (int i = 0; i < 3; ++i) {
      lua_rawgeti(t.L, t.index, i + 1);
      if (lua_isnumber(t.L, -1)) c[i] = static_cast<float>(lua_tonumber(t.L, -1));
      lua_pop(t.L, 1);
    }
  }
  forEachNamedInChain(ctx.wm, name, [&](ui::Window& w) {
    if (auto* tw = dynamic_cast<ui::TextWindow*>(&w)) tw->setColor({c[0], c[1], c[2], 1.0f});
  });
}

// UtilPositionWindow (0041e050): finds the window below the top modal and sets its position.
void nativeUtilPositionWindow(ScriptContext& ctx, std::string name, double x, double y) {
  if (ui::Window* w = ctx.wm.findInTopModal(name)) w->setPos(static_cast<int>(x), static_cast<int>(y));
}

// UtilPulseImage (0041e1c0): a TImagePulser on the BitmapTint of that name; `count` is its number of 100 ms ticks.
void nativeUtilPulseImage(ScriptContext& ctx, std::string name, double count) {
  auto* world = game::choc1::choc1WorldOf(ctx);
  if (!world) return;
  for (auto& p : world->pulses)  // one pulser per image: a new call restarts it
    if (!p.popup && p.name == name) {
      p.count = static_cast<int>(count);
      p.index = 0;
      return;
    }
  game::choc1::Choc1Pulse p;
  p.name = std::move(name);
  p.count = static_cast<int>(count);
  p.nextMs = world->nowMs() + game::choc1::Choc1Pulse::kPeriodMs;
  world->pulses.push_back(std::move(p));
}

void nativeSetUpdateRate(ScriptContext& ctx, double hz) {
  if (auto* world = game::choc1::choc1WorldOf(ctx)) world->money.setRate(hz, world->nowMs());
}

void nativeSetMoneyDisplayTarget(ScriptContext& ctx, double amount) {
  auto* world = game::choc1::choc1WorldOf(ctx);
  if (!world) return;
  if (world->money.setTarget(amount)) choc1PlaySound(ctx, "money_earned");
  world->showMoney();
}

void nativeSetMoneyDisplay(ScriptContext& ctx, double amount) {
  auto* world = game::choc1::choc1WorldOf(ctx);
  if (!world) return;
  world->money.snap(amount);
  world->showMoney();
}

}  // namespace

void forEachNamedInChain(ui::WindowManager& wm, const std::string& name, const std::function<void(ui::Window&)>& fn) {
  if (name.empty()) return;
  const auto& modals = wm.modals();
  for (auto it = modals.rbegin(); it != modals.rend(); ++it) walk(**it, name, fn);
}

void registerChoc1WindowNatives(lua_State* L, ScriptContext& ctx) {
  bind<nativeUtilEnableWindow>(L, ctx, "UtilEnableWindow");
  bind<nativeUtilDisableWindow>(L, ctx, "UtilDisableWindow");
  bind<nativeUtilSetButtonOn>(L, ctx, "UtilSetButtonOn");
  bind<nativeUtilSetButtonOn>(L, ctx, "UtilSetLedgerTab");
  bind<nativeUtilSetGlobalString>(L, ctx, "UtilSetGlobalString");
  bind<nativeUtilSetTextColor>(L, ctx, "UtilSetTextColor");
  bind<nativeUtilPositionWindow>(L, ctx, "UtilPositionWindow");
  bind<nativeUtilPulseImage>(L, ctx, "UtilPulseImage");
  bind<nativeSetUpdateRate>(L, ctx, "SetUpdateRate");
  bind<nativeSetMoneyDisplayTarget>(L, ctx, "SetMoneyDisplayTarget");
  bind<nativeSetMoneyDisplay>(L, ctx, "SetMoneyDisplay");
}

}  // namespace choco::script
