#include "games/greatcc/core/AchievementManager.h"

#include <cctype>
#include <cstdlib>

#include "assets/Xml.h"
#include "games/greatcc/core/Compare.h"

namespace choco::games::greatcc {

namespace {

std::string lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

}  // namespace

bool AchievementManager::load(std::string_view xml) {
  table_.clear();
  pending_.clear();
  assets::Xml doc;
  if (!doc.parse(xml)) return false;
  int ordinal = 0;
  auto read = [&](const pugi::xml_node& n, const std::string& category) {
    Achievement a;
    a.category = category;
    a.name = lower(n.attribute("name").as_string());
    a.ordinal = ordinal++;
    for (pugi::xml_node c : n.children()) {
      const std::string tag = lower(c.name());
      const std::string text = assets::nodeText(c);
      if (tag == "title") a.title = text;
      else if (tag == "description") a.description = text;
      else if (tag == "compare") a.mask = parseComparison(text);
      else if (tag == "threshold") a.threshold = std::strtod(text.c_str(), nullptr);
      else if (tag == "value") a.valueKey = text;
      else if (tag == "medal") a.medal = text;
    }
    table_[category][a.name] = std::move(a);
  };
  for (pugi::xml_node n : doc.root().children()) {
    const std::string tag = lower(n.name());
    if (tag == "achievementcategory") {
      const std::string category = n.attribute("name").as_string();
      for (pugi::xml_node a : n.children())
        if (lower(a.name()) == "achievement") read(a, category);
    } else if (tag == "achievement") {
      read(n, "");
    }
  }
  return true;
}

size_t AchievementManager::size() const {
  size_t n = 0;
  for (const auto& [cat, list] : table_) n += list.size();
  return n;
}

const Achievement* AchievementManager::find(const std::string& name) const {
  for (const auto& [cat, list] : table_) {
    auto it = list.find(name);
    if (it != list.end()) return &it->second;
  }
  return nullptr;
}

std::vector<std::string> AchievementManager::checkAll(const std::string& category) {
  std::vector<std::string> fresh;
  for (const auto& [cat, list] : table_) {
    if (!category.empty() && cat != category) continue;
    for (const auto& [name, a] : list) {
      const double value = values_.getReal(a.valueKey, 0);
      if (isAcquired(name) || !compareValue(value, a.threshold, a.mask)) continue;
      progress_.setInt("achievement." + name, 1);
      fresh.push_back(name);
      pending_.push_back(name);
    }
  }
  return fresh;
}

std::vector<std::string> AchievementManager::takePending() {
  std::vector<std::string> out;
  out.swap(pending_);
  return out;
}

bool AchievementManager::acquire(const std::string& category, const std::string& name) {
  auto c = table_.find(category);
  if (c == table_.end() || !c->second.count(lower(name))) return false;
  progress_.setInt("achievement." + lower(name), 1);
  return true;
}

void AchievementManager::acquireAll() {
  for (const auto& [cat, list] : table_)
    for (const auto& [name, a] : list) progress_.setInt("achievement." + name, 1);
}

bool AchievementManager::release(const std::string& category, const std::string& name) {
  auto c = table_.find(category);
  if (c == table_.end()) return false;
  auto it = c->second.find(lower(name));
  if (it == c->second.end()) return false;
  if (!it->second.valueKey.empty()) values_.setValue(it->second.valueKey, 0, true);
  progress_.erase("achievement." + it->first);
  return true;
}

void AchievementManager::releaseAll() {
  for (const auto& [cat, list] : table_)
    for (const auto& [name, a] : list) release(cat, name);
}

std::vector<std::string> AchievementManager::acquiredNames() const {
  std::vector<std::string> out;
  for (const auto& [cat, list] : table_)
    for (const auto& [name, a] : list)
      if (isAcquired(name)) out.push_back(name);
  return out;
}

std::string AchievementManager::medalXml() const {
  std::string xml;
  for (const auto& [cat, list] : table_)
    for (const auto& [name, a] : list)
      if (!a.medal.empty() && isAcquired(name)) xml += "<medal name=\"" + a.medal + "\" per=\"type\"/>\r\n";
  return xml;
}

}  // namespace choco::games::greatcc
