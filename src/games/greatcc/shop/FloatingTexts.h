#pragma once

// The floating texts of the shop (docs/re/greatcc-shop-simulation.md 8; VPaymentText, VComboText, VGoalText): short-lived texts the
// shop hands to its RActiveTextLayer. Each rises from where it appeared while it fades in, holds, then fades out and drifts up a
// pixel per update until it is over. The text sits centred on its position in a box of its own size.

#include <memory>
#include <string>

#include "games/greatcc/ui/RActiveTextLayer.h"
#include "gfx/text/TextLayout.h"

namespace choco::games::greatcc {

class FloatingText : public ActiveText {
 public:
  enum class Kind { Payment, Combo, Goal };
  // `markup` is shown as given (the callers wrap it), `font` is the .mvec of the shipped fonts folder.
  FloatingText(Kind kind, gfx::text::TextSystem* text, std::string markup, int x, int y, int w, int h, float size, gfx::ColorF color,
               float alpha);
  // The animation of the kind, then the base (nothing); false once it is over.
  bool update(double dtMs) override;
  void draw(ui::DrawContext& ctx) const override;

  Kind kind() const { return kind_; }
  const std::string& markup() const { return markup_; }
  int x() const { return x_; }
  int y() const { return y_; }
  float alpha() const { return alpha_; }
  unsigned elapsedMs() const { return elapsed_; }
  int startY() const { return startY_; }

 private:
  Kind kind_;
  gfx::text::TextSystem* text_;
  std::string markup_;
  int x_, y_, w_, h_;
  int startX_, startY_;
  float size_;
  gfx::ColorF color_;
  float alpha_;
  unsigned elapsed_ = 0;
  mutable std::unique_ptr<gfx::text::TextLayout> layout_;
};

// The three texts ask for the font "edwardian_medium.mvec", which is not a file (the game's is fonts/edwardian_medium.mvec):
// they are drawn in the executable's embedded font.
// VPaymentText(amount, position): "$%0.2i" of the whole dollars, white, 25 pt in 150 x 30, outlined black.
std::unique_ptr<FloatingText> makePaymentText(gfx::text::TextSystem* text, float amount, int x, int y);
// VComboText(bonus, chain, label, position): "<label> x<chain>\n+$<bonus>", yellow, 18 pt in 150 x 36.
std::unique_ptr<FloatingText> makeComboText(gfx::text::TextSystem* text, float bonus, int chain, const std::string& label, int x, int y);
// VGoalText(1 or 2, label): the goal's (green) or the mastery's (gold) 46 pt banner at (400, 250), outlined white.
std::unique_ptr<FloatingText> makeGoalText(gfx::text::TextSystem* text, int which, const std::string& label);

}  // namespace choco::games::greatcc
