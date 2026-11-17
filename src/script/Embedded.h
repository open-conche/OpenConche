#pragma once

#include <cstddef>
#include <string_view>

namespace choco::script {

// Generated at build time from lua/prelude.lua and lua/host.lua (cmake/EmbedLua.cmake).
extern const unsigned char kEmbeddedPrelude[];
extern const std::size_t kEmbeddedPreludeSize;
extern const unsigned char kEmbeddedHost[];
extern const std::size_t kEmbeddedHostSize;
extern const unsigned char kEmbeddedAnimationScript[];
extern const std::size_t kEmbeddedAnimationScriptSize;

extern const unsigned char kEmbeddedEnhance[];
extern const std::size_t kEmbeddedEnhanceSize;
extern const unsigned char kEmbeddedEnhanceChoc1[];
extern const std::size_t kEmbeddedEnhanceChoc1Size;
extern const unsigned char kEmbeddedEnhanceChoc2[];
extern const std::size_t kEmbeddedEnhanceChoc2Size;
extern const unsigned char kEmbeddedEnhanceChoc3[];
extern const std::size_t kEmbeddedEnhanceChoc3Size;

inline std::string_view embeddedPrelude() {
  return {reinterpret_cast<const char*>(kEmbeddedPrelude), kEmbeddedPreludeSize};
}
inline std::string_view embeddedAnimationScript() {
  return {reinterpret_cast<const char*>(kEmbeddedAnimationScript), kEmbeddedAnimationScriptSize};
}
inline std::string_view embeddedHost() {
  return {reinterpret_cast<const char*>(kEmbeddedHost), kEmbeddedHostSize};
}

// The shared enhancement overlay (lua/enhance.lua).
inline std::string_view embeddedEnhanceShared() {
  return {reinterpret_cast<const char*>(kEmbeddedEnhance), kEmbeddedEnhanceSize};
}
// The overlay of the game whose module id is `id` ("choc1", "choc2", "choc3"; Reforged is choc3 too); empty for a
// game with no overlay.
inline std::string_view embeddedEnhanceFor(std::string_view id) {
  if (id == "choc1") return {reinterpret_cast<const char*>(kEmbeddedEnhanceChoc1), kEmbeddedEnhanceChoc1Size};
  if (id == "choc2") return {reinterpret_cast<const char*>(kEmbeddedEnhanceChoc2), kEmbeddedEnhanceChoc2Size};
  if (id == "choc3") return {reinterpret_cast<const char*>(kEmbeddedEnhanceChoc3), kEmbeddedEnhanceChoc3Size};
  return {};
}

}  // namespace choco::script
