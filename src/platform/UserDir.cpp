#include "platform/UserDir.h"

#include <SDL3/SDL.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include <cctype>
#include <cstdlib>
#include <filesystem>

namespace choco::platform {

namespace {
std::string env(const char* name) {
#ifdef _WIN32
  // Wide API: the narrow one returns the ANSI code page, but the engine's paths are UTF-8.
  std::wstring wname;
  for (const char* p = name; *p; ++p) wname += static_cast<wchar_t>(*p);
  wchar_t* buf = nullptr;
  size_t len = 0;
  if (_wdupenv_s(&buf, &len, wname.c_str()) != 0 || !buf) return {};
  const std::u8string u = std::filesystem::path(std::wstring(buf)).u8string();
  std::free(buf);
  return std::string(u.begin(), u.end());
#else
  const char* v = std::getenv(name);
  return v ? v : "";
#endif
}
}  // namespace

std::string defaultUserDir(const std::string& leaf) {
#if defined(_WIN32)
  const std::string appdata = env("APPDATA");
  return appdata.empty() ? std::string() : appdata + "\\" + leaf;
#elif defined(__APPLE__)
  const std::string home = env("HOME");
  return home.empty() ? std::string() : home + "/Library/Application Support/" + leaf;
#else
  std::string lower = leaf;
  for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  const std::string xdg = env("XDG_DATA_HOME");
  if (!xdg.empty()) return xdg + "/" + lower;
  const std::string home = env("HOME");
  return home.empty() ? std::string() : home + "/.local/share/" + lower;
#endif
}

}  // namespace choco::platform

namespace choco::platform {

std::string executableDir() {
  const char* p = SDL_GetBasePath();
  return p ? std::string(p) : std::string();
}

void syncUserDir(bool leaveAfter) {
#ifdef __EMSCRIPTEN__
  // chocoSync is the page's (packaging/web/index.html): it serialises FS.syncfs calls and reloads when asked.
  EM_ASM({ chocoSync($0); }, leaveAfter ? 1 : 0);
#else
  (void)leaveAfter;
#endif
}

}  // namespace choco::platform
