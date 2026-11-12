#include "games/greatcc/core/UpgradeManager.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

#include "assets/Xml.h"

namespace choco::games::greatcc {

namespace {

std::string lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

// VUpgrade::LoadFromNode and AddBoundObject.
void readUpgrade(const pugi::xml_node& node, Upgrade& u) {
  u.hidden = lower(node.attribute("hidden").value()) == "true";
  for (pugi::xml_node c : node.children()) {
    if (c.type() != pugi::node_element) continue;
    const std::string tag = c.name();
    const std::string text = assets::nodeText(c);
    if (tag == "cost") u.cost = static_cast<float>(std::atof(text.c_str()));
    else if (tag == "minlevel") u.minLevel = std::atoi(text.c_str());
    else if (tag == "maxlevel") u.maxLevel = std::atoi(text.c_str());
    else if (tag == "enabled") u.enabled = std::atoi(text.c_str());
    else if (tag == "requires") u.requires_.insert(lower(text));
    else if (tag == "object") {
      BoundObject b;
      b.name = c.attribute("name").value();
      b.asset = c.attribute("asset").value();
      b.state = c.attribute("state").value();
      b.effect = std::string(c.attribute("effect").value()) != "false";
      b.recursive = std::string(c.attribute("recursive").value()) != "false";
      u.bound[b.name] = std::move(b);
    } else {
      u.attributes[tag] = text;
    }
  }
}

}  // namespace

bool UpgradeManager::loadUpgrades(std::string_view xml) {
  byCategory_.clear();
  preview_.clear();
  assets::Xml doc;
  loadFailed_ = !doc.parse(xml);
  if (loadFailed_) return false;
  int index = 0;
  auto add = [&](const pugi::xml_node& node, const std::string& category) {
    const std::string name = lower(node.attribute("name").value());
    if (name.empty()) return;
    auto u = std::make_shared<Upgrade>();
    u->name = name;
    u->category = category;
    u->index = index++;
    readUpgrade(node, *u);
    byCategory_[category][name] = std::move(u);
  };
  for (pugi::xml_node n : doc.root().children()) {
    if (n.type() != pugi::node_element) continue;
    const std::string tag = n.name();
    if (tag == "upgradetype") {
      const std::string category = lower(n.attribute("type").value());
      for (pugi::xml_node up : n.children())
        if (up.type() == pugi::node_element && lower(up.name()) == "upgrade") add(up, category);
    } else if (tag == "upgrade") {
      add(n, std::string());
    }
  }
  return true;
}

std::size_t UpgradeManager::size() const {
  std::size_t n = 0;
  for (const auto& [category, byName] : byCategory_) n += byName.size();
  return n;
}

void UpgradeManager::resolveBoundObjects() {
  for (auto& [category, byName] : byCategory_)
    for (auto& [name, u] : byName)
      for (auto& [key, b] : u->bound) b.resolved = !b.name.empty() && sprites_ && sprites_->exists(b.name);
}

const Upgrade* UpgradeManager::getUpgrade(const std::string& name, const std::string& category) const {
  const auto c = byCategory_.find(lower(category));
  if (c == byCategory_.end()) return nullptr;
  const auto it = c->second.find(lower(name));
  return it == c->second.end() ? nullptr : it->second.get();
}

Upgrade* UpgradeManager::getUpgrade(const std::string& name, const std::string& category) {
  return const_cast<Upgrade*>(std::as_const(*this).getUpgrade(name, category));
}

bool UpgradeManager::isActive(const std::string& name, const std::string& category) const {
  const Upgrade* u = getUpgrade(name, category);
  return u && u->isActive();
}

bool UpgradeManager::checkRequirements(const Upgrade& u) const {
  for (const std::string& r : u.requires_)
    if (!isActive(r, u.category)) return false;
  return true;
}

bool UpgradeManager::isAvailable(const std::string& name, const std::string& category) const {
  const Upgrade* u = getUpgrade(name, category);
  return u && u->isAvailable() && checkRequirements(*u);
}

const std::map<std::string, std::shared_ptr<Upgrade>>& UpgradeManager::typeMap(const std::string& category) const {
  static const std::map<std::string, std::shared_ptr<Upgrade>> none;
  const auto c = byCategory_.find(lower(category));
  return c == byCategory_.end() ? none : c->second;
}

int UpgradeManager::visibleOfType(const std::string& category) const {
  int n = 0;
  for (const auto& [name, u] : typeMap(category))
    if (u && !u->hidden && !u->isActive()) ++n;
  return n;
}

void UpgradeManager::unlockUpgrade(const std::string& name, const std::string& category) {
  if (Upgrade* u = getUpgrade(name, category); u && !u->isAvailable() && !u->isActive()) u->setState(kUpgradeUnlocked);
}

void UpgradeManager::revealUpgrade(const std::string& name, const std::string& category) {
  if (Upgrade* u = getUpgrade(name, category)) u->hidden = false;
}

// VUpgrade::Activate: the state first, then the bound sprites: re-Init (or the asset and state switch the XML names), then the
// spider's effect for those that have it.
void UpgradeManager::activate(Upgrade& u, bool on, bool flag, UpgradeSpider* spider) {
  u.setState(on ? (flag ? kUpgradePreview : kUpgradeOwned) : u.previousState);
  for (auto& [key, b] : u.bound) {
    if (!b.resolved || !sprites_) continue;
    if (!on || (b.asset.empty() && b.state.empty())) {
      sprites_->init(b.name);
    } else if (b.asset.empty()) {
      sprites_->switchState(b.name, b.state);
    } else if (!b.state.empty()) {
      sprites_->switchAsset(b.name, b.asset, b.state);
    }
    if (spider && b.effect) spider->process(u, b);
  }
}

void UpgradeManager::updateUserState(const Upgrade& u) {
  if (noSave_) return;
  if (u.state == kUpgradeOwned) progress_.setInt(saveKey(u), 1);
  else progress_.erase(saveKey(u));
}

bool UpgradeManager::enableUpgrade(const std::string& name, const std::string& category, bool save, UpgradeSpider* spider) {
  const std::shared_ptr<Upgrade> keep = [&] {
    const auto c = byCategory_.find(lower(category));
    if (c == byCategory_.end()) return std::shared_ptr<Upgrade>();
    const auto it = c->second.find(lower(name));
    return it == c->second.end() ? std::shared_ptr<Upgrade>() : it->second;
  }();
  if (!keep) return false;
  activate(*keep, true, false, spider);
  std::erase(preview_, keep);
  if (save) updateUserState(*keep);
  return true;
}

void UpgradeManager::enableTempUpgrade(const std::string& name, const std::string& category, UpgradeSpider* spider) {
  const auto c = byCategory_.find(lower(category));
  if (c == byCategory_.end()) return;
  const auto it = c->second.find(lower(name));
  if (it == c->second.end() || !it->second->isAvailable()) return;
  const std::shared_ptr<Upgrade> u = it->second;
  if (std::find(preview_.begin(), preview_.end(), u) == preview_.end()) preview_.push_back(u);
  activate(*u, true, true, spider);
}

void UpgradeManager::disableTempUpgrades(const std::string& nameFilter, const std::string& categoryFilter, UpgradeSpider* spider) {
  const std::string name = lower(nameFilter), category = lower(categoryFilter);
  const std::vector<std::shared_ptr<Upgrade>> snapshot = preview_;
  std::vector<std::shared_ptr<Upgrade>> done;
  for (const auto& u : snapshot) {
    if (!u) continue;
    if (!category.empty() && u->category != category) continue;
    if (!name.empty() && u->name != name) continue;
    activate(*u, false, false, spider);
    done.push_back(u);
  }
  for (const auto& u : done) std::erase(preview_, u);
}

void UpgradeManager::loadCurrentUser() {
  if (noSave_) return;
  for (auto& [category, byName] : byCategory_)
    for (auto& [name, u] : byName)
      if (u->index >= 0 && u->inRange(level_) && progress_.getInt(saveKey(*u), 0) == 1) u->setState(kUpgradeOwned);
}

void UpgradeManager::setCurrentLevel(int level, std::optional<std::string_view> xml, int p4, int p5, UpgradeSpider* spider) {
  level_ = level;
  if (xml) loadUpgrades(*xml);
  loadCurrentUser();
  for (auto& [category, byName] : byCategory_)
    for (auto& [name, u] : byName) {
      if (!u || u->isActive() || !checkRequirements(*u)) continue;
      if ((p5 < 0 || u->enabled <= p5) && u->enabled <= level_) {
        enableUpgrade(u->name, u->category, true, spider);
      } else if ((p4 < 0 || u->minLevel <= p4) && u->inRange(level_)) {
        unlockUpgrade(u->name, u->category);
      }
    }
}

void UpgradeManager::setCurrentLevel(const std::string& location, int level, std::optional<std::string_view> xml, int p4, int p5,
                                     bool applyGrants, const std::vector<std::pair<std::string, std::string>>& grants,
                                     UpgradeSpider* spider) {
  location_ = location;
  setCurrentLevel(level, xml, p4, p5, spider);
  if (applyGrants)
    for (const auto& [name, category] : grants) enableUpgrade(name, category, false, spider);
}

}  // namespace choco::games::greatcc
