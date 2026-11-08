#include "platform/Console.h"

#include <SDL3/SDL.h>

#include <cstdio>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace choco::platform {

namespace {
#ifdef _WIN32
bool g_output = false;

// A non-null std handle is not proof of usable stdio (GUI processes can inherit unrelated handles, and the CRT
// rejects FILE_TYPE_UNKNOWN). DISK and PIPE mean redirected; CHAR means a console. UNKNOWN means absent.
bool usableStream(DWORD which) {
  const HANDLE h = GetStdHandle(which);
  if (h == nullptr || h == INVALID_HANDLE_VALUE) return false;
  const DWORD t = GetFileType(h);
  return t == FILE_TYPE_DISK || t == FILE_TYPE_PIPE || t == FILE_TYPE_CHAR;
}
#endif
}  // namespace

void attachConsoleIfNeeded() {
#ifdef _WIN32
  const bool outOk = usableStream(STD_OUTPUT_HANDLE);
  const bool errOk = usableStream(STD_ERROR_HANDLE);
  g_output = outOk || errOk;
  if (outOk && errOk) return;
  if (!AttachConsole(ATTACH_PARENT_PROCESS)) return;  // no parent console: stay silent
  FILE* f = nullptr;
  if (!outOk && freopen_s(&f, "CONOUT$", "w", stdout) == 0) g_output = true;
  if (!errOk && freopen_s(&f, "CONOUT$", "w", stderr) == 0) g_output = true;
  std::setvbuf(stdout, nullptr, _IONBF, 0);
#endif
}

bool hasConsoleOutput() {
#ifdef _WIN32
  return g_output;
#else
  return true;
#endif
}

void showErrorIfNoConsole(const std::string& title, const std::string& text) {
  if (!hasConsoleOutput()) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title.c_str(), text.c_str(), nullptr);
}

void reportInfo(const std::string& title, const std::string& text, bool interactive) {
  if (interactive && !hasConsoleOutput()) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, title.c_str(), text.c_str(), nullptr);
    return;
  }
  std::fputs(text.c_str(), stdout);
}

void reportUsageError(const std::string& title, const std::string& text, bool interactive) {
  std::fputs(text.c_str(), stderr);
  if (interactive) showErrorIfNoConsole(title, text);
}

}  // namespace choco::platform
