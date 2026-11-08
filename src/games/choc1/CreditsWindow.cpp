#include "games/choc1/CreditsWindow.h"

#include "ui/WindowManager.h"

namespace choco::game::choc1 {

std::string CreditsWindow::assetPath(std::string_view file) {
  constexpr std::string_view kPrefix = "assets/";
  if (file.size() > kPrefix.size() && file.substr(0, kPrefix.size()) == kPrefix)
    file.remove_prefix(kPrefix.size());
  return std::string(file);
}

std::string CreditsWindow::withVersionLine(std::string_view text,
                                           std::string_view version) {
  std::string out(text);
  // The exe reads the last line without its newline and still drops one
  // character from it.
  if (!out.empty() && out.back() != '\n') {
    out.pop_back();
    out += '\n';
  }
  out += "Chocolatier version ";
  out += version;
  out += '\n';
  return out;
}

std::string CreditsWindow::versionOf(std::string_view gameVersion) {
  const std::size_t comma = gameVersion.rfind(',');
  return std::string(comma == std::string_view::npos
                         ? gameVersion
                         : gameVersion.substr(comma + 1));
}

std::optional<std::string>
CreditsWindow::loadText(const ui::CreditsParams &p) const {
  std::optional<std::string> text = manager().readTextFile(assetPath(p.file));
  if (!text)
    return std::nullopt;
  return withVersionLine(*text, versionOf(ctx_.config.get("gameversion")));
}

} // namespace choco::game::choc1
