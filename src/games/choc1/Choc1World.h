#pragma once

#include <string>
#include <vector>

#include "games/choc1/Choc1Money.h"
#include "games/choc1/Choc1Music.h"
#include "games/choc1/FactorySim.h"
#include "games/common/World.h"
#include "ui/Window.h"

namespace choco::game::choc1 {

// Chocolatier 1's game state: Chocolatier 3's World (its port, map and ledger natives, until Chocolatier 1 gets its own) plus
// what the exe's natives keep in object instances: the music queue and the ambient name, the money counter and the image pulsers.
class Choc1World : public World {
 public:
  explicit Choc1World(script::ScriptContext& ctx) : World(ctx), ctx_(ctx) {}
  void tick(double nowMs) override;
  // Right after Audio::update: hands a finished track on to the queue.
  void afterAudio(audio::Audio& a) override;

  Choc1Music music;
  std::string ambient;  // SetAmbient's current name ("" = none)
  Choc1Money money;
  // The C runtime rand() stream the factory mini-game shuffles its ingredient queue with (seed 1: the exe never calls srand).
  CrtRand crtRand;
  std::vector<Choc1Pulse> pulses;
  // The Rollover popup that is open (0 = none): a new one closes it. `mapTrips` counts the map-travel windows alive (the exe's
  // DAT_005330a0): no Rollover opens a popup while it is above 0. Nothing raises it until the map.
  ui::WindowId rolloverPopup = 0;
  int mapTrips = 0;
  double nowMs() const { return ctx_.clock.nowMs; }  // the script clock (ScriptHost::tick sets it before any Lua runs)

  // Writes the shown amount, formatted like UtilDollars, to every Text named "money" in the modal chain.
  void showMoney();

 private:
  script::ScriptContext& ctx_;
};

// The context's Chocolatier 1 world, or null for the other games.
Choc1World* choc1WorldOf(script::ScriptContext& ctx);

}  // namespace choco::game::choc1
