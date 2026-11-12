#pragma once

// VQuestManager and VQuest (docs/re/greatcc-meta-flow.md 3): the challenges of locations/quests.lua. Each has a level range, a value of
// the value manager compared with a threshold, and a reward (an upgrade activated or revealed, or money). The manager knows which are
// active for the level being played, checks them when a level is passed, writes the text the results screen shows and keeps each
// quest's saved status in Progress (`quest.<name>`: 0 in progress, 1 complete, 2 failed). Pure model: the quest panel is VQuestWindow.

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "games/greatcc/core/Progress.h"
#include "games/greatcc/core/UpgradeManager.h"
#include "games/greatcc/core/ValueManager.h"
#include "ui/Style.h"

namespace choco::games::greatcc {

// How a quest shows its progress: kProgressDisplay "%i/%i units", kGoalDisplay "%i units" (the threshold; "$%i" with format "currency"),
// kBooleanDisplay "In Progress".
enum QuestDisplay { kProgressDisplay = 0, kGoalDisplay = 1, kBooleanDisplay = 2 };
// The status a quest is saved with and the live status it shows while the level runs (the same three values).
enum QuestStatus { kQuestPending = 0, kQuestComplete = 1, kQuestFailed = 2 };

struct Quest {
  int slot = 0;
  std::string key;        // the table key, lower-cased
  std::string name;       // the string key of the title
  std::string units;      // the text after the number (already looked up)
  int minLevel = 0, maxLevel = 0;
  float threshold = 0;
  unsigned mask = 0;      // CSUtil comparison bits
  std::string compValue;  // the value read as the left operand
  std::string reward, category, rewardType = "activate";  // lower case
  int display = kProgressDisplay;
  std::string format;
  int status = kQuestPending;  // the saved status

  bool complete() const { return status == kQuestComplete; }
  // VQuest::IsReady: in range, and not yet completed (a failed quest comes back at every replay).
  bool isReady(int level) const { return level >= minLevel && level <= maxLevel && !complete(); }
};

class QuestManager {
 public:
  // The string table: TStringTable::GetString(key, args...).
  using Text = std::function<std::string(const std::string& key, const std::vector<std::string>& args)>;

  QuestManager(Progress& progress, ValueManager& values, UpgradeManager& upgrades, Text text)
      : progress_(progress), values_(values), upgrades_(upgrades), text_(std::move(text)) {}

  // LoadFromLuaFile on the `quests` table: every table-valued entry is a quest (slot = its place in the iteration, key = the
  // lower-cased key); then the player's saved statuses. Replaces what was loaded.
  void loadFromTable(const ui::Style& quests);
  // LoadCurrentUser: each quest's saved status.
  void loadCurrentUser();
  // Clear: no quests, none active.
  void clear();
  std::size_t size() const { return quests_.size(); }
  bool loaded() const { return !quests_.empty(); }
  Quest* find(const std::string& key);

  // SetCurrentLevel: the active quests are the ones that are ready for `level`; the post-level text is emptied.
  void setCurrentLevel(int level);
  int currentLevel() const { return level_; }
  // GetActiveQuests (in key order; the original keeps them in an ordered set of pointers, so its order is not defined).
  const std::vector<Quest*>& activeQuests() const { return active_; }

  // ---- what a quest says ----
  // VQuest::GetName: the title (the key looked up; a quest with no `name` gives an empty one).
  std::string name(const Quest& q) const;
  // VQuest::GetCurrentStatus: the status from the value as it is now: complete when the comparison holds and the value has reached the
  // threshold, failed when the value has reached it and the comparison fails, else pending.
  int currentStatus(const Quest& q);
  // VQuest::GetStatusInfo.
  std::string statusInfo(const Quest& q);
  // VQuest::GetPostLevelMessage (the quest has been checked): the complete text with the reward (an upgrade's header and content, or
  // "$<amount>"), or the failed text.
  std::string postLevelMessage(const Quest& q) const;

  // ---- the end of a passed level ----
  // VQuest::CheckQuest: a quest not yet complete reads its value (a missing one is made, as GetManagedReal does), compares it to the
  // threshold in single precision and becomes complete or failed; on completion the reward is applied by its type: `reveal` shows
  // the upgrade, `activate` enables it (and saves it), `money` adds the amount to current_worth; any other type does nothing.
  void checkQuest(Quest& q);
  // PostLevelProcess: CheckActiveQuests, then each active quest's status is saved and its message (when it has one) joins the
  // text, one per line. Returns the text.
  std::string postLevelProcess();
  // GetPostLevelText.
  const std::string& postLevelText() const { return postLevelText_; }

 private:
  std::string statusKey(const Quest& q) const { return "quest." + q.key; }

  Progress& progress_;
  ValueManager& values_;
  UpgradeManager& upgrades_;
  Text text_;
  std::map<std::string, std::unique_ptr<Quest>> quests_;
  std::vector<Quest*> active_;
  std::string postLevelText_;
  int level_ = 0;
};

}  // namespace choco::games::greatcc
