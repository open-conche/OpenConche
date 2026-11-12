#pragma once

// VUpgradeManager and VUpgrade (docs/re/greatcc-machines-and-chocolates.md 7): the upgrades of the location being played, read
// from `locations/<loc>/upgrades.xml`, their states 0-4, the level-start grant (SetCurrentLevel), the player's purchases (kept
// in Progress per location) and the temporary preview the upgrade shop shows while the pointer is over an offer. The model has no
// sprites: what an upgrade does to the scene goes through `UpgradeSprites` (the shop's lookup, re-Init and asset switch of the
// sprites it is bound to) and, for the shop's glow, through the `UpgradeSpider` of the call.

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "games/greatcc/core/Progress.h"

namespace choco::games::greatcc {

// VUpgrade state (docs/re 7.2). Only 3 is saved.
inline constexpr int kUpgradeLocked = 0;     // not available (the default)
inline constexpr int kUpgradeUnlocked = 1;   // available to buy
inline constexpr int kUpgradeSelected = 2;   // never assigned by the model
inline constexpr int kUpgradeOwned = 3;      // owned, persistent
inline constexpr int kUpgradePreview = 4;    // temporarily active, not saved

// A sprite an upgrade is bound to (`<object name=.. asset=.. state=.. effect=.. recursive=../>`). `resolved` is set by
// UpgradeManager::resolveBoundObjects when the sprite existed.
struct BoundObject {
  std::string name, asset, state;
  bool effect = true, recursive = true;
  bool resolved = false;
};

struct Upgrade {
  std::string name, category;  // both lower case
  std::set<std::string> requires_;  // lower case names of the same category
  int enabled = 0x7fffffff;         // auto-granted at levels >= this (INT_MAX: never)
  int minLevel = 0, maxLevel = 0x7fffffff;
  int index = -1;  // file order across every type, hidden ones included
  float cost = 0;
  int state = kUpgradeLocked, previousState = kUpgradeLocked;
  bool hidden = false;
  std::map<std::string, std::string> attributes;  // every other child element: tag -> text
  std::map<std::string, BoundObject> bound;       // by sprite name

  bool isActive() const { return state == kUpgradeOwned || state == kUpgradePreview; }
  bool isAvailable() const { return state != kUpgradeLocked && state != kUpgradeOwned; }
  bool inRange(int level) const { return level >= minLevel && level <= maxLevel; }
  // VUpgrade::SetState: a changed state remembers the one it leaves, unless that was the preview.
  void setState(int s) {
    if (state == s) return;
    if (state != kUpgradePreview) previousState = state;
    state = s;
  }
  // VUpgrade::GetAttribute: the text of a child element, "" for one that is not there.
  std::string attribute(const std::string& key) const {
    const auto it = attributes.find(key);
    return it == attributes.end() ? std::string() : it->second;
  }
};

// What the shop gives the manager to act on the scene. Every call names a sprite; a sprite that is gone is skipped.
class UpgradeSprites {
 public:
  virtual ~UpgradeSprites() = default;
  // ResolveBoundObjects: does a sprite of that name exist now?
  virtual bool exists(const std::string& name) = 0;
  // VSprite::Init (vtable +0x28).
  virtual void init(const std::string& name) = 0;
  // VObject::SwitchAsset(asset, state, "default") and VObject::SwitchState(state, "default").
  virtual void switchAsset(const std::string& name, const std::string& asset, const std::string& state) = 0;
  virtual void switchState(const std::string& name, const std::string& state) = 0;
};

// VUpgradeSpider: told which upgrade it acts for, then each bound sprite that has the effect (VUpgradeSelect adds its glow to the
// sprite while the upgrade is active and takes it away when it is not).
class UpgradeSpider {
 public:
  virtual ~UpgradeSpider() = default;
  virtual void process(const Upgrade& upgrade, const BoundObject& object) = 0;
};

class UpgradeManager {
 public:
  explicit UpgradeManager(Progress& progress) : progress_(progress) {}

  // LoadUpgrades: ClearUpgrades, then the schema of 7.1. False when the text does not parse (the list is then empty).
  bool loadUpgrades(std::string_view xml);
  // The last loadUpgrades did not parse (SetCurrentLevel loads the file itself; this tells whether it could be read).
  bool loadFailed() const { return loadFailed_; }
  void clear() { byCategory_.clear(); }
  std::size_t size() const;
  // SetNoSave: nothing is read from or written to Progress (the endless runs).
  void setNoSave(bool noSave) { noSave_ = noSave; }
  bool noSave() const { return noSave_; }
  void setSprites(std::shared_ptr<UpgradeSprites> s) { sprites_ = std::move(s); }
  // VUpgradeManager::ResolveBoundObjects(sprite manager): every bound object learns whether its sprite exists.
  void resolveBoundObjects();

