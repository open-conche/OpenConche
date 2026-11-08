#pragma once

#include <memory>
#include <string_view>

#include "gfx/Texture.h"
#include "ui/Message.h"
#include "ui/Window.h"

namespace choco::game::choc2 {

// MainMenu (Chocolatier 2), after TMainMenu in the Chocolatier 2 Mac binary (docs/re/choc2-map-binary.md sections 3.1, 3.2).
// Two jobs.
//
// Draw (idle and during the intro): after its children it paints four searchlight beams, the blimp and two skyline
// pictures over the menu bitmap. FACT: the pivots, angle limits, `up` angles, half-periods, the triangle wave and the blimp's
// path. INFERENCE (kBeamClockwise and the placement in postDraw): the beam's placement (its centre lies half its length from
// the pivot along the beam) and the direction the angle turns (clockwise from straight up, which is the only sign that
// points both pairs of beams into the screen); chosen by looking at a screenshot.
//
// DoIntro: a five-state machine on a 40 ms tick (all FACT). 1 fades every child of the parent bitmap (the menu art,
// `splash_title`, the buttons) out over 1000 ms while the beams swing to their `up` angle; 2 shows `intro_button` and fades
// `intro_text` in over 1000 ms; 3 holds it, static, for 22000 ms; 4 fades it out over 2000 ms; then the button's command
// (MainMenuFinishIntro) runs once. There is no scroll. A click or Enter on the button runs the same command early. The
// original hides the cursor in DoIntro; the engine has no cursor control, so that is logged as unimplemented, and the
// cursor is "restored" (INFERENCE: whether the original does is unknown) when the intro ends or is cancelled.
class MainMenuWindow : public ui::Window {
 public:
  static constexpr double kTickMs = 40;
  static constexpr double kFadeOutMs = 1000, kFadeInMs = 1000, kHoldMs = 22000, kFadeEndMs = 2000;
  // The ticks are counted: 650 ticks of 40 ms, so the command runs exactly this long after DoIntro on the modal clock.
  static constexpr double kDurationMs = kFadeOutMs + kFadeInMs + kHoldMs + kFadeEndMs;

  enum class State { Idle, FadeOut, FadeIn, Hold, FadeEnd };

  struct Beam {
    float pivotX, pivotY, minDeg, maxDeg, upDeg;
    double halfPeriodMs;
  };
  static constexpr int kBeams = 4;
  static const Beam kBeamTable[kBeams];
  static constexpr double kBlimpPeriodMs = 25000;
  // INFERENCE: positive angles turn clockwise from straight up (screen coordinates).
  static constexpr bool kBeamClockwise = true;

  explicit MainMenuWindow(ui::WindowManager& wm) : Window(wm) {}
  std::string_view typeName() const override { return "MainMenu"; }
  void init(const ui::Style& style) override;

  // DoIntro: starts state 1. Ignored (still true) unless idle. Returns false when the tree has no `intro_button`.
  bool doIntro();
  bool introRunning() const { return state_ != State::Idle; }
  State state() const { return state_; }

  // The beam's angle in degrees at free-clock `clockMs` (the triangle wave), lerped to `up` by `introT` in [0,1] (state 1);
  // `introT` >= 1 gives exactly `up`. Exposed for tests.
  static float beamAngle(const Beam& b, double clockMs, double introT);
  // The blimp's centre x at `clockMs` for a screen `w` wide.
  static float blimpX(double clockMs, int w);

  bool onAnimate() override;
  // The button's kButtonPress ends the intro (so the end of the fades can never run the command a second time).
  bool onMessage(const ui::Message& m) override;
  void postDraw(ui::DrawContext& ctx) const override;

 private:
  void fadeParentChildren(float v);
  void fadeOwnChildren(float v);
  void endIntro();
  State state_ = State::Idle;
  double clockMs_ = 0;  // per-state clock, counted per tick on the modal's clock
  std::shared_ptr<gfx::Texture> beam_, blimp_, skyL_, skyR_;
};

}  // namespace choco::game::choc2
