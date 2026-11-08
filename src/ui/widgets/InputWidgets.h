#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "gfx/SpriteSheet.h"
#include "gfx/Texture.h"
#include "ui/widgets/TextWidgets.h"

namespace choco::ui {

// TextEdit (registered by the game as TCustomTextEdit over TTextEdit and TText; docs/re/fonts-and-text.md section 4 and
// docs/re/design-questions.md section 7). Shows its text with the Text machinery, keeps a caret that blinks on the
// simulated clock, and takes typed characters, Backspace, Delete and the arrow keys while it has the focus.
//
// Keys read: clearinitial, enablewindow, onkey, widthlimited, password, length (10), ignore, utf8, plus the Text keys
// (the label is literal: the string table is off while a TextEdit builds). Not done yet: paste, Tab to the next edit,
// and the check that a glyph exists in the font before accepting a UTF-8 character.
class TextEditWindow : public TextWindow {
 public:
  explicit TextEditWindow(WindowManager& wm);
  std::string_view typeName() const override { return "TextEdit"; }
  void init(const Style& style) override;
  void draw(DrawContext& ctx) const override;
  std::string detail() const override;

  // GetLabel returns the real text (a password field returns the password, not the stars); SetLabel is SetText: it
  // replaces the text and puts the caret at its end.
  const std::string& label() const override { return text_; }
  void setLabel(std::string s) override;

  bool onMouseUp(int x, int y) override;
  bool onKeyDown(const KeyEvent& ev) override;
  bool onChar(const KeyEvent& ev) override;
  bool wantsTextInput() const override { return true; }
  bool onText(const std::string& utf8) override;
  void onSetFocus(WindowId old) override;

  // ---- state, for the window itself and for tests ----
  const std::string& text() const { return text_; }
  std::size_t caretByte() const { return caret_; }
  bool password() const { return password_; }
  bool utf8Allowed() const { return utf8_; }
  int maxLength() const { return maxLen_; }
  bool initialPending() const { return !cleared_; }
  // The string the Text machinery lays out: the text, or one `*` per byte in a password field.
  std::string displayText() const;
  // The caret is drawn only while this window has the focus and the blink phase says so: time % 800 < 401 hides it.
  static bool caretPhaseVisible(double nowMs);

  // One typed character as OnUTF8Char handles it (`ch` is one UTF-8 character, or "\b" for Backspace). Returns true when
  // the edit took it (inserted, deleted, or swallowed by `ignore`). Posts kTextEditChanged on success.
  bool typeCharacter(std::string_view ch);
  // Several characters from one text event.
  bool typeText(std::string_view utf8);
  // The key presses OnKeyDown handles; false when the edit has no use for the key.
  bool editKey(const std::string& keyName);

 private:
  enum KeyHitType { kInsert = 0, kEdit = 1, kReject = 5 };
  // TCustomTextEdit::KeyHit: clears a pending initial text on the first call, then checks the type.
  bool keyHit(KeyHitType type, std::string_view ch);
  void clearInitial();
  void setText(std::string s);
  void testEnable(int delta);
  void notifyChanged();
  void callOnKey();
  void refreshDisplay();
  std::size_t glyphIndexOfCaret() const;
  static std::size_t charLengthAt(const std::string& s, std::size_t at);
  static std::size_t prevCharStart(const std::string& s, std::size_t at);
  int codePoints() const;

  std::string text_;
  std::size_t caret_ = 0;  // byte offset
  bool cleared_ = true;    // false while a clearinitial text is waiting for the first key or click
  bool password_ = false, utf8_ = false, widthLimited_ = false;
  int maxLen_ = 10;
  std::string ignore_, enableWindow_;
  script::LuaRef onKey_;
  unsigned soundSeed_ = 1;
};

// Slider (TSlider): a horizontal or vertical rail with a draggable knob. The value runs 0 to 1. A change posts
// kSliderValChanged (9) to the parent; releasing the knob posts kSliderMouseUp (10); pressing the rail beside the knob
// posts kSliderPageUp (11) or kSliderPageDown (12). The messages are sent at once, not queued, as the original does.
//
// Keys read: w, h (orientation only), hslider, railtop, railmid, railbot, sliderimage, sliderrollimage, name, scale,
// yoffset, value.
class SliderWindow : public Window {
 public:
  explicit SliderWindow(WindowManager& wm) : Window(wm) {}
  std::string_view typeName() const override { return "Slider"; }
  void init(const Style& style) override;
  void draw(DrawContext& ctx) const override;
  bool onMouseDown(int x, int y) override;
  bool onMouseUp(int x, int y) override;
  bool onMouseMove(int x, int y) override;
  void onMouseLeave() override;
  std::string detail() const override;

