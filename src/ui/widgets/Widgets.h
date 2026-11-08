#pragma once

#include <memory>
#include <string>

#include "gfx/HitMask.h"
#include "gfx/Texture.h"
#include "script/LuaState.h"
#include "ui/Window.h"

namespace choco::ui {

// Bitmap (TImage): shows the texture named by `image`, scaled by `scale`. Sizes itself to the scaled texture and keeps
// that size whatever the style's w/h say (they only feed the position); with no texture the style size applies. Keys read: image, mask (the alpha, merged into the texture), scale, hflip, vflip. Not honoured yet: mipmap, rotate, and
// the boolean `alpha` (a texture flag; our textures always carry their alpha channel).
class BitmapWindow : public Window {
 public:
  explicit BitmapWindow(WindowManager& wm) : Window(wm) {}
  std::string_view typeName() const override { return "Bitmap"; }
  void init(const Style& style) override;
  void draw(DrawContext& ctx) const override;
  bool imageLoaded() const override { return texture_ != nullptr; }
  bool takesFaderAlpha() const override { return true; }
  std::string detail() const override;

  // _SetBitmap: replaces the texture and scale, and resizes the window to the new scaled image.
  void setBitmap(const std::string& image, double scale);
  // TImage::SetTexture(tex, s) on the current texture (the zoomin transition): stores the scale and, with a texture,
  // resizes the window to the texture size times `s` (rounded), keeping x and y, so the image grows from its top-left.
  void setScale(double scale);
  void setFlips(bool h, bool v) { hflip_ = h, vflip_ = v; }
  const std::shared_ptr<gfx::Texture>& texture() const { return texture_; }
  double scale() const { return scale_; }
  // A texture made in memory (Chocolatier 2's colormask): replaces the image and resizes the window to it at the current scale.
  void setTexture(std::shared_ptr<gfx::Texture> tex);

 protected:
  // Loads the image and sizes the window to it; the part of init before TWindow::Init.
  void initImage(const Style& style);
  bool hflip() const { return hflip_; }
  bool vflip() const { return vflip_; }
  gfx::ColorF tint_{1, 1, 1, 1};

 private:
  void loadTexture(const std::string& image, const std::string& mask = {});
  void sizeToTexture(const Style* style);
  std::string image_;
  std::shared_ptr<gfx::Texture> texture_;
  double scale_ = 1;
  bool hflip_ = false, vflip_ = false;
};

// BitmapTint (TBitmapTint): a Bitmap drawn with `tint` = {r,g,b,a} multiplying the texture and ignoring the image alpha.
// dx/dy are the image centre in window space (times scale), each defaulting to half the window size; w/h resize the
// window when both are above 0.
class BitmapTintWindow : public BitmapWindow {
 public:
  explicit BitmapTintWindow(WindowManager& wm) : BitmapWindow(wm) {}
  std::string_view typeName() const override { return "BitmapTint"; }
  void init(const Style& style) override;
  void draw(DrawContext& ctx) const override;
  std::string detail() const override;
  // BTSetTint: replaces the tint (the alpha is still ignored when drawing).
  void setTint(const gfx::ColorF& c) { tint_ = c; }
  const gfx::ColorF& tint() const { return tint_; }

 private:
  static constexpr float kNoCentre = -9999;
  gfx::PointF centre_{kNoCentre, kNoCentre};
};

// Rectangle (TRectangle): a flat fill in `color`, with `alpha` (default the colour's own alpha). Keys are read at
// PostChildrenInit, as in the original. Chocolatier 1's Lua also sets `shrink` (with `pad`, default 5), `invisible` (no fill) and
// `swallow` (mouse events stop here): with `shrink` every Text child is sized to its text plus `pad` and height, and the
// rectangle and its parent take the size (right edge of the widest child, bottom of the lowest plus `pad`)
// (TRectangle::PostChildrenInit 0041a5f0). Nothing in the Chocolatier 2 or 3 Lua sets those keys.
class RectangleWindow : public Window {
 public:
  explicit RectangleWindow(WindowManager& wm) : Window(wm) {}
  std::string_view typeName() const override { return "Rectangle"; }
  void postChildrenInit(const Style& style) override;
  void draw(DrawContext& ctx) const override;
  std::string detail() const override;
  bool onMouseDown(int, int) override { return swallow_; }
  bool onMouseUp(int, int) override { return swallow_; }
  bool onMouseMove(int, int) override { return swallow_; }
  // SetRectangleColor.
  void setColor(const gfx::ColorF& c) { color_ = c; }
  const gfx::ColorF& color() const { return color_; }

