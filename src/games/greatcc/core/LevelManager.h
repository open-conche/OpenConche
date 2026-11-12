#pragma once

// VLevelManager (docs/re/greatcc-boot-and-natives.md 5.6, docs/re/greatcc-shop-simulation.md 3.2 and 7): levels 1..60 with a
// goal and a mastery score each (levels/goals.xml), the current and highest level (the saved values `current_level` and
// `highest_level`), per level a status (0 none, 1 goal, 2 mastered) and best score, and one spendable cash per location.
// Status and best live in Progress as `level.<n>` = "status,best", the cash as `cash.<location>`.

#include <string>
#include <string_view>
#include <vector>

#include "games/greatcc/core/Progress.h"
#include "games/greatcc/core/ValueManager.h"

namespace choco::games::greatcc {

class LevelManager {
 public:
  static constexpr int kLevels = 60;
  static constexpr int kLevelsPerLocation = 10;

  LevelManager(Progress& progress, ValueManager& values) : progress_(progress), values_(values) {}

  // levels/goals.xml: <levelgoals><levelgoal level= goal= mastery=/>. False when it does not parse.
  bool loadGoals(std::string_view xml);
  // 0 for a level outside 1..60 or without a row.
  int goal(int level) const;
  int mastery(int level) const;

  int currentLevel() { return values_.getInt("current_level", 0); }
  void setCurrentLevel(int level) { values_.setValue("current_level", level, true); }
  int highestComplete() { return values_.getInt("highest_level", 0); }
  // Unless `force`, only raises the highest level. Then every level at or below `n` with status 0 becomes 1 and every level
  // above it with a status goes back to 0.
  void setHighestComplete(int n, bool force);
  // When the current level has a status: SetHighestComplete(current), then the current level moves to the next.
  void incrementLevel();

  int status(int level) const;
  double bestScore(int level) const;
  int currentResult() { return status(currentLevel()); }

  struct Outcome {
    int result = 0;      // 0 not passed, 1 goal reached, 2 mastered
    double delta = 0;    // the money this attempt credits
  };
  // LevelComplete(score): result = score < mastery ? (score < goal ? 0 : 1) : 2. The credit is the whole score when the level
  // had no status before, else the improvement over the stored best (never negative). The status only rises; the best score
  // keeps the maximum (failed runs too). A level outside 1..60 (no level object) gives result 0, delta 0 and changes nothing.
  Outcome levelComplete(int level, double score);

  // The location of a level (1-10 iquitos, 11-20 grytviken, 21-30 namche, 31-40 krakatoa, 41-50 kamchatka, 51-60 gaberoun).
  static const char* locationOf(int level);
  // The location's spendable cash (level_score[(level-1)/10]).
  double locationCash(int level) const;
  double currentScore() { return locationCash(currentLevel()); }
  void updateCurrentScore(double v);
  // UpdateLevelScore: the cash of the location that holds `level` (nothing for a level outside 1..60).
  void updateLevelScore(int level, double v);

 private:
  struct Entry {
    int goal = 0, mastery = 0;
  };
  void write(int level, int status, double best);

  Progress& progress_;
  ValueManager& values_;
  std::vector<Entry> goals_ = std::vector<Entry>(kLevels + 1);
};

}  // namespace choco::games::greatcc
