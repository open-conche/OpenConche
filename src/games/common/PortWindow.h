#pragma once

#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "games/common/PortDef.h"
#include "sdk/Ticker.h"
#include "games/common/sprite/PathSprite.h"
#include "games/common/sprite/PulseSprite.h"
#include "sdk/Sprite.h"
#include "sdk/SpriteScript.h"
#include "ui/Window.h"
#include "ui/widgets/InputWidgets.h"
#include "games/common/SdkNames.h"

namespace choco::script {
class Rng;
class Log;
}  // namespace choco::script

namespace choco::game {

class World;

// The port screen (docs/re/port-window.md section 1, docs/design/game-windows.md 3.7). Reads the `port` style key (the Lua
// Port table) once, in postChildrenInit, into a sprite tree: the background, the buildings with their hidden highlight
// sprites, and the scenery (static, animated and path sprites). A 50 ms task, which keeps running while dialogs cover
// the port, fades the scene in, pulses the hover highlight and moves the path sprites by a fixed 50 ms a tick.
//
// Keys read: background, port, musicDelay (25000).
class PortWindow : public ui::Window, public ui::PopupClickTarget {
 public:
  struct Building {
    std::string name;
    Sprite* sprite = nullptr;
    Sprite* highlight = nullptr;
    ui::WindowId label = 0;  // 0: none
    float rolloverScale = 0;
    // Chocolatier 2: the click closure and the hover popup's Lua source, and where the popup goes (window-local).
    script::LuaRef command;
    std::string contents;
    std::optional<float> popupX, popupY;
  };
  static constexpr double kTaskMs = 50;
  static constexpr double kIntroMs = 2000;

  // `rng` and `log` are the script context's. Sheets and textures come from the window manager.
  PortWindow(ui::WindowManager& wm, script::Rng& rng, script::Log& log, World* world = nullptr);
  ~PortWindow() override;

  std::string_view typeName() const override { return "PortWindow"; }
  void init(const ui::Style& style) override;
  void postChildrenInit(const ui::Style& style) override;
  void draw(ui::DrawContext& ctx) const override;
  bool onMouseDown(int x, int y) override;
  bool onMouseUp(int x, int y) override;
  bool onMouseMove(int x, int y) override;

  const Sprite* spriteRoot() const override { return root_.get(); }
  std::string detail() const override;

  // One frame from World::tick: the 50 ms task when due, then the sprite scripts.
  void frame(double nowMs);
  // PausePortAnimations: stops (or restarts) the 50 ms task. Sprite scripts keep running.
  void pauseAnimations(bool pause);
  // The app lost (false) or regained (true) the foreground: the 50 ms task is paused, and restarted from `nowMs` (no
  // catch-up) unless the script paused the animations itself.
  void setForeground(bool fg, double nowMs);
  bool animationRunning() const { return anim_.running(); }

  // ---- for the window itself and for tests ----
  const std::vector<Building>& buildings() const { return buildings_; }
  int target() const { return target_; }
  double rolloverTime() const { return rolloverTime_; }
  // The building under a window-local point: the highest layer wins, the first of equals; -1 for none.
  int findBuilding(gfx::PointF p) const;
  // One 50 ms task tick at platform time `t`.
  void doAnimation(double t);
  // The intro brightness and label alpha at `elapsedMs` since the port opened.
  static float introBrightness(double elapsedMs);
  static float introLabelAlpha(double elapsedMs);
  // The hover highlight (alpha, scale factor f) for a rolloverTime in [0,3000).
  struct Pulse {
    float alpha, f;
  };
  static Pulse pulse(double rolloverTimeMs);
  // Chocolatier 2's pulse (docs/re/choc2-port-binary.md 1.5, 1.6): a triangle 0..1..0 with a period of 1300 ms.
  static constexpr double kPulsePeriodMs = 1300;
  static constexpr double kIntroPulseMs = 3900;
  static float choc2PulsePhase(double clockMs);
  // One 40 ms tick of the scene: both clocks advance, the intro ends at kIntroPulseMs, the pulses are updated.
  void pulseTick(double dtMs);
  // StopPulsing(): ends the intro pulse, hides the `buildinglabels` window at once and forgets the hovered building (the
  // popup is untouched). Chocolatier 2 only.
  void stopPulsing();
  bool introPulsing() const { return introPulse_; }
  int pulseHover() const { return pulseHover_; }
  double sceneMs() const { return sceneMs_; }
  static float rolloverScaleFor(int w, int h);
  static std::string clickString(const std::string& building) { return "gCurrentBuilding=nil;" + building + ":OnClick()"; }
  Sprite* rootSprite() { return root_.get(); }
  // The hover popup's window id, or 0.
  ui::WindowId popup() const { return popup_; }
  // A release on the hover popup runs the hovered building's command.
  void popupClicked() override;
  // The popup stays only while the pointer is still over the hovered building (overlapping buildings share a rectangle).
  bool keepsPopup(int screenX, int screenY) override;
  const std::vector<PathSprite*>& pathSprites() const { return paths_; }

 protected:
  // The scene, once, from postChildrenInit. The default reads the `port` style key with parsePort (Chocolatier 3); a game
  // that builds its scene another way overrides this.
  virtual PortDef readScene(const ui::Style& style);
  // false: the scene is not tinted dark and faded in over kIntroMs (Chocolatier 3 does that).
  bool fadeIn_ = true;
  // false: the window does not run the random port music (Chocolatier 3 does that).
  bool portMusic_ = true;
  // true: Chocolatier 2's pulse (PulseSprite behind every building with a `command`) replaces the hover highlight.
  bool choc2Pulse_ = false;

 private:
  void closePopup();
  void activate(int building);
  void build(PortDef& def);
  void addBuildings(PortDef& def);
  void addSprite(const PortDef& def, const SpriteDef& sd);
  void tintBackground(float v);
  void cancelHighlight();
  void applyPulses();
  void changeMusic(double now);
  ui::Window* labelFor(const std::string& name);
  void play(Sprite& s);
  std::shared_ptr<gfx::Texture> loadTexture(const std::string& name);

  script::Rng& rng_;
  script::Log& log_;
  World* world_;
  std::unique_ptr<Sprite> root_;  // owns the whole tree; the non-owning pointers below are declared after it
  std::vector<Building> buildings_;
  std::vector<Sprite*> scenery_;  // tinted with the background
  std::vector<PathSprite*> paths_;
  int target_ = -1, pressed_ = -1;
  ui::WindowId popup_ = 0;
  Ticker anim_;
  double startTime_ = 0, lastTime_ = 0, rolloverTime_ = 0, musicTime_ = 0, musicDelay_ = 25000;
  bool introPulse_ = false;
  int pulseHover_ = -1;
  double sceneMs_ = 0, hoverMs_ = 0;
  bool music_ = true, explicitPause_ = false, built_ = false;
  std::string background_;
  std::set<std::string> warned_;
};

}  // namespace choco::game
