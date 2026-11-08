#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "gfx/Types.h"
#include "ui/DrawContext.h"
#include "ui/Input.h"
#include "ui/Message.h"
#include "ui/Style.h"

namespace choco::sdk {
class Sprite;
}

namespace choco::ui {

class WindowManager;

// The clock a modal's own timers read: the platform time minus the time the modal spent covered by another modal
// (window-system design WS 3.2). pause() freezes it, start() resumes it without the gap.
class ModalClock {
 public:
  double read(double platformNow) const { return (pausedAt_ >= 0 ? pausedAt_ : platformNow) - pausedTotal_; }
  void pause(double platformNow) {
    if (pausedAt_ < 0) pausedAt_ = platformNow;
  }
  void start(double platformNow) {
    if (pausedAt_ < 0) return;
    pausedTotal_ += platformNow - pausedAt_;
    pausedAt_ = -1;
  }
  bool paused() const { return pausedAt_ >= 0; }

 private:
  double pausedTotal_ = 0;
  double pausedAt_ = -1;
};

// One node of the window tree (window-system design section 2). Children are owned with unique_ptr and the first child
// is the frontmost. Everything outside the tree refers to a window by WindowId and resolves it through the manager.
class Window {
 public:
  // Flag bits, as in the original. Visible and enabled are the same bit.
  static constexpr std::uint32_t kModalFlag = 0x001;
  static constexpr std::uint32_t kFocusableFlag = 0x002;
  static constexpr std::uint32_t kEnabledFlag = 0x100;

  explicit Window(WindowManager& wm);  // takes the next id from the manager and registers in its id map
  virtual ~Window();
  Window(const Window&) = delete;
  Window& operator=(const Window&) = delete;

  WindowId id() const { return id_; }
  WindowManager& manager() const { return wm_; }

  // ---- tree ----
  Window* parent() const { return parent_; }  // raw: a parent always outlives its children
  const std::vector<std::unique_ptr<Window>>& children() const { return children_; }
  // Puts the child at the front (frontmost) and makes this its parent.
  virtual Window& adoptFront(std::unique_ptr<Window> child);
  // Removes a direct child from the tree and returns it (null if it is not a child).
  std::unique_ptr<Window> orphan(Window& child);
  // Pre-order, depth-first, in list order (front to back); a window matches itself first. An empty name never matches.
  Window* find(std::string_view name);
  const Window* find(std::string_view name) const;

  // ---- state ----
  const std::string& name() const { return name_; }
  void setName(std::string n) { name_ = std::move(n); }
  const gfx::RectI& rect() const { return rect_; }  // in the parent's coordinates
  void setRect(const gfx::RectI& r) { rect_ = r; }
  void setPos(int x, int y) { rect_.x = x, rect_.y = y; }
  void setSize(int w, int h) { rect_.w = w, rect_.h = h; }
  std::uint32_t flags() const { return flags_; }
  bool shown() const { return (flags_ & kEnabledFlag) != 0; }
  void setShown(bool v) { flags_ = v ? (flags_ | kEnabledFlag) : (flags_ & ~kEnabledFlag); }
  // Own opacity, 0..1. Each window draws with its own; nothing multiplies down the tree.
  float alpha() const { return alpha_; }
  void setAlpha(float a) { alpha_ = a; }
  bool isDestroyed() const { return destroyed_; }
  bool isModal() const { return (flags_ & kModalFlag) != 0; }

  // ---- virtuals ----
  // The style name for logs and dumps, e.g. "Text".
  virtual std::string_view typeName() const { return "Window"; }
  // Called by InitWindow right after the window is adopted, and by PostCreate after its children exist.
  virtual void init(const Style& style);
  virtual void postChildrenInit(const Style& style);
  // The window's own handling of a message; true consumes it.
  virtual bool onMessage(const Message&) { return false; }
  // Drawing. `ctx.origin` is this window's screen position and the clip is already its rectangle; the children are
  // drawn by drawWindow between draw and postDraw.
  virtual void draw(DrawContext&) const {}
  virtual void postDraw(DrawContext&) const {}
  // Drawing outside the window's rectangle (BSGWindow's frame border): when true, drawOutside runs right after draw
  // with only the ancestors' clip in force, so the window may paint beyond its own rectangle.
  virtual bool drawsOutside() const { return false; }
  virtual void drawOutside(DrawContext&) const {}
  // True when the window holds an image whose texture resolved (ImageLoaded). Windows without an image are false.
  virtual bool imageLoaded() const { return false; }
  // True for the types TWindowFader::Process sets alpha on (Text, Bitmap, Character, BSGWindow); others ignore fades.
  virtual bool takesFaderAlpha() const { return false; }
  // Label support (GetLabel/SetLabel work on these). Other windows are not label windows.
  virtual bool hasLabel() const { return false; }
  virtual const std::string& label() const;
  virtual void setLabel(std::string) {}

