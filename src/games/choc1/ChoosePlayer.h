#pragma once

// Chocolatier 1's ChoosePlayer window (TChoosePlayer, exe vtable 004e6bd4;
// docs/re/choc1-natives.md section 9): a container whose Lua children are found
// by name: `player%d` (a radio Button), `label%d` (Text), `edit%d`, `delete%d`
// for the ten slots 0..9, and `newplayer`. The native
// UpdatePlayerButtons(selected) lays them out for the users that exist.

#include <vector>

#include "script/ScriptContext.h"
#include "ui/Window.h"

namespace choco::game::choc1 {

constexpr int kMaxPlayers = 10;

// What one slot shows. `label` says which text goes in the slot's label: the
// user's name or the "new player" string.
struct PlayerSlot {
  enum class Label { None, UserName, NewPlayer } label = Label::None;
  bool shown = false;     // player%d and label%d enabled
  bool radioOn = false;   // player%d toggled on
  bool editShown = false; // edit%d and delete%d enabled
};

struct PlayerLayout {
  std::vector<PlayerSlot> slots; // kMaxPlayers entries
  bool newPlayerShown =
      true; // the `newplayer` button: off once ten users exist
};

// The rules of UpdatePlayerButtons (exe 00407180) for `userCount` users and the
// highlighted slot `selected`: newplayer is disabled at ten users; slot i <
// userCount shows its user's name; i == userCount is the "new player" entry
// (radio off); i > userCount is disabled. The selected slot's radio is on and
// its edit/delete are enabled, the others' off. INFERENCE: the exe's order of
// those steps for the "new player" entry is not read; edit/delete are enabled
// only for a slot that holds a user.
PlayerLayout playerLayout(int selected, int userCount);

class ChoosePlayerWindow : public ui::Window {
public:
  ChoosePlayerWindow(ui::WindowManager &wm, script::ScriptContext &ctx)
      : Window(wm), ctx_(ctx) {}
  std::string_view typeName() const override { return "ChoosePlayer"; }
  // The first layout: the current user is the highlighted slot (the Lua starts
  // from gSelectedPlayer = GetCurrentUser()).
  void postChildrenInit(const ui::Style &style) override;
  void update(int selected);

  // The ChoosePlayer window anywhere in the modal chain, or null.
  static ChoosePlayerWindow *inChain(ui::WindowManager &wm);

private:
  script::ScriptContext &ctx_;
};

} // namespace choco::game::choc1
