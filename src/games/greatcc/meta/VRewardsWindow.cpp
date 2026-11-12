#include "games/greatcc/meta/VRewardsWindow.h"

#include "games/greatcc/GreatCCWorld.h"
#include "ui/WindowManager.h"

namespace choco::games::greatcc {

namespace {
int luaRewardWindowReady(lua_State* L) {
  script::ScriptContext& ctx = script::contextOf(L);
  if (auto* w = dynamic_cast<VRewardsWindow*>(ctx.wm.window(ScopedNatives::ownerOf(L)))) w->ready();
  return 0;
}

}  // namespace

// ================================================================ VReward

void VReward::loadFromTable(const StyleChain& style) {
  VObject::loadFromTable(style);
  category_ = style.stringOr("category", "");
  const auto boxStyle = style.table("box");
  if (!box_) {
    box_ = TiledObject::create(context(), "", "");
    if (manager()) manager()->addObject(box_);
    else addChild(box_);
  }
  GreatCCWorld* world = worldOf(context());
  const Achievement* ach = world ? world->achievements().find(name()) : nullptr;
  if (boxStyle) {
    const StyleChain bc(*boxStyle, &style);
    box_->loadFromTable(bc);
  }
  box_->init();
  box_->setVisible(false);
  box_->setPosition({static_cast<float>(style.numberOr("boxX", 0)), static_cast<float>(style.numberOr("boxY", 0))});
  if (!ach) return;
  auto addText = [&](const char* styleKey, const std::string& key, float y) {
    std::shared_ptr<TextSprite> t = TextSprite::create(context());
    if (const auto ts = style.table(styleKey, 1)) {
      const StyleChain tc(*ts, &style);
      t->loadFromTable(tc);
    }
    t->setSize(box_->width(), box_->height());
    t->setText(context().strings.sdkLookup(key));
    t->setPosition({0, y});
    box_->addChild(t);
    return t;
  };
  header_ = addText("headerStyle", ach->title, 0);
  // the description starts one header line (the header's font size) below the top of the box
  content_ = addText("contentStyle", ach->description, header_->textStyle().size);
}

void VReward::init() {
  VObject::init();
  setVisible(true);
  GreatCCWorld* world = worldOf(context());
  if (!(world && world->achievements().isAcquired(name()))) {
    setAlpha(0.2f);
    setTint({0.2f, 0.2f, 0.2f, 1.0f});
  }
}

void VReward::onMouseEnter() {
  if (!box_) return;
  const float from = box_->visible() ? box_->alpha() : 0.0f;
  box_->setVisible(true);
  if (effect_) box_->removeEffect(effect_);
  effect_.reset();
  box_->addEffect(std::make_shared<AlphaEffect>(true, 1.0f, 300, from, 0));
}

void VReward::onMouseLeave() {
  if (!box_) return;
  box_->cancelEffects();
  effect_ = std::make_shared<AlphaEffect>(false, 1.0f, 100, box_->alpha(), 0);
  box_->addEffect(effect_);
}

void VReward::update(bool softPaused) {
  if (effect_ && !effect_->inProgress()) {
    effect_.reset();
    if (box_) box_->setVisible(false);
  }
  VObject::update(softPaused);
}

// ================================================================ VRewardsWindow

VRewardsWindow::VRewardsWindow(ui::WindowManager& wm, script::ScriptContext& ctx) : VSpriteWindow(wm, ctx), natives_(ctx) {
  natives_.bindOwned("RewardWindowReady", &luaRewardWindowReady, id());
}

void VRewardsWindow::postChildrenInit(const ui::Style& style) {
  if (const auto top = style.table("box_top")) {
    const StyleChain chain(*top, nullptr);
    lid_ = VObject::create(ctx_, "", "");
    lid_->loadFromTable(chain);
    lid_->init();
    sprites().addObject(lid_);
  }
  if (const auto table = style.table("rewards")) {
    const StyleChain root(*table, nullptr);
    table->forEachEntry([&](const std::string& key, const ui::Style* t, const std::optional<std::string>&) {
      if (!t) return;
      const std::string name = VObject::lower(key);
      if (name.compare(0, 7, "reward_") != 0) return;
      std::shared_ptr<VReward>& slot = rewards_[name];
      if (!slot) {
        slot = VReward::create(ctx_);
        sprites().addObject(slot);
      }
      const StyleChain chain(*t, &root);
      slot->loadFromTable(chain);
      slot->init();
    });
  }
  VSpriteWindow::postChildrenInit(style);
}

void VRewardsWindow::ready() {
  if (lid_) lid_->addEffect(std::make_shared<SpriteMove>(gfx::PointF{600, -600}, 400, 1.9f, 1000));
}

std::string VRewardsWindow::detail() const {
  std::string s = VSpriteWindow::detail() + " rewards=" + std::to_string(rewards_.size());
  GreatCCWorld* world = worldOf(ctx_);
  int earned = 0;
  for (const auto& [key, r] : rewards_)
    if (world && world->achievements().isAcquired(r->name())) ++earned;
  s += " earned=" + std::to_string(earned);
  if (lid_) s += " lid=" + std::to_string(static_cast<int>(lid_->position().x)) + "," + std::to_string(static_cast<int>(lid_->position().y));
  if (const VSprite* h = sprites().hover()) s += " hover=" + h->name();
  for (const auto& [key, r] : rewards_) {
    if (!r->boxVisible()) continue;
    const TextLineCount n = countTextLines(*r->box());
    s += " tip=" + r->name() + " lines=" + std::to_string(n.drawn) + "/" + std::to_string(n.total);
  }
  return s;
}

}  // namespace choco::games::greatcc
