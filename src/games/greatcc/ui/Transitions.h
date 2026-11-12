#pragma once

// The transition manager of The Great Chocolate Chase (docs/re/greatcc-ui-library.md 3, docs/design/greatcc.md 3.2): one
// current overlay, a FIFO of pending ones, a retired list, and a 16 ms monitor. An overlay's constructor does the work the
// original does at construction time (retarget the bounce windows, start the tint fade-out, capture and hide the window)
// whether or not it is current yet; the queue only delays its timer, its Lua string and the resume of the waiting coroutine.
// The manager is owned by the world; it reaches the window manager and the script host through the context.

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "script/ScriptContext.h"
#include "script/ScriptHost.h"
#include "sdk/Ticker.h"
#include "ui/DrawContext.h"
#include "ui/Input.h"
#include "ui/Message.h"

namespace choco::games::greatcc {

// RUtil::DoLuaString: the text becomes a command-queue entry, run the next time the script waits for a message.
void doLuaString(script::ScriptContext& ctx, const std::string& text);

// scripts/transitions.lua
enum TransitionId {
  kFadeToBlack = 1001,
  kFadeFromBlack = 1002,
  kFadeToWhite = 1003,
  kFadeFromWhite = 1004,
  kCrossFadeIn = 1005,
  kCrossFadeOut = 1006,
  kBounceBack = 1007,
  kBounceThrough = 1008,
  kBounceClock = 1009,
  kBounceCounter = 1010,
  kTintOff = 1011,
};
constexpr int kFirstTransitionMessage = 1001;
constexpr int kLastTransitionMessage = 1013;  // 1012 and 1013 are consumed and create nothing

// ROverlay: the common part. Time advances in draw() only, the first draw contributes nothing.
class Overlay {
 public:
  virtual ~Overlay() = default;
  // One draw of the overlay at script time `nowMs`. The base only accounts time; subclasses draw and set `done`.
  virtual void draw(ui::DrawContext& ctx, double nowMs);
  float progress() const;  // min(1, elapsed / duration)

  int id = 0;
  std::string luaString;                              // run through the command queue when the overlay finishes
  std::optional<script::ScriptHost::Suspension> waiter;  // the coroutine DoTransition suspended, resumed with true
  double durationMs = 500;
  double elapsedMs = 0;
  bool done = false;

 protected:
  void account(double nowMs);

 private:
  bool started_ = false;
  double lastMs_ = 0;
};

class TransitionManager {
 public:
  explicit TransitionManager(script::ScriptContext& ctx);

  // GetTransition(id, luaString): the overlay for 1001..1011, constructed (with its side effects); null for any other id.
  std::unique_ptr<Overlay> make(int id, std::string luaString);
  // DoTransition(overlay): current when there is none (true), else appended to the pending queue (false).
  bool submit(std::unique_ptr<Overlay> overlay);
  // The message entry: types 1001..1013 build a transition with the message's name as its Lua string. True = consumed.
  bool onMessage(const ui::Message& m);
  // The input filter (design X21): mouse and key events are consumed while a transition is current or pending; text input is
  // not.
  bool handleEvent(const ui::InputEvent& ev) const;
  // The platform task: every 16 ms of `nowMs`, runs monitor(). Call it each frame.
  void tick(double nowMs);
  // MonitorTransition: drop the retired overlays; if the current one is done run its Lua string, resume its coroutine with
  // true, retire it and start the next pending one.
  void monitor();
  // The overlay drawer (after the modal stack): draws the current overlay, if any.
  void draw(ui::DrawContext& ctx);

  bool active() const { return current_ != nullptr || !pending_.empty(); }
  const Overlay* current() const { return current_.get(); }
  std::size_t pendingCount() const { return pending_.size(); }
  std::size_t retiredCount() const { return retired_.size(); }

 private:
  script::ScriptContext& ctx_;
  std::unique_ptr<Overlay> current_;
  std::vector<std::unique_ptr<Overlay>> pending_;  // FIFO: erase(begin()) pops
  std::vector<std::unique_ptr<Overlay>> retired_;
  sdk::Ticker ticker_;
};

}  // namespace choco::games::greatcc
