#include "games/greatcc/meta/PlotManager.h"

#include <algorithm>

#include "games/greatcc/scene/StyleChain.h"
#include "games/greatcc/ui/Transitions.h"
#include "script/ScriptContext.h"

namespace choco::games::greatcc {

void PlotManager::loadFromTable(const ui::Style& plot) {
  clear();
  const StyleChain root(plot, nullptr);
  int slot = 0;
  plot.forEachEntry([&](const std::string& key, const ui::Style* table, const std::optional<std::string>&) {
    if (!table) return;
    const std::string name = VObject::lower(key);
    if (name.compare(0, 6, "story_") != 0) return;
    const StyleChain chain(*table, &root);
    const bool quest = VObject::lower(table->stringOr("type")) == "quest";
    auto item = PlotItem::create(ctx_, name, slot++, quest);
    item->loadFromTable(chain);
    items_[name] = std::move(item);
  });
}

void PlotManager::clear() {
  items_.clear();
  active_.clear();
}

void PlotManager::setCurrentLevel(const std::vector<std::string>& pages) {
  active_.clear();
  for (const std::string& name : pages) {
    if (std::find(active_.begin(), active_.end(), name) == active_.end()) active_.push_back(name);
    if (const auto it = items_.find(name); it != items_.end() && it->second) it->second->init();
  }
}

bool PlotManager::isPlotItemActive(const std::string& name) const {
  return std::find(active_.begin(), active_.end(), name) != active_.end();
}

std::shared_ptr<PlotItem> PlotManager::item(const std::string& name) const {
  const auto it = items_.find(name);
  return it == items_.end() ? nullptr : it->second;
}

bool PlotManager::advance(const std::string& name) {
  const auto it = item(name);
  return it && it->advance();
}

bool PlotManager::chooseResponse(const std::string& name, const std::string& id) {
  const auto it = item(name);
  return it && it->chooseResponse(id);
}

bool PlotManager::launchPlotDialog() {
  if (active_.empty()) return false;
  doLuaString(ctx_, "DoModal ( 'scripts/plotwindow.lua' );");
  return true;
}

}  // namespace choco::games::greatcc
