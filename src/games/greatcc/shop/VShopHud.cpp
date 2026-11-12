#include "games/greatcc/shop/VShopHud.h"

#include <cstdio>

#include "games/greatcc/GreatCCWorld.h"
#include "games/greatcc/ui/BLayeredWindow.h"
#include "games/greatcc/ui/KDial.h"
#include "games/greatcc/ui/RTimer.h"

namespace choco::games::greatcc {

namespace {
template <class T>
T* childAs(ui::Window& parent, const char* name) {
  ui::Window* w = parent.find(name);
  return w ? dynamic_cast<T*>(w) : nullptr;
}
std::string fmt(const char* f, int a) {
  char buf[48];
  std::snprintf(buf, sizeof buf, f, a);
  return buf;
}
std::string fmt2(const char* f, int a, int b) {
  char buf[64];
  std::snprintf(buf, sizeof buf, f, a, b);
  return buf;
}
}  // namespace

void VShopHud::postChildrenInit(const ui::Style& style) {
  Window::postChildrenInit(style);
  gauge_ = childAs<BLayeredWindow>(*this, "progress_gauge");
  needle_ = childAs<KDial>(*this, "dialPower");
  timer_ = childAs<RTimer>(*this, "game_timer");
  sweep_ = childAs<KSweepDial>(*this, "timer_sweep");
  goalText_ = find("goal_score");
  masteryText_ = find("mastery_score");
  angerText_ = find("anger_score");
  nameText_ = find("level_name");
  GreatCCWorld* world = worldOf(ctx_);
  if (world && world->settings().mode != 0) {
    if (gauge_) gauge_->setCurrentLayer(2);
    if (nameText_) nameText_->setShown(false);
    if (timer_) timer_->setPos(timer_->rect().x, timer_->rect().y - 10);
  }
}

void VShopHud::levelInit(const LevelData* level, bool endless) {
  GreatCCWorld* world = worldOf(ctx_);
  if (!world) return;
  if (timer_) {
    timer_->reset();
    timer_->timer().incrementing = endless;
    levelMs_ = 0;
    if (!endless && level) {
      timer_->timer().durationMs += level->minutes * 60000;
      timer_->timer().durationMs += level->seconds * 1000;
      levelMs_ = timer_->timer().durationMs;
    }
    timer_->refreshText();
  }
  if (sweep_) sweep_->setGauge(1.0f);
  if (needle_) needle_->setGauge(0.0f);
  const int current = world->levels().currentLevel();
  if (!endless) {
    if (goalText_) goalText_->setLabel(fmt("$%i", world->levels().goal(current)));
    if (masteryText_) masteryText_->setLabel(fmt("$%i", world->levels().mastery(current)));
    if (ui::Window* ext = find("extension")) ext->setShown(false);
  } else if (angerText_) {
    angerText_->setLabel(fmt("0/%i", world->values().getInt("angry_customer_limit", 0)));
  }
  if (nameText_) nameText_->setLabel(ctx_.strings.sdkLookup("level_abbreviation") + "  " + std::to_string(current));
}

void VShopHud::updateHud(bool softPaused, bool tutorialInEffect, bool endless) {
  GreatCCWorld* world = worldOf(ctx_);
  if (!world) return;
  if (timer_) {
    LevelTimer& t = timer_->timer();
    if (!tutorialInEffect && !softPaused) {
      if (!t.running) timer_->resumeFromNow();
    } else if (t.running) {
      t.running = false;
    }
  }
  if (sweep_ && timer_) {
    if (!endless) {
      if (levelMs_ > 0) {
        const float v = 1.0f - static_cast<float>(timer_->timer().elapsedMs) / static_cast<float>(levelMs_);
        if (v != sweep_->gauge()) sweep_->setGauge(v);
      }
    } else {
      sweep_->setGauge(1.0f);
    }
  }
  ValueManager& values = world->values();
  if (!endless) {
    const int current = world->levels().currentLevel();
    const float goal = static_cast<float>(world->levels().goal(current));
    const int mastery = world->levels().mastery(current);
    const float total = static_cast<float>(values.getReal("current_level_total", 0));
    if (gauge_) gauge_->setCurrentLayer(goal <= total ? 1 : 0);
    if (needle_) needle_->setGauge(total < goal ? total / goal : (total - goal) / (static_cast<float>(mastery) - goal));
  } else {
    const double angry = values.getReal("angry_this_level", 0);
    const double limit = values.getReal("angry_customer_limit", 0);
    if (needle_) needle_->setGauge(static_cast<float>(angry) / static_cast<float>(limit));
    if (angerText_) angerText_->setLabel(fmt2("%i/%i", static_cast<int>(angry), static_cast<int>(limit)));
  }
}

std::string VShopHud::detail() const {
  return std::string(" timer=") + (timer_ ? "yes" : "no") + " needle=" + (needle_ ? "yes" : "no") + " sweep=" + (sweep_ ? "yes" : "no") +
         " gauge=" + (gauge_ ? std::to_string(gauge_->currentLayer()) : "-");
}

}  // namespace choco::games::greatcc
