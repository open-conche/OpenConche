#pragma once

#include <cstdio>
#include <deque>
#include <functional>
#include <string_view>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "sdk/Ticker.h"
#include "gfx/TextureCache.h"
#include "ui/DrawContext.h"
#include "ui/Input.h"
#include "ui/Message.h"
#include "ui/ScriptSink.h"
#include "ui/Style.h"
#include "ui/Transition.h"
#include "ui/Window.h"
#include "ui/WindowQuery.h"

namespace choco::gfx::text {
class TextSystem;
}
namespace choco::gfx {
class SheetCache;
}

namespace choco::ui {

// Owns every window: the modal chain (Screen at the bottom), the deferred-destruction list, the id map, the creation
// stack and the message queue (window-system design sections 2, 4 and 5).
class WindowManager : public WindowQuery {
 public:
  using Factory = std::function<std::unique_ptr<Window>(WindowManager&)>;

  WindowManager();
  ~WindowManager() override;
  WindowManager(const WindowManager&) = delete;
  WindowManager& operator=(const WindowManager&) = delete;

  void setScriptSink(ScriptSink* sink) { sink_ = sink; }
  ScriptSink* scriptSink() const { return sink_; }

  // ---- window types ----
  // Registers a type name (AddWindowType). An empty factory means a placeholder window.
  void addWindowType(std::string name, Factory make = {});
  const std::vector<std::string>& windowTypes() const { return types_; }
  bool hasWindowType(const std::string& name) const { return factories_.count(name) != 0; }
  // Built-in widget types (Bitmap, Rectangle, ...): found by createWindow even before AddWindowType runs for the name.
  void addBuiltinType(std::string name, Factory make) { builtins_[std::move(name)] = std::move(make); }
  // Textures for the image widgets; null in tests with no renderer (images then stay unloaded). Not owned.
  void setTextures(gfx::TextureCache* t) { textures_ = t; }
  gfx::TextureCache* textures() const { return textures_; }
  // Sprite sheets for the game windows (ports, characters); null in tests with no renderer. Not owned.
  void setSheets(gfx::SheetCache* s) { sheets_ = s; }
  gfx::SheetCache* sheets() const { return sheets_; }
  // Fonts and the glyph atlas for Text windows; null in tests with no renderer (text then lays out with the built-in
  // font and is not drawn). Not owned.
  void setTextSystem(gfx::text::TextSystem* t) { textSystem_ = t; }
  gfx::text::TextSystem* textSystem() const { return textSystem_; }
  // String-table lookup for Text labels (set by the script side). Without one labels are taken literally.
  void setLabelLookup(std::function<std::string(const std::string&)> f) { labelLookup_ = std::move(f); }
  const std::function<std::string(const std::string&)>& labelLookup() const { return labelLookup_; }
  // Reads a loose text file from the asset tree (CreditsWindow's `file`); empty when missing. Without one nothing opens.
  using TextFileReader = std::function<std::optional<std::string>(const std::string&)>;
  void setTextFileReader(TextFileReader f) { textFileReader_ = std::move(f); }
  std::optional<std::string> readTextFile(const std::string& name) const {
    return textFileReader_ ? textFileReader_(name) : std::nullopt;
  }
  // A window of that type: the factory's, or a placeholder (also for names never registered).
  std::unique_ptr<Window> createWindow(const std::string& typeName);
  // True the first time a placeholder is made for `typeName` (so the caller logs it once).
  bool firstPlaceholderOf(const std::string& typeName) { return placeholderSeen_.insert(typeName).second; }

  // ---- lookup ----
  // The live window with that id, or null once it is destroyed or detached from the tree.
  Window* window(WindowId id) const;
  ModalWindow* topModal() const { return modals_.back().get(); }
  Screen* screen() const;
  int modalDepth() const { return static_cast<int>(modals_.size()); }  // the Screen counts
  const std::vector<std::unique_ptr<ModalWindow>>& modals() const { return modals_; }
  // Search roots (window-system design section 1): the top modal only, or the top modal and down the chain.
  Window* findInTopModal(std::string_view name) const;
  Window* findInChain(std::string_view name) const;
  // The window on top of the script's creation stack (the top modal when the stack is empty or its top is gone).
  Window* creationTop() const;

