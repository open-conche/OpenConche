#pragma once

// Chocolatier 1's FactoryGame window (TFactoryGame, exe vtable 004e740c; docs/re/choc1-factory.md) and its natives StartGame,
// RestartGame, GetProductCount and TogglePause. The window owns a FactorySim (all the rules, no window) and wires it to the
// 10 ms task, the mouse, the Lua children (counter, timer, countdown, keeper, gamestart, gameend, paused, pause_label), the
// sounds and the music. It draws the wheel, trays, belt, shots, miss grid, arms and hub over the Lua's backdrop bitmap.

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "games/choc1/FactorySim.h"
#include "gfx/SpriteSheet.h"
#include "gfx/Texture.h"
#include "script/ScriptContext.h"
#include "ui/Window.h"

namespace choco::game::choc1 {

class FactoryGame : public ui::Window {
 public:
  // INFERENCE: the frame periods of the two cosmetic sheets are not in the exe's data that was read.
  static constexpr double kGunFrameMs = 100;
  static constexpr double kPowerupFrameMs = 50;

  FactoryGame(ui::WindowManager& wm, script::ScriptContext& ctx) : Window(wm), ctx_(ctx) {}
  ~FactoryGame() override;
  std::string_view typeName() const override { return "FactoryGame"; }
  void init(const ui::Style& style) override;
  void postChildrenInit(const ui::Style& style) override;
  void draw(ui::DrawContext& ctx) const override;
  bool onMouseDown(int x, int y) override;
  bool onMouseMove(int x, int y) override;
  bool onAnimate() override;
  void onForeground(bool fg) override;
  std::optional<gfx::PointF> autoAim() const override;
  std::string detail() const override;

  // The natives (2.3).
  void startGame();
  void restartGame();
  int productCount() const { return sim_ ? sim_->count() : 0; }
  void togglePause();
  // The exe's loop-level Escape (the main loop's key-27 check, choc1-natives.md section 8) runs the same pause helper
  // 0040afc0 as the close and deactivate messages: a game that is not in its start dialog and not already paused pauses.
  // Runs from Choc1Module::onEscapeKey (the check is at the loop level, not the window's focus). True if it paused.
  bool escapePause();

  const FactorySim* sim() const { return sim_.get(); }

  // The FactoryGame in the modal chain (top first), or null.
  static FactoryGame* inChain(ui::WindowManager& wm);

 private:
  void beginRun();                      // what StartGame does after the sim's start()
  void processEvents();
  void syncTexts();
  // Chocolatier 1's factory cursors: the aimer `cursor/gun_aimer?simple` at StartGame (16,16) and plain on resume,
  // the arrow `cursor/cursor` (1,1) - facts of docs/re/choc1-factory.md 197-209 and 338. Asks the app's cursor service;
  // without it (unit tests) logs once per name as before.
  void setCursor(const char* name, bool simpleVariant = false);
  void setLabelOf(const char* child, const std::string& text);
  void showChild(const char* child, bool shown);
  void playSound(const std::string& name);
  void playMusic(const std::string& file);
  std::string stringOr(const std::string& key, const std::string& fallback) const;
  void showEnd();

  void drawCentered(ui::DrawContext& ctx, const gfx::Texture& t, double cx, double cy, float rotationDeg = 0) const;
  void drawFrame(ui::DrawContext& ctx, const gfx::Texture& image, const gfx::SheetFrame& f, double x, double y, float rotationDeg = 0) const;
  void drawItem(ui::DrawContext& ctx, int ingredient, double cx, double cy) const;

  script::ScriptContext& ctx_;
  std::unique_ptr<FactorySim> sim_;
  CrtRand localRand_;  // used when there is no Chocolatier 1 world (a test)
  std::string character_ = "sffactorykeep", port_, prodname_ = "milkbar";
  std::vector<std::string> ingredients_;  // the recipe's names, in the sim's ingredient order
  std::string cursor_ = "cursor";
  // The texts last written, so a tick that changes nothing does not re-lay-out a Text.
  std::string counterText_, timerText_, countdownText_;
  bool countdownVisible_ = false;
  // Art.
  std::shared_ptr<gfx::Texture> wheel_, flywheel_, gear_, smallWheel_, plate_, machineOver_, upperArm_, lowerArm_, crateFront_,
      crateLid_, bellows_, missedCell_, trayEmpty_, trayFull_, product_;
  std::vector<std::shared_ptr<gfx::Texture>> icons_;
  std::shared_ptr<const gfx::SpriteSheet> gun_, powerup_;
};

}  // namespace choco::game::choc1