  // ---- questions (both names are lower-cased first; an unknown upgrade is inactive, and nothing is created for it) ----
  bool isActive(const std::string& name, const std::string& category) const;
  // IsAvailable: the upgrade is available and every upgrade it requires is active.
  bool isAvailable(const std::string& name, const std::string& category) const;
  bool checkRequirements(const Upgrade& u) const;
  Upgrade* getUpgrade(const std::string& name, const std::string& category);
  const Upgrade* getUpgrade(const std::string& name, const std::string& category) const;
  // GetTypeMap: the upgrades of a category by name (empty for an unknown one).
  const std::map<std::string, std::shared_ptr<Upgrade>>& typeMap(const std::string& category) const;
  // GetVisibleOfType: the upgrades of the category that are neither hidden nor active.
  int visibleOfType(const std::string& category) const;

  // ---- changes ----
  // UnlockUpgrade: a locked, inactive upgrade becomes state 1.
  void unlockUpgrade(const std::string& name, const std::string& category);
  // RevealUpgrade: clears `hidden`.
  void revealUpgrade(const std::string& name, const std::string& category);
  // EnableUpgrade: state 3 and the bound sprites, out of the preview set, and with `save` written to Progress. False for an
  // unknown upgrade. Requirements, level and cost are not looked at here.
  bool enableUpgrade(const std::string& name, const std::string& category, bool save, UpgradeSpider* spider = nullptr);
  // EnableTempUpgrade: an available upgrade becomes state 4 and joins the preview set.
  void enableTempUpgrade(const std::string& name, const std::string& category, UpgradeSpider* spider = nullptr);
  // DisableTempUpgrades: every upgrade of the preview set that matches the filters (empty matches all) goes back to the state it
  // had, and the set loses it.
  void disableTempUpgrades(const std::string& nameFilter, const std::string& categoryFilter, UpgradeSpider* spider = nullptr);
  std::size_t previewCount() const { return preview_.size(); }

  // ---- levels ----
  // SetCurrentLevel(levelNumber, path, p4, p5, spider): the level number is kept; with `xml` the list is replaced; the player's
  // purchases are read; then every upgrade that is inactive and whose requirements are all active is looked at in category then
  // name order: granted (EnableUpgrade with save) when `(p5 < 0 || enabled <= p5) && enabled <= level`, else unlocked when
  // `(p4 < 0 || minlevel <= p4) && inRange(level)`. One pass: a requirement chains only if it sorts earlier.
  void setCurrentLevel(int level, std::optional<std::string_view> xml, int p4, int p5, UpgradeSpider* spider = nullptr);
  // The overload with a level: its location is kept (it names the saved block), its number is the level number, and with
  // `applyGrants` the (name, category) pairs of the level are enabled without saving.
  void setCurrentLevel(const std::string& location, int level, std::optional<std::string_view> xml, int p4, int p5, bool applyGrants = false,
                       const std::vector<std::pair<std::string, std::string>>& grants = {}, UpgradeSpider* spider = nullptr);
  int currentLevel() const { return level_; }
  const std::string& location() const { return location_; }
  // LoadCurrentUser: unless noSave, each upgrade in range whose saved flag is set becomes state 3 (no sprite is touched).
  void loadCurrentUser();

  // The Progress key of an upgrade's saved flag.
  std::string saveKey(const Upgrade& u) const { return "upgrade." + location_ + "." + u.name; }

 private:
  void activate(Upgrade& u, bool on, bool flag, UpgradeSpider* spider);
  void updateUserState(const Upgrade& u);

  Progress& progress_;
  // category -> name -> upgrade, both lower case: the original's map order
  std::map<std::string, std::map<std::string, std::shared_ptr<Upgrade>>> byCategory_;
  std::vector<std::shared_ptr<Upgrade>> preview_;
  std::shared_ptr<UpgradeSprites> sprites_;
  std::string location_;
  int level_ = 0;
  bool noSave_ = false;
  bool loadFailed_ = false;
};

}  // namespace choco::games::greatcc