  // ---- creation stack ----
  // InitWindow: adopts `w` into the creation top, calls init, pushes it. Returns the window.
  Window& initWindow(std::unique_ptr<Window> w, const Style& style);
  void popCreation();  // never pops the last entry
  // PushWindow: makes `w` the window the DSL builds into, until the matching popCreation.
  void pushCreation(const Window& w) { creationStack_.push_back(w.id()); }
  std::size_t creationDepth() const { return creationStack_.size(); }

  // ---- modals ----
  // Appends a new modal (child of the old top), focuses it, resets the creation stack to [it]. A zero-width modal takes
  // its parent's size.
  ModalWindow& pushModal(const std::string& name);
  // Pops the first modal from the top whose name or id string equals `s`, and everything above it. The Screen is never
  // popped. False when nothing matched. Popped modals are destroyed at the next clearDestroyed().
  bool popModal(const std::string& s);
  // Empties the top modal (deferred destruction of the children) and sets its size and position to 0.
  void clearModalChildren();
  static std::string idString(WindowId id);  // "%8x"

  // ---- destruction ----
  // Detaches a non-modal window from its parent and queues it for deletion.
  void destroyWindow(Window& w);
  // Deletes the queued windows in queue order; a deleted modal posts kModalClosed with its id string.
  void clearDestroyed();
  std::size_t destroyedCount() const { return destroyed_.size(); }
  // Deletes every window now: the queued ones, then the modals from the top (children before parents). The destructor
  // does the same; call it first when the windows' destructors need something the owner destroys before the manager
  // (a game window may release its world state through the script context).
  void destroyAll();

  // ---- focus ----
  // Makes `id` the focus and tells the window (onSetFocus with the old focus). 0, or a window that is gone, clears it.
  void setFocus(WindowId id);
  WindowId focus() const { return focus_; }
  // True while the focus window takes typed text (a TextEdit); `area` is then its rectangle in screen coordinates.
  bool textInputWanted(gfx::RectI& area) const;

