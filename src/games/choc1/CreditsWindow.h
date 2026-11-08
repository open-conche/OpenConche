#pragma once

#include <string>
#include <string_view>

#include "script/ScriptContext.h"
#include "ui/widgets/CreditsWindow.h"

namespace choco::game::choc1 {

// Chocolatier 1's CreditsWindow (TCredits, vtable 004e6cb4, Init 00407b00, draw
// 00408470; docs/re/choc1-options-credits.md). Its keys, columns, markup,
// padding and scroll formula are Chocolatier 3's (ui::CreditsWindow), so that
// class does the work. What Chocolatier 1 adds: the Lua names the file
// `assets/credits.txt`, a path relative to the install folder where the exe
// finds the loose file, while the engine's asset root is that `assets` folder
// itself; and Init appends one line, "Chocolatier version <version>" (the
// number of settings.xml `gameversion`), as a full-width line after the file's
// last line.
class CreditsWindow : public ui::CreditsWindow {
public:
  CreditsWindow(ui::WindowManager &wm, script::ScriptContext &ctx)
      : ui::CreditsWindow(wm), ctx_(ctx) {}

  // The asset-root-relative name of a Lua `file` value: a leading "assets/" is
  // dropped.
  static std::string assetPath(std::string_view file);
  // The version number in settings.xml's `gameversion` ("dl,pf,1.0": the part
  // after the last comma). INFERENCE: the exe's kGameVersion state value is
  // built while it reads the settings (the split was not decoded); the Windows
  // build's own number is not in the exe.
  static std::string versionOf(std::string_view gameVersion);
  // The file text with the version line appended. Like the exe's fgets loop, a
  // last line with no newline loses its final character (the parser drops one
  // per line), so the newline is supplied before the new line is added.
  static std::string withVersionLine(std::string_view text,
                                     std::string_view version);

protected:
  std::optional<std::string>
  loadText(const ui::CreditsParams &p) const override;

private:
  script::ScriptContext &ctx_;
};

} // namespace choco::game::choc1