  float value() const { return value_; }
  // Clamps to 0..1; `quiet` suppresses the kSliderValChanged message (what Init and the Options screen use).
  void setValue(float v, bool quiet);
  bool horizontal() const { return horizontal_; }
  bool dragging() const { return dragging_; }
  // The knob's rectangle in the slider's own coordinates.
  gfx::RectI knobRect() const;

 private:
  void send(int type);
  void layoutSize();
  gfx::RectI knobAt(float v) const;
  std::shared_ptr<gfx::Texture> railTop_, railMid_, railBot_, knob_, knobOver_;
  bool horizontal_ = true;
  bool dragging_ = false, hover_ = false;
  float value_ = 0;
  double scale_ = 1;
  int yoffset_ = 0;
  int knobW_ = 0, knobH_ = 0;
};

// BSGWindow (TBSGWindow): a window with an optional flat fill and mouse swallowing. `color` defaults to (1,1,1,0), so the
// fill is invisible unless a colour with alpha is given; the colour's alpha is the window's own alpha (a fade changes
// it), and a given colour's alpha is also applied to the children that take fades. `fill` (default true) turns the fill
// off, `swallowmouse` makes the window consume mouse events. `frame` names a sprite sheet of eight border pieces (UL, UR, LL, LR, left, right,
// top, bottom) that is tiled around the outside of the rectangle with the window colour's alpha.
class BSGWindow : public Window {
 public:
  explicit BSGWindow(WindowManager& wm);
  std::string_view typeName() const override { return "BSGWindow"; }
  void postChildrenInit(const Style& style) override;
  void draw(DrawContext& ctx) const override;
  bool takesFaderAlpha() const override { return true; }
  // The `frame` border sheet (controls/rollover): drawn outside the rectangle, see drawOutside.
  bool drawsOutside() const override { return frameSheet_ != nullptr; }
  void drawOutside(DrawContext& ctx) const override;
  bool onMouseDown(int, int) override { return swallow_; }
  bool onMouseUp(int, int) override { return swallow_; }
  bool onMouseMove(int, int) override { return swallow_; }
  std::string detail() const override;
  bool swallowsMouse() const { return swallow_; }
  const gfx::ColorF& color() const { return color_; }

 protected:
  bool swallow_ = false;
  bool fill_ = true;
  gfx::ColorF color_{1, 1, 1, 0};
  std::string frame_;
  std::shared_ptr<const gfx::SpriteSheet> frameSheet_;
};

// PopupWindow: the BSGWindow a Rollover creates in the top modal. It lives while the pointer stays inside the Rollover's
// screen rectangle, listens to the mouse for as long as it does, swallows presses, and a release inside it runs the
// Rollover's click (the `command`) and closes it.
class PopupWindow : public BSGWindow {
 public:
  PopupWindow(WindowManager& wm, WindowId rollover, gfx::RectI keepVisible);
  std::string_view typeName() const override { return "PopupWindow"; }
  bool onMouseDown(int, int) override;
  bool onMouseUp(int x, int y) override;
  bool onMouseMove(int x, int y) override;
  void onMouseLeave() override { kill(); }
  // Removes the mouse listener and destroys the window.
  void kill();
  // Keeps the popup inside the screen window with a 4 pixel margin (Reposition): `x`,`y` are screen coordinates of its
  // top-left; one that would overflow the right edge moves left by the overflow and down 32 pixels, one that would
  // overflow the bottom moves up.
  static std::pair<int, int> reposition(int x, int y, int w, int h, int screenW, int screenH, int margin = 4);

