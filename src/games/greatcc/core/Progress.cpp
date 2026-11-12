#include "games/greatcc/core/Progress.h"

#include <cstdio>
#include <cstdlib>

namespace choco::games::greatcc {

namespace {
constexpr const char* kHeader = "greatcc-progress 1";
constexpr const char* kFileName = "greatcc-progress.txt";
}  // namespace

std::string Progress::get(const std::string& key, const std::string& def) const {
  auto it = entries_.find(key);
  return it == entries_.end() ? def : it->second;
}

void Progress::set(const std::string& key, const std::string& value) {
  if (key.empty() || key.find('=') != std::string::npos) return;
  auto it = entries_.find(key);
  if (it != entries_.end() && it->second == value) return;
  entries_[key] = value;
  dirty_ = true;
}

void Progress::erase(const std::string& key) {
  if (entries_.erase(key)) dirty_ = true;
}

double Progress::getDouble(const std::string& key, double def) const {
  auto it = entries_.find(key);
  if (it == entries_.end() || it->second.empty()) return def;
  char* end = nullptr;
  const double v = std::strtod(it->second.c_str(), &end);
  return end == it->second.c_str() ? def : v;
}

void Progress::setDouble(const std::string& key, double v) {
  char buf[48];
  std::snprintf(buf, sizeof buf, "%.17g", v);
  set(key, buf);
}

int Progress::getInt(const std::string& key, int def) const {
  auto it = entries_.find(key);
  if (it == entries_.end() || it->second.empty()) return def;
  char* end = nullptr;
  const long v = std::strtol(it->second.c_str(), &end, 10);
  return end == it->second.c_str() ? def : static_cast<int>(v);
}

std::vector<std::string> Progress::keysWithPrefix(const std::string& prefix) const {
  std::vector<std::string> out;
  for (auto it = entries_.lower_bound(prefix); it != entries_.end() && it->first.compare(0, prefix.size(), prefix) == 0; ++it)
    out.push_back(it->first);
  return out;
}

std::string Progress::path() const {
  if (!hasUser()) return {};
  return "profiles/" + users_->folder(users_->current()) + "/" + kFileName;
}

std::string Progress::render(const std::map<std::string, std::string>& entries) {
  std::string text = std::string(kHeader) + "\n";
  for (const auto& [k, v] : entries) text += sdk::UserStore::escape(k) + "=" + sdk::UserStore::escape(v) + "\n";
  return text;
}

std::map<std::string, std::string> Progress::parse(std::string_view text) {
  std::map<std::string, std::string> out;
  std::size_t pos = 0;
  bool first = true;
  while (pos < text.size()) {
    std::size_t nl = text.find('\n', pos);
    if (nl == std::string_view::npos) nl = text.size();
    std::string_view line = text.substr(pos, nl - pos);
    pos = nl + 1;
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    if (first) {
      first = false;
      if (line != kHeader) return {};
      continue;
    }
    const std::size_t eq = line.find('=');
    if (eq == std::string_view::npos || eq == 0) continue;
    out[sdk::UserStore::unescape(line.substr(0, eq))] = sdk::UserStore::unescape(line.substr(eq + 1));
  }
  return out;
}

bool Progress::load() {
  entries_.clear();
  dirty_ = false;
  const std::string p = path();
  if (p.empty()) return true;
  const auto text = users_->readFile(p);
  if (!text) return true;
  const std::size_t nl = text->find('\n');
  std::string_view first = std::string_view(*text).substr(0, nl == std::string::npos ? text->size() : nl);
  if (!first.empty() && first.back() == '\r') first.remove_suffix(1);
  if (first != kHeader) {
    users_->writeFile(p + ".bad", *text);
    return false;
  }
  entries_ = parse(*text);
  return true;
}

bool Progress::flush() {
  if (!dirty_) return true;
  const std::string p = path();
  if (p.empty()) {
    dirty_ = false;
    return true;
  }
  if (!users_->writeFile(p, render(entries_))) return false;
  dirty_ = false;
  ++writes_;
  return true;
}

}  // namespace choco::games::greatcc
