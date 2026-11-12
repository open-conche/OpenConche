#pragma once

// TMainMenu (docs/re/greatcc-ui-library.md 7, docs/re/greatcc-meta-flow.md 7): the window of scripts/mainmenu.lua. Its constructor
// binds SetWelcomeText and MainDebug (every achievement acquired and saved; no script calls it). After its
// children exist it hides the debug button, fills the welcome and user texts, becomes its modal's default focus, and hides
// `moregames` when xsell/xsell.lua is missing (it never ships). The menu music (audio/music/theme.ogg) is slice 5.

#include "games/greatcc/natives/ScopedNatives.h"
#include "script/ScriptContext.h"
#include "ui/Window.h"

namespace choco::games::greatcc {

class TMainMenu : public ui::Window {
 public:
  TMainMenu(ui::WindowManager& wm, script::ScriptContext& ctx);
  std::string_view typeName() const override { return "TMainMenu"; }
  void postChildrenInit(const ui::Style& style) override;
  // Engine addition (the original has no way to reach MainDebug): Ctrl+D, with cheatmode on and this menu the top modal, runs MainDebug
  // and logs one line. Without cheatmode the key does nothing. Never consumed.
  bool onKeyDown(const ui::KeyEvent& ev) override;
  // The modal push and pop leave the key focus on the modal itself; while this menu is the top modal's default focus the focus moves here.
  bool onAnimate() override;
  // MainDebug: every achievement acquired and saved, nothing announced.
  void mainDebug();

  // SetWelcomeText: txt_welcome and txt_welcome_shadow get string `mainmenu_welcome` with "," as its argument when a user
  // exists and "!" when not; txt_user and txt_user_shadow get "<name>!" when a user exists and "" when not.
  void setWelcomeText();

 private:
  script::ScriptContext& ctx_;
  ScopedNatives natives_;
};

}  // namespace choco::games::greatcc
