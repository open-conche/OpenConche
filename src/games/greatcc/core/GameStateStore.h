#pragma once

// TGameState (docs/re/greatcc-boot-and-natives.md 1.6): a string-to-string map the game raises events into. The machines and stations
// write it (`++TrashUsed`, `++DisplayUsed`, `TrashUsedItemID`); the tutorials write their own (`TutorialEnd`, `TutorialTime_<n>`) and nothing
// reads them, so the store only keeps the strings. ShopWindow's level setup starts the level's states over (InitMetrics). Observers that rewrite a read, and the timers the Lua `SetState` drives, are not here.

#include <cstdlib>
#include <map>
#include <string>

namespace choco::games::greatcc {

class GameStateStore {
 public:
  // SetState: a key that starts with `++` adds one to the integer held under the rest of the key (a missing or non-numeric state
  // counts as 0) and stores that; `value` is not used then. Any other key stores `value`.
  void set(const std::string& key, const std::string& value = {}) {
    if (key.compare(0, 2, "++") == 0) {
      const std::string rest = key.substr(2);
      const auto it = states_.find(rest);
      const long n = it == states_.end() ? 0 : std::strtol(it->second.c_str(), nullptr, 10);
      states_[rest] = std::to_string(n + 1);
    } else {
      states_[key] = value;
    }
  }
  // GetState: the stored string, empty when the key was never set.
  std::string get(const std::string& key) const {
    const auto it = states_.find(key);
    return it == states_.end() ? std::string() : it->second;
  }
  void clear() { states_.clear(); }
  // PLAY_COUNTER: bumped once per level load (InitMetrics); the tutorial's timer states are named after it.
  int playCounter() const { return playCounter_; }
  int nextPlay() { return ++playCounter_; }

 private:
  std::map<std::string, std::string> states_;
  int playCounter_ = 0;
};

}  // namespace choco::games::greatcc
