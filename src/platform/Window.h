#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include "gfx/Image.h"
#include <vector>

#include "platform/DisplayMode.h"
#include "platform/Platform.h"
#include "platform/Png.h"

namespace choco::platform {

struct Color {
  std::uint8_t r = 0, g = 0, b = 0, a = 255;
};

struct Rect {
  float x = 0, y = 0, w = 0, h = 0;
};

struct WindowConfig {
  double scale = 1;  // 1 = 800x600; fractions such as 1.5 are fine
  bool fullscreen = false;
  bool headless = false;
  // Headless only: the emulated desktop. Fullscreen resizes the dummy-driver window to this size.
  int displayWidth = 2560;
  int displayHeight = 1440;
  std::string title = "OpenConche";
};

// Input as the engine sees it. Mouse positions are already in logical (800x600) coordinates; with letterboxing they
// can lie outside the picture, in which case inArea is false.
struct InputEvent {
  enum class Type {
    Quit,
    MouseMove,
    MouseDown,
    MouseUp,
    Key,
    Text,               // key holds the typed UTF-8 text
    FullscreenChanged,  // fullscreen holds the new state; x, y the output size if known (headless), else 0
    RenderReset,        // key holds "targets" or "device"
    Resized,            // the output size changed (user drag, maximise, OS fullscreen, script `resize`); x, y = the new
                        // output size in pixels. Several size events in one poll collapse into one, the last size wins.
    Foreground          // the app lost or regained the foreground (focus, minimise, background); `fullscreen` is unused,
                        // `foreground` holds the new state
  } type = Type::Quit;
  float x = 0, y = 0;
  bool inArea = true;
  int button = 0;
  std::string key;  // SDL key name for Key events
  bool repeat = false;  // Key: an OS auto-repeat of a held key
  bool fullscreen = false;
  bool foreground = true;
  bool alt = false, ctrl = false, shift = false, gui = false;  // gui: Cmd on macOS, Win on Windows
};

// What the script driver uses to feed input in as if it came from the user.
class InputInjector {
 public:
  virtual ~InputInjector() = default;
  virtual void injectMouseMove(float lx, float ly) = 0;
  virtual void injectMouseButton(float lx, float ly, bool down) = 0;
  // The same in window coordinates (what the OS would deliver), bypassing the logical mapping.
  virtual void injectWindowMouseMove(float wx, float wy) = 0;
  virtual void injectWindowMouseButton(float wx, float wy, bool down) = 0;
  // "Return", "F11", "alt+Return", ... Returns false for an unknown key name.
  virtual bool injectKey(const std::string& spec) = 0;
  // Typed text as the OS text input would deliver it (UTF-8). A printable key from injectKey also types its character.
  virtual void injectText(const std::string& utf8) { (void)utf8; }
  virtual void injectQuit() = 0;
  // The OS telling the app it lost (false) or regained (true) the foreground. Default: ignored.
  virtual void injectForeground(bool fg) { (void)fg; }
  // Resizes the window to w x h (window pixels), as a user dragging its edge would. Takes effect at its place in the
  // event sequence of the next pollEvents (earlier queued input keeps the old mapping). Default: ignored.
  virtual void injectResize(int w, int h) { (void)w; (void)h; }
};

// True if `spec` ("Return", "alt+Return", "F11", ...) names a key injectKey can send. Needs no window.
bool isValidKeySpec(const std::string& spec);

// SDL3 window + renderer. Logical size is always 800x600, letterboxed. Needs a live Platform. Headless and
// windowed modes share one drawing path: the dummy-driver window has the real output size.
class Window : public InputInjector {
 public:
  Window(Platform& platform, const WindowConfig& config);
  ~Window() override;
  Window(const Window&) = delete;
  Window& operator=(const Window&) = delete;

  bool ok() const { return ok_; }
  const std::string& error() const { return error_; }
  double scale() const { return scale_; }  // the scale actually used after fallback

  // The SDL_Renderer*, opaque so only platform code needs SDL headers. For the future gfx module, which casts it
  // inside src/platform or through a platform-side wrapper. Valid while this Window lives.
  void* rendererHandle() const;
  // Current output size in pixels (the window's pixel size, or the emulated desktop in headless fullscreen).
  bool outputSize(int& w, int& h) const;