  // ---- input (design section 5) ----
  // Routes one input event: mouse events to the top modal's tree (captured windows first, then the first-to-last hit
  // test with fall-through to the sibling behind and then the parent), key events to the focus window and up, then the
  // modal's hotkeys. An event nobody handled that reaches the modal posts kPressAnyKey unless `overlaymouse` is on.
  void handleInput(const InputEvent& ev);
  // Chocolatier 2 era window system (TWindow::HandleEvent, C2 0x34028): when an unhandled mouse event falls through to
  // the sibling behind, that sibling's children are searched with the point still in the PARENT's coordinates (0x3412b
  // picks the sibling, 0x3414f searches its children, 0x34170-0x34194 descends subtracting only the child's offset).
  // Chocolatier 3 translates into the sibling's space first, which is the default. Set from the selected game at
  // startup; Chocolatier 1 is unknown (no decomp of its HandleEvent), so it keeps the default. See
  // docs/re/choc2-bottom-hud-clicks.md section 0.1.
  void setLegacyFallThroughPoint(bool on) { legacyFallThroughPoint_ = on; }
  bool legacyFallThroughPoint() const { return legacyFallThroughPoint_; }
  // Chocolatier 2's TSprite::HitTest: a sprite under the pointer counts only where its rendered alpha is at least
  // `spriteHitAlpha()` (192 of 255), children included (docs/re/choc2-port-binary.md 1.3). Off (0) is Chocolatier 3's plain
  // image rectangle. Set from the selected game at startup.
  void setSpriteHitAlpha(int threshold) { spriteHitAlpha_ = threshold; }
  int spriteHitAlpha() const { return spriteHitAlpha_; }
  // Chocolatier 2's popup fader (TPopupWindow::TFader::Process, C2 0x264b4) for every popup openContentsPopup makes (building
  // hover, map and ledger rollovers, items, factories, quest, medals, the lab game): 125 ms in 50 ms steps (0.4, 0.8, 1.0),
  // every Rectangle ends at alpha 0.8 whatever its Lua alpha, Text, Image and Frame at 1 (docs/re/choc2-port-binary.md 3.3).
  // Off is Chocolatier 3's smooth fade that leaves Rectangles alone. Set from the selected game at startup.
  void setLegacyPopupFader(bool on) { legacyPopupFader_ = on; }
  bool legacyPopupFader() const { return legacyPopupFader_; }
  // Chocolatier 2's Rollover widget (TRollover, C2 0x29ec8/0x2a4c0; docs/re/choc2-map-binary.md 1.5): `xOffset` defaults to 20,
  // `rolloverSound` plays when the popup opens and an optional `area` list of {x, y, x2, y2} rects restricts the hot zone.
  // Off is Chocolatier 3's Rollover (offset 0, no sound, no area). Set from the selected game at startup.
  void setLegacyRollover(bool on) { legacyRollover_ = on; }
  bool legacyRollover() const { return legacyRollover_; }
  // Chocolatier 1's TText wrap keeps a final character that overflows the box on its line (docs/re/choc1-text-flags.md 2); off is
  // the later games' rule (it moves to a new line). Read by TextWindow when it builds its TextStyle.
  void setKeepLastOverflowChar(bool on) { keepLastOverflowChar_ = on; }
  bool keepLastOverflowChar() const { return keepLastOverflowChar_; }
  // Chocolatier 2's ui/buysell.lua marks its Cancel button `default=true` and gives the OK button no flag, so the original
  // TModalWindow::OnChar (Enter only, FindDefault takes a `default` button; C2 has no `cancel` key) would press Cancel on
  // Enter. The engine deliberately deviates (docs/re/choc2-logo-and-trade.md 2.4): with this on, a `default` button whose
  // label key is "cancel" is the Escape button, and Enter presses the first other visible button of its parent instead.
  // Set from the selected game at startup.
  void setLegacyCancelDefault(bool on) { legacyCancelDefault_ = on; }
  bool legacyCancelDefault() const { return legacyCancelDefault_; }
  // Mouse capture: captured windows get mouse events before the hit test. Removed by onMouseLeave, destruction, a modal
  // push or pop.
  void addMouseListener(WindowId id);
  void removeMouseListener(WindowId id);
  bool isMouseListener(WindowId id) const;
  // The window that handled the last mouse move (0 if none).
  WindowId hover() const { return hover_; }
  // `overlaymouse` config gate for kPressAnyKey (unset or 0 means post). Called on every unhandled event.
  void setOverlayMouseQuery(std::function<bool()> q) { overlayMouse_ = std::move(q); }
  // A line for the script log (hot key errors). Null in tests.
  void setLogger(std::function<void(const std::string&)> f) { log_ = std::move(f); }
  void log(const std::string& line) const {
    if (log_) log_(line);
  }
  // A button asks for its `sound` / `rolloversound` file (and the text box its typing click) through this hook.
  void setSoundHook(std::function<void(const std::string& file, bool rollover)> f) { sound_ = std::move(f); }
  // Called for every Escape before the windows see it (Chocolatier 1's loop-level key-27 pause). Not consumed: the key
  // continues into the normal routing.
  void setEscapeHook(std::function<void()> f) { escape_ = std::move(f); }
  void playSound(const std::string& file, bool rollover) const {
    if (sound_) sound_(file, rollover);
  }
  // The text box's typing click: prefix + %02u(roll % count + 1) + suffix. count 0 (the default) = none.
  void setTypingSound(std::string prefix, unsigned count, std::string suffix) {
    typingPrefix_ = std::move(prefix);
    typingCount_ = count;
    typingSuffix_ = std::move(suffix);
  }
  std::string typingSoundFile(unsigned roll) const {
    if (typingCount_ == 0) return {};
    char n[16];
    std::snprintf(n, sizeof n, "%02u", roll % typingCount_ + 1u);
    return typingPrefix_ + n + typingSuffix_;
  }
  // Turns a sound name from a style (Chocolatier 2's Rollover `rolloverSound`, "ding") into the file to play; the game module
  // installs its rule. Unset: the name as given.
  void setSoundNameResolver(std::function<std::string(const std::string&)> f) { soundName_ = std::move(f); }
  std::string resolveSoundName(const std::string& name) const { return soundName_ ? soundName_(name) : name; }
  // The Options sliders apply their level live (TOptions::OnMessage): `slider` is the child's name, `value` is 0..1.
  void setVolumeHook(std::function<void(const std::string& slider, float value)> f) { volume_ = std::move(f); }
  void previewVolume(const std::string& slider, float value) const {
    if (volume_) volume_(slider, value);
  }
  // The screen position of `w`'s top-left corner: the sum of rectangles up to and including its modal's.
  static gfx::PointF screenOrigin(const Window& w);

