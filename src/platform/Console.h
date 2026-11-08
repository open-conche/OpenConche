#pragma once

#include <string>

namespace choco::platform {

// On Windows the exe uses the GUI subsystem, so it starts without a console. Attach to the parent's console if there
// is one (a no-op without) and point any unusable stdout/stderr at it; streams redirected to a file or pipe (as under
// CTest) are left alone. A no-op elsewhere. Call first thing in main.
void attachConsoleIfNeeded();

// True when stderr can reach someone: always off Windows, on Windows only when a console or redirect is present.
bool hasConsoleOutput();

// Makes a message visible when stdout/stderr are not: shows a modal message box if there is no console output.
// Callers still print to stderr themselves (diag::err); this adds the box, and does nothing when a console exists.
void showErrorIfNoConsole(const std::string& title, const std::string& text);

// Prints text to stdout, or shows it in a message box when there is no console and `interactive` is true (--help).
void reportInfo(const std::string& title, const std::string& text, bool interactive);

// Usage errors: stderr, plus a message box when there is no console and `interactive` is true.
void reportUsageError(const std::string& title, const std::string& text, bool interactive);

}  // namespace choco::platform
