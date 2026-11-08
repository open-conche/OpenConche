#pragma once

#include <filesystem>
#include <string>

namespace choco::platform {

// Interprets a std::string as UTF-8 (the engine's path encoding) rather than the Windows ANSI code page.
inline std::filesystem::path pathFromUtf8(const std::string& s) {
  return std::filesystem::path(std::u8string(s.begin(), s.end()));
}

// The path as UTF-8 (never the Windows ANSI code page).
inline std::string pathToUtf8(const std::filesystem::path& p) {
  const std::u8string u = p.u8string();
  return std::string(u.begin(), u.end());
}

}  // namespace choco::platform
