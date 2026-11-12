#pragma once

// VTiledObject (docs/re/greatcc-sprites-and-zones.md 4.5, X23): an object that draws its texture as a stretching nine-slice.
// x1/x2/y1/y2 are the left/right/top/bottom insets in texture pixels and w x h the size on screen, changeable after load.

#include <array>

#include "games/greatcc/scene/VObject.h"

namespace choco::games::greatcc {

// One piece of the nine: the source rectangle (texture pixels, relative to the image's top-left), where its centre lands in
// the box and the scale about that centre.
struct TiledPiece {
  gfx::RectF src;
  gfx::PointF center;
  gfx::PointF scale{1, 1};
  // Skipped when its source rectangle has no area (or the box has none).
  bool drawn = true;
};

// The nine pieces in the order top-left, top-right, bottom-left, bottom-right, top, bottom, left, right, centre, for an image
// of tw x th and a w x h box with the insets x1 (left), x2 (right), y1 (top), y2 (bottom). Float arithmetic as the original;
// nothing guards w < x1 + x2.
std::array<TiledPiece, 9> tiledPieces(int tw, int th, int w, int h, int x1, int x2, int y1, int y2);

class TiledObject : public VObject {
 public:
  TiledObject(script::ScriptContext& ctx, std::string name, std::string type) : VObject(ctx, std::move(name), std::move(type)) {}
  static std::shared_ptr<TiledObject> create(script::ScriptContext& ctx, std::string name, std::string type) {
    return std::make_shared<TiledObject>(ctx, std::move(name), std::move(type));
  }
  const char* kind() const override { return "VTiledObject"; }
  // Keys x1, x2, y1, y2, w, h (integers, own table only, 0), then the object's.
  void loadFromTable(const StyleChain& style) override;
  // Selects the `background` asset's default state first.
  void init() override;
  void setSize(int w, int h) {
    w_ = w;
    h_ = h;
  }
  // VTiledObject::CopyTo: the object's copy, then the four insets and the size.
  void copyTo(VSprite& dst) override;
  int width() const { return w_; }
  int height() const { return h_; }
  void drawImage(gfx::Renderer& r, const DrawSpec& spec) const override;

 protected:
  gfx::RectF frameRect(const DrawSpec& spec) const override;
  // The box rectangle decides (the nine pieces are not sampled one by one).
  bool imageHit(gfx::PointI p, const DrawSpec& spec, int threshold) const override;

 private:
  int x1_ = 0, x2_ = 0, y1_ = 0, y2_ = 0, w_ = 0, h_ = 0;
};

}  // namespace choco::games::greatcc
