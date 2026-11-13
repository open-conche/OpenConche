#pragma once

// Chocolatier 1's Rollover (TRollover and TRolloverPopup;
// docs/re/choc1-rollover-and-quirks.md). Registered under the type name
// `Rollover` after the engine's built-in, which stays Chocolatier 2 and 3's.
//
// Keys: x y w h name, area (a list of {x1, y1, x2, y2}), target (a Lua
// expression text), script ("ui/rollover.lua"), image (a BitmapTint pulsed
// while the popup is open), clicksound, popupsound, hotSize (40), hotWidth
// (hotSize), hotHeight (hotWidth), xOffset (20), yOffset (0), xAbsolute (-1),
// yAbsolute (-1).
//
// A move over the Rollover (inside an `area` rect when there are any) opens a
// popup unless one is open for it already or a map trip runs. Opening sets the
// global `_rollover` to `target`, runs `script` with the popup as the window
// being built, places the popup at the pointer plus the offsets (or at the
// absolute anchor plus the offsets when both xAbsolute and yAbsolute are >= 1),
// keeps it inside 800x600 with no margin, and shows it at once (no fade). The
// popup closes on the first move outside its hot rectangle (that move then reaches the hit test, so a new one opens
// at once if the pointer is still over the Rollover), or on any mouse up,
// which also queues DispatchRolloverClick and plays `clicksound`.

#include <string>
#include <utility>
#include <vector>

#include "gfx/Types.h"
#include "script/ScriptContext.h"
#include "ui/Window.h"
#include "ui/widgets/InputWidgets.h"

namespace choco::game::choc1 {

// Where a popup goes and the rectangle that keeps it open, both in screen
// coordinates, for a pointer at (px, py).
struct RolloverGeometry {
  int anchorX = 0,
      anchorY = 0; // the popup's top-left before it is moved inside the screen
  gfx::RectI hot;  // x, y, w, h: the move test is x <= px < x + w
  bool absolute = false;
};

// The placement rules of TRollover::OnMouseMove (the popup open of the decompile, around 0041b2df). Absolute
// mode needs both xAbsolute and yAbsolute >= 1. The popup is placed at (xOffset, yOffset) + (pointer, or the
// absolute anchor in absolute mode); its popup-local hot rectangle is -(xOffset, yOffset) - hot/2 normally, and
// gains (absolute - pointer) in absolute mode - FACT (0x4cxxxx arithmetic): with the popup at (xOffset +
// xAbsolute) the hot rectangle's screen centre really is 2 * absolute - pointer, the pointer mirrored about the
// anchor. Known small deviation: the exe's 800x600 wrap clamps shift the popup AND the hot rectangle by the same
// deltas (at 0041b365..: iVar6 += right - 800 etc., with a 32 px vertical nudge); the engine clamps the popup
// only, so a pop-up pushed off screen by the wrap keeps its hot rectangle at the unclamped spot.
RolloverGeometry rolloverGeometry(int px, int py, int xOffset, int yOffset,
                                  int xAbsolute, int yAbsolute, int hotWidth,
                                  int hotHeight);

// The popup (TRolloverPopup): swallows every mouse event; a move outside the
// hot rectangle or any mouse up closes it.
class RolloverPopup : public ui::PopupWindow {
public:
  RolloverPopup(ui::WindowManager &wm, ui::WindowId rollover,
                const gfx::RectI &hot, std::string clickSound,
                std::function<void()> onClose);
  std::string_view typeName() const override { return "RolloverPopup"; }
  bool onMouseDown(int, int) override { return true; }
  bool onMouseUp(int x, int y) override;
  bool onMouseMove(int x, int y) override;
  void onMouseLeave() override { close(); }
  // Runs the close notice once, then removes the popup.
  void close();

private:
  gfx::RectI hot_;
  std::string clickSound_;
  std::function<void()> onClose_;
  bool closed_ = false;
};

class RolloverWindow : public ui::Window {
public:
  RolloverWindow(ui::WindowManager &wm, script::ScriptContext &ctx)
      : Window(wm), ctx_(ctx) {}
  std::string_view typeName() const override { return "Rollover"; }
  void init(const ui::Style &style) override;
  void postChildrenInit(const ui::Style &style) override;
  bool onMouseMove(int x, int y) override;
  std::string detail() const override;

  // True while this Rollover's popup exists.
  bool popupOpen() const;
  const std::string &target() const { return target_; }

private:
  void placeRaw(const ui::Style &style);
  void openPopup(int screenX, int screenY);
  script::ScriptContext &ctx_;
  std::string target_, script_ = "ui/rollover.lua", image_, clickSound_,
                       popupSound_;
  int hotW_ = 40, hotH_ = 40, xOffset_ = 20, yOffset_ = 0, xAbsolute_ = -1,
      yAbsolute_ = -1;
  std::vector<gfx::RectI> area_; // {left, top, right, bottom} stored as x, y,
                                 // w, h (the built-in's convention)
  ui::WindowId popup_ = 0;
};

} // namespace choco::game::choc1
