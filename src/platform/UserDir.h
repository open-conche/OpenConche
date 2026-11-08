#pragma once

#include <string>

namespace choco::platform {

// Default per-user folder for settings and saves (UTF-8):
// macOS ~/Library/Application Support/<leaf>, Windows %APPDATA%\<leaf>,
// elsewhere $XDG_DATA_HOME/<leaf> (falling back to ~/.local/share/<leaf>).
// Empty if no home folder can be found. Does not create anything.
//
// `leaf` is the folder name, the game profile's `userDirName` (each game has its own, so one game never loads another's
// saves); it is lower-cased on the XDG path.
std::string defaultUserDir(const std::string& leaf);

// Folder holding the running executable (UTF-8, from SDL_GetBasePath), or empty if unknown.
std::string executableDir();

// After a write under the user folder: nothing on a native build, where the file is on disk already. The web build's
// user folder is an IDBFS mount (an in-memory file system the browser persists to IndexedDB only when asked), so it is
// flushed here; with leaveAfter the page goes back to the site's game chooser once the flush is done, which is how a web
// run ends (cmake/Web.cmake).
void syncUserDir(bool leaveAfter = false);

}  // namespace choco::platform
