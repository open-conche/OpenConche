#include "games/greatcc/core/LevelManager.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "assets/Xml.h"

namespace choco::games::greatcc {

namespace {
const char* const kLocations[] = {"iquitos", "grytviken", "namche", "krakatoa", "kamchatka", "gaberoun"};
std::string levelKey(int level) { return "level." + std::to_string(level); }
}  // namespace

bool LevelManager::loadGoals(std::string_view xml) {
  goals_.assign(kLevels + 1, Entry{});
  assets::Xml doc;
  if (!doc.parse(xml)) return false;
  for (pugi::xml_node n : doc.root().children("levelgoal")) {
    const int level = n.attribute("level").as_int();
    if (level < 1 || level > kLevels) continue;
    goals_[static_cast<std::size_t>(level)] = {n.attribute("goal").as_int(), n.attribute("mastery").as_int()};
  }
  return true;
}

int LevelManager::goal(int level) const { return level < 1 || level > kLevels ? 0 : goals_[static_cast<std::size_t>(level)].goal; }
int LevelManager::mastery(int level) const {
  return level < 1 || level > kLevels ? 0 : goals_[static_cast<std::size_t>(level)].mastery;
}

int LevelManager::status(int level) const {
  if (level < 1 || level > kLevels) return 0;
  return std::atoi(progress_.get(levelKey(level), "0,0").c_str());
}

double LevelManager::bestScore(int level) const {
  if (level < 1 || level > kLevels) return 0;
  const std::string v = progress_.get(levelKey(level), "0,0");
  const std::size_t comma = v.find(',');
  return comma == std::string::npos ? 0 : std::strtod(v.c_str() + comma + 1, nullptr);
}

void LevelManager::write(int level, int status, double best) {
  if (status == 0 && best == 0) {
    progress_.erase(levelKey(level));
    return;
  }
  char buf[64];
  std::snprintf(buf, sizeof buf, "%d,%.17g", status, best);
  progress_.set(levelKey(level), buf);
}

void LevelManager::setHighestComplete(int n, bool force) {
  if (!force && n <= highestComplete()) return;
  values_.setValue("highest_level", n, true);
  for (int level = 1; level <= kLevels; ++level) {
    const int s = status(level);
    if (level <= n && s == 0) write(level, 1, bestScore(level));
    else if (level > n && s != 0) write(level, 0, bestScore(level));
  }
}

void LevelManager::incrementLevel() {
  const int current = currentLevel();
  if (status(current) == 0) return;
  setHighestComplete(current, false);
  setCurrentLevel(current + 1);
}

LevelManager::Outcome LevelManager::levelComplete(int level, double score) {
  Outcome out;
  if (level < 1 || level > kLevels) return out;
  out.result = score < mastery(level) ? (score < goal(level) ? 0 : 1) : 2;
  const int before = status(level);
  const double best = bestScore(level);
  out.delta = before == 0 ? score : std::max(0.0, score - best);
  write(level, std::max(before, out.result), std::max(best, score));
  return out;
}

const char* LevelManager::locationOf(int level) {
  const int slot = (std::clamp(level, 1, kLevels) - 1) / kLevelsPerLocation;
  return kLocations[slot];
}

double LevelManager::locationCash(int level) const {
  if (level < 1 || level > kLevels) return 0;
  return progress_.getDouble(std::string("cash.") + locationOf(level), 0);
}

void LevelManager::updateLevelScore(int level, double v) {
  if (level < 1 || level > kLevels) return;
  progress_.setDouble(std::string("cash.") + locationOf(level), v);
}

void LevelManager::updateCurrentScore(double v) { updateLevelScore(currentLevel(), v); }

}  // namespace choco::games::greatcc
