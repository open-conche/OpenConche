#include "games/common/World.h"

#include <cstdio>

#include "games/common/Travel.h"

#include "audio/Audio.h"
#include "games/common/MapWindow.h"
#include "games/common/factory/FactoryWindow.h"
#include "games/common/PortWindow.h"
#include "script/AudioBridge.h"
#include "script/GameModule.h"
#include "script/LuaApi.h"
#include "script/ScriptHost.h"
#include "script/RunFile.h"
#include "ui/Layout.h"
#include "ui/WindowManager.h"

namespace choco::game {

PortWindow* World::port() const { return port_ ? dynamic_cast<PortWindow*>(ctx_.wm.window(port_)) : nullptr; }

MapWindow* World::map() const { return map_ ? dynamic_cast<MapWindow*>(ctx_.wm.window(map_)) : nullptr; }

FactoryWindow* World::factory() const { return factory_ ? dynamic_cast<FactoryWindow*>(ctx_.wm.window(factory_)) : nullptr; }

namespace {

// True when `w` belongs to the top modal.
bool onTopModal(ui::WindowManager& wm, const ui::Window* w) {
  while (w && !w->isModal()) w = w->parent();
  return w && w == wm.topModal();
}

}  // namespace

void World::setForeground(bool fg, double nowMs) {
  // The pause dialog below is the game profile's (ui/ui_pause.lua, Chocolatier 3); windows that handle the change themselves
  // (Chocolatier 2's Map) get Window::onForeground through the manager.
  const bool openPause = !fg && ctx_.module && ctx_.module->profile().pauseDialogOnFocusLoss;
  if (MapWindow* m = map()) {
    m->setForeground(fg, nowMs);
    const bool travelPaused = m->travel() && m->travel()->paused();
    if (openPause && !travelPaused && onTopModal(ctx_.wm, m) && ctx_.host)
      ctx_.host->runString("if not gUIPaused then DoModal('ui/ui_pause.lua') end");
  }
  ctx_.wm.dispatchForeground(fg);
  if (PortWindow* p = port()) {
    p->setForeground(fg, nowMs);
    if (openPause && onTopModal(ctx_.wm, p) && ctx_.host)
      ctx_.host->runString("if not gUIPaused then DoModal('ui/ui_pause.lua') end");
  }
  ledger.setForeground(fg, nowMs);
  // Both edges pause; regain never resumes (the player presses OK).
  if (FactoryWindow* f = factory()) f->focusChanged();
  // Losing focus pauses every voice (TSound streams and CADI); regaining it resumes them, but not a pause the factory set.
  if (ctx_.audio) ctx_.audio->setForeground(fg);
  else script::soundEvent(ctx_, fg ? "Unpause" : "Pause");
}

void World::sound(const std::string& name) { script::soundEvent(ctx_, name); }

void World::tick(double nowMs) {
  // By id each time: a travel event may swap the modal and destroy the map or the port.
  if (PortWindow* p = port()) p->frame(nowMs);
  if (MapWindow* m = map()) m->frame(nowMs);
  if (FactoryWindow* f = factory()) f->frame(nowMs);
  ledger.tick(nowMs);
}

LedgerWindow* LedgerHost::get() const {
  if (detached_) return detached_.get();
  return dynamic_cast<LedgerWindow*>(ctx_.wm.window(attached_));
}

void LedgerHost::grab(lua_State* L) {
  ui::WindowManager& wm = ctx_.wm;
  ui::Window& top = *wm.topModal();
  LedgerWindow* existing = get();
  if (!existing) {
    detached_.reset();
    // Attached before the script runs: the creation stack holds ids, and a window outside the tree resolves to nothing.
    auto owned = std::make_unique<LedgerWindow>(wm, ctx_);
    LedgerWindow& ledger = static_cast<LedgerWindow&>(top.adoptFront(std::move(owned)));
    attached_ = ledger.id();
    script::runFileInto(ctx_, L, ledger, "ui/ledger.lua");
    ui::fitToChildren(ledger);
    ledger.prepareInstance();
    nextTickMs_ = ctx_.clock.nowMs + LedgerWindow::kTickMs;
    return;
  }
  if (detached_) {
    attached_ = detached_->id();
    top.adoptFront(std::move(detached_));
  } else if (ui::Window* parent = existing->parent()) {
    top.adoptFront(parent->orphan(*existing));  // to the front of the top modal's children
  }
}

void LedgerHost::release() {
  if (detached_) return;
  LedgerWindow* ledger = get();
  if (!ledger || !ledger->parent()) return;
  std::unique_ptr<ui::Window> taken = ledger->parent()->orphan(*ledger);
  detached_.reset(static_cast<LedgerWindow*>(taken.release()));
  attached_ = 0;
}

void LedgerHost::setForeground(bool fg, double nowMs) {
  background_ = !fg;
  nextTickMs_ = fg ? nowMs + LedgerWindow::kTickMs : -1;
}

void LedgerHost::tick(double nowMs) {
  if (background_) return;
  LedgerWindow* ledger = get();
  if (!ledger) {
    nextTickMs_ = -1;
    return;
  }
  if (nextTickMs_ < 0) nextTickMs_ = nowMs + LedgerWindow::kTickMs;
  if (nowMs + 1e-6 < nextTickMs_) return;  // one step per call at most, re-armed from now (no catch-up)
  nextTickMs_ = nowMs + LedgerWindow::kTickMs;
  ledger->animate();
}

void appendChocolatierDiagnostics(script::ScriptContext& ctx, std::string& out) {
  char buf[320];
  if (ctx.host) {
    std::string g = "globals:";
    lua_State* L = ctx.lua.L();
    for (const char* name : {"gTravelActive", "gQuitActive", "gUIPaused"}) {
      lua_getglobal(L, name);
      const char* v = lua_isnil(L, -1)       ? "nil"
                      : lua_isboolean(L, -1) ? (lua_toboolean(L, -1) ? "true" : "false")
                      : lua_isnumber(L, -1)  ? lua_tostring(L, -1)
                                             : lua_typename(L, lua_type(L, -1));
      g += std::string(" ") + name + "=" + v;
      lua_pop(L, 1);
    }
    out += g + "\n";
  }

  out += "travel: ";
  const game::MapWindow* map = game::worldOf(ctx) ? game::worldOf(ctx)->map() : nullptr;
  const game::Travel* t = map ? map->travel() : nullptr;
  if (!t) {
    out += map ? "none (a map window exists)\n" : "none (no map window)\n";
  } else {
    const auto& pts = t->curve().points();
    if (!pts.empty()) {
      std::snprintf(buf, sizeof buf, "origin (%.0f,%.0f) destination (%.0f,%.0f), ", double(pts.front().x), double(pts.front().y),
                    double(pts.back().x), double(pts.back().y));
      out += buf;
    }
    std::snprintf(buf, sizeof buf, "progress %.3f (%.0f of %.0f ms), %s, finished=%d, events %zu/%zu, trail dots %d, mode %s\n",
                  double(t->fraction()), t->timeMs(), double(t->durationMs()), t->paused() ? "PAUSED" : "not paused",
                  t->finished() ? 1 : 0, t->eventsFired(), t->eventCount(), t->dots(), t->mode().c_str());
    out += buf;
  }

}

}  // namespace choco::game
