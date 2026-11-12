#pragma once

// VTextSprite (docs/re/greatcc-sprites-and-zones.md 7.3): a sprite that draws laid-out text. It is a VSprite with no image;
// its rectangle is the text box (w x h from the sprite's own origin).

#include <memory>
#include <string>

#include "games/greatcc/scene/StyleChain.h"
#include "games/greatcc/scene/VSprite.h"
#include "gfx/text/TextLayout.h"

namespace choco::games::greatcc {

class TextSprite : public VSprite {
 public:
  explicit TextSprite(script::ScriptContext& ctx, std::string name = {});
  static std::shared_ptr<TextSprite> create(script::ScriptContext& ctx, std::string name = {}) {
    return std::make_shared<TextSprite>(ctx, std::move(name));
  }
  const char* kind() const override { return "VTextSprite"; }

  // Keys: spriteLayer, layer, name, label, scale, w, h, x, y, bold, italic, margin, flags, font, outline, rotation, shadow.
  void loadFromTable(const StyleChain& style);
  // SetText: the text as given (markup allowed); bold, italic and outline wrap it before layout.
  void setText(const std::string& s);
  const std::string& text() const { return text_; }
  // The markup the layout is built from (after the wrapping).
  std::string markup() const;
  void setSize(int w, int h);
  int width() const { return w_; }
  int height() const { return h_; }
  bool bold() const { return bold_; }
  bool italic() const { return italic_; }
  const gfx::text::TextStyle& textStyle() const { return style_; }

  // VTextSprite's flags word (the alignment bits), which VButton forces to 5 after loading.
  void setFlags(int flags) {
    style_.flags = flags;
    invalidate();
  }
  // CopyStyle: the font, flags, margins, outline, shadow, emphasis, size, position and text of `other`.
  void copyStyleFrom(const TextSprite& other);

  // Wrapped lines and how many of them draw (the rest are cut by the clip height).
  int lineCount() const { return text_.empty() ? 0 : layoutFor(false).lineCount(); }
  int drawnLineCount() const { return text_.empty() ? 0 : layoutFor(false).drawnLineCount(); }

  // VTextSprite::GetTextBounds: the tight bounds of the laid-out text, moved by the margins (left and top come in by the margin's
  // left and top, right and bottom go out by the rest of it), so a text with a vertical margin is that much taller.
  gfx::RectI textBounds() const;

  void drawImage(gfx::Renderer& r, const DrawSpec& spec) const override;

 protected:
  gfx::RectF frameRect(const DrawSpec& spec) const override;
  // A text box has no texture, so nothing is hit.
  bool imageHit(gfx::PointI, const DrawSpec&, int) const override { return false; }

 private:
  void invalidate() { layout_.reset(), shadowLayout_.reset(); }
  const gfx::text::TextLayout& layoutFor(bool shadow) const;

  std::string text_;
  gfx::text::TextStyle style_;
  int w_ = 10, h_ = 10;
  bool bold_ = false, italic_ = false;
  float margin_[4] = {0, 0, 0, 0};  // the margin rectangle (left, top, left + m3, top + m4): the layout is w - margin_[2] by h - margin_[3]
  int outlineWidth_ = 0;
  gfx::ColorF outlineColor_{0, 0, 0, 1};
  bool hasShadow_ = false;
  gfx::PointF shadowOffset_;
  gfx::ColorF shadowColor_{0, 0, 0, 0.5f};
  mutable std::unique_ptr<gfx::text::TextLayout> layout_, shadowLayout_;
};

// The wrapped lines of every text sprite in the tree under `root` (itself included): how many are drawn (the rest are cut by the
// clip height) and how many there are. The window dumps print them as `lines=drawn/total`.
struct TextLineCount {
  int drawn = 0, total = 0;
};
TextLineCount countTextLines(const VSprite& root);

}  // namespace choco::games::greatcc