  // ---- input (window-system design section 5) ----
  // Points are in this window's own coordinates. A handler returns true to consume the event; an unhandled mouse event
  // falls through to the sibling behind, then to the parent.
  virtual bool onMouseDown(int x, int y) { (void)x, (void)y; return false; }
  virtual bool onMouseUp(int x, int y) { (void)x, (void)y; return false; }
  virtual bool onMouseMove(int x, int y) { (void)x, (void)y; return false; }
  // The pointer left the window, or a push/pop cancelled its capture.
  virtual void onMouseLeave() {}
  // Keys go to the focus window and bubble up to the modal. onKeyDown sees every key; onChar sees Enter and Escape.
  virtual bool onKeyDown(const KeyEvent&) { return false; }
  virtual bool onChar(const KeyEvent&) { return false; }
  // Text typed (UTF-8, from the platform's text input, one or more characters). Goes to the focus window and bubbles up.
  virtual bool onText(const std::string&) { return false; }
  // The window became the manager's focus; `old` is the previous focus (0 if none).
  virtual void onSetFocus(WindowId old) { (void)old; }
  // True for a window that takes typed text (TextEdit): the platform's text input is on only while one has the focus.
  virtual bool wantsTextInput() const { return false; }
  bool focusable() const { return (flags_ & kFocusableFlag) != 0; }

  // Window animation (StartWindowAnimation): called when the animation's interval is due on the owning modal's clock.
  // Returns true to keep animating. The window may be destroyed by what it does, so the caller touches nothing after.
  virtual bool onAnimate() { return false; }
  // The app lost (false) or regained (true) the foreground; the manager sends it to every window of every modal.
  virtual void onForeground(bool) {}

  // The root of the window's sprite tree, for the sprite probes and the dump. Null for windows with no sprites.
  virtual const sdk::Sprite* spriteRoot() const { return nullptr; }

  // The harness `autoshoot` command: a window-local point to shoot at now, or none. Only the factory games answer.
  virtual std::optional<gfx::PointF> autoAim() const { return std::nullopt; }

  // Extra text for --dump-windows after the common fields.
  virtual std::string detail() const { return {}; }

  // One line for --dump-windows (no children).
  std::string describe() const;

 protected:
  void setFlags(std::uint32_t f) { flags_ = f; }

 private:
  friend class WindowManager;
  WindowManager& wm_;
  WindowId id_;
  Window* parent_ = nullptr;
  std::vector<std::unique_ptr<Window>> children_;
  std::string name_;
  gfx::RectI rect_;
  std::uint32_t flags_ = kEnabledFlag;
  bool destroyed_ = false;
  float alpha_ = 1;
};

// A plain window that keeps what the style table says: name, the laid-out rectangle and the label. Stands in for every
// window type until its slice exists (widgets and game windows alike) and is the plain `Window` type itself. Draws nothing.
class PlaceholderWindow : public Window {
 public:
  PlaceholderWindow(WindowManager& wm, std::string typeName);
  std::string_view typeName() const override { return typeName_; }
  void init(const Style& style) override;
  // Test hook: PostCreate reaches the window on top of the creation stack.
  void postChildrenInit(const Style& style) override {
    Window::postChildrenInit(style);
    ++postInits;
  }
  int postInits = 0;
  bool hasLabel() const override { return hasLabel_; }
  const std::string& label() const override { return label_; }
  void setLabel(std::string s) override { label_ = std::move(s); }

 private:
  std::string typeName_;
  std::string label_;
  bool hasLabel_;
};

// A window in the modal chain. Its name is the script path it was pushed for. Not in its parent's child list: each
// modal's parent() is the modal below it, and the manager steps from one to the next.
class ModalWindow : public Window {
 public:
  explicit ModalWindow(WindowManager& wm);
  std::string_view typeName() const override { return "Modal"; }
  // A modal always fits its children first (TModalWindow::PostChildrenInit).
  void postChildrenInit(const Style& style) override;
  // The only consumer of close requests: a kCloseWindow with a name pops that modal (and everything above it); one
  // with an empty name is swallowed. Anything else is not handled.
  bool onMessage(const Message& m) override;
  // The script path the modal was pushed for. The name starts out the same, but the dialog's MakeDialog{ name = ... }
  // renames the modal (SetWindowName on the creation stack top), so only this stays fixed; expect-window matches it.
  const std::string& scriptPath() const { return scriptPath_; }
  void setScriptPath(std::string p) { scriptPath_ = std::move(p); }
  std::string detail() const override { return scriptPath_.empty() ? std::string() : " script=\"" + scriptPath_ + "\""; }
  WindowId defaultFocus = 0;
  // Paused while another modal is pushed on top of this one.
  ModalClock clock;
  // Hotkeys (RegisterHotkey): normalised key string -> button name. An empty button name erases the entry.
  void addHotkey(const std::string& key, const std::string& button);
  const std::map<std::string, std::string>& hotkeys() const { return hotkeys_; }
  // Hotkeys live here; Enter and Escape press the `default` and `cancel` buttons (docs/re/window-system.md 4.3).
  bool onKeyDown(const KeyEvent& ev) override;
  bool onChar(const KeyEvent& ev) override;
  // A modal that gets the focus hands it to its default focus window.
  void onSetFocus(WindowId old) override;
  // Set by ClearModalChildren (SwapToModal): the next file loaded becomes the modal's script path.
  bool awaitingScript = false;
  // Lua errors logged since the modal was pushed (attributed to the top modal when they happen).
  int scriptErrors = 0;

 private:
  std::string scriptPath_;
  std::map<std::string, std::string> hotkeys_;
};

// The bottom of the chain: 800x600, never popped.
class Screen : public ModalWindow {
 public:
  explicit Screen(WindowManager& wm);
  std::string_view typeName() const override { return "Screen"; }
};

}  // namespace choco::ui
