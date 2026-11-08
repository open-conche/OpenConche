#include "games/common/LegacyUtilNatives.h"

#include <ctime>

#include "sdk/Users.h"

namespace choco::script {

std::string legacyTimeStamp(ScriptContext&) {
  const std::time_t t = std::time(nullptr);
  std::tm tmv{};
#ifdef _WIN32
  localtime_s(&tmv, &t);
#else
  localtime_r(&t, &tmv);
#endif
  char buf[64];
  std::strftime(buf, sizeof buf, "%Y.%m.%d-%H.%M.%S", &tmv);
  return buf;
}

std::string legacyNextSaveFile(ScriptContext&) { return {}; }

void legacySetCurrentGameMode(ScriptContext& ctx, int mode) {
  if (ctx.users) ctx.users->setPref("gamemode", std::to_string(mode));
}

}  // namespace choco::script
