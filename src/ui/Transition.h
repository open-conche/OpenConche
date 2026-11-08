#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "gfx/Curve.h"
#include "script/LuaState.h"
#include "ui/Window.h"

namespace choco::ui {

class WindowManager;

// One running transition (window-system design section 7). It names its target by id and never holds the window.
struct Transition {
  enum class Kind { FadeIn, FadeOut, Swipe, Path, ZoomIn };
  Kind kind = Kind::FadeIn;
  WindowId target = 0;
  double startMs = 0;
  double durationMs = 100;
  float alpha = 1;                // FadeIn / FadeOut: the full-opacity value
  float rectAlphaScale = 1;       // FadeIn / FadeOut: a Rectangle gets alpha * this (Chocolatier 2 popups: 0.8)
  double stepMs = 0;              // FadeIn / FadeOut: > 0 advances in steps of this many ms (Chocolatier 2 popups: 50) instead of smoothly
  int startX = 0, startY = 0, endX = 0, endY = 0;  // Swipe
  gfx::Curve curve;               // Path
  std::optional<script::LuaRef> onend;
};

// The platform idle task list's transitions. tick() advances them all and runs the finished ones' `onend`s after the
// loop, so an `onend` may start transitions, destroy windows or close dialogs.
class TransitionList {
 public:
  void start(Transition t) { items_.push_back(std::move(t)); }
  std::size_t size() const { return items_.size(); }
  bool empty() const { return items_.empty(); }
  void tick(WindowManager& wm, double nowMs);

 private:
  std::vector<Transition> items_;
};

// Applies the transition's effect at progress `t` in [0,1] to its target (without the show step).
void applyTransition(const Transition& tr, Window& w, float t);

}  // namespace choco::ui
