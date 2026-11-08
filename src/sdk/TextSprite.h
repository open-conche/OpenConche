#pragma once

#include <string>

#include "sdk/Sprite.h"
#include "gfx/text/TextLayout.h"

namespace choco::sdk {

// A text label in a sprite tree (TTextSprite). The sprite's position is the anchor the alignment flags refer to: for
// kHAlignLeft the box starts there, for kHAlignRight it ends there, for kHAlignCenter it is centred on it; for
// kVAlignCenter the box is centred on the position vertically. The anchoring is inferred from the map's use (docs/re/
// map-window.md section 4, "Unknown"). Draws nothing when no text system is installed.
class TextSprite : public Sprite {
 public:
  TextSprite(int layer, std::string debugName, gfx::text::TextLayout layout, int flags, int boxW, int boxH);
  void draw(gfx::Renderer& r, const SpriteSpec& parent) const override;
  // Top-left of the text box in the sprite's parent space.
  gfx::PointF boxOrigin() const;
  // Replaces the laid-out text (a new string or colour); the sprite's position, flags and box stay.
  void setLayout(gfx::text::TextLayout layout) { layout_ = std::move(layout); }
  int boxWidth() const { return w_; }

 private:
  gfx::text::TextLayout layout_;
  int flags_, w_, h_;
};

}  // namespace choco::sdk
