#pragma once

// VAchievementManager and VAchievement, model only (docs/re/greatcc-boot-and-natives.md 6): achievements.xml, the check
// order, the acquired set (Progress key `achievement.<name>`) and the queue of newly acquired ones. The notification windows
// are slice 17.

#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "games/greatcc/core/Progress.h"
#include "games/greatcc/core/ValueManager.h"

namespace choco::games::greatcc {

struct Achievement {
  std::string category, name;  // the name is lowercase, the category as written
  std::string title, description, valueKey, medal;
  double threshold = 0;
  unsigned mask = 0;  // CSUtil comparison bits
  int ordinal = 0;    // 0-based, in file order across all categories
};

class AchievementManager {
 public:
  AchievementManager(Progress& progress, ValueManager& values) : progress_(progress), values_(values) {}

  // False when the file does not parse (the table is then empty).
  bool load(std::string_view xml);
  size_t size() const;
  // Category by category in string order, then achievement by name in string order: the order checks run in.
  const std::map<std::string, std::map<std::string, Achievement>>& all() const { return table_; }
  const Achievement* find(const std::string& name) const;

  bool isAcquired(const std::string& name) const { return progress_.getInt("achievement." + name, 0) == 1; }
  // CheckAllAchievements without the windows: for each achievement of `category` ("" = every category) not yet acquired whose
  // value (GetReal of its value key, which creates a missing value) satisfies its comparison: acquire it and queue it. Returns
  // the ones acquired by this call.
  std::vector<std::string> checkAll(const std::string& category);
  // Acquired and not yet shown, in acquisition order.
  const std::vector<std::string>& pending() const { return pending_; }
  std::vector<std::string> takePending();

  bool acquire(const std::string& category, const std::string& name);  // false when unknown
  void acquireAll();
  // Sets the achievement's value to 0 (saved) and clears it.
  bool release(const std::string& category, const std::string& name);
  void releaseAll();
  std::vector<std::string> acquiredNames() const;  // in table order
  // The medals of the acquired achievements: `<medal name="..." per="type"/>\r\n` each.
  std::string medalXml() const;

  // A new user: the queue of the previous one goes.
  void reload() { pending_.clear(); }

 private:
  Progress& progress_;
  ValueManager& values_;
  std::map<std::string, std::map<std::string, Achievement>> table_;
  std::vector<std::string> pending_;
};

}  // namespace choco::games::greatcc