  // ---- messages ----
  void post(Message m) { queue_.push_back(std::move(m)); }
  std::size_t pendingMessages() const { return queue_.size(); }
  // Message listeners: `fn` is called for every message of `type` that is handled, before the window climb, for as long as
  // the window `owner` is alive (a stale entry is dropped on the next matching message). Used by the Chocolatier 2
  // DynamicWindow `ontext` hook, which is a sibling of the text field and so not on its climb path.
  using MessageListener = std::function<void(const Message&)>;
  void addMessageListener(WindowId owner, int type, MessageListener fn) { listeners_.push_back({owner, type, std::move(fn)}); }
  std::size_t messageListenerCount() const { return listeners_.size(); }
  // SendWindowMessage: handles a message now, without queueing it (the window climb, then the script).
  void sendMessage(const Message& m) { handle(m); }
  // Handles queued messages in order (handlers may post more; those run in the same pass), at most kMaxDispatch per call.
  void dispatchMessages();
  static constexpr int kMaxDispatch = 10000;
  bool quitRequested() const { return quit_; }
  bool takeDispatchOverflow() { return std::exchange(overflow_, false); }

  // ---- WindowQuery ----
  bool isShown(std::string_view name) const override;
  bool hasScriptError(std::string_view name) const override;
  int countSprites(std::string_view prefix) const override;
  std::string dumpWindows() const override { return dump(); }
  std::optional<gfx::PointF> autoAim() const override;
  bool labelOf(std::string_view name, std::string& out) const override;
  // A Lua error was logged: counted against the top modal.
  void noteScriptError() { modals_.back()->scriptErrors++; }
  // The host loaded a file with dofile: after ClearModalChildren it becomes the top modal's script path.
  void noteScriptLoad(std::string_view name);

  // ---- transitions (design section 7) ----
  void startTransition(Transition t) { transitions_.start(std::move(t)); }
  // Steps every transition to `nowMs` and then runs the finished ones' `onend`s through the script sink.
  void tickTransitions(double nowMs) {
    nowMs_ = nowMs;
    transitions_.tick(*this, nowMs);
  }
  std::size_t transitionCount() const { return transitions_.size(); }
  // The script clock as of the last tickTransitions (windows with their own timing, such as the text caret, read it).
  double nowMs() const { return nowMs_; }
  void setNowMs(double ms) { nowMs_ = ms; }

