#include "games/choc1/ChoosePlayer.h"

#include <string>

#include "sdk/Users.h"
#include "ui/WindowManager.h"
#include "ui/widgets/TextWidgets.h"
#include "ui/widgets/Widgets.h"

namespace choco::game::choc1 {

PlayerLayout playerLayout(int selected, int userCount) {
  PlayerLayout out;
  out.slots.resize(kMaxPlayers);
  out.newPlayerShown = userCount < kMaxPlayers;
  for (int i = 0; i < kMaxPlayers; ++i) {
    PlayerSlot &s = out.slots[static_cast<std::size_t>(i)];
    if (i < userCount) {
      s.shown = true;
      s.label = PlayerSlot::Label::UserName;
    } else if (i == userCount) {
      s.shown = true;
      s.label = PlayerSlot::Label::NewPlayer;
    }
    if (i == selected && s.shown) {
      s.radioOn = i < userCount; // the new-player entry's radio stays off
      s.editShown = i < userCount;
    }
  }
  return out;
}

void ChoosePlayerWindow::update(int selected) {
  const int count = ctx_.users ? ctx_.users->count() : 0;
  const PlayerLayout layout = playerLayout(selected, count);
  if (ui::Window *w = find("newplayer"))
    w->setShown(layout.newPlayerShown);
  const std::string newPlayer =
      ui::resolveLabel("opt_newplayer", manager().labelLookup());
  for (int i = 0; i < kMaxPlayers; ++i) {
    const PlayerSlot &s = layout.slots[static_cast<std::size_t>(i)];
    const std::string n = std::to_string(i);
    if (ui::Window *player = find("player" + n)) {
      player->setShown(s.shown);
      if (auto *b = dynamic_cast<ui::ButtonWindow *>(player))
        b->setOn(s.radioOn);
    }
    if (ui::Window *label = find("label" + n)) {
      label->setShown(s.shown);
      if (s.label == PlayerSlot::Label::UserName)
        label->setLabel(ctx_.users->name(i));
      else if (s.label == PlayerSlot::Label::NewPlayer)
        label->setLabel(newPlayer);
    }
    if (ui::Window *edit = find("edit" + n))
      edit->setShown(s.editShown);
    if (ui::Window *del = find("delete" + n))
      del->setShown(s.editShown);
  }
}

void ChoosePlayerWindow::postChildrenInit(const ui::Style &style) {
  Window::postChildrenInit(style);
  update(ctx_.users ? ctx_.users->current() : -1);
}

namespace {
ChoosePlayerWindow *findIn(ui::Window &w) {
  if (auto *c = dynamic_cast<ChoosePlayerWindow *>(&w))
    return c;
  for (const auto &child : w.children())
    if (auto *c = findIn(*child))
      return c;
  return nullptr;
}
} // namespace

ChoosePlayerWindow *ChoosePlayerWindow::inChain(ui::WindowManager &wm) {
  const auto &modals = wm.modals();
  for (auto it = modals.rbegin(); it != modals.rend(); ++it)
    if (auto *c = findIn(**it))
      return c;
  return nullptr;
}

} // namespace choco::game::choc1
