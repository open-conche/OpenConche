#pragma once

// VButton (docs/re/greatcc-meta-flow.md 4.2, re/greatcc/decomp/VButton.c): a sprite button. A VObject whose asset states are
// `default`, `over`, `disabled`, `on` and `on_over`, with a label (a VTextSprite child at layer 2, sized and centred on the
// button's image), an `id` (the level number of a globe), a `type` (0 push, 1 and 2 toggle) and a click `sound`. A click that
// reaches OnClick posts a button-press message named after the sprite, aimed at the window that owns the manager.

#include "games/greatcc/scene/TextSprite.h"
#include "games/greatcc/scene/VObject.h"

namespace choco::games::greatcc {

class VButton : public VObject {
 public:
  // The name is "<name>_<id>" and the type "vbutton"; the button is clickable.
  VButton(script::ScriptContext& ctx, int id, const std::string& name);
  static std::shared_ptr<VButton> create(script::ScriptContext& ctx, int id, const std::string& name) {
    return std::make_shared<VButton>(ctx, id, name);
  }
  const char* kind() const override { return "VButton"; }

  int id() const { return id_; }
  // The string form of the id (the response a plot button stands for).
  const std::string& idString() const { return idString_; }
  void setId(int id) { id_ = id; }
  int buttonType() const { return type_; }
  bool disabled() const { return disabled_; }
  bool selected() const { return selected_; }
  float buttonScale() const { return scale_; }
  const std::string& sound() const { return sound_; }
  TextSprite* label() const { return text_.get(); }

  // Keys (own table only unless noted): id, type, scale, `sound` (any depth); then the text sprite is made once, loaded from
  // the same style with its flags forced to 5; then the VObject keys.
  void loadFromTable(const StyleChain& style) override;
  void setText(const std::string& s);
  // SetDisabled: a change toggles `clickable` and switches to the disabled state, or back to over/default (on/on_over when
  // selected).
  void setDisabled(bool d);
  // SetSelected: a change switches to on/on_over, or back to default/over.
  void setSelected(bool s);

  // VObject::SwitchAsset, then the label is resized to the image's rectangle and centred on the button's origin (type 0).
  void switchAsset(const std::string& asset, const std::string& state, const std::string& fallback) override;

  void onMouseEnter() override;
  void onMouseLeave() override;
  // OnClick (unless disabled): a toggle type flips `selected`, the `sound` plays (group 1000), and the message
  // {button press, sprite name, owning window} is posted.
  void onClick() override;

 private:
  int id_;
  std::string idString_;
  int type_ = 0;
  float scale_ = 1;
  std::string sound_;
  bool disabled_ = false, selected_ = false;
  std::shared_ptr<TextSprite> text_;
};

}  // namespace choco::games::greatcc
