#pragma once

// The current player's saved state (docs/design/greatcc.md 3.4): one `greatcc-progress.txt` per user, `key=value` lines,
// keys are names ("value.total_worth", "level.3", "achievement.bars10"), never slot numbers. The managers read and write
// through it; any change marks it dirty and the world writes it once per frame (flush). With no current user the entries
// live in memory only and are dropped by the next load, as the original does not save without a user.

#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "sdk/Users.h"

namespace choco::games::greatcc {

class Progress {
 public:
  Progress() = default;
  explicit Progress(std::shared_ptr<sdk::UserStore> users) : users_(std::move(users)) {}
  void attach(std::shared_ptr<sdk::UserStore> users) { users_ = std::move(users); }

  bool hasUser() const { return users_ && users_->current() >= 0; }

  // Raw entries. Keys must not contain '=' (such a key is refused).
  bool has(const std::string& key) const { return entries_.count(key) != 0; }
  std::string get(const std::string& key, const std::string& def = {}) const;
  void set(const std::string& key, const std::string& value);
  void erase(const std::string& key);
  // Doubles are written with 17 significant digits and read back exactly.
  double getDouble(const std::string& key, double def) const;
  void setDouble(const std::string& key, double v);
  int getInt(const std::string& key, int def) const;
  void setInt(const std::string& key, int v) { set(key, std::to_string(v)); }
  std::vector<std::string> keysWithPrefix(const std::string& prefix) const;

  // The Options "Tutorials" checkbox (VTutorialManager::ShouldShowTutorials), key `tutorials.show`; on by default.
  bool showTutorials() const { return getInt("tutorials.show", 1) != 0; }
  void setShowTutorials(bool on) { setInt("tutorials.show", on ? 1 : 0); }

  // Replaces the entries with the current user's file (none: empty entries, i.e. every default). Clears the dirty flag.
  // A file that is not ours to read (a wrong or missing header line) is copied to `greatcc-progress.txt.bad` first, so
  // the next write does not destroy it; false then, and the entries are the defaults.
  bool load();
  // Writes the file when dirty and there is a current user; the dirty flag is cleared either way. False on a write failure
  // (the flag then stays set so the next frame retries).
  bool flush();
  bool dirty() const { return dirty_; }
  // Files written so far (for tests: one per frame at most).
  int writes() const { return writes_; }

  static std::string render(const std::map<std::string, std::string>& entries);
  static std::map<std::string, std::string> parse(std::string_view text);

 private:
  std::string path() const;  // relative to the user root; "" with no user

  std::shared_ptr<sdk::UserStore> users_;
  std::map<std::string, std::string> entries_;
  bool dirty_ = false;
  int writes_ = 0;
};

}  // namespace choco::games::greatcc
