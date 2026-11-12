#include "games/greatcc/meta/VQuestWindow.h"

#include <cstdio>

#include "games/greatcc/GreatCCWorld.h"
#include "ui/WindowManager.h"
#include "ui/widgets/TextWidgets.h"

namespace choco::games::greatcc {

namespace {
constexpr double kUpdateMs = 300;

std::string hex2(float c) {
  char buf[8];
  std::snprintf(buf, sizeof buf, "%02x", static_cast<unsigned>(static_cast<int>(c * 255.0f)) & 0xff);
  return buf;
}
}  // namespace

void VQuestWindow::postChildrenInit(const ui::Style& style) {
  if (ui::Window* w = find("txt_quest_status"); dynamic_cast<ui::TextWindow*>(w)) status_ = w->id();
  if (ui::Window* w = find("txt_quest_names"); dynamic_cast<ui::TextWindow*>(w)) names_ = w->id();
  if (ui::Window* w = find("background")) background_ = w->id();
  marginY_ = static_cast<int>(style.numberOr("marginY", 0));
  if (const auto c = style.numbers("outlinecolor", 4, 1.0))
    outlineColor_ = {static_cast<float>((*c)[0]), static_cast<float>((*c)[1]), static_cast<float>((*c)[2]), 1.0f};
  outlineSize_ = static_cast<int>(style.numberOr("outlinesize", 0));
  if (const auto t = style.table("colors")) {
    for (int i = 1; i <= t->size(); ++i) {
      gfx::ColorF c{1, 1, 1, 1};
      if (const auto e = t->element(i))
        c = {static_cast<float>(e->numberAt(1).value_or(1)), static_cast<float>(e->numberAt(2).value_or(1)),
             static_cast<float>(e->numberAt(3).value_or(1)), 1.0f};
      colors_.push_back(c);
    }
  }
  Window::postChildrenInit(style);
}

bool VQuestWindow::updateText() {
  GreatCCWorld* world = worldOf(ctx_);
  lines_ = 0;
  if (!world) return false;
  QuestManager& quests = world->quests();
  const auto& active = quests.activeQuests();
  if (active.empty()) return false;
  std::string left, right;
  if (outlineSize_ > 0) {
    left = right = "<outline color='" + hex2(outlineColor_.r) + hex2(outlineColor_.g) + hex2(outlineColor_.b) + "' size=" +
                   std::to_string(outlineSize_) + ">";
  }
  for (Quest* q : active) {
    const int status = quests.currentStatus(*q);
    gfx::ColorF c{1, 1, 1, 1};
    if (status >= 0 && status < static_cast<int>(colors_.size())) c = colors_[static_cast<std::size_t>(status)];
    const std::string open = "<font color='" + hex2(c.r) + hex2(c.g) + hex2(c.b) + "'>";
    left += open + quests.name(*q) + "</font>\n";
    right += open + quests.statusInfo(*q) + "</font>\n";
    ++lines_;
  }
  if (outlineSize_ > 0) {
    left += "</outline>";
    right += "</outline>";
  }
  if (ui::Window* w = manager().window(names_)) w->setLabel(left);
  if (ui::Window* w = manager().window(status_)) w->setLabel(right);
  return true;
}

void VQuestWindow::levelInit() {
  shown_ = updateText();
  setShown(shown_);
  if (ui::Window* bg = manager().window(background_)) {
    int bottom = rect().h;
    if (auto* names = dynamic_cast<ui::TextWindow*>(manager().window(names_)))
      bottom = names->textBounds().h + names->rect().y + marginY_;
    bg->setPos(bg->rect().x, bottom - rect().h);
  }
  manager().startWindowAnimation(id(), kUpdateMs);
}

std::string VQuestWindow::detail() const {
  return " quests=" + std::to_string(lines_);
}

}  // namespace choco::games::greatcc
