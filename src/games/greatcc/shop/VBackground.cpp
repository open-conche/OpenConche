#include "games/greatcc/shop/VBackground.h"

#include "games/greatcc/GreatCCWorld.h"
#include "games/greatcc/scene/SpriteManager.h"

namespace choco::games::greatcc {

namespace {
// ForEachChild(spider, recursive) with VStateSpider::Process: every VObject below `s` takes the state.
void switchDescendants(VSprite& s, const std::string& state) {
  const auto kids = s.children();
  for (const auto& c : kids) {
    if (auto* o = dynamic_cast<VObject*>(c.get())) o->switchState(state, "default");
    switchDescendants(*c, state);
  }
}
}  // namespace

VBackground::VBackground(script::ScriptContext& ctx) : VObject(ctx, "", "background") {
  setDepth(-1000);
  setMouse(false);
}

void VBackground::removeObjects() {
  if (SpriteManager* m = manager()) {
    for (auto& [key, s] : statics_) m->removeObject(*s);
    for (auto& [key, s] : anims_) m->removeObject(*s);
  }
}

void VBackground::loadFromTable(const StyleChain& style) {
  VObject::loadFromTable(style);
  removeObjects();
  statics_.clear();
  anims_.clear();
  auto load = [&](const char* group, std::map<std::string, std::shared_ptr<VObject>>& into) {
    auto table = style.table(group, 0);
    if (!table) return;
    const StyleChain gc(*table, &style);
    table->forEachEntry([&](const std::string& key, const ui::Style* t, const std::optional<std::string>&) {
      if (!t) return;
      const StyleChain ec(*t, &gc);
      auto obj = VObject::create(context(), "", "");
      obj->loadFromTable(ec);
      into[lower(key)] = obj;
      if (SpriteManager* m = manager()) m->addObject(obj);
    });
  };
  load("statics", statics_);
  load("anims", anims_);
}

void VBackground::init() {
  VObject::init();
  GreatCCWorld* world = worldOf(context());
  auto active = [&](const char* name, const char* category) { return world && world->upgradeActive(name, category); };
  std::string state = "default";
  if (active("kBackground2", "aesthetic")) state = "upgrade2";
  else if (active("kBackground1", "aesthetic")) state = "upgrade1";
  switchState(state, "default");
  for (auto& [key, s] : statics_) {
    const bool on = active(s->attribute("upgrade", "").c_str(), s->attribute("category", "aesthetic").c_str());
    if (!on) {
      s->stop(true);
    } else {
      s->switchState(state, "default");
      s->init();
      // VStateSpider over the subtree
      switchDescendants(*s, state);
    }
    s->setVisible(on);
  }
}

void VBackground::setManager(SpriteManager* m) {
  SpriteManager* old = manager();
  if (m != old)
    for (auto& [key, s] : statics_) {
      if (old) old->removeObject(*s);
      if (m) m->addObject(s);
    }
  VObject::setManager(m);
}

}  // namespace choco::games::greatcc
