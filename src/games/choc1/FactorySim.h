#pragma once

// The rules of Chocolatier 1's factory mini-game (the exe's TFactoryGame; docs/re/choc1-factory.md). Plain logic: no window,
// no renderer, no Lua and no clock of its own. The caller feeds it one fixed 10 ms tick at a time (tick()), the mouse
// (aim / click) and TogglePause, and reads the state and the events (sounds and screen changes) back. The window around it is
// games/choc1/FactoryGame. Step numbers in the comments ("2.6 step 3") are the sections of the doc.

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace choco::game::choc1 {

// The MSVC C runtime's rand(): seed = seed * 214013 + 2531011, the result is bits 16..30. The exe never calls srand, so its
// generator starts from seed 1 (docs/re/choc1-factory.md 2.4); the engine does the same, one stream per game session.
class CrtRand {
 public:
  explicit CrtRand(std::uint32_t seed = 1) : seed_(seed) {}
  int next() {
    seed_ = seed_ * 214013u + 2531011u;
    return static_cast<int>((seed_ >> 16) & 0x7fff);
  }

 private:
  std::uint32_t seed_;
};

class FactorySim {
 public:
  using RandFn = std::function<int()>;

  enum class State { Idle = 0, Countdown = 1, Running = 2, Paused = 3, Finishing = 4, Over = 5 };

  // ---- constants (2.5, 2.6, 2.8) ----
  static constexpr int kStepMs = 10;           // the tick: dt is always 10
  static constexpr int kCountdownMs = 2400;
  static constexpr int kRunMs = 60000;
  static constexpr int kEmpty = 99;            // an empty slot
  static constexpr int kMissLimit = 8;
  static constexpr double kCentreX = 300, kCentreY = 300;  // the wheel centre and the gun pivot
  static constexpr double kTrayRadius = 225;
  static constexpr int kTraySize = 150;        // the tray pictures are 150x150
  static constexpr long kHitRadiusSq = 5881;
  static constexpr int kCollectY = 252;
  static constexpr int kBeltY = 571;           // the belt runs (0, 571) -> (300, 571) -> (300, 300)
  static constexpr int kBeltLength = 300 + (kBeltY - 300);
  static constexpr double kOutStartY = 300, kOutEndY = 516;
  static constexpr int kPlayfieldW = 800, kPlayfieldH = 600;

  struct Config {
    int trayCount = 7;
    int ingredientRate = 350;
    double speedup = 1.1, startspeed = 1.0, maxspeed = 8.0;
    std::vector<int> quantities;  // per ingredient, in the recipe's iteration order
    bool firstMiss = false;
  };

  struct Tray {
    int left = 0, top = 0, right = 0, bottom = 0;  // the screen bounding box (ints)
    std::vector<int> slots;                        // template-length; kEmpty or the ingredient
    int filled = 0;
    double powerupMs = -1;                         // cosmetic: ms since the tray was collected, -1 = none
    int cx() const { return (left + right) / 2; }
    int cy() const { return (top + bottom) / 2; }
  };
  struct BeltNode {
    int ingredient = 0;
    int s = 0;  // pixels along the belt path
  };
  struct Shot {
    int x = 300, y = 300;
    float dx = 0, dy = 0;
    int ingredient = 0;
  };

  struct Event {
    enum class Kind {
      CountdownTick,   // countdown_tick
      StartBell,       // minigame_starting_bell
      WarningBell,     // warning_bell
      ProductionOver,  // mini_game_production_over
      Fire,            // minigame_load_launch
      Success,         // ingredient_success_0<arg> (arg 1..5)
      TemplateComplete,
      Failure,         // ingredient_failure_0<arg> (arg 1..8)
      Snatch,          // factory_snatch
      TemplateSuccess,
      Powerup,
      Ended,           // arg 1 = aborted by the miss limit: factory_shut_down, the verdict text, the end dialog
      Paused,          // arg 1 = now paused, 0 = resumed
      FirstMiss,       // the first-waste warning is due (the Lua dialog)
    };
    Kind kind;
    int arg = 0;
  };
  // The file name (under audio/sfx, no extension) the event plays, or "" for the events that are not sounds.
  static std::string soundName(const Event& e);

  FactorySim(Config cfg, RandFn rand);

  // ---- the natives' targets ----
  // 2.4 Reset (also PostInit and RestartGame): everything but the state machine's clocks.
  void reset();
  // 2.3 StartGame's body (the speed, angle, count, clocks, gun) and state 1.
  void start();
  // 2.3 RestartGame: Reset, then StartGame.
  void restart();
  // 2.3 TogglePause. Ignored in state 5.
  void togglePause();
  // 2.6 one 10 ms tick.
  void tick();
  // 2.7 aim, and the click (fires when a gun is loaded and the state is 2; true when it fired).
  void aim(int mx, int my);
  bool click(int mx, int my);

  // The events since the last call.
  std::vector<Event> takeEvents();

  // ---- state ----
  State state() const { return state_; }
  State savedState() const { return saved_; }
  int elapsedMs() const { return elapsed_; }
  int count() const { return count_; }
  double speed() const { return speed_; }
  double angle() const { return angle_; }
  int misses() const { return static_cast<int>(missed_.size()); }
  const std::vector<int>& missedIngredients() const { return missed_; }
  int loaded() const { return loaded_; }  // the ingredient in the gun, or -1
  int total() const { return static_cast<int>(template_.size()); }  // ingredients per tray
  const std::vector<int>& slotTemplate() const { return template_; }
  const std::vector<int>& queue() const { return queue_; }
  const std::vector<Tray>& trays() const { return trays_; }
  const std::vector<BeltNode>& belt() const { return belt_; }
  const std::vector<Shot>& shots() const { return shots_; }
  const std::vector<double>& products() const { return products_; }
  int spawnClock() const { return spawnClock_; }
  const Config& config() const { return cfg_; }
  bool firstMissPending() const { return cfg_.firstMiss; }
  // The countdown digit (3, 2, 1) while state 1.
  int countdownDigit() const { return digit_; }
  bool countdownShown() const { return countdownShown_; }
  // The verdict string key once the game is over (production_low ... production_stopped), else "".
  const std::string& verdict() const { return verdict_; }
  // 2.6 "the timer text": m:ss of the whole seconds left of the 60 s.
  std::string timerText() const;
  // 2.9 the verdict key for a count (no port prefix): production_stopped when aborted, else by the ratio.
  static std::string verdictKey(int count, int total, int ingredientRate, bool aborted);

  // The aim vector and gun angle of the last aim() (cosmetic: the gun sprite).
  float aimX() const { return aimX_; }
  float aimY() const { return aimY_; }
  double gunAngle() const { return gunAngle_; }
  double gunAnimMs() const { return gunAnimMs_; }  // ms since the last shot, -1 = none
  // The arm animation (2.8): its position 0..7 or -1 when idle, and the x offsets of the two arms at that position.
  double armPosition() const;
  static double armUpperOffset(double position);
  static double armLowerOffset(double position);

  // The tray's screen centre at the current angle (exact, before the bounding box's int truncation).
  struct Point { double x, y; };
  Point trayScreen(int i, double angle) const;
  // Where a click should land now to hit a tray that needs the loaded ingredient, leading the moving tray; nullopt when
  // the game is not running, the gun is empty, or no tray needs it. Used by the harness's `autoshoot`.
  std::optional<Point> bestAim() const;

 private:
  void emit(Event::Kind k, int arg = 0) { events_.push_back({k, arg}); }
  void updateBoxes();
  void rotate(int dt);
  void hitTest();
  void moveShots(int dt);
  void moveBelt(int dt);
  void conveyor(int dt);
  void armTick(int dt);
  void collect(int tray);
  void end(bool aborted);
  void buildQueue();
  bool trayNeeds(const Tray& t, int ingredient) const;

  Config cfg_;
  RandFn rand_;
  std::vector<int> template_;  // slotTemplate: the ingredient of slot j
  std::vector<int> queue_;
  std::vector<Tray> trays_;
  std::vector<BeltNode> belt_;
  std::vector<Shot> shots_;
  std::vector<int> missed_;
  std::vector<double> products_;
  std::vector<Event> events_;
  State state_ = State::Idle, saved_ = State::Idle;
  int elapsed_ = 0, count_ = 0;
  double speed_ = 1.0, decay_ = 0, angle_ = 0;
  int loaded_ = -1;
  int spawnClock_ = 0, nextSpawn_ = 0;
  int digit_ = 3;
  bool countdownShown_ = false;
  bool armActive_ = false;
  unsigned armClock_ = 0;
  float aimX_ = 0, aimY_ = 1;
  double gunAngle_ = 4.71238898038468985769 /* 3 pi / 2 */, gunAnimMs_ = -1;  // the constructor's 3 pi / 2; Reset leaves it
  std::string verdict_;
};

}  // namespace choco::game::choc1
