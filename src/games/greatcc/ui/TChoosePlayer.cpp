#include "games/greatcc/ui/TChoosePlayer.h"

#include "sdk/Users.h"
#include "ui/WindowManager.h"
#include "ui/widgets/Widgets.h"

namespace choco::games::greatcc {

namespace {

TChoosePlayer* ownerWindow(lua_State* L) {
  script::ScriptContext& ctx = script::contextOf(L);
  return dynamic_cast<TChoosePlayer*>(ctx.wm.window(ScopedNatives::ownerOf(L)));
}

int luaSetPlayerChoices(lua_State* L) {
  if (auto* w = ownerWindow(L)) w->setPlayerChoices();
  return 0;
}
int luaGetSelectedUserIndex(lua_State* L) {
  auto* w = ownerWindow(L);
  lua_pushnumber(L, w ? w->selectedUserIndex() : 0);
  return 1;
}
int luaGetSelectedUserName(lua_State* L) {
  auto* w = ownerWindow(L);
  const std::string s = w ? w->selectedUserName() : std::string();
  lua_pushlstring(L, s.data(), s.size());
  return 1;
}
int luaSetNameField(lua_State* L) {
  const char* button = luaL_checkstring(L, 1);
  const char* text = luaL_checkstring(L, 2);
  if (auto* w = ownerWindow(L)) w->setNameField(button, text);
  return 0;
}

}  // namespace

std::string updatePlayerChoicesLine(int current, int count, const std::vector<std::string>& names) {
  std::string list = "{";
  for (int i = 0; i < count && i < TChoosePlayer::kMaxButtons && i < static_cast<int>(names.size()); ++i) {
    list += '\'';
    for (char c : names[static_cast<std::size_t>(i)]) {
      if (c == '\'' || c == '\\') list += '\\';
      if (c == '\n') list += "\\n";
      else list += c;
    }
    list += "',";
  }
  list += "}";
  return "UpdatePlayerChoices( " + std::to_string(current) + ", " + std::to_string(count) + ", " + list + " )";
}

TChoosePlayer::TChoosePlayer(ui::WindowManager& wm, script::ScriptContext& ctx) : Window(wm), ctx_(ctx), natives_(ctx) {
  natives_.bindOwned("SetPlayerChoices", &luaSetPlayerChoices, id());
  natives_.bindOwned("GetSelectedUserIndex", &luaGetSelectedUserIndex, id());
  natives_.bindOwned("GetSelectedUserName", &luaGetSelectedUserName, id());
  natives_.bindOwned("SetNameField", &luaSetNameField, id());
}

void TChoosePlayer::postChildrenInit(const ui::Style& style) {
  Window::postChildrenInit(style);
  setPlayerChoices();
}

void TChoosePlayer::setPlayerChoices() {
  if (!ctx_.users) return;
  std::vector<std::string> names;
  const int n = ctx_.users->count();
  for (int i = 0; i < n && i < kMaxButtons; ++i) names.push_back(ctx_.users->name(i));
  if (ui::ScriptSink* sink = manager().scriptSink()) sink->runString(updatePlayerChoicesLine(ctx_.users->current(), n, names));
}

int TChoosePlayer::selectedUserIndex() const {
  for (int i = 0; i < kMaxButtons; ++i)
    if (auto* b = dynamic_cast<const ui::ButtonWindow*>(find("chooseplayer_button_" + std::to_string(i))); b && b->on()) return i;
  return 0;
}

std::string TChoosePlayer::selectedUserName() const { return ctx_.users ? ctx_.users->name(selectedUserIndex()) : std::string(); }

void TChoosePlayer::setNameField(const std::string& buttonName, const std::string& text) {
  ui::Window* button = find(buttonName);
  ui::Window* field = button ? button->find("namefield") : nullptr;
  if (field && field->hasLabel()) field->setLabel(text);
}

}  // namespace choco::games::greatcc
