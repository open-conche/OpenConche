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

inline std::string_view embeddedPrelude() {
  return {reinterpret_cast<const char*>(kEmbeddedPrelude), kEmbeddedPreludeSize};
}
inline std::string_view embeddedAnimationScript() {
  return {reinterpret_cast<const char*>(kEmbeddedAnimationScript), kEmbeddedAnimationScriptSize};
}
inline std::string_view embeddedHost() {
  return {reinterpret_cast<const char*>(kEmbeddedHost), kEmbeddedHostSize};
}

}  // namespace choco::script
