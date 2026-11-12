#pragma once

// VBackground (docs/re/greatcc-sprites-and-zones.md 4.5): the level's backdrop, a VObject at depth -1000 that also owns two
// groups of objects it adds to the sprite manager: `statics` (shown only when their upgrade is active) and `anims` (not tied to
// an upgrade). The backdrop's own state is `upgrade2`, `upgrade1` or `default` by the kBackground upgrades.

#include <map>
#include <memory>
#include <string>

#include "games/greatcc/scene/VObject.h"

namespace choco::games::greatcc {

class VBackground : public VObject {
 public:
  explicit VBackground(script::ScriptContext& ctx);
  static std::shared_ptr<VBackground> create(script::ScriptContext& ctx) { return std::make_shared<VBackground>(ctx); }
  const char* kind() const override { return "VBackground"; }

  // The VObject keys, then the entries of `statics` and `anims` (names lower-cased; each a VObject read through the group
  // table and then this one) are made and added to the manager; the previous ones are removed first. Non-table entries of the
  // group tables (`spriteLayer`, `visible`) reach the entries through the chain.
  void loadFromTable(const StyleChain& style) override;
  // The backdrop's state from the aesthetic upgrades, then each static: an inactive one stops (it and its direct children) and
  // hides; an active one takes the state, initialises and gives the state to its whole subtree.
  void init() override;
  // The statics follow the sprite into another manager.
  void setManager(SpriteManager* m) override;

  const std::map<std::string, std::shared_ptr<VObject>>& statics() const { return statics_; }
  const std::map<std::string, std::shared_ptr<VObject>>& anims() const { return anims_; }

 private:
  void removeObjects();
  std::map<std::string, std::shared_ptr<VObject>> statics_, anims_;
};

}  // namespace choco::games::greatcc
