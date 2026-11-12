#include "games/greatcc/meta/PlotItem.h"

#include "games/greatcc/GreatCCWorld.h"
#include "games/greatcc/meta/PlotManager.h"
#include "games/greatcc/scene/SpriteManager.h"

namespace choco::games::greatcc {

namespace {
constexpr double kFadeMs = 500;  // a step fades in when it starts and out when it is done
}

// ================================================================ the listener

void PlotListener::onClick(VSprite& sprite) {
  GreatCCWorld* world = worldOf(ctx_);
  if (!world) return;
  if (const auto* button = dynamic_cast<const VButton*>(&sprite)) world->plots().chooseResponse(item_, button->idString());
  else world->plots().advance(item_);
}

// ================================================================ the step

void PlotStep::loadFromTable(const StyleChain& style) {
  speaker_ = style.stringOr("speaker", "", -1);
  bool responses = false;
  if (const auto r = style.table("responses", -1)) {
    r->forEachEntry([&](const std::string&, const ui::Style* t, const std::optional<std::string>&) { responses = responses || t; });
    if (responses && context().warned.insert("greatcc-plot-responses").second)
      context().log.write("[greatcc] a plot step has `responses`: they are not built (no shipped plot has any)");
  }
  if (!responses) {
    if (const auto b = style.table("default_button", -1)) {
      const StyleChain bc(*b, &style);
      button_ = VButton::create(context(), 0, "");
      button_->loadFromTable(bc);
      addChild(button_);
    }
  }
  VObject::loadFromTable(style);
}

void PlotStep::init() {
  VObject::init();
  if (button_) button_->init();
  if (fade_) {
    fade_->destroy();
    fade_.reset();
  }
  begun_ = advanced_ = false;
}

void PlotStep::reset() { begun_ = advanced_ = false; }

void PlotStep::begin() {
  fade_ = std::make_shared<AlphaEffect>(true, 1.0f, kFadeMs, -1.0f, 0);
  addEffect(fade_);
}

void PlotStep::end() {
  if (fade_) removeEffect(fade_);
  fade_ = std::make_shared<AlphaEffect>(false, 1.0f, kFadeMs, alpha(), 0);
  addEffect(fade_);
}

bool PlotStep::advance() {
  if (!advanced_) {
    if (!begun_) {
      begun_ = true;
      begin();
    } else {
      advanced_ = true;  // no responses to wait for
      end();
    }
  }
  return advanced_;
}

int PlotStep::chooseResponse(const std::string&, std::shared_ptr<PlotStep>& next) {
  next.reset();
  return 0;
}

void PlotStep::setListener(const std::shared_ptr<SpriteListener>& listener) {
  if (button_ && listener) button_->addListener(listener);
}

// ================================================================ the item

PlotItem::PlotItem(script::ScriptContext& ctx, std::string name, int slot, bool quest)
    : VObject(ctx, std::move(name), "plot item"), slot_(slot), quest_(quest) {
  setClickable(true);
}

void PlotItem::loadFromTable(const StyleChain& style) {
  type_ = style.stringOr("type", "", 0);
  if (const auto t = style.table("nextStep", 0)) {
    const StyleChain sc(*t, &style);
    first_ = PlotStep::create(context());
    first_->loadFromTable(sc);
  }
  VObject::loadFromTable(style);
  next_ = first_;
}

void PlotItem::init() {
  VObject::init();
  if (first_) first_->init();
  value_ = 0;
}

void PlotItem::reset() {
  if (first_) first_->reset();
}

bool PlotItem::advanceStep() {
  prev_ = cur_;
  cur_ = next_;
  next_.reset();
  if (!cur_) return true;  // no step left: the item is done
  cur_->setListener(listener_);
  if (!prev_ || prev_->isComplete()) {
    addChild(cur_);
    return advance();
  }
  return false;
}

bool PlotItem::advance() {
  if (complete_) return false;
  if (!cur_) {
    if (!next_) return complete_;
  } else if (!cur_->advance()) {
    return complete_;
  }
  complete_ = advanceStep();
  return complete_;
}

bool PlotItem::chooseResponse(const std::string& id) {
  if (!complete_ && cur_) {
    value_ += cur_->chooseResponse(id, next_);
    if (cur_->advance()) complete_ = advanceStep();
  } else {
    complete_ = true;
  }
  return complete_;
}

void PlotItem::update(bool softPaused) {
  VObject::update(softPaused);
  if (prev_ && prev_->isComplete()) {
    removeChild(*prev_);
    if (cur_) addChild(cur_);
    prev_.reset();
    if (!complete_) advance();
  }
}

}  // namespace choco::games::greatcc
