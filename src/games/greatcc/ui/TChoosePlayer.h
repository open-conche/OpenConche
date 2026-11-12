#pragma once

// TChoosePlayer (docs/re/greatcc-meta-flow.md 7): the player picker of scripts/chooseplayer.lua. Its constructor binds
// SetPlayerChoices, GetSelectedUserIndex, GetSelectedUserName and SetNameField; after its children exist it runs
// SetPlayerChoices once. Players live in the engine's user store.

#include <string>
#include <vector>

#include "games/greatcc/natives/ScopedNatives.h"
#include "script/ScriptContext.h"
#include "ui/Window.h"

namespace choco::games::greatcc {

// The Lua line SetPlayerChoices runs: `UpdatePlayerChoices( <current>, <count>, {'a','b'} )`, with at most 10 names (the
// count is the real user count, as in the original). Names are escaped for a single-quoted Lua string.
std::string updatePlayerChoicesLine(int current, int count, const std::vector<std::string>& names);

class TChoosePlayer : public ui::Window {
 public:
  static constexpr int kMaxButtons = 10;
  TChoosePlayer(ui::WindowManager& wm, script::ScriptContext& ctx);
  std::string_view typeName() const override { return "TChoosePlayer"; }
  void postChildrenInit(const ui::Style& style) override;

  void setPlayerChoices();
  // The first of chooseplayer_button_0..9 that is switched on, else 0.
  int selectedUserIndex() const;
  std::string selectedUserName() const;
  // The label of the Text named `namefield` inside the button `buttonName`.
  void setNameField(const std::string& buttonName, const std::string& text);

 private:
  script::ScriptContext& ctx_;
  ScopedNatives natives_;
};

}  // namespace choco::games::greatcc
