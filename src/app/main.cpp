// SDL_main.h turns this into the real entry point where one is needed. On Windows that is WinMain for the GUI
// subsystem, and it hands us UTF-8 argv (the plain CRT would give the ANSI code page). Include it in this file only.
#include <SDL3/SDL_main.h>

#include <string>
#include <vector>

#include "app/App.h"
#include "platform/Console.h"

int main(int argc, char* argv[]) {
  std::vector<std::string> args(argv + 1, argv + argc);
  bool headless = false;
  for (const auto& a : args) headless = headless || a == "--headless";
  choco::platform::attachConsoleIfNeeded();

  auto parsed = choco::app::parseArgs(args);
  if (!parsed.ok()) {
    choco::platform::reportUsageError("OpenConche", "error: " + parsed.error + (parsed.shortError ? "\n" : "\n\n" + choco::app::usageText()),
                                      !headless);
    return 2;
  }
  if (parsed.options.help) {
    choco::platform::reportInfo("OpenConche", choco::app::usageText(), !parsed.options.headless);
    return 0;
  }
  if (parsed.options.version) {
    choco::platform::reportInfo("OpenConche", choco::app::versionText(), !parsed.options.headless);
    return 0;
  }
  // The in-game game switcher (Ctrl+Shift+G) reboots into the chosen game: the same process, a fresh App, so every
  // engine state (script context, audio, window) is rebuilt exactly as a launch would.
  choco::app::Options options = parsed.options;
  int rc = 0;
  for (;;) {
    choco::app::App app(options);
    rc = app.run();
    if (!app.takeReboot(options)) break;
  }
  return rc;
}
