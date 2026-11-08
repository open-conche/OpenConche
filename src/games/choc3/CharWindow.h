#pragma once

#include <memory>
#include <string>

#include "games/common/MoodFade.h"
#include "gfx/SpriteSheet.h"
#include "gfx/Texture.h"
#include "ui/widgets/InputWidgets.h"

namespace choco::game {

// A character portrait (Character, docs/re/port-window.md section 2). Sheet frame 0 is the body; frames 1.. are faces
// drawn over it, chosen by mood and cross-faded over 200 ms. Keys read: name, alpha (1), scale (1), happiness (50) and
// the BSGWindow keys. The window's own alpha is the character's alpha, so fades drive it.
class CharWindow : public ui::BSGWindow {
 public:
  explicit CharWindow(ui::WindowManager& wm) : BSGWindow(wm) {}
  std::string_view typeName() const override { return "CharWindow"; }
  void postChildrenInit(const ui::Style& style) override;
  void draw(ui::DrawContext& ctx) const override;
  bool drawsOutside() const override { return false; }
  bool onAnimate() override;
  std::string detail() const override;

  // SetMood: h in 0..100. Starts the cross-fade when the target frame differs from the one shown.
  void setMood(double h);
  // The face frame for a mood among `frames` sheet frames (docs/re/port-window.md 2.5); -1 when there are fewer than 2.
  static int moodFrame(int frames, double h);

  const std::string& characterName() const { return charName_; }
  int shownFrame() const { return fade_.shown; }
  int targetFrame() const { return fade_.target; }
  int fadeTimer() const { return fade_.timer; }
  static constexpr int kFadeMs = MoodFade::kFadeMs;
  static constexpr int kTickMs = MoodFade::kTickMs;

 private:
  std::string charName_;
  std::shared_ptr<const gfx::SpriteSheet> sheet_;
  std::shared_ptr<gfx::Texture> texture_;
  float scale_ = 1;
  MoodFade fade_;
};

}  // namespace choco::game
