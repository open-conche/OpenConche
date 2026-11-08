#pragma once

#include <string_view>

namespace choco::ui {

// The Options screen's volume sliders, by the child name the Lua gives them, and the per-user preference each one edits.
// Chocolatier 3 names them `sfxlevelslider` / `musiclevelslider` / `ambientlevelslider` (ui/ui_options.lua); Chocolatier 1
// and 2 name them `sfxslider` / `musicslider` / `ambientslider` (docs/re/choc1-natives.md section 9, ui/options.lua). Both
// sets are accepted in every game: the names do not collide, and only the screen that has them is affected.
struct VolumeSlider {
  const char* name;
  const char* pref;     // soundlevel, musiclevel or ambientlevel: per mille in the user's prefs
  int defaultPerMille;  // used when the pref is unset (docs/design/audio.md section 1)
  int channel;          // 0 sound effects, 1 music, 2 ambient
};

inline constexpr VolumeSlider kVolumeSliders[] = {
    {"sfxlevelslider", "soundlevel", 750, 0}, {"musiclevelslider", "musiclevel", 650, 1},
    {"ambientlevelslider", "ambientlevel", 500, 2}, {"sfxslider", "soundlevel", 750, 0},
    {"musicslider", "musiclevel", 650, 1}, {"ambientslider", "ambientlevel", 500, 2},
};

// The slider row for a child name, or null.
inline const VolumeSlider* findVolumeSlider(std::string_view name) {
  for (const VolumeSlider& s : kVolumeSliders)
    if (name == s.name) return &s;
  return nullptr;
}

}  // namespace choco::ui
