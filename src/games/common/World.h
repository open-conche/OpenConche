#pragma once

#include <memory>
#include <string>

#include "games/common/Ledger.h"
#include "script/GameModule.h"
#include "script/ScriptContext.h"

struct lua_State;

namespace choco::game {

// Owns the ledger while it is detached and holds its window id while it is attached to a modal (game-windows design
// section 3.6). Ownership is never shared: a detached ledger is a unique_ptr here, an attached one belongs to its
// modal's child list. A ledger whose modal dies without a ReleaseLedger dies with it, and the next grab builds a new one.
class LedgerHost {
 public:
  explicit LedgerHost(script::ScriptContext& ctx) : ctx_(ctx) {}

  // The live ledger, or null (never made, or lost with its modal).
  LedgerWindow* get() const;
  // GrabLedger: builds the ledger from ui/ledger.lua the first time (running the script on `L`), then moves it into
  // the top modal, in front of its other children.
  void grab(lua_State* L);
  // ReleaseLedger: takes the ledger out of its modal and keeps it. No ledger, or one that is already detached: nothing.
  void release();
  // Steps the 50 ms animation on the script clock (one step per call at most, no catch-up).
  void tick(double nowMs);
  // The app lost or regained the foreground: the task stops, and restarts from `nowMs` (no catch-up).
  void setForeground(bool fg, double nowMs);

  bool detached() const { return detached_ != nullptr; }
  ui::WindowId id() const { return detached_ ? detached_->id() : attached_; }

 private:
  script::ScriptContext& ctx_;
  ui::WindowId attached_ = 0;
  std::unique_ptr<LedgerWindow> detached_;
  double nextTickMs_ = -1;
  bool background_ = false;
};

// Instances of the game windows and the natives that reach them. Owned by the script context (made by
// registerGameNatives), so it is destroyed before the window manager and the Lua state it holds references into.
class PortWindow;
class MapWindow;
class FactoryWindow;
class World : public script::GameState {
 public:
  explicit World(script::ScriptContext& ctx) : ledger(ctx), ctx_(ctx) {}
  // Once per frame with the script clock, from ScriptHost::tick: the port, then the map, then the ledger.
  void tick(double nowMs) override;
  // The app lost (false) or regained (true) the foreground (docs/re/focus-and-display.md 1). Map, port, ledger and the
  // factory in that order, then sound (logged only). The pause dialog opens from the map or port when it is the top modal.
  void setForeground(bool fg, double nowMs) override;
  // The newest port and map windows (the newest to finish building wins), by id; null once gone.
  PortWindow* port() const;
  MapWindow* map() const;
  FactoryWindow* factory() const;
  // SoundEvent(name) through the script context (audio service, or the logged stub in tests).
  void sound(const std::string& name);
  void setPort(ui::WindowId id) { port_ = id; }
  void setMap(ui::WindowId id) { map_ = id; }
  void setFactory(ui::WindowId id) { factory_ = id; }
  LedgerHost ledger;

 private:
  script::ScriptContext& ctx_;
  ui::WindowId port_ = 0, map_ = 0, factory_ = 0;
};

// The context's World, or null when it has none (a dynamic_cast per call; do not cache the result).
inline World* worldOf(script::ScriptContext& ctx) { return dynamic_cast<World*>(ctx.gameState.get()); }
inline const World* worldOf(const script::ScriptContext& ctx) { return dynamic_cast<const World*>(ctx.gameState.get()); }

// The Chocolatier diagnostics lines (globals and travel state) appended to the F10 report.
void appendChocolatierDiagnostics(script::ScriptContext& ctx, std::string& out);

}  // namespace choco::game
