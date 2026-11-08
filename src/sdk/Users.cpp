#include "sdk/Users.h"
#include "app/Diagnostics.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "platform/PathUtil.h"
#include "platform/UserDir.h"

namespace choco::sdk {

namespace fs = std::filesystem;

namespace {

constexpr const char* kIndexHeader = "choco3-users 1";

std::string asciiLower(std::string_view s) {
  std::string r(s);
  for (char& c : r) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return r;
}

bool validUtf8(std::string_view s) {
  std::size_t i = 0;
  while (i < s.size()) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    std::size_t n = 0;
    unsigned cp = 0;
    if (c < 0x80) {
      ++i;
      continue;
    } else if (c >= 0xC2 && c <= 0xDF) {
      n = 1;
      cp = c & 0x1F;
    } else if (c >= 0xE0 && c <= 0xEF) {
      n = 2;
      cp = c & 0x0F;
    } else if (c >= 0xF0 && c <= 0xF4) {
      n = 3;
      cp = c & 0x07;
    } else {
      return false;
    }
    if (i + n >= s.size()) return false;
    for (std::size_t k = 1; k <= n; ++k) {
      const unsigned char d = static_cast<unsigned char>(s[i + k]);
      if ((d & 0xC0) != 0x80) return false;
      cp = (cp << 6) | (d & 0x3F);
    }
    if ((n == 2 && cp < 0x800) || (n == 3 && (cp < 0x10000 || cp > 0x10FFFF)) || (cp >= 0xD800 && cp <= 0xDFFF))
      return false;
    i += n + 1;
  }
  return true;
}

bool reservedDeviceName(const std::string& lowerStem) {
  static const char* const names[] = {"con", "prn", "aux", "nul", "com1", "com2", "com3", "com4", "com5", "com6",
                                      "com7", "com8", "com9", "lpt1", "lpt2", "lpt3", "lpt4", "lpt5", "lpt6",
                                      "lpt7", "lpt8", "lpt9", "conin$", "conout$"};
  for (const char* n : names)
    if (lowerStem == n) return true;
  return false;
}

bool safeFolderName(const std::string& f) {
  return !f.empty() && f != "." && f != ".." && f.find_first_of("/\\:*?\"<>|") == std::string::npos &&
         f.find_first_of(std::string_view("\0", 1)) == std::string::npos;
}

std::vector<std::string> splitLines(const std::string& text) {
  std::vector<std::string> lines;
  std::size_t i = 0;
  while (i < text.size()) {
    std::size_t e = text.find('\n', i);
    if (e == std::string::npos) e = text.size();
    std::size_t end = e;
    if (end > i && text[end - 1] == '\r') --end;
    lines.push_back(text.substr(i, end - i));
    i = e + 1;
  }
  return lines;
}

std::string renderPrefs(const std::map<std::string, std::string>& m) {
  std::string out;
  for (const auto& [k, v] : m) out += UserStore::escape(k) + "\t" + UserStore::escape(v) + "\n";
  return out;
}

void parsePrefs(const std::string& text, std::map<std::string, std::string>& m) {
  m.clear();
  for (const auto& line : splitLines(text)) {
    const std::size_t tab = line.find('\t');
    if (tab == std::string::npos || tab == 0) continue;
    m[UserStore::unescape(line.substr(0, tab))] = UserStore::unescape(line.substr(tab + 1));
  }
}

// The default for a per-user pref, or nullptr.
const char* defaultFor(const std::string& key) {
  if (key == "soundlevel") return "750";
  if (key == "musiclevel") return "650";
  if (key == "ambientlevel") return "500";
  if (key == "soundoff") return "0";
  return nullptr;
}

}  // namespace

std::string UserStore::defaultOf(const std::string& key) const {
  if (auto it = defaults_.find(key); it != defaults_.end()) return it->second;
  const char* d = defaultFor(key);
  return d ? d : "";
}

std::string UserStore::escape(std::string_view s) {
  static const char* hex = "0123456789ABCDEF";
  std::string r;
  for (char ch : s) {
    const unsigned char c = static_cast<unsigned char>(ch);
    if (c < 0x20 || c == 0x7F || c == '%') {
      r += '%';
      r += hex[c >> 4];
      r += hex[c & 15];
    } else {
      r += ch;
    }
  }
  return r;
}

std::string UserStore::unescape(std::string_view s) {
  auto val = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
  };
  std::string r;
  for (std::size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '%' && i + 2 < s.size() && val(s[i + 1]) >= 0 && val(s[i + 2]) >= 0) {
      r += static_cast<char>(val(s[i + 1]) * 16 + val(s[i + 2]));
      i += 2;
    } else {
      r += s[i];
    }
  }
  return r;
}

bool UserStore::validName(std::string_view name) {
  if (name.empty() || name.size() > 255) return false;
  for (char ch : name) {
    const unsigned char c = static_cast<unsigned char>(ch);
    if (c < 0x20 || c == 0x7F) return false;
  }
  return validUtf8(name);
}

