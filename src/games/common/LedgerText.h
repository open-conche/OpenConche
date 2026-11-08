#pragma once

#include <string>

#include "script/Formats.h"
#include "script/LuaApi.h"
#include "script/ScriptContext.h"
#include "ui/Window.h"

namespace choco::game {

// Text helpers shared by Chocolatier 3's LedgerWindow and Chocolatier 2's Ledger.

// Sets a window's label when it has one; a missing window is skipped.
inline void ledgerSetText(ui::Window* w, std::string text) {
  if (w && w->hasLabel()) w->setLabel(std::move(text));
}

}  // namespace choco::game
