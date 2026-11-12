#include "games/greatcc/ui/TMainMenu.h"

#include "games/greatcc/GreatCCWorld.h"
#include "sdk/Users.h"
#include "ui/WindowManager.h"

namespace choco::games::greatcc {

namespace {
int luaSetWelcomeText(lua_State* L) {
  script::ScriptContext& ctx = script::contextOf(L);
  if (auto* w = dynamic_cast<TMainMenu*>(ctx.wm.window(ScopedNatives::ownerOf(L)))) w->setWelcomeText();
  return 0;
}
// MainDebug (TMainMenu::MainDebug): every achievement is acquired and saved, with no notification and without the end-level list. No
// script calls it (the menu's debug button is hidden and opens the upsell dialog).
int luaMainDebug(lua_State* L) {
  if (GreatCCWorld* w = worldOf(script::contextOf(L))) w->achievements().acquireAll();
  return 0;
}
}  // namespace

TMainMenu::TMainMenu(ui::WindowManager& wm, script::ScriptContext& ctx) : Window(wm), ctx_(ctx), natives_(ctx) {
  natives_.bindOwned("SetWelcomeText", &luaSetWelcomeText, id());
  natives_.bindOwned("MainDebug", &luaMainDebug, id());
}

void TMainMenu::postChildrenInit(const ui::Style& style) {
  Window::postChildrenInit(style);
  if (ui::Window* debug = find("mainmenu_debug_button")) debug->setShown(false);
  manager().startWindowAnimation(id(), 100);
  setWelcomeText();
  // PlayMusic(theme, loop, fade) every time the menu is built: the same track already playing is left alone.
  if (GreatCCWorld* w = worldOf(ctx_)) w->sound().playMusic("audio/music/theme.ogg", true, true);
  for (ui::Window* p = parent(); p; p = p->parent())
    if (auto* modal = dynamic_cast<ui::ModalWindow*>(p)) {
      modal->defaultFocus = id();
      break;
    }
  if (!ctx_.fs.exists("xsell/xsell.lua"))
    if (ui::Window* more = find("moregames")) more->setShown(false);
}

void TMainMenu::mainDebug() {
  if (GreatCCWorld* w = worldOf(ctx_)) w->achievements().acquireAll();
}

bool TMainMenu::onKeyDown(const ui::KeyEvent& ev) {
  if (!ev.ctrl || ev.name.size() != 1 || (ev.name[0] != 'd' && ev.name[0] != 'D')) return false;
  if (!cheatModeOn(ctx_)) return false;
  const ui::Window* modal = parent();
  while (modal && !modal->isModal()) modal = modal->parent();
  if (!modal || manager().topModal() != modal) return false;
  mainDebug();
  ctx_.log.write("[greatcc] MainDebug: all achievements acquired");
  return false;
}

bool TMainMenu::onAnimate() {
  const ui::Window* modal = parent();
  while (modal && !modal->isModal()) modal = modal->parent();
  if (modal && manager().topModal() == modal && manager().focus() == modal->id()) manager().setFocus(id());
  return true;
}

void TMainMenu::setWelcomeText() {
  std::string user;
  std::string separator = "!";
  if (ctx_.users && ctx_.users->count() > 0) {
    user = ctx_.users->currentName() + "!";
    separator = ",";
  }
  const std::string welcome = ctx_.strings.sdkLookup("mainmenu_welcome", {separator});
  for (const char* n : {"txt_welcome_shadow", "txt_welcome"})
    if (ui::Window* w = find(n); w && w->hasLabel()) w->setLabel(welcome);
  for (const char* n : {"txt_user_shadow", "txt_user"})
    if (ui::Window* w = find(n); w && w->hasLabel()) w->setLabel(user);
}

}  // namespace choco::games::greatcc
