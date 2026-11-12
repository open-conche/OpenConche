#pragma once

// ShopWindow::OnTaskAnimate's state machine (docs/re/greatcc-shop-simulation.md 2.3), the order of its phases and the flags it
// keeps, with everything it touches behind an interface so the order is testable on its own.

namespace choco::games::greatcc {

// What the tick reads and does. The shop window implements it over the real objects; the tests over a recorder.
class ShopTickEnv {
 public:
  virtual ~ShopTickEnv() = default;
  // VSpriteWindow::GetElapsed: the milliseconds since the sprite manager's last update.
  virtual int windowElapsedMs() = 0;
  virtual bool hasLevel() = 0;
  virtual bool tutorialInEffect() = 0;
  virtual bool showTutorials() = 0;
  virtual bool hasTimer() = 0;
  virtual float percentElapsed() = 0;
  virtual bool levelInLockDown(float percent) = 0;
  virtual void setLockDown(bool on) = 0;
  // The level starts: the timer runs from now, the soft pause and the lockdown end, the tutorial trigger endless_start fires.
  virtual void startLevel() = 0;
  virtual void updateCustomers() = 0;
  virtual void updateHud() = 0;
  // CSLevel::NeedPrelevel; running the pre-level dialog also clears the level's flag.
  virtual bool levelNeedsPrelevel() = 0;
  virtual void showPrelevel() = 0;
  virtual bool levelNeedsPlot() = 0;
  virtual void launchPlotDialog() = 0;
  virtual int timerElapsedMs() = 0;
  virtual int timerDurationMs() = 0;
  virtual bool activeCustomersEmpty() = 0;
  virtual bool pendingCustomersEmpty() = 0;
  virtual void updateEndlessMode() = 0;
  virtual unsigned angryThisLevel() = 0;
  virtual unsigned angryLimit() = 0;
  virtual void doEndLevel() = 0;
};

// The flags of the window the tick keeps (field offsets of ShopWindow in the fact file).
struct ShopTickState {
  int startCountdownMs = 0;      // 0xf8: the pre-level phase
  bool waitingToStart = false;   // 0xfc
  bool endless = false;          // 0x1c4
  bool prelevelPending = false;  // 0x1de
  bool plotPending = false;      // 0x1dd
  int levelOverStamp = 0;        // 0x1d0: the timer's elapsed ms when the end was noticed, 0 while running
  bool waitingOnZeppelin = false;  // 0x1d4
};

// The delay between noticing the end and ending the level, in timer milliseconds.
constexpr int kEndLevelDelayMs = 2000;

enum class TickResult {
  Continue,  // the sprite window's own update follows
  Stopped,   // DoEndLevel ran: the animation task ends and no sprite update follows
};

TickResult runShopTick(ShopTickState& s, ShopTickEnv& env);

}  // namespace choco::games::greatcc
