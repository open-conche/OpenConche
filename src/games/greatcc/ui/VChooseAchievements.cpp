#include "games/greatcc/ui/VChooseAchievements.h"

#include "games/greatcc/GreatCCWorld.h"
#include "games/greatcc/ui/RScrollable.h"
#include "ui/Layout.h"
#include "ui/WindowManager.h"

namespace choco::games::greatcc {

void VChooseAchievements::postChildrenInit(const ui::Style& style) {
  VSpriteWindow::postChildrenInit(style);
  ui::Window* scroller = find("scroller");
  auto* inner = scroller ? dynamic_cast<VSpriteWindow*>(scroller->find("sprite_window")) : nullptr;
  GreatCCWorld* world = worldOf(ctx_);
  if (!inner || !world) return;
  const auto checkStyle = style.table("checkstyle");
  const int width = rect().w;
  int i = 0;
  for (const auto& [category, list] : world->achievements().all())
    for (const auto& [name, a] : list) {
      auto button = VButton::create(ctx_, 0, "");
      if (checkStyle) {
        const StyleChain chain(*checkStyle, nullptr);
        button->loadFromTable(chain);
      }
      button->init();
      button->setText(name);
      const std::string buttonName = "btn_achieve_" + name;
      button->setName(buttonName);
      button->setPosition({static_cast<float>((i % 2) * ((width - 40) / 2) + 20), static_cast<float>((i / 2) * 25 + 20)});
      button->setSelected(world->achievements().isAcquired(name));
      inner->sprites().addObject(button);
      achievements_[buttonName] = {name, category};
      toggles_[buttonName] = button;
      ++i;
    }
  inner->setSize(width, (i / 2) * 25 + 30);
  if (auto* sc = dynamic_cast<RScrollable*>(scroller))
    if (ui::Window* interior = sc->interior()) ui::fitToChildren(*interior);
}

VButton* VChooseAchievements::toggle(const std::string& buttonName) const {
  const auto it = toggles_.find(buttonName);
  return it == toggles_.end() ? nullptr : it->second.get();
}

bool VChooseAchievements::onMessage(const ui::Message& m) {
  if (m.type != ui::kButtonPress) return false;
  GreatCCWorld* world = worldOf(ctx_);
  if (!world) return false;
  AchievementManager& ach = world->achievements();
  if (m.name == "btn_notify") {
    world->notices().show(ach.acquiredNames(), 3000);
    return true;
  }
  if (m.name == "btn_release" || m.name == "btn_acquire") {
    const bool acquire = m.name == "btn_acquire";
    if (acquire) ach.acquireAll();
    else ach.releaseAll();
    for (auto& [name, t] : toggles_) t->setSelected(acquire);
    return true;
  }
  const auto it = achievements_.find(m.name);
  const auto t = toggles_.find(m.name);
  if (it == achievements_.end() || t == toggles_.end()) return false;
  // the toggle flipped itself before the message: its new state is what the player asked for
  if (t->second->selected()) ach.acquire(it->second.second, it->second.first);
  else ach.release(it->second.second, it->second.first);
  return true;
}

std::string VChooseAchievements::detail() const {
  int on = 0;
  for (const auto& [name, t] : toggles_) on += t->selected() ? 1 : 0;
  return VSpriteWindow::detail() + " toggles=" + std::to_string(toggles_.size()) + " selected=" + std::to_string(on);
}

}  // namespace choco::games::greatcc