 private:
  gfx::ColorF color_{1, 1, 1, 1};
  bool swallow_ = false, invisible_ = false;
};

// TLayeredWindow: children belong to numbered layers, and showing a layer shows exactly the children adopted under it or
// under kAllLayers (-1). docs/re/design-questions.md section 5.
class LayeredWindow : public Window {
 public:
  LayeredWindow(WindowManager& wm, int layerCount);
  // A child goes onto the current layer's list, or onto every list when the current layer is -1.
  Window& adoptFront(std::unique_ptr<Window> child) override;
  int currentLayer() const { return current_; }
  int layerCount() const { return static_cast<int>(layers_.size()); }
  // SetCurrentLayer: a no-op when `n` is already current. Otherwise stores it, hides the children and shows the ones
  // listed for layer n (-1 shows layer 0's list, which holds the all-layers children too).
  void setCurrentLayer(int n);
  const std::vector<WindowId>& layerMembers(int n) const;
  // Lists an existing child under layer n as well (TLayeredWindow::AdoptChild on a child that is already adopted): the
  // child is shared, not duplicated. No-op when `child` is not a child of this window or n is out of range.
  void addToLayer(int n, WindowId child);

 private:
  int current_ = 0;
  std::vector<std::vector<WindowId>> layers_;
};

enum class ButtonKind { Push = 0, Toggle = 1, Radio = 2 };

// Button (TButton): a layered window with the state images `image0..` as children, the mouse state machine and the
// command. Keys read: sound, rolloversound, mask, type, scale, sendToParent, beginGroup, close, default, cancel, on,
// graphics, hflip, vflip, command, and label (kept for GetLabel before the Text child exists). tooltip is read and
// discarded by the original, and `rotate` is not honoured yet.
class ButtonWindow : public LayeredWindow {
 public:
  enum MouseState { kNormal = 0, kDown = 1, kOver = 2, kCommit = 3 };

  explicit ButtonWindow(WindowManager& wm);
  std::string_view typeName() const override { return "Button"; }
  void init(const Style& style) override;
  void postChildrenInit(const Style& style) override;
  bool onMouseDown(int x, int y) override;
  bool onMouseUp(int x, int y) override;
  bool onMouseMove(int x, int y) override;
  void onMouseLeave() override;
  bool hasLabel() const override { return true; }
  const std::string& label() const override;
  void setLabel(std::string s) override;
  std::string detail() const override;

  // The StateUpdate table: which layer a mouse state shows (-1 for "unchanged", state 3).
  static int layerFor(ButtonKind kind, int mouseState, bool on);

  ButtonKind kind() const { return kind_; }
  int mouseState() const { return mouseState_; }
  bool on() const { return on_; }
  // SetOn: sets the toggle bit and refreshes the layer; turning a radio on turns its group's other radios off.
  void setOn(bool on);
  bool closes() const { return close_; }
  bool isDefault() const { return default_; }
  bool isCancel() const { return cancel_; }
  // The `label` style key as written ("cancel" for a TextButton{label="cancel"}), kept for the Chocolatier 2 Enter rule.
  const std::string& labelKey() const { return labelKey_; }
  bool hasCommand() const { return command_.valid(); }
  void setCommand(script::LuaRef fn) { command_ = std::move(fn); }
  void setClickMask(std::shared_ptr<gfx::HitMask> m) { mask_ = std::move(m); }
  // The point (button coordinates) is on the button: inside its rectangle and, with a mask, on a lit mask pixel (the
  // mask is mapped by proportion, so a scaled button works).
  bool hitsPoint(int x, int y) const;

  // Press: toggles or selects, plays the click sound, then DoCommand.
  void press();
  // DoCommand: close request, kButtonPress, the command (delivered synchronously), then the posted message if the
  // button is still attached. Returns true when the button was detached from the screen by the command.
  bool doCommand();
  // EnableWindow reaches here: drops capture and goes back to the normal image.
  void resetState();

 private:
  void setMouseState(int s);
  void refreshLayer();
  bool visibleChain() const;
  void clearRadioGroup();

  ButtonKind kind_ = ButtonKind::Push;
  int mouseState_ = kNormal;
  bool on_ = false, close_ = false, default_ = false, cancel_ = false, sendToParent_ = false, beginGroup_ = false;
  std::string sound_, rolloverSound_, labelKey_;
  std::shared_ptr<gfx::HitMask> mask_;
  script::LuaRef command_;
};

// Registers Bitmap, BitmapTint, Rectangle and Button with the manager.
void registerBuiltinWidgets(WindowManager& wm);

}  // namespace choco::ui
