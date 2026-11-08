#pragma once

#include <cmath>
#include <cstdlib>
#include <functional>
#include <string>

namespace choco::audio {

// The per-user audio preferences (docs/design/audio.md s.1): per mille in prefs.txt, 0..1 in the mixer.
struct Levels {
  float sfx = 0.75f, music = 0.65f, env = 0.5f;
  bool muted = false;
};

inline constexpr int kDefaultSoundPerMille = 750, kDefaultMusicPerMille = 650, kDefaultAmbientPerMille = 500;

// "650" -> 0.65. Empty or non-numeric text gives the default; the result is clamped to 0..1.
inline float levelFromPerMille(const std::string& text, int defaultPerMille) {
  char* end = nullptr;
  const double v = text.empty() ? 0.0 : std::strtod(text.c_str(), &end);
  const bool numeric = !text.empty() && end != text.c_str() && std::isfinite(v);  // "nan" and "inf" parse, but are garbage
  const double pm = numeric ? v : static_cast<double>(defaultPerMille);
  return static_cast<float>(pm < 0 ? 0.0 : pm > 1000 ? 1.0 : pm / 1000.0);
}

// 0.65 -> 650, as TOptions::SaveVolumes stores it (times 1000, truncated). The nudge keeps 650/1000*1000 from truncating
// to 649 through float rounding, so a saved value round-trips.
inline int perMilleFromLevel(float level) { return static_cast<int>(level * 1000.0f + 0.001f); }

// Reads the four prefs through `pref(key)`, which returns "" for a key that is not set.
inline Levels levelsFromPrefs(const std::function<std::string(const char*)>& pref) {
  Levels l;
  l.sfx = levelFromPerMille(pref("soundlevel"), kDefaultSoundPerMille);
  l.music = levelFromPerMille(pref("musiclevel"), kDefaultMusicPerMille);
  l.env = levelFromPerMille(pref("ambientlevel"), kDefaultAmbientPerMille);
  l.muted = pref("soundoff") == "1";
  return l;
}

}  // namespace choco::audio