  // ---- window animations (StartWindowAnimation) ----
  // Calls `Window::onAnimate` every `ms` of the owning modal's clock (so it freezes while a dialog covers the modal).
  // Starting one for a window that already has one restarts it. The entry ends when onAnimate returns false or the
  // window is gone.
  // With `catchUp` the animation is a sdk::FixedStep: onAnimate runs once per due step, several times in one call when
  // the frame was late, until it returns false, stops or restarts its own animation, or destroys the window.
  void startWindowAnimation(WindowId id, double ms, bool catchUp = false);
  // The clock of the modal that contains `w`, as of the last tick: what its window animations measure. It stops while a dialog
  // covers the modal.
  double modalNow(const Window& w) const;
  void stopWindowAnimation(WindowId id);
  // Runs every due animation once (at most one tick each, re-armed from now). Iterates a copy of the entries.
  void tickWindowAnimations(double platformNowMs);
  // Window::onForeground for every window in every modal's tree (a window may add or remove windows, so ids are walked).
  void dispatchForeground(bool fg);
  std::size_t windowAnimationCount() const { return anims_.size(); }

  // ---- drawing ----
  // Draws every modal from the Screen up to the top one, each as drawWindow does.
  void draw(gfx::Renderer& renderer) const;

  // The tree as text: one line per window, children indented, rects in parent coordinates.
  std::string dump() const;
  // Diagnostics (F10 / `diag`): the modal stack, queued messages, transitions and running window animations, as text.
  std::string diagnose() const;

 private:
  friend class Window;
  WindowId nextId() { return nextId_++; }
  void registerWindow(Window* w) { byId_[w->id()] = w; }
  void unregisterWindow(Window* w) { byId_.erase(w->id()); }
  void popTop();
  void handle(const Message& m);
  void notifyListeners(const Message& m);
  void routeMouse(const InputEvent& ev);
  bool legacyFallThroughPoint_ = false;
  int spriteHitAlpha_ = 0;
  bool legacyPopupFader_ = false;
  bool legacyRollover_ = false;
  bool keepLastOverflowChar_ = false;
  std::function<std::string(const std::string&)> soundName_;
  bool legacyCancelDefault_ = false;
  void routeKey(const InputEvent& ev);
  void routeText(const InputEvent& ev);
  void postPressAnyKey(const std::string& name);
  void cancelMouseState();

  WindowId nextId_ = 1;
  // Declared first so it is destroyed last: windows unregister themselves while they die.
  std::unordered_map<WindowId, Window*> byId_;
  std::vector<std::unique_ptr<ModalWindow>> modals_;  // [0] = Screen, back() = top
  std::vector<std::unique_ptr<Window>> destroyed_;
  std::vector<WindowId> creationStack_;
  struct Listener {
    WindowId owner;
    int type;
    MessageListener fn;
  };
  std::vector<Listener> listeners_;
  std::deque<Message> queue_;
  TransitionList transitions_;
  struct WindowAnim {
    WindowId id = 0;
    std::uint64_t serial = 0;
    sdk::Ticker ticker;
    sdk::FixedStep step;
    bool catchUp = false;
  };
  std::vector<WindowAnim> anims_;
  std::uint64_t animSerial_ = 0;
  std::vector<std::string> types_;
  std::unordered_map<std::string, Factory> factories_;
  std::unordered_map<std::string, Factory> builtins_;
  gfx::TextureCache* textures_ = nullptr;
  gfx::SheetCache* sheets_ = nullptr;
  gfx::text::TextSystem* textSystem_ = nullptr;
  std::function<std::string(const std::string&)> labelLookup_;
  TextFileReader textFileReader_;
  std::set<std::string> placeholderSeen_;
  WindowId focus_ = 0;
  WindowId hover_ = 0;
  std::vector<WindowId> mouseListeners_;
  std::function<bool()> overlayMouse_;
  std::function<void(const std::string&)> log_;
  std::function<void(const std::string&, bool)> sound_;
  std::function<void()> escape_;
  std::string typingPrefix_, typingSuffix_;
  unsigned typingCount_ = 0;
  std::function<void(const std::string&, float)> volume_;
  ScriptSink* sink_ = nullptr;
  double nowMs_ = 0;
  bool quit_ = false;
  bool overflow_ = false;
};

}  // namespace choco::ui
