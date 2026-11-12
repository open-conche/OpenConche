#pragma once

// VWorldMap (docs/re/greatcc-meta-flow.md 4, re/greatcc/decomp/VWorldMap.c): the world-map window of scripts/worldmap.lua, a
// sprite window that holds one VPushPin per entry of its `pins` table, the dot trails of its `paths` table and a template for
// the dots (`dot`). While it lives it binds ResetMap, DoCheatMap, PlayTransition and ShowCurrentLocation. A globe's button-press
// message `level_btn_NN` selects level NN and runs CloseWorldMap().

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "games/greatcc/natives/ScopedNatives.h"
#include "games/greatcc/scene/PushPin.h"
#include "games/greatcc/scene/VSpriteWindow.h"

namespace choco::games::greatcc {

class VWorldMap : public VSpriteWindow {
 public:
  VWorldMap(ui::WindowManager& wm, script::ScriptContext& ctx);
  std::string_view typeName() const override { return "VWorldMap"; }
  // Pins (made, initialised and added in key order of the table), paths, the dot template; then the base; then this window
  // becomes its modal's default focus; in an endless mode `btn_options` moves to x = 341.
  void postChildrenInit(const ui::Style& style) override;
  // OnTaskAnimate: the dot trail and the arrival glow, then the sprite update.
  bool onAnimate() override;
  // A button-press message named level_btn_NN (more than 10 characters) selects level NN and queues CloseWorldMap(); every
  // button-press message is consumed.
  bool onMessage(const ui::Message& m) override;
  std::string detail() const override;

  // PlayTransition(location): starts the dot trail of a known path (the first dot at the next frame) and locks the map's input
  // with an empty exclusive set; an unknown name only resets the trail state.
  void playTransition(const std::string& location);
  // GetCurrentLocationPin: the first pin (by key) with 0 <= highest + 1 - baseLevel < 10; null after level 60.
  PushPin* currentLocationPin() const;
  void showCurrentLocation();
  // ShowCurrentLocation, then Init on every pin.
  void resetMap();
  // DoCheatMap(code, arg): 3 adds money to the open pin's location and the lifetime total, 4 sets the current level and runs
  // CloseWorldMap(), 5 sets the highest complete level and resets the map; the result is the message the cheat window shows.
  std::string doCheat(int code, const std::string& arg);

  const std::map<std::string, std::shared_ptr<PushPin>>& pins() const { return pins_; }
  struct DotInfo {
    gfx::PointF pos;
    std::string state;
  };
  const std::map<std::string, std::vector<DotInfo>>& paths() const { return paths_; }
  const std::string& transition() const { return transition_; }
  std::size_t dotsPlaced() const { return dots_; }

 private:
  ScopedNatives natives_;
  std::map<std::string, std::shared_ptr<PushPin>> pins_;
  std::map<std::string, std::vector<DotInfo>> paths_;
  std::shared_ptr<VObject> dotTemplate_;
  std::shared_ptr<SpriteEffect> arrivalGlow_;
  std::string transition_;
  int dotIndex_ = 0;
  double timerMs_ = 0;
  std::size_t dots_ = 0;
};

}  // namespace choco::games::greatcc
