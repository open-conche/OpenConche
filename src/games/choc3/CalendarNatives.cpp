// Date and DayOfYear: Chocolatier 3's calendar natives, bound through GameModule::registerCoreExtras.
#include "games/choc3/Choc3Natives.h"

#include "games/common/Calendar.h"
#include "script/Bind.h"
#include "script/ScriptHost.h"

namespace choco::script {

namespace {

std::string nativeDate(ScriptContext& ctx, double weeks) { return game::dateText(ctx.strings, weeks); }

int nativeDayOfYear(ScriptContext&, double weeks) { return game::dayOfYear(weeks); }

}  // namespace

void registerCalendarNatives(lua_State* L, ScriptContext& ctx) {
  bind<nativeDate>(L, ctx, "Date");
  bind<nativeDayOfYear>(L, ctx, "DayOfYear");
}

}  // namespace choco::script
