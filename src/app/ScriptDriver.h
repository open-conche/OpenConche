#pragma once

#include <functional>
#include <string>
#include <vector>

#include "platform/Window.h"
#include "ui/WindowQuery.h"

namespace choco::app {

struct ScriptCommand {
  enum class Kind {
    Click, Move, Down, Up,                        // logical coordinates
    ClickWindow, MoveWindow, DownWindow, UpWindow,  // window coordinates
    Key, Type, Lua, Wait, Screenshot, ScreenshotLogical, ExpectWindow, ExpectLabel, ExpectSprite, ExpectLua, Dump, Autoshoot, ExpectDump, Focus, Resize, Diag, Quit
  } kind = Kind::Quit;
  float x = 0, y = 0;
  double ms = 0;  // wait duration, or the expect-window timeout (-1 = default)
  std::string text;  // key spec, screenshot path or window name
  std::string text2;  // expect-label: the label wanted
  int count = 1;  // expect-sprite: the least number of sprites
  bool flag = false;  // focus: true for `focus gained`
  int line = 0;
};

struct ParsedScript {
  std::vector<ScriptCommand> commands;
  std::vector<std::string> errors;  // "line N: message"
};

ParsedScript parseScript(const std::string& text);

// Plays a parsed script against an InputInjector, driven by a clock in milliseconds
// (simulated in headless mode, so runs are deterministic).
class ScriptDriver {
 public:
  ScriptDriver(std::vector<ScriptCommand> commands, platform::InputInjector& input);

  // Runs commands until one needs time to pass or a frame to render (wait, screenshot, key: a key may change the
  // display, so the next command runs after this frame's events are processed). Call once per frame, before polling events.
  void update(double nowMs);

  // The `lua <code>` command runs source text in the game script (as DoLuaString does). Without a runner it fails the run.
  void setLuaRunner(std::function<void(const std::string&)> f) { luaRunner_ = std::move(f); }

  // expect-lua: evaluates a Lua expression on the main state each frame; true when it is truthy.
  void setLuaEvaluator(std::function<bool(const std::string&)> f) { luaEval_ = std::move(f); }

  // The `diag` command writes the diagnostics report (the same as the F10 hotkey).
  void setDiagnostics(std::function<void()> f) { diagnostics_ = std::move(f); }

  // Without a query (no script host) expect-window logs "not implemented" and continues.
  void setWindowQuery(const ui::WindowQuery* q) { windows_ = q; }

  // Extra lines appended to the window dump by `dump` and searched by `expect-dump` (the `audio ...` lines).
  void setExtraDump(std::function<std::string()> f) { extraDump_ = std::move(f); }
  std::string fullDump() const {
    std::string text = windows_ ? windows_->dumpWindows() : std::string();
    if (extraDump_) text += extraDump_();
    return text;
  }

  // True while an expect-window is still waiting.
  bool expectPending() const { return expecting_ || expectingSprite_ || expectingLua_ || expectingDump_; }
  // Called when the app stops: a pending expect-window then fails the run (FAIL line, failed()).
  void failIfPending(const char* reason);

  // All commands done, including a trailing wait.
  bool finished() const {
    return next_ >= commands_.size() && !pendingScreenshot_ && waitUntil_ < 0 && !expecting_ && !expectingSprite_ && !expectingLua_ && !expectingDump_;
  }
  bool failed() const { return failed_; }
  // Set by the `quit` command: the app stops directly (a window-close event would open the quit dialog).
  bool quitRequested() const { return quitRequested_; }
  // Set after a `screenshot` or `screenshot-logical` command; the app takes it once the frame is rendered and calls
  // screenshotDone().
  const std::string* pendingScreenshot() const { return pendingScreenshot_ ? &screenshotPath_ : nullptr; }
  bool pendingScreenshotIsLogical() const { return screenshotLogical_; }
  void screenshotDone() { pendingScreenshot_ = false; }

 private:
  std::vector<ScriptCommand> commands_;
  std::size_t next_ = 0;
  platform::InputInjector& input_;
  double waitUntil_ = -1;
  bool pendingScreenshot_ = false;
  bool screenshotLogical_ = false;
  bool failed_ = false;
  const ui::WindowQuery* windows_ = nullptr;
  std::function<std::string()> extraDump_;
  std::function<void(const std::string&)> luaRunner_;
  bool expecting_ = false;  // an expect-window is waiting for its window
  std::string expectName_;
  std::function<bool(const std::string&)> luaEval_;
  std::function<void()> diagnostics_;
  bool expectingLua_ = false;  // an expect-lua expression is waiting to become true
  bool expectingDump_ = false;  // an expect-dump text is waiting to appear in the window dump
  double autoshootUntilMs_ = -1;  // autoshoot: clicks at WindowQuery::autoAim() every frame until this clock time
  bool expectingSprite_ = false;  // an expect-sprite is waiting for enough sprites
  int expectCount_ = 1;
  double expectDeadlineMs_ = 0;
  std::uint64_t frames_ = 0;  // update() calls, for the log
  bool quitRequested_ = false;
  std::string screenshotPath_;
};

}  // namespace choco::app
