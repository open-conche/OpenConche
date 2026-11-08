#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "games/common/Textures.h"
#include "games/common/factory/Parts.h"
#include "sdk/Sprite.h"
#include "script/ScriptContext.h"
#include "ui/widgets/InputWidgets.h"
#include "games/common/SdkNames.h"

struct lua_State;

namespace choco::audio {
class Audio;
}
namespace choco::game {

// What a factory game reads from its style besides its own parts (docs/re/chocolate-factory.md 3.1), with Chocolatier 3's
// values as the defaults: the clock rows, the misses and the names of the Text children it keeps up to date.
struct FactoryCommon {
  bool warnOnMiss = false;
  int countdownMs = 3000, gameMs = 60000, warnMs = 8000;
  std::vector<gfx::PointF> park;        // the `missed` points; their number is the miss limit
  std::string missMarker;               // a picture put under every parked ingredient; empty: none
  std::string scoreChild = "score", timeChild = "time", countdownChild = "countdown";
  // The gun aimer and the plain arrow pictures (hotspots 16,16 and 1,1). Chocolatier 1 and 3 keep them under cursor/
  // (facts: choc1-factory.md 197-209, chocolate-factory.md 133-134); Chocolatier 2's art tree has them under image/
  // (image/gun_aimer, image/cursor - the assets' location; the packed exe's path is inference).
  std::string gunCursor = "cursor/gun_aimer", plainCursor = "cursor/cursor";
  // Chocolatier 2: a reset shows the whole game time as m:ss and clears the countdown digit; the digit stays empty until the
  // countdown first crosses a whole second (2400 ms: nothing, then 2, then 1; docs/re/choc2-factory-binary.md 3).
  bool clockTexts = false;
  // Pausing the game pauses the CADI voices and the music (Chocolatier 3). Chocolatier 2's script pauses its music itself.
  bool pauseAudio = true;
  // Chocolatier 2's miss rules (docs/re/choc2-factory-binary.md 4): the tray speed loses half of its excess over 1 (not half), the
  // n-th miss plays failure sound n + 1 (the last one plays `snatch`), and a parked ingredient keeps its full size.
  bool choc2Misses = false;
  float missScale = 0.6f;
};

// What both factory mini-games share (docs/design/factories.md 3.3, docs/re/chocolate-factory.md 4): the 10 ms step as a
// catch-up window animation, the state machine's clock rows, the pause and resume with the `information` overlay, the
// misses, the score and the four natives' targets. A subclass owns its parts and composes its own step from the
// helpers here. The window's sprites live in `root_`, drawn after the Text children.
class FactoryWindow : public ui::BSGWindow {
 public:
  enum class State { Init = 0, Countdown = 1, Playing = 2, Ending = 3, Aborted = 4, Over = 5 };
  static constexpr double kStepMs = 10;
  static constexpr int kStepInt = 10;

  // Fills a sibling window with a script (FillWindow semantics): the window's children are destroyed, then `file` runs with
  // the window as the one being built. `pauseText`, when given, is first stored in the global gPauseText. `L` is the
  // caller's Lua state. Supplied by the script side; tests record the calls.
  using FillFn = std::function<void(lua_State* L, ui::Window& target, const std::string& file, const std::string* pauseText)>;

  FactoryWindow(ui::WindowManager& wm, script::ScriptContext& ctx, FillFn fill);
  ~FactoryWindow() override;

  // The newest live factory window: the target of StartGame, PauseGame, ResumeGame and GetScore. Null when none exists.
  static FactoryWindow* active();

  // StartGame / PauseGame([key]) / ResumeGame (CF 4.1). `L` is the calling Lua state.
  void startGame(lua_State* L);
  void pauseGame(lua_State* L, const std::string& key);
  void resumeGame(lua_State* L);
  // The app lost or regained the foreground: PauseGame("game_paused") either way (idempotent). A finished game stays as is.
  void focusChanged();
  unsigned score() const { return score_; }

  // ---- ui::Window ----
  void init(const ui::Style& style) override;
  void postChildrenInit(const ui::Style& style) override;
  void draw(ui::DrawContext& ctx) const override;  // the timer dial, under the Text children
  void postDraw(ui::DrawContext& ctx) const override;
  bool onMouseMove(int x, int y) override;
  bool onMouseDown(int x, int y) override;
  bool onKeyDown(const ui::KeyEvent& ev) override;
  bool onAnimate() override {
    stepOnce();
    return true;
  }
  const Sprite* spriteRoot() const override { return root_.get(); }
  std::string detail() const override;

