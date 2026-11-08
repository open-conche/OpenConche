#pragma once

#include <chrono>
#include <string>

#include "app/Args.h"

namespace choco::app {

// "<userDir>-backup-YYYYMMDD-HHMM" for the given UTC time (a trailing slash on userDir is dropped).
std::string backupDirName(const std::string& userDir, std::chrono::system_clock::time_point when);

// Copies `userDir` recursively to a new backup folder next to it (a "-2", "-3"... suffix if the name is taken). Returns the
// folder, "" when there was nothing to back up (the folder is missing or empty), and sets `error` if the copy failed.
std::string backUpUserDir(const std::string& userDir, std::chrono::system_clock::time_point when, std::string& error);

// --import-prefs: reads the file (read-only), backs up the user dir, imports into it, prints the summary. Never starts the
// game and never writes to the source. Exit codes as the rest of the CLI: 0 ok, 1 the file could not be imported, 2 usage.
int runImportPrefs(const Options& options);

}  // namespace choco::app
