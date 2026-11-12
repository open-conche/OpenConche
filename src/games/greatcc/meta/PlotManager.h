#pragma once

// VPlotManager (docs/re/greatcc-meta-flow.md 2): the diary pages of the location being played. Each level load gives it the
// location's plot table (every `story_*` entry becomes an item) and then the names of the level's own pages, which become the active
// list; at the start of the level the diary window shows the active pages one after the other. Nothing is saved: the original never
// writes the pages' completed flags (its UpdateCurrentUser has no caller), so every entry of a level shows its pages again.

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "games/greatcc/meta/PlotItem.h"
#include "script/ScriptContext.h"
#include "ui/Style.h"

namespace choco::games::greatcc {

class PlotManager {
 public:
  explicit PlotManager(script::ScriptContext& ctx) : ctx_(ctx) {}

  // LoadFromTable: ClearPlotItems, then every table-valued entry whose key starts with `story_` (any case) becomes an item named
  // by the lower-cased key (a `quest` type makes the quest flavour, which is a plain page here: nothing evaluates it); items are
  // loaded, not initialised.
  void loadFromTable(const ui::Style& plot);
  // ClearPlotItems: no items and no active pages.
  void clear();
  // SetCurrentLevel(level): the active list is the level's page names in order (each once), and each is initialised (a name that
  // has no item gets an empty entry, as the original's map lookup does).
  void setCurrentLevel(const std::vector<std::string>& pages);
  // IsPlotItemActive(name): the value modifiers that name a page ask this.
  bool isPlotItemActive(const std::string& name) const;
  const std::vector<std::string>& activeItems() const { return active_; }
  std::shared_ptr<PlotItem> item(const std::string& name) const;
  std::size_t itemCount() const { return items_.size(); }

  // Advance(name) and ChooseResponse(name, id): false for a name with no item.
  bool advance(const std::string& name);
  bool chooseResponse(const std::string& name, const std::string& id);
  // GetPostLevelMessage: the pages' messages joined (a plain page has none).
  std::string postLevelMessage() const { return {}; }
  // LaunchPlotDialog: with active pages, the diary dialog (scripts/plotwindow.lua) is opened on Main's coroutine, as the shop is.
  // False (and nothing opened) when no page is active.
  bool launchPlotDialog();

 private:
  script::ScriptContext& ctx_;
  std::map<std::string, std::shared_ptr<PlotItem>> items_;
  std::vector<std::string> active_;
};

}  // namespace choco::games::greatcc