  // From World::tick: the sprite scripts (gun, sparkle, recycler) and the layer re-sort.
  void frame(double nowMs);

  // ---- state, for tests and the dump ----
  State state() const { return state_; }
  bool paused() const { return paused_; }
  unsigned bonus() const { return bonus_; }
  unsigned misses() const { return misses_; }
  int clockMs() const { return clock_; }
  const std::string& cursor() const { return cursor_; }
  const std::vector<Ingredient*>& parked() const { return missed_; }
  Sprite& spriteTree() { return *root_; }

 protected:
  // ---- the per-factory pieces ----
  virtual void enterState(State s) = 0;       // SetGameState's table
  virtual void resetParts() = 0;              // every task Reset(1.0), everything cleared
  virtual void pauseParts() = 0;
  virtual void restartParts() = 0;
  virtual bool fire(gfx::PointF target) = 0;  // PLAYING and not paused only
  virtual void aim(gfx::PointF mouse) = 0;
  virtual ScalingTask& speedTask() = 0;       // trays / grid: halved on a miss
  virtual const char* firstMissKey() const = 0;
  virtual const char* gameOverFile() const = 0;
  virtual void stepOnce() = 0;                // one 10 ms step
  virtual const char* introSound() const = 0;

  // ---- helpers the subclasses compose ----
  static FactoryCommon parseCommon(const ui::Style& style);  // warnOnMiss, countdowntime, gametime, warntime, missed
  void applyCommon(const FactoryCommon& c);
  void readCommon(const ui::Style& style) { applyCommon(parseCommon(style)); }
  void buildTimer();                          // the face and the hand (a game without a dial does not call it)
  // The COUNTDOWN and PLAYING rows of the state machine (CF 4.2): digits, bell, whistle, hand, m:ss, and the changes to
  // PLAYING and ENDING.
  void stepClock();
  void setState(State s);                     // enterState(s), then state_ = s
  void resetGame();                           // CF 4.1 ResetGame
  void missed(Ingredient& ing);               // CF 5.4
  void addScore(unsigned n, bool bonus);
  // The game-over overlay. A game with another way to say the game is over replaces it.
  virtual void showGameOver();
  // Plays the Chocolatier 3 event `name`. A game with its own sound files maps it.
  virtual void sound(std::string_view name) const;
  // A fresh pause (not one that found the game paused) has paused the parts: `key` is its text key. Default nothing.
  virtual void notifyPaused(lua_State* L, const std::string& key) { (void)L, (void)key; }
  // The gun aimer (16,16) and the arrow (1,1): facts of both engines (docs/re/chocolate-factory.md 133-134,
  // docs/re/choc-factory-game.md 130/223). Asks the app's cursor service; without it (unit tests) logs once per name.
  void setCursor(const char* name);
  void setText(const std::string& child, const std::string& text);
  static std::string clockText(int ms);       // m:ss, rounded up to whole seconds
  static std::string clockTextFloor(int ms);  // m:ss, rounded down (Chocolatier 2)
  ui::Window* sibling(const char* name) const;
  void showButtons(bool go, bool pause);      // the Go and Pause buttons: shown or hidden
  lua_State* mainState() const { return ctx_.lua.L(); }
  // The Lua state to run callbacks on: the calling native's while one runs, else the main state.
  lua_State* luaForStep() const { return callerState_ ? callerState_ : mainState(); }

  script::ScriptContext& ctx_;
  Textures tex_;
  std::unique_ptr<Sprite> back_;  // the timer dial: drawn before the Text children so the score shows on it
  std::unique_ptr<Sprite> root_;  // declared first: the parts of a subclass point into it
  std::vector<Ingredient*> missed_;
  std::vector<gfx::PointF> park_;  // the `missed` points; its size is the miss limit
  FactoryCommon common_;
  State state_ = State::Init;
  bool paused_ = false, warnOnMiss_ = false;
  unsigned score_ = 0, bonus_ = 0, misses_ = 0;
  int clock_ = 0, gameMs_ = 60000, countdownMs_ = 3000, warnMs_ = 8000;
  Sprite* face_ = nullptr;
  Sprite* hand_ = nullptr;
  lua_State* callerState_ = nullptr;  // the Lua state of the native that is running, else null
  std::string cursor_ = "cursor";

 private:
  void fillSibling(lua_State* L, ui::Window& target, const char* file, const std::string* pauseText);
  FillFn fillFn_;
  // The audio service, taken at construction: the destructor must not go through ctx_, which is destroyed before the windows.
  audio::Audio* audio_ = nullptr;
};

}  // namespace choco::game
