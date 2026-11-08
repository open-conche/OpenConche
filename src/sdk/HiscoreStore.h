#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace choco::sdk {

// One stored score (docs/re/ledger-and-game-natives.md 8.1).
struct HiscoreRow {
  int rank = 1;  // tied scores share a rank and the next rank skips (1, 2, 2, 4)
  long long score = 0;
  std::string user;
  std::string a;  // game data "R-WWWWWWWWW-MMMMMMMMMMMM"
  std::string b;  // medals XML
};

// The local high-score tables (TPfHiscores), one per game mode. Rows are sorted by score descending; a new row goes after
// every existing row whose score is greater than or equal to it. At most kMaxRows per table, one row per user. Pure data:
// UserStore persists it as plain text in user:hiscore.dat (serialize and parse).
class HiscoreStore {
 public:
  static constexpr std::size_t kMaxRows = 500;

  // RecordScore with replaceOwn: scores below 1 are skipped; a user who already has a row with a score >= this one changes
  // nothing; a higher score removes the user's old row first. Returns true when the table changed.
  bool record(const std::string& mode, std::string_view user, double score, std::string_view a, std::string_view b);

  // The mode's rows, best first (empty for an unknown mode).
  const std::vector<HiscoreRow>& rows(const std::string& mode) const;
  // GetUserEligibleScore: the user has a row in the mode's table.
  bool hasUser(const std::string& mode, std::string_view user) const;

  // Plain text: "choco3-hiscores 1", then one "mode<TAB>score<TAB>user<TAB>a<TAB>b" line per row, escaped like the prefs.
  std::string serialize() const;
  // Replaces the content; unreadable lines are skipped. Applies the same rules as record (sorting, cap, ranks).
  void parse(const std::string& text);

 private:
  using Table = std::vector<HiscoreRow>;
  static void rerank(Table& t);
  static bool insert(Table& t, HiscoreRow row);  // false when the table is full and the row ranks below it
  std::map<std::string, Table> tables_;
};

}  // namespace choco::sdk
