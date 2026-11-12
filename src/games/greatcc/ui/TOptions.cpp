#include "games/greatcc/ui/TOptions.h"

#include "games/common/OptionsPanel.h"
#include "games/greatcc/GreatCCWorld.h"
#include "ui/WindowManager.h"
#include "ui/widgets/Widgets.h"

namespace choco::games::greatcc {

void TOptions::postChildrenInit(const ui::Style& style) {
  OptionsWindow::postChildrenInit(style);  // both sliders, from the user's prefs
  if (auto* b = dynamic_cast<ui::ButtonWindow*>(find("options_fullscreen_checkbox")))
    b->setOn(game::OptionsPanel::fullscreenNow(ctx_));
  if (auto* b = dynamic_cast<ui::ButtonWindow*>(find("options_muteall_checkbox"))) b->setOn(game::OptionsPanel::mutedNow(ctx_));
  if (auto* b = dynamic_cast<ui::ButtonWindow*>(find("options_tutorials_checkbox"))) {
    const GreatCCWorld* w = worldOf(ctx_);
    b->setOn(!w || w->tutorials().shouldShowTutorials());
  }
  updateFill("sound_fill", "options_sfxlevel_slider");
  updateFill("music_fill", "options_musiclevel_slider");
}

void TOptions::updateFill(const char* fillName, const char* sliderName) {
  auto* slider = dynamic_cast<ui::SliderWindow*>(find(sliderName));
  auto* fill = dynamic_cast<ui::BitmapWindow*>(find(fillName));
  if (!slider || !fill || !fill->texture()) return;
  fill->setSize(fillWidth(fill->texture()->width(), slider->value()), fill->texture()->height());
}

bool TOptions::onMessage(const ui::Message& m) {
  if (m.type == ui::kSliderValChanged) {
    const bool handled = OptionsWindow::onMessage(m);  // the level applies live
    if (m.name == "options_sfxlevel_slider") updateFill("sound_fill", "options_sfxlevel_slider");
    else if (m.name == "options_musiclevel_slider") updateFill("music_fill", "options_musiclevel_slider");
    return handled;
  }
  if (m.type == ui::kButtonPress && m.name == "options_tutorials_checkbox")
    if (GreatCCWorld* w = worldOf(ctx_)) w->tutorials().showTutorials(!w->tutorials().shouldShowTutorials());  // VTutorialManager::ShowTutorials(!current, true)
  return false;
}

}  // namespace choco::games::greatcc
