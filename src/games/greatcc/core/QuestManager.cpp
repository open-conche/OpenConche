#include "games/greatcc/core/QuestManager.h"

#include <cctype>
#include <cstdlib>

#include "games/greatcc/core/Compare.h"

namespace choco::games::greatcc {

namespace {

std::string lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

}  // namespace

void QuestManager::loadFromTable(const ui::Style& quests) {
  quests_.clear();
  active_.clear();
  int slot = 0;
  quests.forEachEntry([&](const std::string& key, const ui::Style* t, const std::optional<std::string>&) {
    if (!t) return;
    auto q = std::make_unique<Quest>();
    q->slot = slot++;
    q->key = lower(key);
    // VQuest::LoadFromStyle: every key is read from this table only.
    q->name = t->stringOr("name");
    if (const std::string units = t->stringOr("units"); !units.empty()) q->units = text_(units, {});
    q->minLevel = static_cast<int>(t->numberOr("minlevel", 0));
    q->maxLevel = static_cast<int>(t->numberOr("maxlevel", 0));
    q->threshold = static_cast<float>(t->numberOr("threshold", 0));
    q->mask = parseComparison(t->stringOr("compare"));
    q->compValue = t->stringOr("compvalue");
    q->category = lower(t->stringOr("rewardcategory"));
    q->reward = lower(t->stringOr("reward"));
    q->rewardType = lower(t->stringOr("rewardtype", "activate"));
    q->display = static_cast<int>(t->numberOr("display", q->display));
    q->format = lower(t->stringOr("format"));
    q->status = progress_.getInt(statusKey(*q), 0);
    const std::string k = q->key;
    quests_[k] = std::move(q);
  });
}

void QuestManager::loadCurrentUser() {
  for (auto& [key, q] : quests_) q->status = progress_.getInt(statusKey(*q), 0);
}

void QuestManager::clear() {
  quests_.clear();
  active_.clear();
  postLevelText_.clear();
}

Quest* QuestManager::find(const std::string& key) {
  const auto it = quests_.find(lower(key));
  return it == quests_.end() ? nullptr : it->second.get();
}

void QuestManager::setCurrentLevel(int level) {
  level_ = level;
  active_.clear();
  postLevelText_.clear();
  for (auto& [key, q] : quests_)
    if (q->isReady(level)) active_.push_back(q.get());
}

std::string QuestManager::name(const Quest& q) const { return q.name.empty() ? q.name : text_(q.name, {}); }

int QuestManager::currentStatus(const Quest& q) {
  const float value = static_cast<float>(values_.getReal(q.compValue, 0));
  if (compareValue(value, q.threshold, q.mask)) return q.threshold <= value ? kQuestComplete : kQuestPending;
  return q.threshold <= value ? kQuestFailed : kQuestPending;
}

std::string QuestManager::statusInfo(const Quest& q) {
  switch (currentStatus(q)) {
    case kQuestComplete: return text_("quest_complete", {});
    case kQuestFailed: return text_("quest_failed", {});
    case kQuestPending: break;
    default: return {};
  }
  const int threshold = static_cast<int>(q.threshold);
  if (q.display == kProgressDisplay) {
    const int value = values_.getInt(q.compValue, 0);
    return std::to_string(value) + "/" + std::to_string(threshold) + " " + q.units;
  }
  if (q.display == kGoalDisplay) return (q.format == "currency" ? "$" : "") + std::to_string(threshold) + " " + q.units;
  return text_("quest_pending", {});
}

std::string QuestManager::postLevelMessage(const Quest& q) const {
  if (!q.complete()) return text_("quest_failed_info", {text_(q.name, {})});
  if (q.rewardType == "money") return text_("quest_complete_info", {text_(q.name, {}), "$" + q.reward});
  const Upgrade* u = upgrades_.getUpgrade(q.reward, q.category);
  if (!u) return {};
  return text_("quest_complete_info", {text_(q.name, {}), text_(u->attribute("header"), {}), text_(u->attribute("content"), {})});
}

void QuestManager::checkQuest(Quest& q) {
  if (q.complete()) return;
  const float value = static_cast<float>(values_.getReal(q.compValue, 0));
  q.status = compareValue(value, q.threshold, q.mask) ? kQuestComplete : kQuestFailed;
  if (q.status != kQuestComplete || q.reward.empty()) return;
  if (q.rewardType == "reveal") {
    upgrades_.revealUpgrade(q.reward, q.category);
  } else if (q.rewardType == "activate") {
    upgrades_.enableUpgrade(q.reward, q.category, true, nullptr);
  } else if (q.rewardType == "money") {
    const float amount = static_cast<float>(std::strtod(q.reward.c_str(), nullptr));
    if (amount > 0) values_.addToValue("current_worth", amount, 0, false);
  }
}

std::string QuestManager::postLevelProcess() {
  for (Quest* q : active_) checkQuest(*q);
  postLevelText_.clear();
  for (Quest* q : active_) {
    progress_.setInt(statusKey(*q), q->status);
    const std::string message = postLevelMessage(*q);
    if (message.empty()) continue;
    if (!postLevelText_.empty()) postLevelText_ += "\n";
    postLevelText_ += message;
  }
  return postLevelText_;
}

}  // namespace choco::games::greatcc
