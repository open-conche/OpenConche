#include "games/greatcc/meta/VRewardNotify.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "games/greatcc/GreatCCWorld.h"
#include "games/greatcc/scene/TextSprite.h"
#include "ui/WindowManager.h"

namespace choco::games::greatcc {

namespace {
constexpr int kMaxItems = 6;
constexpr int kMinItemHeight = 55;

unsigned elapsedSince(double now, double start) {
  return static_cast<unsigned>(static_cast<long long>(now) - static_cast<long long>(start));
}

}  // namespace

void VRewardNotify::postChildrenInit(const ui::Style& style) {
  transition_ = static_cast<float>(style.numberOr("transition", 300));
  const double d = style.numberOr("duration", 5000);
  duration_ = d <= 0 ? 0u : (d >= 4294967295.0 ? 4294967295u : static_cast<unsigned>(d));
  spacing_ = static_cast<float>(style.numberOr("spacing", 0));
  if (const auto b = style.table("background")) {
    const StyleChain chain(*b, nullptr);
    background_ = TiledObject::create(ctx_, "", "");
    background_->loadFromTable(chain);
    background_->init();
    sprites().addObject(background_);
  }
  if (const auto i = style.table("item")) {
    const StyleChain chain(*i, nullptr);
    item_ = TiledObject::create(ctx_, "", "");
    item_->loadFromTable(chain);
  }
  VSpriteWindow::postChildrenInit(style);
}

void VRewardNotify::addAchievements(const std::vector<std::string>& names) {
  GreatCCWorld* world = worldOf(ctx_);
  if (!world || !background_) return;
  const gfx::PointF origin = item_ ? item_->position() : gfx::PointF{0, 0};
  gfx::PointF pos = origin;
  int itemHeight = 0;
  items_ = 0;
  for (const std::string& name : names) {
    if (items_ >= kMaxItems) break;
    const Achievement* a = world->achievements().find(name);
    if (!a || !item_) continue;
    auto copy = TiledObject::create(ctx_, "", "");
    item_->copyTo(*copy);
    copy->setPosition(pos);
    int height = 0;
    if (TextSprite* header = copy->textSprite("header")) {
      header->setText(ctx_.strings.sdkLookup(a->title));
      height = header->textBounds().h;
    }
    if (TextSprite* description = copy->textSprite("description")) {
      description->setText(ctx_.strings.sdkLookup(a->description));
      const int h = description->textBounds().h;
      description->setSize(copy->width(), h);
      height += h;
    }
    itemHeight = height > kMinItemHeight - 1 ? height : kMinItemHeight;
    copy->setSize(copy->width(), itemHeight);
    if (VObject* icon = copy->childObject("icon")) {
      icon->switchAsset("icon", a->name, "default");
      icon->setPosition({icon->position().x, static_cast<float>(itemHeight / 2)});
      icon->init();
    }
    copy->init();
    background_->addChild(copy);
    pos.y = static_cast<float>(itemHeight) + spacing_ + pos.y;
    ++items_;
  }
  background_->setSize(background_->width(), static_cast<int>(pos.y + origin.y - spacing_));
  setSize(rect().w, background_->height());
}

void VRewardNotify::setCloseCondition(unsigned n, std::shared_ptr<bool> flag) {
  duration_ = n;
  closeFlag_ = std::move(flag);
}

void VRewardNotify::start() {
  started_ = true;
  phaseStart_ = manager().modalNow(*this);
  manager().startWindowAnimation(id(), 16);
}

bool VRewardNotify::onAnimate() {
  if (!started_ || closed_) return VSpriteWindow::onAnimate();
  const double now = manager().modalNow(*this);
  const float width = background_ ? static_cast<float>(background_->width()) : 0.0f;
  if (phase_ == 0) {
    if (background_ && (background_->position().x != 0 || background_->position().y != 0)) {
      // the box is moved by the time since the phase began (not since the last frame): it speeds up
      const float step = static_cast<float>(elapsedSince(now, phaseStart_)) * width / transition_;
      const float x = background_->position().x;
      background_->setPosition({x - step >= 0 ? x - step : 0.0f, background_->position().y});
      return VSpriteWindow::onAnimate();
    }
    phase_ = 1;
    phaseStart_ = now;
  } else if (phase_ == 1) {
    if (closeFlag_ && !*closeFlag_) return VSpriteWindow::onAnimate();
    if (elapsedSince(now, phaseStart_) <= duration_) return VSpriteWindow::onAnimate();
    phase_ = 2;
    closeFlag_.reset();
    phaseStart_ = now;
  } else if (phase_ == 2 && background_) {
    const float windowWidth = static_cast<float>(rect().w);
    if (background_->position().x == windowWidth && background_->position().y == 0) {
      close();
      return VSpriteWindow::onAnimate();
    }
    const float step = static_cast<float>(elapsedSince(now, phaseStart_)) * width / transition_;
    const float x = background_->position().x;
    background_->setPosition({windowWidth <= step + x ? windowWidth : step + x, background_->position().y});
  }
  return VSpriteWindow::onAnimate();
}

void VRewardNotify::close() {
  if (closed_) return;
  closed_ = true;
  if (GreatCCWorld* world = worldOf(ctx_)) world->notices().notifyComplete(id());
  manager().destroyWindow(*this);
}

std::string VRewardNotify::detail() const {
  std::string s = VSpriteWindow::detail() + " phase=" + std::to_string(phase_) + " items=" + std::to_string(items_);
  if (background_) {
    char buf[64];
    std::snprintf(buf, sizeof buf, " box=%.0f,%.0f,%dx%d", background_->position().x, background_->position().y, background_->width(),
                  background_->height());
    s += buf;
    const TextLineCount n = countTextLines(*background_);
    s += " lines=" + std::to_string(n.drawn) + "/" + std::to_string(n.total);
  }
  return s;
}

}  // namespace choco::games::greatcc