  // Drain pending events in order. Fullscreen hotkeys (Alt+Enter, F11) are applied immediately and reported as a
  // FullscreenChanged event at their place in the sequence, not forwarded as keys. A fullscreen change the OS made
  // (the green button, a window menu) is reported the same way, once. A changed output size is reported as one Resized
  // event at the end of the poll; the window is resizable and any shape other than 4:3 is letterboxed.
  std::vector<InputEvent> pollEvents();
  // False if the platform refused the change (the state is then unchanged).
  bool toggleFullscreen();
  // Text input (the OS keyboard, IME and typed-character events) is on only while a text field has the focus. The area
  // is that field's rectangle in logical coordinates, so an IME can place its candidate list; ignored when `on` is false.
  void setTextInput(bool on, float lx = 0, float ly = 0, float lw = 0, float lh = 0);
  bool textInputActive() const { return textInput_; }
  bool isFullscreen() const;
  // The fullscreen state last applied or reported: equal to isFullscreen() once the OS has finished the transition.
  // What a caller saves after its own toggle, since a desktop's transition (Spaces on macOS) can still be under way.
  bool fullscreenState() const { return reportedFullscreen_; }
  // A custom mouse cursor: an RGBA image with the hotspot in image pixels (the factory mini-games' aimer and arrow
  // graphics). False if the image is empty or the platform has no cursor (headless dummy video). clearCursor() puts
  // the OS default back. The previous custom cursor is destroyed.
  bool setCursor(const gfx::ImageData& image, int hotspotX, int hotspotY);
  void clearCursor();
  // True while a custom cursor this window installed is in force.
  bool cursorActive() const;
  // The shipped cursor pictures are the ONLY pfp art without real transparency: 8-bit paletted PNGs with no tRNS chunk,
  // whose transparency is the pure-magenta colour key (255,0,255) - every other paletted pfp PNG carries tRNS
  // (scans of Chocolatier 1's, 2's and 3's trees: exactly four key files, the cursor sets). This zeroes the alpha of
  // every pure-magenta pixel and returns true when it changed one; the cursor loader applies it before setCursor.
  static bool applyCursorColourKey(gfx::ImageData& img);

  // Frame: beginFrame (clears the whole output to black, so letterbox bars are black), draw*, endFrame, optionally
  // readPixels, present. All drawing is in logical 800x600 coordinates straight to the backbuffer.
  void beginFrame();
  // Fills the 800x600 logical area (not the bars) with c.
  void clear(Color c);
  void fillRect(Rect r, Color c);
  void outlineRect(Rect r, Color c);
  void endFrame();
  // A renderer that queues its own draws registers here. Rule: every direct SDL draw, clear, read or present in this
  // class must call flushDraws() first, so queued draws land before it.
  void setFlushHook(std::function<void()> hook) { flushHook_ = std::move(hook); }
  void present();
  // Captures the whole composited output, bars included.
  bool readPixels(Image& out);
  // Captures only the logical area, downscaled to 800x600 if the output scale is not 1.
  bool readLogicalPixels(Image& out);

  // The native folder picker (SDL_ShowOpenFolderDialog): asks the OS for a folder, modal to this window where the
  // platform supports it. Non-blocking; the SDL callback may run on another thread, so `onDone` is stored and
  // invoked from a later pollEvents() on this thread - with the chosen folder, or nullopt when the dialog was
  // cancelled, unsupported (headless dummy video, some setups) or failed. A second call while one is pending
  // replaces it. Needs a live window (the dialog keeps a parent pointer).
  void pickFolder(std::function<void(const std::optional<std::string>&)> onDone);
  // Forgets `onDone` and any result still in flight: a dialog that finishes later is dropped on the floor. The
  // caller whose locals `onDone` captured must call this before they go out of scope (the OS dialog cannot be
  // closed from here). Safe with no pick pending.
  void cancelFolderPick();
  // Set while onDone runs for a pick that never showed a dialog (no portal or zenity on Linux, a headless session):
  // the reason, so a caller can tell this from the user cancelling. Empty otherwise.
  const std::string& folderPickError() const { return folderPickError_; }

  void injectMouseMove(float lx, float ly) override;
  void injectMouseButton(float lx, float ly, bool down) override;
  void injectWindowMouseMove(float wx, float wy) override;
  void injectWindowMouseButton(float wx, float wy, bool down) override;
  bool injectKey(const std::string& spec) override;
  void injectText(const std::string& utf8) override;
  void injectQuit() override;
  void injectForeground(bool fg) override;
  void injectResize(int w, int h) override;

 private:
  struct Impl;
  void flushDraws() const {
    if (flushHook_) flushHook_();
  }
  std::function<void()> flushHook_;
  bool applyFullscreen(bool on);
  void applyWindowSize(int w, int h);  // a script `resize`, at its place in the event sequence
  void refreshPresentation();
  bool readRegion(Image& out, bool logicalOnly);
  void deliverFolderPick();  // moves a completed pick onto onDone (pollEvents calls this)
  static void folderPickCallback(void* userdata, const char* const* files, int filter);
  std::function<void(const std::optional<std::string>&)> folderOnDone_;
  // The pick's result lives in a shared state the SDL callback reaches through a weak pointer, so a dialog that
  // outlives the pick (cancelFolderPick, or the Window itself) writes into nothing instead of a dead object.
  struct FolderPickState {
    std::mutex mutex;
    std::optional<std::optional<std::string>> result;  // outer: the pick happened; inner: the folder or cancel
    std::string error;                                  // why no dialog could be shown (empty for a pick or a cancel)
  };
  std::shared_ptr<FolderPickState> folderState_;
  std::string folderPickError_;
  std::unique_ptr<Impl> impl_;
  bool ok_ = false;
  bool headless_ = false;
  bool emulatedFullscreen_ = false;
  bool reportedFullscreen_ = false;  // the fullscreen state the app was last told about (or asked for)
  bool sizeEventSeen_ = false;       // set by injectResize: the next poll compares the size even with no SDL event
  int windowedW_ = kLogicalWidth, windowedH_ = kLogicalHeight;  // headless: the size fullscreen returns to
  int reportedW_ = 0, reportedH_ = 0;                           // the output size the app was last told about
  bool textInput_ = false;
  double scale_ = 1;
  int displayWidth_ = 2560, displayHeight_ = 1440;
  std::string error_;
};

}  // namespace choco::platform
