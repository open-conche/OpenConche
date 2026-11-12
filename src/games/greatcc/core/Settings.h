#pragma once

// What the original keeps in TSettings (docs/re/greatcc-boot-and-natives.md 2.2) plus the engine's own switches. Pure data:
// the current game mode is mirrored into the global `gamemode` pref by the world, the user by UserStore.

namespace choco::games::greatcc {

// The four high-score tables (TPfHiscores mode names, boot 5.5); the mode is passed to the store explicitly.
inline constexpr const char* kModeNames[4] = {"greatchocolatechase_storymode", "greatchocolatechase_endlesseasy",
                                              "greatchocolatechase_endlessmedium", "greatchocolatechase_endlesshard"};

struct Settings {
  // The original-bug switch (design section 7): `fixbugs` in Config, read once at attach. Missing or anything but "0" is on.
  bool fixBugs = true;
  int mode = 0;        // 0 story, 1..3 endless easy/medium/hard
  int difficulty = 0;  // a UI value only: SetCurrentDifficulty(d) maps to mode d+1
  bool insideGame = false;
  bool gameLoaded = false;
  bool quitting = false;

  const char* hiscoreMode() const { return kModeNames[mode < 0 || mode > 3 ? 0 : mode]; }
  // SetCurrentGameMode: n < 4 only; zeroes the difficulty. True when it applied.
  bool setMode(int n) {
    if (n < 0 || n > 3) return false;
    mode = n;
    difficulty = 0;
    return true;
  }
  // SetCurrentDifficulty: d < 3 only; in an endless mode first moves to mode d + 1. True when it applied.
  bool setDifficulty(int d) {
    if (d < 0 || d > 2) return false;
    if (mode > 0) setMode(d + 1);
    difficulty = d;
    return true;
  }
};

}  // namespace choco::games::greatcc
