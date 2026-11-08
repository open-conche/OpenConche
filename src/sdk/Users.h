#pragma once

#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "sdk/HiscoreStore.h"

namespace choco::sdk {

// Players, their save string and their preferences, kept in the engine's own plain-text format under the user
// directory (the original's encrypted prefs.dat is not reproduced). Layout below `root`:
//   users.txt                    "choco3-users 1", "current <index>", then one "folder<TAB>name" line per user, in order
//   prefs.txt                    global key/value preferences (language, fullscreen, ...)
//   profiles/<folder>/save.txt   the SaveGameString payload of that user
//   profiles/<folder>/prefs.txt  that user's preferences (sound levels, mute, ...)
//   hiscore.dat                  the local high-score tables (HiscoreStore::serialize), plain text
// Values are escaped (see escape()): tab, newline, '%' and control bytes become %XX. Every write goes to a temporary
// file next to the target and is renamed over it, so a crash leaves either the old or the new file.
// With an empty root nothing touches the disk and the store only lives in memory.
// Indices are 0-based; -1 means "no current user".

class UserStore {
 public:
  explicit UserStore(std::string root);

  int count() const { return static_cast<int>(users_.size()); }
  int current() const { return current_; }
  // "" for an index out of range.
  std::string name(int index) const;
  std::string currentName() const { return name(current_); }
  // Exact, case-sensitive.
  bool nameInUse(std::string_view name) const;

  // Display names must be valid UTF-8, 1 to 255 bytes and free of control characters.
  static bool validName(std::string_view name);
  // A Windows-safe folder name for a display name: unsafe characters become '_', trailing dots and spaces are dropped,
  // reserved device names are suffixed, at most 40 bytes (on a UTF-8 boundary). Never empty.
  static std::string sanitiseFolder(std::string_view name);

  // Adds a user, copies the current user's sound prefs to it and makes it current. Returns the new index, or -1 for an
  // invalid name. Duplicates are allowed (the scripts check IsNameInUse first).
  int create(std::string_view name);
  // False (ignored) for an index >= count or < -1.
  bool setCurrent(int index);
  bool rename(int index, std::string_view name);
  // Removes the user and its folder, then selects index-1, or the last user if that is < 0, or none.
  bool remove(int index);

  // The current user's save string; "" with no user or no save.
  // `slot` 0 is save.txt, which is all Chocolatier 3 has; slot 1 is save_free.txt (Chocolatier 2's Free Play game).
  std::string loadSave(int slot = 0) const;
  bool saveSave(std::string_view data, int slot = 0);  // false (and nothing written) with no current user

  // Per-user prefs fall back to the defaults (sound levels), then to `def`. With no current user they read and write
  // the global prefs.
  std::string pref(const std::string& key, const std::string& def = {}) const;
  bool setPref(const std::string& key, const std::string& value);
  std::string globalPref(const std::string& key, const std::string& def = {}) const;
  bool setGlobalPref(const std::string& key, const std::string& value);
  // The game's default sound levels, per mille (GameProfile::volumeDefaults); the built-in ones are 750/650/500.
  void setVolumeDefaults(int sfx, int music, int ambient) {
    defaults_["soundlevel"] = std::to_string(sfx);
    defaults_["musiclevel"] = std::to_string(music);
    defaults_["ambientlevel"] = std::to_string(ambient);
  }

  // LogScore: records the score for the current user in the current game mode's table (HiscoreStore::record rules) and
  // rewrites hiscore.dat when it changed. Nothing is recorded with no current user. True when nothing needed writing or the write worked.
  bool logScore(double score, std::string_view a, std::string_view b);
  const HiscoreStore& hiscores() const { return hiscores_; }
  // The table the current user's scores go to: the "gamemode" pref, "0" (Story) when unset.
  std::string hiscoreMode() const { return pref("gamemode", "0"); }
  // ScoreAvailable: the current user has a row in the current mode's table.
  bool scoreAvailable() const { return hiscores_.hasUser(hiscoreMode(), currentName()); }

  // Replaces a file below the root through a temporary file and a rename. `rel` must already be a safe relative path.
  bool writeFile(const std::string& rel, std::string_view data) const;

  // File names (relative to the root, top level only) ending in `suffix`, sorted.
  std::vector<std::string> listFiles(std::string_view suffix) const;

  // Iterator over listFiles(".choco3") with the extension stripped; "" when exhausted, which also resets it.
  std::string nextSaveName();

  const std::string& root() const { return root_; }
  // The folder name of a user ("" out of range), not its full path.
  std::string folder(int index) const;

  static std::string escape(std::string_view s);
  static std::string unescape(std::string_view s);

 private:
  struct User {
    std::string folder, name;
    std::map<std::string, std::string> prefs;
  };

  std::string defaultOf(const std::string& key) const;  // the override, else the built-in default; "" when none
  std::map<std::string, std::string> defaults_;
  bool persistent() const { return !root_.empty(); }
  bool writeAtomic(const std::string& rel, std::string_view data) const;
  bool readWhole(const std::string& rel, std::string& out) const;
  bool saveIndex() const;
  bool savePrefs(const User& u) const;
  bool saveGlobalPrefs() const;
  void load();
  std::string uniqueFolder(std::string_view name) const;

  std::string root_;
  std::vector<User> users_;
  int current_ = -1;
  std::map<std::string, std::string> global_;
  HiscoreStore hiscores_;
  std::vector<std::string> saveIter_;
  bool iterating_ = false;
  std::size_t iterPos_ = 0;
  std::map<std::string, std::string> memSaves_;  // by folder, only when not persistent
};

}  // namespace choco::sdk