 private:
  WindowId rollover_;
  bool pressed_ = false;  // the press began on the popup
  gfx::RectI keep_;  // the Rollover's rectangle in screen coordinates
};

// A window that owns a popup made by openContentsPopup, other than a Rollover: a release on the popup, after a press that
// began on it, calls popupClicked() (a Rollover runs its own click instead).
class PopupClickTarget {
 public:
  virtual ~PopupClickTarget() = default;
  virtual void popupClicked() = 0;
  // Asked from the popup's mouse listener while the pointer is inside the popup's keep rectangle, with the pointer in screen
  // coordinates: false closes the popup and lets the move reach the owner. The default keeps the rectangle test alone.
  virtual bool keepsPopup(int screenX, int screenY) { (void)screenX, (void)screenY; return true; }
};

// Builds a PopupWindow in the top modal from `contents` (a string of Lua run with the popup as the window being built), fits
// it, fades it in over RolloverWindow::kFadeMs, moves it to the screen point (ax, ay) kept inside the screen window, and
// listens to the mouse so it dies when the pointer leaves `keep` (a screen rectangle). `owner` is the window whose click a
// release on the popup runs. Returns the popup's id, or 0 when there is no script host to run `contents`. This is the body
// of a Rollover's hover, shared with the Chocolatier 2 port (docs/design/choc2-port-and-ledger.md 2.1).
// With WindowManager::legacyPopupFader() the fade is Chocolatier 2's (stepped, Rectangles at 0.8).
WindowId openContentsPopup(WindowManager& wm, WindowId owner, const gfx::RectI& keep, const std::string& contents, int ax, int ay);

// How openPopup differs from the default: `instant` skips the fade (the popup is fully visible from the first frame), `margin` is
// the gap kept to the screen edges when the popup is moved back inside (Chocolatier 1: instant, no margin).
struct PopupOptions {
  bool instant = false;
  int margin = 4;
};

// openContentsPopup's body for a popup the caller built (a PopupWindow subclass that handles its own mouse events).
WindowId openPopup(WindowManager& wm, std::unique_ptr<PopupWindow> popup, const std::string& contents, int ax, int ay,
                   const PopupOptions& options);

// Rollover (Rollover): an invisible hot area. Hovering it builds a PopupWindow in the top modal from `contents`, a string
// of Lua run with the popup as the window being built (through the same path as other injected commands), fits the
// popup, and fades it in over 125 ms. Keys read: xOffset, yOffset (0), xAbsolute, yAbsolute (-1), contents, command.
class RolloverWindow : public Window {
 public:
  explicit RolloverWindow(WindowManager& wm) : Window(wm) {}
  std::string_view typeName() const override { return "Rollover"; }
  void init(const Style& style) override;
  void postChildrenInit(const Style& style) override;
  bool onMouseMove(int x, int y) override;
  std::string detail() const override;
  // Runs the click function, if any (a click on the popup).
  void click();
  bool hasCommand() const { return command_.valid(); }
  const std::string& contents() const { return contents_; }
  // Chocolatier 2's hot zone test (an `area` entry {left, top, right, bottom}: left <= x < right, top <= y < bottom, stored
  // in a RectI as x, y, w, h = left, top, right, bottom); true when there is no area.
  static bool inArea(const std::vector<gfx::RectI>& area, int x, int y);
  const std::vector<gfx::RectI>& area() const { return area_; }
  // A Rollover made from C++ (the map's ports): `rect` in the parent's coordinates, `contents` as above, and a click that
  // evaluates `clickString` with runString when there is no `command`. A negative popup position means "at the pointer".
  static std::unique_ptr<RolloverWindow> makeNative(WindowManager& wm, const gfx::RectI& rect, std::string contents,
                                                    std::string clickString, int popupX, int popupY);
  static constexpr double kFadeMs = 125;

 private:
  std::string contents_;
  int xOffset_ = 0, yOffset_ = 0, xAbsolute_ = -1, yAbsolute_ = -1;
  std::string rolloverSound_;     // Chocolatier 2 only: played when the popup opens
  std::vector<gfx::RectI> area_;  // Chocolatier 2 only: {left, top, right, bottom} in client coordinates; empty = anywhere
  script::LuaRef command_;
  std::string clickString_;
};

// OptionsWindow (TOptions): the container of the Options screen. At PostChildrenInit it sets the three volume sliders to
// their stored values (the soundlevel, musiclevel and ambientlevel preferences, per mille) without sending a message, and
// it answers the sliders' kSliderValChanged by applying the level live through the manager's volume hook.
class OptionsWindow : public Window {
 public:
  explicit OptionsWindow(WindowManager& wm) : Window(wm) {}
  std::string_view typeName() const override { return "OptionsWindow"; }
  void postChildrenInit(const Style& style) override;
  bool onMessage(const Message& m) override;
};

// Registers TextEdit, Slider, BSGWindow, Rollover and OptionsWindow with the manager.
void registerInputWidgets(WindowManager& wm);

}  // namespace choco::ui
