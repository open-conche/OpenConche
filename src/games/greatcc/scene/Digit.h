#pragma once

// VDigit (docs/re/greatcc-sprites-and-zones.md 7.1): an object with one text-sprite child that shows a digit, rolling toward
// its target one step every 31 ms. The displayed value starts at -1 and the text at "0", so after load it counts up from -1.

#include "games/greatcc/scene/TextSprite.h"
#include "games/greatcc/scene/VObject.h"

namespace choco::games::greatcc {

class Digit : public VObject {
 public:
  static constexpr double kTickMs = 31;
  Digit(script::ScriptContext& ctx, std::string name) : VObject(ctx, std::move(name), "digit") {}
  static std::shared_ptr<Digit> create(script::ScriptContext& ctx, std::string name) {
    return std::make_shared<Digit>(ctx, std::move(name));
  }
  const char* kind() const override { return "VDigit"; }
  // The object loader, then the text child made from the same table (its keys sit directly in the digit's table).
  void loadFromTable(const StyleChain& style) override;
  // Clamped to 0..9.
  void setDigit(int n) { target_ = n < 0 ? 0 : (n > 9 ? 9 : n); }
  int target() const { return target_; }
  int shown() const { return shown_; }
  TextSprite* text() const { return text_.get(); }
  void update(bool softPaused) override;

 private:
  std::shared_ptr<TextSprite> text_;
  int target_ = 0;
  int shown_ = -1;
  double lastTick_ = 0;
};

}  // namespace choco::games::greatcc
