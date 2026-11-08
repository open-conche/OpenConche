#include "games/choc2/Choc2MenuNatives.h"

#include <cstdio>
#include <memory>
#include <optional>
#include <string>

#include "sdk/Users.h"
#include "games/choc2/CreditsWindow.h"
#include "games/choc2/HighScoresWindow.h"
#include "games/choc2/OptionsWindow.h"
#include "script/ScriptHost.h"
#include "script/Stubs.h"

namespace choco::script {

namespace {

// A single-quoted Lua string literal's body for `s` (the submit dialog loadstring's what GetSubmitInfo returns).
std::string luaQuote(const std::string& s) {
  std::string out;
  for (unsigned char c : s) {
    if (c == '\\' || c == '\'') {
      out += '\\';
      out += static_cast<char>(c);
    } else if (c == '\n') {
      out += "\\n";
    } else if (c < 32 || c == 127) {  // three digits, so a digit that follows is not read as part of the escape
      char esc[8];
      std::snprintf(esc, sizeof esc, "\\%03d", c);
      out += esc;
    } else {
      out += static_cast<char>(c);
    }
  }
  return out;
}

// GetSubmitInfo: Lua source that the submit dialog (ui/hiscoresubmit.lua) runs before it builds its fields. It reads the
// globals gName, gUsername, gPassword, gRemember and gScore. There are no online accounts: the player name is the current
// user's, the account fields are empty and the eligible score is 0.
std::string nativeGetSubmitInfo(ScriptContext& ctx) {
  const std::string name = ctx.users ? ctx.users->currentName() : std::string();
  return "gName='" + luaQuote(name) + "'; gUsername=''; gPassword=''; gRemember=false; gScore=0";
}

// SubmitScore(name, username, password, remember) and SubmitMedals(medalFlags, name, username, password, remember): the
// dialog's Submit button. They would send to PlayFirst's server, which no longer exists and which this engine never
// contacts; the dialog closes itself afterwards. Logged once, as the Chocolatier 3 SubmitToServer is.
void nativeSubmitScore(ScriptContext& ctx, std::string, std::string, std::string, std::optional<bool>) {
  if (ctx.stubs.hit("SubmitScore")) ctx.log.write("[unimplemented] SubmitScore (offline; nothing is submitted)");
}

void nativeSubmitMedals(ScriptContext& ctx, double, std::string, std::string, std::string, std::optional<bool>) {
  if (ctx.stubs.hit("SubmitMedals")) ctx.log.write("[unimplemented] SubmitMedals (offline; nothing is submitted)");
}

}  // namespace

void registerChoc2MenuNatives(lua_State* L, ScriptContext& ctx) {
  ScriptContext* c = &ctx;
  ctx.wm.addWindowType("Options", [c](ui::WindowManager& wm) { return std::make_unique<game::choc2::Options>(wm, *c); });
  ctx.wm.addWindowType("Credits", [](ui::WindowManager& wm) { return std::make_unique<game::choc2::Credits>(wm); });
  game::choc2::registerHighScoresNatives(L, ctx);
  bind<nativeGetSubmitInfo>(L, ctx, "GetSubmitInfo");
  bind<nativeSubmitScore>(L, ctx, "SubmitScore");
  bind<nativeSubmitMedals>(L, ctx, "SubmitMedals");
}

}  // namespace choco::script
