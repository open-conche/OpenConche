#include "games/greatcc/scene/VButton.h"

#include <cstdio>

#include "games/greatcc/GreatCCWorld.h"
#include "games/greatcc/scene/SpriteManager.h"
#include "ui/WindowManager.h"

namespace choco::games::greatcc {

VButton::VButton(script::ScriptContext& ctx, int id, const std::string& name)
    : VObject(ctx, name + "_" + std::to_string(id), "vbutton"), id_(id), idString_(name) {
  setClickable(true);
}

void VButton::loadFromTable(const StyleChain& style) {
  id_ = style.intOr("id", id_, 0);
  idString_ = style.stringOr("id", idString_, 0);
  type_ = style.intOr("type", type_, 0);
  scale_ = static_cast<float>(style.numberOr("scale", 1.0, 0));
  sound_ = style.stringOr("sound", "");
  setScale(scale_, scale_);
  if (!text_) {
    text_ = TextSprite::create(context());
    text_->setLayer(2);
    addChild(text_);
  }
  text_->loadFromTable(style);
  text_->setFlags(5);
  VObject::loadFromTable(style);
}

void VButton::setText(const std::string& s) {
  if (text_) text_->setText(s);
}

void VButton::setDisabled(bool d) {
  if (disabled_ == d) return;
  disabled_ = d;
  setClickable(!d);
  if (d) {
    switchState("disabled", "default");
  } else if (!selected_) {
    switchState(hovered() ? "over" : "default", "default");
  } else {
    switchState(hovered() ? "on_over" : "on", hovered() ? "over" : "default");
  }
}

void VButton::setSelected(bool s) {
  if (selected_ == s) return;
  selected_ = s;
  if (!hovered()) {
    switchState(s ? "on" : "default", "default");
  } else {
    switchState(s ? "on_over" : "over", s ? "on" : "default");
  }
}

void VButton::switchAsset(const std::string& asset, const std::string& state, const std::string& fallback) {
  VObject::switchAsset(asset, state, fallback);
  if (!text_) return;
  const gfx::RectF r = getRect(DrawSpec{}, -1);
  const int w = static_cast<int>(r.w), h = static_cast<int>(r.h);
  if (type_ == 0) {
    text_->setSize(w, h);
    text_->setPosition({static_cast<float>(w) * -0.5f, static_cast<float>(h) * -0.5f});
  } else {
    text_->setSize(800, h);
    text_->setFlags(4);
    text_->setPosition({static_cast<float>(w) * 0.5f, static_cast<float>(h) * -0.5f});
  }
}

void VButton::onMouseEnter() {
  if (pressed() || disabled_) return;
  if (selected_) switchState("on_over", "on");
  else switchState("over", "over");
}

void VButton::onMouseLeave() {
  if (pressed() || disabled_) return;
  switchState(selected_ ? "on" : "default", "default");
}

void VButton::onClick() {
  if (disabled_) return;
  const std::shared_ptr<VSprite> keep = shared_from_this();
  if (type_ == 1 || type_ == 2) setSelected(!selected_);
  if (!sound_.empty())
    if (GreatCCWorld* w = worldOf(context())) w->sound().playSfx(sound_, false);
  const ui::WindowId window = manager() ? manager()->parentWindowId() : 0;
  context().wm.post({ui::kButtonPress, name(), window});
}

}  // namespace choco::games::greatcc
