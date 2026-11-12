#include "games/greatcc/scene/PushPin.h"

#include <cstdio>

#include "games/greatcc/GreatCCWorld.h"
#include "games/greatcc/scene/Effects.h"
#include "games/greatcc/scene/SpriteManager.h"
#include "ui/WindowManager.h"

namespace choco::games::greatcc {

// VPinListener: a globe's Enter and Leave show and hide the score box.
struct PushPin::Listener : SpriteListener {
  explicit Listener(PushPin& p) : pin(p.weak_from_this()) {}
  void show(VSprite& s, bool on) {
    auto p = std::static_pointer_cast<PushPin>(pin.lock());
    auto* b = dynamic_cast<VButton*>(&s);
    if (p && b) p->setScoreInfo(on, b->id());
  }
  void onMouseEnter(VSprite& s) override { show(s, true); }
  void onMouseLeave(VSprite& s) override { show(s, false); }
  std::weak_ptr<VSprite> pin;
};

PushPin::PushPin(script::ScriptContext& ctx) : VObject(ctx, "", "") { setClickable(true); }

VButton* PushPin::globe(int level) const {
  auto it = globes_.find(level);
  return it == globes_.end() ? nullptr : it->second.get();
}

void PushPin::loadFromTable(const StyleChain& style) {
  for (const auto& [lvl, b] : globes_) removeChild(*b);
  globes_.clear();
  baseLevel_ = style.intOr("baseLevel", 0);
  move_ = {static_cast<float>(style.numberOr("movex", 0.0)), static_cast<float>(style.numberOr("movey", 0.0))};
  if (auto globes = style.table("globes", 0)) {
    const StyleChain group(*globes, &style);
    globes->forEachEntry([&](const std::string& key, const ui::Style* t, const std::optional<std::string>&) {
      if (!t || lower(key).compare(0, 6, "globe_") != 0) return;
      const StyleChain gc(*t, &group);
      auto b = VButton::create(context(), 0, "");
      b->loadFromTable(gc);
      b->setId(b->id() + baseLevel_);
      char name[32];
      std::snprintf(name, sizeof name, "level_btn_%02d", b->id());
      b->setName(name);
      b->setPosition({b->position().x - kGlobeOffsetX, b->position().y - kGlobeOffsetY});
      b->setText(std::to_string(b->id()));
      globes_[b->id()] = b;
      addChild(b);
    });
  }
  if (auto box = style.table("box", 0)) {
    if (!box_) {
      box_ = TiledObject::create(context(), "", "");
      addChild(box_);
    }
    const StyleChain bc(*box, &style);
    box_->loadFromTable(bc);
  }
  VObject::loadFromTable(style);
  positions_.clear();
  for (const auto& [key, text] : textSprites())
    if (text) positions_[key] = text->position();
}

void PushPin::setGlobeVisible(bool v) {
  for (const auto& [lvl, b] : globes_) b->setVisible(v);
}

void PushPin::setGlobeEnabled(bool e) {
  for (const auto& [lvl, b] : globes_) b->setMouse(e);
  if (manager()) manager()->recalcMouse();
}

void PushPin::init() {
  switchAsset("base", "enabled", "default");
  setGlobeVisible(false);
  selected_.reset();
  state_ = 0;
  GreatCCWorld* world = worldOf(context());
  if (!world) return;
  const int highest = world->levels().highestComplete();
  const int next = highest + 1;
  bool transitionPending = false;
  if (world->levels().currentLevel() == baseLevel_) transitionPending = world->values().getInt("do_map_transition", 0) == 1;
  const bool demoHidden = context().config.get("firstpeek", "") == "1" && baseLevel_ >= 21;
  setVisible(!(next < baseLevel_ || demoHidden || transitionPending));
  if (box_) {
    box_->init();
    box_->setVisible(false);
  }
  if (!listener_) listener_ = std::make_shared<Listener>(*this);
  for (const auto& [level, b] : globes_) {
    const int status = world->levels().status(level);
    b->setDisabled(status == 0 && level != next);
    b->cancelEffects();
    if (status == 0) {
      if (level == next) {
        b->switchAsset("current", "default", "default");
        b->addListener(listener_);
        b->addEffect(std::make_shared<HoverGlow>(0.15f, true, true, true));
        selected_ = b;
      } else {
        b->switchAsset("none", "default", "default");
        b->removeListener(listener_);
      }
    } else {
      b->addListener(listener_);
      b->addEffect(std::make_shared<HoverGlow>(0.15f, true, true, true));
      b->switchAsset(status == 2 ? "mastered" : "cleared", "default", "default");
    }
  }
  if (!glow_ && !open_) {
    glow_ = std::make_shared<HoverGlow>(0.15f, false, true, true);
    addEffect(glow_);
  }
}

void PushPin::setOpen(bool open) {
  if (open_ != open) {
    for (const auto& [key, text] : textSprites()) {
      if (!text) continue;
      text->cancelEffects();
      if (!open) {
        const auto it = positions_.find(key);
        const gfx::PointF target = it != positions_.end() ? it->second : gfx::PointF{};
        text->addEffect(std::make_shared<SpriteMove>(target, 200, 0.0f, 0.0));
      } else {
        const gfx::PointF cur = text->position();
        text->addEffect(std::make_shared<SpriteMove>(gfx::PointF{cur.x + move_.x, cur.y + move_.y}, 200, 0.0f, 450.0));
      }
    }
    if (!open) {
      glow_ = std::make_shared<HoverGlow>(0.15f, false, true, true);
      addEffect(glow_);
    } else if (glow_) {
      glow_->stop();
      glow_.reset();
    }
  }
  open_ = open;
  if (open && manager()) {
    // VPinSpider: every other top-level pin closes
    manager()->forEachSprite(
        [this](VSprite& s) {
          if (auto* p = dynamic_cast<PushPin*>(&s); p && p != this) p->setOpen(false);
          return true;
        },
        false);
  }
}

void PushPin::onClick() {
  GreatCCWorld* world = worldOf(context());
  if (!world) return;
  if (world->settings().mode == 0) {
    if (state_ == 0) setOpen(!open_);
  } else if (VButton* b = globe(baseLevel_)) {
    b->doEvent(SpriteEvent::Click, {0, 0});
  }
}

void PushPin::update(bool softPaused) {
  switch (state_) {
    case 0:
      if (open_) {
        state_ = 1;
        play("DoAnim", false, false, 0);
      }
      break;
    case 1:
      if (!animPlaying()) {
        if (!open_) {
          setGlobeVisible(false);
          play("ReverseAnim", false, false, 0);
          state_ = 4;
        } else {
          setGlobeEnabled(false);
          state_ = 2;
          index_ = baseLevel_;
          timer_ = 0;
        }
      }
      break;
    case 2: {
      timer_ += getElapsed();
      if (timer_ > kCascadeThresholdMs) {
        if (VButton* b = globe(index_)) {
          b->setVisible(true);
          b->addEffect(std::make_shared<AlphaEffect>(true, 1.0f, 300, -1.0f, 0));
        }
        if (VButton* b = globe(index_ - 2)) {
          const bool none = !b->currentAsset().empty() && b->currentAsset() == "none";
          if (!none && b != selected_.get()) b->addEffect(std::make_shared<GlowEffect>(1.0f, 800, 1.0f, 1.0f));
        }
        timer_ -= kCascadeStepMs;
        ++index_;
      }
      if (index_ > baseLevel_ + 11) {
        setGlobeEnabled(true);
        state_ = 3;
        if (selected_) selected_->addEffect(std::make_shared<GlowEffect>(1.0f, 600, -1.0f, 0.0f));
      }
      break;
    }
    case 3:
      if (!open_) {
        state_ = 4;
        setGlobeVisible(false);
        if (selected_) selected_->cancelEffects();
        play("ReverseAnim", false, false, 0);
      }
      break;
    case 4:
      if (animPlaying()) break;
      if (!open_) {
        state_ = 0;
      } else {
        state_ = 1;
        play("DoAnim", false, false, 0);
      }
      break;
    default: break;
  }
  VObject::update(softPaused);
}

void PushPin::setScoreInfo(bool show, int level) {
  if (!box_) return;
  GreatCCWorld* world = worldOf(context());
  if (!world) return;
  float start = 1.0f;
  if (show) {
    auto set = [&](const char* key, const std::string& text) {
      if (TextSprite* t = box_->textSprite(key)) t->setText(text);
    };
    set("level", context().strings.sdkLookup("map_level", {}) + "  " + std::to_string(level));
    set("best", "$" + std::to_string(static_cast<int>(world->levels().bestScore(level))));
    set("goal", "$" + std::to_string(world->levels().goal(level)));
    set("mastery", "$" + std::to_string(world->levels().mastery(level)));
    box_->setVisible(true);
    start = 0.0f;
  }
  if (scoreFade_) {
    start = scoreFade_->alpha();
    scoreFade_->destroy();
  }
  double duration = 500, delay = 0;
  if (show) duration = 200;
  else if (start >= 1.0f) delay = 500;
  scoreFade_ = std::make_shared<AlphaEffect>(show, 1.0f, duration, start, delay);
  scoreFade_->onUpdate(manager() ? manager()->now() : 0, false);
  box_->addEffect(scoreFade_);
}

}  // namespace choco::games::greatcc
