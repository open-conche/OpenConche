#include "games/choc1/Rollover.h"

#include <algorithm>
#include <climits>

#include "games/choc1/Choc1Windows.h"
#include "games/choc1/Choc1World.h"
#include "ui/Layout.h"
#include "ui/ScriptSink.h"
#include "ui/WindowManager.h"
#include "ui/widgets/Widgets.h"

namespace choco::game::choc1 {

RolloverGeometry rolloverGeometry(int px, int py, int xOffset, int yOffset,
                                  int xAbsolute, int yAbsolute, int hotWidth,
                                  int hotHeight) {
  RolloverGeometry g;
  g.absolute = xAbsolute >= 1 && yAbsolute >= 1;
  g.anchorX = (g.absolute ? xAbsolute : px) + xOffset;
  g.anchorY = (g.absolute ? yAbsolute : py) + yOffset;
  // The pointer's position in screen space, or its mirror about the anchor in
  // absolute mode (see the header).
  const int cx = g.absolute ? 2 * xAbsolute - px : px;
  const int cy = g.absolute ? 2 * yAbsolute - py : py;
  g.hot = {cx - hotWidth / 2, cy - hotHeight / 2, hotWidth, hotHeight};
  return g;
}

// ---- the popup ----

RolloverPopup::RolloverPopup(ui::WindowManager &wm, ui::WindowId rollover,
                             const gfx::RectI &hot, std::string clickSound,
                             std::function<void()> onClose)
    : PopupWindow(wm, rollover, hot), hot_(hot),
      clickSound_(std::move(clickSound)), onClose_(std::move(onClose)) {}

void RolloverPopup::close() {
  if (closed_)
    return;
  closed_ = true;
  if (onClose_)
    onClose_();
  kill();
}

bool RolloverPopup::onMouseMove(int x, int y) {
  const gfx::PointF o = ui::WindowManager::screenOrigin(*this);
  const int sx = static_cast<int>(o.x) + x, sy = static_cast<int>(o.y) + y;
  if (sx < hot_.x || sx >= hot_.x + hot_.w || sy < hot_.y ||
      sy >= hot_.y + hot_.h)
  {
    // Closing here and letting the same move fall through to the hit test opens the next popup in this event
    // (no frame without one while it follows the pointer), and only if the Rollover is what the pointer reaches.
    close();
    return false;
  }
  return true; // always handled
}

// Any mouse up that reaches the popup: close it, queue the click for the
// owner's object, play the click sound.
bool RolloverPopup::onMouseUp(int, int) {
  ui::WindowManager &wm = manager();
  const std::string sound = clickSound_;
  close();
  if (ui::ScriptSink *sink = wm.scriptSink())
    sink->runString("table.insert(gCommandQueue,DispatchRolloverClick)");
  if (!sound.empty())
    wm.playSound(sound, false);
  return true;
}

// ---- the Rollover ----

void RolloverWindow::init(const ui::Style &style) {
  Window::init(style);
  placeRaw(style);
  const int hotSize = static_cast<int>(style.numberOr("hotSize", 40));
  hotW_ = static_cast<int>(style.numberOr("hotWidth", hotSize));
  hotH_ = static_cast<int>(style.numberOr("hotHeight", hotW_));
  xOffset_ = static_cast<int>(style.numberOr("xOffset", 20));
  yOffset_ = static_cast<int>(style.numberOr("yOffset", 0));
  xAbsolute_ = static_cast<int>(style.numberOr("xAbsolute", -1));
  yAbsolute_ = static_cast<int>(style.numberOr("yAbsolute", -1));
  script_ = style.stringOr("script", "ui/rollover.lua");
  target_ = style.stringOr("target");
  image_ = style.stringOr("image");
  clickSound_ = style.stringOr("clicksound");
  popupSound_ = style.stringOr("popupsound");
  for (int i = 1; i < 256; ++i) { // entries until the first missing one
    auto r = style.tableNumbers("area", i, 4, 0.0);
    if (!r)
      break;
    area_.push_back({static_cast<int>((*r)[0]), static_cast<int>((*r)[1]),
                     static_cast<int>((*r)[2]), static_cast<int>((*r)[3])});
  }
}

// TRollover::Init: after the generic init, left/top are set straight from `x`
// and `y` (default 0): no wrap of a negative value, no centre or far bands.
void RolloverWindow::placeRaw(const ui::Style &style) {
  setPos(static_cast<int>(style.numberOr("x", 0)),
         static_cast<int>(style.numberOr("y", 0)));
}

void RolloverWindow::postChildrenInit(const ui::Style &style) {
  Window::postChildrenInit(style);
  ui::fitToChildren(*this); // the union of the children from the origin (the
                            // Lua sets no w and h)
  // The generic layout only takes a position while left/top is 0, and a
  // negative x passes its range test once the size is non-zero, so a pass after
  // the union (or a second post-children call) would wrap -3 to 797 on an 800
  // parent. The raw x/y are the Rollover's own and win (docs/re/choc1-port-view.md
  // 1.2).
  placeRaw(style);
}

bool RolloverWindow::popupOpen() const {
  const ui::Window *p = manager().window(popup_);
  return p && !p->isDestroyed();
}

bool RolloverWindow::onMouseMove(int x, int y) {
  if (!manager().scriptSink())
    return true;
  if (!ui::RolloverWindow::inArea(area_, x, y))
    return false; // outside every area rect: not handled, the event falls
                  // through
  auto *world = choc1WorldOf(ctx_);
  if (popupOpen() || (world && world->mapTrips > 0))
    return true; // one popup per Rollover; none while a map trip runs
  const gfx::PointF o = ui::WindowManager::screenOrigin(*this);
  openPopup(static_cast<int>(o.x) + x, static_cast<int>(o.y) + y);
  return true;
}

void RolloverWindow::openPopup(int sx, int sy) {
  ui::WindowManager &wm = manager();
  Choc1World *world = choc1WorldOf(ctx_);
  // Only one popup exists at a time: a new one closes the last.
  if (world) {
    if (auto *last =
            dynamic_cast<RolloverPopup *>(wm.window(world->rolloverPopup)))
      last->close();
    world->rolloverPopup = 0;
  }
  const RolloverGeometry g = rolloverGeometry(
      sx, sy, xOffset_, yOffset_, xAbsolute_, yAbsolute_, hotW_, hotH_);
  const std::string image = image_;
  auto onClose = [this_ = &ctx_, image, world]() {
    if (world)
      world->rolloverPopup = 0;
    if (image.empty() || !world)
      return;
    std::erase_if(world->pulses,
                  [&](const Choc1Pulse &p) { return p.popup && p.name == image; });
    // Only the popup's own pulser ends here; a UtilPulseImage of the same image keeps running and sets its own tint.
    const bool other = std::any_of(world->pulses.begin(), world->pulses.end(),
                                   [&](const Choc1Pulse &p) { return p.name == image; });
    if (!other)
      script::forEachNamedInChain(this_->wm, image, [](ui::Window &w) {
        if (auto *t = dynamic_cast<ui::BitmapTintWindow *>(&w))
          t->setTint({1, 1, 1, 1});
      });
  };
  auto popup = std::make_unique<RolloverPopup>(wm, id(), g.hot, clickSound_,
                                               std::move(onClose));
  wm.scriptSink()->setGlobalString("_rollover", target_);
  ui::PopupOptions opt;
  opt.instant = true;
  opt.margin = 0;
  popup_ = ui::openPopup(wm, std::move(popup), "dofile(\"" + script_ + "\")",
                         g.anchorX, g.anchorY, opt);
  if (world)
    world->rolloverPopup = popup_;
  if (world && !image_.empty() && popupOpen()) {
    // TImagePulser: every 100 ms until the popup closes; the close notice above
    // ends it.
    auto it =
        std::find_if(world->pulses.begin(), world->pulses.end(),
                     [&](const Choc1Pulse &p) { return p.popup && p.name == image_; });
    if (it == world->pulses.end()) {
      world->pulses.push_back({});
      it = world->pulses.end() - 1;
      it->name = image_;
      it->popup = true;
    }
    it->count = INT_MAX / 2;
    it->index = 0;
    it->nextMs = world->nowMs() + Choc1Pulse::kPeriodMs;
    // A UtilPulseImage already pulsing this image (the port's intro flash) writes the same tint on its own 100 ms
    // phase; two phases interleave into a double-speed flicker, so the popup's pulser joins the running one's phase.
    for (const Choc1Pulse &other : world->pulses)
      if (!other.popup && other.name == image_) {
        it->index = other.index;
        it->nextMs = other.nextMs;
        break;
      }
  }
  if (!popupSound_.empty())
    wm.playSound(popupSound_, false);
}

std::string RolloverWindow::detail() const {
  return " target=\"" + target_ + "\" hot=" + std::to_string(hotW_) + "x" +
         std::to_string(hotH_) +
         (area_.empty() ? "" : " areas=" + std::to_string(area_.size()));
}

} // namespace choco::game::choc1
