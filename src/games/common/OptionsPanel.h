#pragma once

#include <string>
#include <string_view>

#include "script/ScriptContext.h"
#include "ui/widgets/InputWidgets.h"

namespace choco::game {

// The container of a game's options screen: Chocolatier 3's ui::OptionsWindow
// (the three volume sliders take the current user's soundlevel, musiclevel and
// ambientlevel on open, apply live through the volume hook, and the SaveVolumes
// native stores them) plus what TOptions also does when it opens
// (docs/re/choc1-natives.md section 9, exe 004186b0): the `fullscreen` button
// is set to the current display state and the `mute` button to the user's
// `soundoff` pref. Chocolatier 2 (`Options`) and Chocolatier 1
// (`OptionsDialog`) are the same class in the exe; they differ only in the Lua
// type name the window is built under, which is `name` here. The slider
// defaults come from the user store (the profile's volume defaults).
class OptionsPanel : public ui::OptionsWindow {
public:
  OptionsPanel(ui::WindowManager &wm, script::ScriptContext &ctx,
               std::string name)
      : OptionsWindow(wm), ctx_(ctx), name_(std::move(name)) {}
  std::string_view typeName() const override { return name_; }
  void postChildrenInit(const ui::Style &style) override;

  // Is the display fullscreen now (the app's hook, else the stored pref), and
  // is the current user muted.
  static bool fullscreenNow(script::ScriptContext &ctx);
  static bool mutedNow(script::ScriptContext &ctx);

private:
  script::ScriptContext &ctx_;
  std::string name_;
};

} // namespace choco::game