std::string UserStore::sanitiseFolder(std::string_view name) {
  std::string r;
  for (char ch : name) {
    const unsigned char c = static_cast<unsigned char>(ch);
    if (c < 0x20 || c == 0x7F || std::string_view("<>:\"/\\|?*").find(ch) != std::string_view::npos) r += '_';
    else if (ch == '.' && !r.empty() && r.back() == '.') continue;  // collapse dot runs: no ".." inside a name
    else r += ch;
  }
  while (!r.empty() && (r.back() == '.' || r.back() == ' ')) r.pop_back();
  while (!r.empty() && r.front() == ' ') r.erase(r.begin());
  if (r.size() > 40) {
    std::size_t cut = 40;
    while (cut > 0 && (static_cast<unsigned char>(r[cut]) & 0xC0) == 0x80) --cut;
    r.resize(cut);
    while (!r.empty() && (r.back() == '.' || r.back() == ' ')) r.pop_back();
  }
  if (r.empty()) r = "user";
  const std::size_t dot = r.find('.');
  if (reservedDeviceName(asciiLower(r.substr(0, dot)))) r.insert(dot == std::string::npos ? r.size() : dot, "_");
  return r;
}

UserStore::UserStore(std::string root) : root_(std::move(root)) {
  if (persistent()) load();
}

bool UserStore::writeAtomic(const std::string& rel, std::string_view data) const {
  if (!persistent()) return true;
  std::error_code ec;
  const fs::path target = platform::pathFromUtf8(root_) / platform::pathFromUtf8(rel);
  fs::create_directories(target.parent_path(), ec);
  if (ec) return false;
  fs::path tmp = target;
  tmp += ".tmp";
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    out.flush();
    if (!out) {
      out.close();
      fs::remove(tmp, ec);
      return false;
    }
  }
  fs::rename(tmp, target, ec);
  if (ec) {
    fs::remove(tmp, ec);
    return false;
  }
  platform::syncUserDir();
  return true;
}

bool UserStore::readWhole(const std::string& rel, std::string& out) const {
  std::ifstream in(platform::pathFromUtf8(root_) / platform::pathFromUtf8(rel), std::ios::binary);
  if (!in) return false;
  std::ostringstream ss;
  ss << in.rdbuf();
  out = ss.str();
  return true;
}

bool UserStore::saveIndex() const {
  std::string text = std::string(kIndexHeader) + "\ncurrent " + std::to_string(current_) + "\n";
  for (const auto& u : users_) text += u.folder + "\t" + escape(u.name) + "\n";
  return writeAtomic("users.txt", text);
}

bool UserStore::savePrefs(const User& u) const { return writeAtomic("profiles/" + u.folder + "/prefs.txt", renderPrefs(u.prefs)); }
bool UserStore::saveGlobalPrefs() const { return writeAtomic("prefs.txt", renderPrefs(global_)); }

void UserStore::load() {
  std::string text;
  if (readWhole("prefs.txt", text)) parsePrefs(text, global_);
  if (readWhole("hiscore.dat", text)) hiscores_.parse(text);
  if (!readWhole("users.txt", text)) return;
  const auto lines = splitLines(text);
  if (lines.empty() || lines[0] != kIndexHeader) return;
  // Line 2 is "current <index>"; every later line is a user, whatever the name looks like.
  if (lines.size() < 2 || lines[1].compare(0, 8, "current ") != 0) return;
  current_ = std::atoi(lines[1].c_str() + 8);
  for (std::size_t i = 2; i < lines.size(); ++i) {
    const std::string& l = lines[i];
    const std::size_t tab = l.find('\t');
    if (tab == std::string::npos) continue;
    User u;
    u.folder = l.substr(0, tab);
    u.name = unescape(l.substr(tab + 1));
    if (!safeFolderName(u.folder)) {
      const std::string bad = u.folder;
      u.folder = uniqueFolder(u.name);
      diag::err("[users] unsafe profile folder '%s' for user '%s'; re-homed to '%s'\n", bad.c_str(),
                   u.name.c_str(), u.folder.c_str());
    }
    if (readWhole("profiles/" + u.folder + "/prefs.txt", text)) parsePrefs(text, u.prefs);
    users_.push_back(std::move(u));
  }
  if (current_ < -1 || current_ >= count()) current_ = users_.empty() ? -1 : 0;
}

std::string UserStore::name(int index) const { return index >= 0 && index < count() ? users_[index].name : std::string(); }
std::string UserStore::folder(int index) const { return index >= 0 && index < count() ? users_[index].folder : std::string(); }

bool UserStore::nameInUse(std::string_view n) const {
  for (const auto& u : users_)
    if (u.name == n) return true;
  return false;
}

std::string UserStore::uniqueFolder(std::string_view name) const {
  const std::string base = sanitiseFolder(name);
  for (int n = 1;; ++n) {
    std::string cand = n == 1 ? base : base + "-" + std::to_string(n);
    const std::string lower = asciiLower(cand);
    bool clash = false;
    for (const auto& u : users_) clash = clash || asciiLower(u.folder) == lower;
    if (!clash && persistent()) {
      std::error_code ec;
      clash = fs::exists(platform::pathFromUtf8(root_) / "profiles" / platform::pathFromUtf8(cand), ec);
    }
    if (!clash) return cand;
  }
}

