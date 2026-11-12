#pragma once

// VPushPin (docs/re/greatcc-meta-flow.md 4.2, re/greatcc/decomp/VPushPin.c): one location on the world map. A VObject with the
// pin art (`base` asset, an animation), two text sprites (name and location), a score box (a VTiledObject), and ten level
// globes (VButtons named level_btn_NN). Closed it shows only the pin; open, the pin animation plays, the labels move aside and
// the globes appear one after another. Only one pin of a manager is open at a time.

#include <map>

#include "games/greatcc/scene/TiledObject.h"
#include "games/greatcc/scene/VButton.h"

namespace choco::games::greatcc {

class HoverGlow;
class AlphaEffect;

class PushPin : public VObject {
 public:
  explicit PushPin(script::ScriptContext& ctx);
  static std::shared_ptr<PushPin> create(script::ScriptContext& ctx) { return std::make_shared<PushPin>(ctx); }
  const char* kind() const override { return "VPushPin"; }

  // The globes' constant offset from the loaded position (VPushPin::LoadFromTable subtracts it).
  static constexpr float kGlobeOffsetX = 67, kGlobeOffsetY = 125;
  // The cascade: one globe per update once the timer passes 24 ms, the timer then drops by 25.
  static constexpr double kCascadeThresholdMs = 24, kCascadeStepMs = 25;

  // baseLevel, movex, movey; every `globes` entry named globe_* becomes a VButton (level = id + baseLevel, named
  // level_btn_NN, moved by (-67,-125), labelled with the level, added as a child); `box` becomes the score box; the VObject
  // keys; then the loaded position of every text sprite is remembered (the closed layout).
  void loadFromTable(const StyleChain& style) override;
  // VPushPin::Init: the pin art, the globes' states from the level manager, listeners and glows, visibility.
  void init() override;
  // SetOpen: a change moves the text sprites (closing: back to the stored position in 200 ms; opening: current position plus
  // (movex, movey) in 200 ms after 450 ms), toggles the pin's own hover glow, and an opening pin closes the other pins of its
  // manager.
  void setOpen(bool open);
  bool open() const { return open_; }
  // OnClick: in story mode a closed, idle pin opens; in an endless mode the click goes to the globe of the pin's first level.
  void onClick() override;
  // The 5-state machine (closed, opening, globe cascade, open, closing), then the sprite's own update.
  void update(bool softPaused) override;
  // SetScoreInfo: shows or hides the score box for `level` (best, goal and mastery from the level manager).
  void setScoreInfo(bool show, int level);

  int baseLevel() const { return baseLevel_; }
  int state() const { return state_; }
  gfx::PointF move() const { return move_; }
  const std::map<int, std::shared_ptr<VButton>>& globes() const { return globes_; }
  VButton* globe(int level) const;
  VButton* selectedGlobe() const { return selected_.get(); }
  TiledObject* box() const { return box_.get(); }
  const std::map<std::string, gfx::PointF>& closedPositions() const { return positions_; }

 private:
  void setGlobeVisible(bool v);
  void setGlobeEnabled(bool e);
  struct Listener;

  int baseLevel_ = 0;
  gfx::PointF move_;
  bool open_ = false;
  int state_ = 0;
  double timer_ = 0;
  int index_ = 0;
  std::map<int, std::shared_ptr<VButton>> globes_;
  std::shared_ptr<VButton> selected_;
  std::shared_ptr<TiledObject> box_;
  std::shared_ptr<AlphaEffect> scoreFade_;
  std::shared_ptr<SpriteListener> listener_;
  std::shared_ptr<HoverGlow> glow_;
  std::map<std::string, gfx::PointF> positions_;
};

}  // namespace choco::games::greatcc
