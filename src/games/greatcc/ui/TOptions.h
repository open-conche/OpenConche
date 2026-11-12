#pragma once

// TOptions (docs/re/greatcc-ui-library.md 7, docs/re/greatcc-meta-flow.md 7): the Options container. Sliders
// options_sfxlevel_slider and options_musiclevel_slider start from the user's soundlevel / musiclevel (750 per mille when unset)
// and apply live; their fill bitmaps sound_fill / music_fill are as wide as the texture times the value. The checkboxes start
// from the display (options_fullscreen_checkbox), the user's `soundoff` (options_muteall_checkbox) and the tutorials flag
// (options_tutorials_checkbox), whose press flips that flag. SaveVolumes and the mute and fullscreen natives are the
// engine's shared ones (script/UserNatives.cpp).

#include "script/ScriptContext.h"
#include "ui/widgets/InputWidgets.h"

namespace choco::games::greatcc {

class TOptions : public ui::OptionsWindow {
 public:
  TOptions(ui::WindowManager& wm, script::ScriptContext& ctx) : OptionsWindow(wm), ctx_(ctx) {}
  std::string_view typeName() const override { return "TOptions"; }
  void postChildrenInit(const ui::Style& style) override;
  bool onMessage(const ui::Message& m) override;

  // The fill bitmap's width for a slider value: (int)(texture width * value), full texture height.
  static int fillWidth(int textureWidth, float value) { return static_cast<int>(static_cast<float>(textureWidth) * value); }

 private:
  void updateFill(const char* fillName, const char* sliderName);
  script::ScriptContext& ctx_;
};

}  // namespace choco::games::greatcc