int UserStore::create(std::string_view n) {
  if (!validName(n)) return -1;
  User u;
  u.name = std::string(n);
  u.folder = uniqueFolder(n);
  // pref() reads the global slot when there is no current user, as the original's user -1 does.
  for (const char* k : {"soundlevel", "musiclevel", "ambientlevel", "soundoff"}) u.prefs[k] = pref(k);
  users_.push_back(std::move(u));
  current_ = count() - 1;
  savePrefs(users_.back());
  saveIndex();
  return current_;
}

bool UserStore::setCurrent(int index) {
  if (index < -1 || index >= count()) return false;
  current_ = index;
  saveIndex();
  return true;
}

bool UserStore::rename(int index, std::string_view n) {
  if (index < 0 || index >= count() || !validName(n)) return false;
  users_[index].name = std::string(n);
  return saveIndex();
}

bool UserStore::remove(int index) {
  if (index < 0 || index >= count()) return false;
  const std::string folderName = users_[index].folder;
  users_.erase(users_.begin() + index);
  memSaves_.erase(folderName);
  memSaves_.erase(folderName + "/free");
  if (persistent()) {
    std::error_code ec;
    fs::remove_all(platform::pathFromUtf8(root_) / "profiles" / platform::pathFromUtf8(folderName), ec);
  }
  int sel = index - 1;
  if (sel < 0) sel = count() - 1;  // -1 when none are left
  current_ = sel;
  saveIndex();
  return true;
}

std::string UserStore::loadSave(int slot) const {
  if (current_ < 0) return {};
  const User& u = users_[current_];
  if (!persistent()) {
    auto it = memSaves_.find(slot ? u.folder + "/free" : u.folder);
    return it == memSaves_.end() ? std::string() : it->second;
  }
  std::string s;
  readWhole("profiles/" + u.folder + (slot ? "/save_free.txt" : "/save.txt"), s);
  return s;
}

bool UserStore::saveSave(std::string_view data, int slot) {
  if (current_ < 0) return false;
  const User& u = users_[current_];
  if (!persistent()) {
    memSaves_[slot ? u.folder + "/free" : u.folder] = std::string(data);
    return true;
  }
  return writeAtomic("profiles/" + u.folder + (slot ? "/save_free.txt" : "/save.txt"), data);
}

std::string UserStore::pref(const std::string& key, const std::string& def) const {
  if (current_ < 0) return globalPref(key, def);
  const auto& p = users_[current_].prefs;
  auto it = p.find(key);
  if (it != p.end()) return it->second;
  const std::string d = defaultOf(key);
  return d.empty() ? def : d;
}

bool UserStore::setPref(const std::string& key, const std::string& value) {
  if (current_ < 0) return setGlobalPref(key, value);
  users_[current_].prefs[key] = value;
  return savePrefs(users_[current_]);
}

std::string UserStore::globalPref(const std::string& key, const std::string& def) const {
  auto it = global_.find(key);
  if (it != global_.end()) return it->second;
  const std::string d = defaultOf(key);
  return d.empty() ? def : d;
}

bool UserStore::setGlobalPref(const std::string& key, const std::string& value) {
  global_[key] = value;
  return saveGlobalPrefs();
}

bool UserStore::logScore(double score, std::string_view a, std::string_view b) {
  if (current_ < 0) return true;  // a score needs a player to belong to
  if (!hiscores_.record(hiscoreMode(), currentName(), score, a, b)) return true;
  return !persistent() || writeAtomic("hiscore.dat", hiscores_.serialize());
}

bool UserStore::writeFile(const std::string& rel, std::string_view data) const { return writeAtomic(rel, data); }

std::vector<std::string> UserStore::listFiles(std::string_view suffix) const {
  std::vector<std::string> out;
  if (!persistent()) return out;
  std::error_code ec;
  const fs::path rootPath = platform::pathFromUtf8(root_);
  for (fs::directory_iterator it(rootPath, fs::directory_options::skip_permission_denied, ec), end;
       !ec && it != end; it.increment(ec)) {
    if (!it->is_regular_file(ec)) continue;
    const std::u8string u8 = it->path().lexically_relative(rootPath).generic_u8string();
    const std::string rel(u8.begin(), u8.end());
    if (rel.size() >= suffix.size() && rel.compare(rel.size() - suffix.size(), suffix.size(), suffix) == 0) out.push_back(rel);
  }
  std::sort(out.begin(), out.end());
  return out;
}

std::string UserStore::nextSaveName() {
  constexpr std::string_view ext = ".choco3";
  if (!iterating_) {
    saveIter_ = listFiles(ext);
    iterPos_ = 0;
    iterating_ = true;
  }
  if (iterPos_ >= saveIter_.size()) {
    iterating_ = false;
    return {};
  }
  std::string n = saveIter_[iterPos_++];
  n.resize(n.size() - ext.size());
  return n;
}

}  // namespace choco::sdk
