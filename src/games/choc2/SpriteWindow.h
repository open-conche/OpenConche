#pragma once

#include <memory>
#include <string>

#include "sdk/Sprite.h"
#include "script/ScriptContext.h"
#include "ui/Window.h"
#include "games/common/SdkNames.h"

namespace choco::game::choc2 {

// Sprite{x y image rotation tint scale} (Chocolatier 2, docs/re/choc2-port-and-ledger.md 4.1, design 2.5): one image placed by
// its registration point. x,y is where that point lands in the parent. `image` is a texture, or a sheet if it ends in `.xml`
// (the map's port icons; a sheet's DoAnim plays, ticked by a 10 ms window animation on the manager clock). `rotation` is in
// degrees, `tint` a colour and `scale` a number. A texture's registration point is its centre, a sheet's the frame's.
// The window's rectangle is the image's bounds around the point.
class SpriteWindow : public ui::Window {
 public:
  static constexpr double kTickMs = 10;

  SpriteWindow(ui::WindowManager& wm, script::ScriptContext& ctx) : Window(wm), ctx_(ctx) {}
  std::string_view typeName() const override { return "Sprite"; }
  void postChildrenInit(const ui::Style& style) override;
  void draw(ui::DrawContext& ctx) const override;
  bool onAnimate() override;
  const Sprite* spriteRoot() const override { return sprite_.get(); }
  std::string detail() const override;

  const Sprite* sprite() const { return sprite_.get(); }

 private:
  bool scriptRunning() const;
  script::ScriptContext& ctx_;
  std::unique_ptr<Sprite> sprite_;
  std::string image_;
};

// AnimationWindow{fps} (design 2.5): a plain container. Its Sprite children animate themselves, so `fps` is only recorded.
class AnimationWindow : public ui::Window {
 public:
  explicit AnimationWindow(ui::WindowManager& wm) : Window(wm) {}
  std::string_view typeName() const override { return "AnimationWindow"; }
  void init(const ui::Style& style) override;
  std::string detail() const override;
  double fps() const { return fps_; }

 private:
  double fps_ = 0;
};

}  // namespace choco::game::choc2
