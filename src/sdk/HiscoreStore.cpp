#include "sdk/HiscoreStore.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "sdk/Users.h"

namespace choco::sdk {

namespace {
const std::vector<HiscoreRow> kNone;
constexpr const char* kHeader = "choco3-hiscores 1";
}  // namespace

void HiscoreStore::rerank(Table& t) {
  for (std::size_t i = 0; i < t.size(); ++i)
    t[i].rank = (i > 0 && t[i].score == t[i - 1].score) ? t[i - 1].rank : static_cast<int>(i) + 1;
}

bool HiscoreStore::insert(Table& t, HiscoreRow row) {
  std::size_t pos = 0;
  while (pos < t.size() && t[pos].score >= row.score) ++pos;
  if (pos >= kMaxRows) return false;
  t.insert(t.begin() + static_cast<std::ptrdiff_t>(pos), std::move(row));
  if (t.size() > kMaxRows) t.resize(kMaxRows);
  rerank(t);
  return true;
}

bool HiscoreStore::record(const std::string& mode, std::string_view user, double score, std::string_view a,
                          std::string_view b) {
  if (!(score >= 1)) return false;  // zero, negative and NaN scores are skipped
  HiscoreRow row;
  row.score = static_cast<long long>(std::floor(std::min(score, 9e15)));
  row.user = std::string(user);
  row.a = std::string(a);
  row.b = std::string(b);
  Table& t = tables_[mode];
  Table backup;
  bool removed = false;
  for (std::size_t i = 0; i < t.size(); ++i) {
    if (t[i].user != row.user) continue;
    if (t[i].score >= row.score) return false;
    backup = t;
    t.erase(t.begin() + static_cast<std::ptrdiff_t>(i));
    removed = true;
    break;
  }
  if (insert(t, std::move(row))) return true;
  if (removed) t = std::move(backup);  // cannot happen once the user's own row made room, kept for safety
  if (t.empty()) tables_.erase(mode);
  return false;
}

const std::vector<HiscoreRow>& HiscoreStore::rows(const std::string& mode) const {
  auto it = tables_.find(mode);
  return it == tables_.end() ? kNone : it->second;
}

bool HiscoreStore::hasUser(const std::string& mode, std::string_view user) const {
  for (const HiscoreRow& r : rows(mode))
    if (r.user == user) return true;
  return false;
}

std::string HiscoreStore::serialize() const {
  std::string out = std::string(kHeader) + "\n";
  for (const auto& [mode, t] : tables_)
    for (const HiscoreRow& r : t)
      out += UserStore::escape(mode) + "\t" + std::to_string(r.score) + "\t" + UserStore::escape(r.user) + "\t" +
             UserStore::escape(r.a) + "\t" + UserStore::escape(r.b) + "\n";
  return out;
}

void HiscoreStore::parse(const std::string& text) {
  tables_.clear();
  std::size_t i = 0;
  bool first = true;
  while (i < text.size()) {
    std::size_t e = text.find('\n', i);
    if (e == std::string::npos) e = text.size();
    std::size_t end = e;
    if (end > i && text[end - 1] == '\r') --end;
    const std::string line = text.substr(i, end - i);
    i = e + 1;
    if (first) {
      first = false;
      if (line != kHeader) return;
      continue;
    }
    std::vector<std::string> f;
    std::size_t s = 0;
    for (int k = 0; k < 4; ++k) {
      const std::size_t tab = line.find('\t', s);
      if (tab == std::string::npos) break;
      f.push_back(line.substr(s, tab - s));
      s = tab + 1;
    }
    if (f.size() != 4) continue;
    f.push_back(line.substr(s));
    HiscoreRow row;
    row.score = std::strtoll(f[1].c_str(), nullptr, 10);
    if (row.score < 1) continue;
    row.user = UserStore::unescape(f[2]);
    row.a = UserStore::unescape(f[3]);
    row.b = UserStore::unescape(f[4]);
    Table& t = tables_[UserStore::unescape(f[0])];
    bool dup = false;  // one row per user: a hand-edited file keeps the user's first (best, as written) row
    for (const HiscoreRow& r : t) dup = dup || r.user == row.user;
    if (!dup) insert(t, std::move(row));
  }
}

}  // namespace choco::sdk
