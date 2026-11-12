#pragma once

// RActiveTextLayer and RActiveText (docs/re/greatcc-ui-library.md 6.2): a window that owns short-lived floating texts (the
// payment, combo and goal popups of the shop), updates each by the 16 ms tick's delta and drops the ones that report they are
// over. RActiveText is the interface the popups implement (slice 13); the layer draws what they draw.

#include <memory>
#include <vector>

#include "games/greatcc/ui/RWindow.h"

namespace choco::games::greatcc {

class ActiveText {
 public:
  virtual ~ActiveText() = default;
  // Update(dt ms): false when the text is over and the layer should drop it.
  virtual bool update(double dtMs) { (void)dtMs; return true; }
  // `origin` is the layer's screen position.
  virtual void draw(ui::DrawContext& ctx) const { (void)ctx; }
};

class RActiveTextLayer : public RWindow {
 public:
  explicit RActiveTextLayer(ui::WindowManager& wm) : RWindow(wm) {}
  std::string_view typeName() const override { return "RActiveTextLayer"; }
  // AdoptText: appended; it is updated from the next tick on.
  void adoptText(std::unique_ptr<ActiveText> t);
  // ClearText: every text is dropped.
  void clearText() { texts_.clear(); }
  std::size_t textCount() const { return texts_.size(); }
  const std::vector<std::unique_ptr<ActiveText>>& texts() const { return texts_; }
  // OnTaskAnimate: RWindow's, then Update(delta) on each text in order; the ones that return false go.
  bool onAnimate() override;
  void draw(ui::DrawContext& ctx) const override;
  std::string detail() const override { return RWindow::detail() + " texts=" + std::to_string(texts_.size()); }

 private:
  std::vector<std::unique_ptr<ActiveText>> texts_;
};

}  // namespace choco::games::greatcc
