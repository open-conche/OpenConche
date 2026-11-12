#pragma once

// KDial and KSweepDial (docs/re/greatcc-ui-library.md 9.2, 9.3): the gauges of the shop HUD. A KDial turns a needle about a
// pivot between two angles; a KSweepDial also shows a wedge of a texture from its start angle clockwise to the current one
// (the timer's pie). Both draw after their children, and position 0..1 is set by the HUD.

#include <memory>
#include <vector>

#include "gfx/Texture.h"
#include "gfx/Types.h"
#include "ui/Window.h"

namespace choco::games::greatcc {

class KDial : public ui::Window {
 public:
  explicit KDial(ui::WindowManager& wm) : Window(wm) {}
  std::string_view typeName() const override { return "KDial"; }
  // Keys: center_x, center_y (default -1337), needle_regx, needle_regy (default -1337), minrot (0), maxrot (2 pi), needle,
  // overlay (textures), assert, startpos (0..1, default 0). The needle's registration point becomes an offset from the needle's
  // centre (integer halves of its size). Keys the Lua names that the class never reads (`needle_scale`, `startingPos`) have no effect.
  void init(const ui::Style& style) override;
  void postDraw(ui::DrawContext& ctx) const override;
  std::string detail() const override;

  // SetPos: clamps to [0,1]; the angle is minrot + (maxrot - minrot) * p.
  virtual void setGauge(float p);
  // GetPos: (angle - minrot) / (maxrot - minrot).
  float gauge() const { return (angle_ - minrot_) / (maxrot_ - minrot_); }
  float angle() const { return angle_; }
  float minRot() const { return minrot_; }
  float maxRot() const { return maxrot_; }
  bool hasNeedle() const { return static_cast<bool>(needle_); }
  gfx::PointF pivot() const { return {static_cast<float>(cx_), static_cast<float>(cy_)}; }
  gfx::PointF needleReg() const { return {static_cast<float>(regx_), static_cast<float>(regy_)}; }

 protected:
  // The needle and the overlay (PostDraw of KDial).
  void drawNeedleAndOverlay(ui::DrawContext& ctx) const;
  std::shared_ptr<gfx::Texture> loadTexture(const std::string& path);

  float angle_ = 0;
  float alpha_ = 1;
  int cx_ = -1337, cy_ = -1337;
  int regx_ = -1337, regy_ = -1337;
  float minrot_ = 0, maxrot_ = 6.2831855f;
  std::shared_ptr<gfx::Texture> needle_, overlay_;
  bool assert_ = true;
};

class KSweepDial : public KDial {
 public:
  explicit KSweepDial(ui::WindowManager& wm) : KDial(wm) {}
  std::string_view typeName() const override { return "KSweepDial"; }
  void init(const ui::Style& style) override;
  // Start and end angles become the current one; the fan is built for the window's size.
  void postChildrenInit(const ui::Style& style) override;
  void postDraw(ui::DrawContext& ctx) const override;
  void setGauge(float p) override;
  std::string detail() const override;

  // ThetaToWindow: where the ray from the pivot in direction (sin t, -cos t) leaves the window rectangle. The formulas use the
  // pivot's own distances for the far edges, so they are exact only for a centred pivot (as the original's).
  gfx::PointF thetaToWindow(float theta) const;
  // The wedge's vertices after the last update (the closing one last) and how many triangles are drawn.
  const std::vector<gfx::PointF>& wedge() const { return wedge_; }
  int triangles() const { return count_; }

 private:
  void updateSegments();
  std::shared_ptr<gfx::Texture> sweep_;
  int numSegments_ = 32;
  float start_ = 0, end_ = 0;
  float w_ = 0, h_ = 0, aspect_ = 0;
  int count_ = 0;
  bool ready_ = false;
  std::vector<gfx::PointF> wedge_;
};

}  // namespace choco::games::greatcc
