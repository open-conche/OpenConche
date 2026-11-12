#include "games/greatcc/shop/VCharacter.h"

#include <algorithm>
#include <cmath>

#include "games/greatcc/GreatCCWorld.h"

namespace choco::games::greatcc {

namespace {
constexpr float kPi = 3.1415927f;
constexpr float kTwoPi = 6.2831855f;

float lengthOf(gfx::PointF v) { return std::sqrt(v.x * v.x + v.y * v.y); }
}  // namespace

VCharacter::VCharacter(script::ScriptContext& ctx, std::shared_ptr<ZoneGraph> zones)
    : VSuspendable(ctx, {}, {}), zones_(std::move(zones)) {}

double VCharacter::valueReal(const char* name, double def) const {
  GreatCCWorld* w = worldOf(context());
  return w ? w->values().getReal(name, def) : def;
}

int VCharacter::valueInt(const char* name, int def) const {
  GreatCCWorld* w = worldOf(context());
  return w ? w->values().getInt(name, def) : def;
}

void VCharacter::noteNode(const ZonePoint* p) {
  if (!p) return;
  trail_.push_back(p->name);
  if (trail_.size() > 32) trail_.erase(trail_.begin());
}

// ---- loading ----

void VCharacter::loadFromTable(const StyleChain& style) {
  VSuspendable::loadFromTable(style);
  idles_.clear();
  idle_.reset();
  for (const auto& [key, child] : childObjects()) {
    if (!child || !isIdleName(key)) continue;
    idles_.push_back(child);
    child->setVisible(false);
  }
  if (zones_) {
    const std::string zone = style.stringOr("startZone", "", 1);
    const std::string point = style.stringOr("startPoint", "", 1);
    start_ = zones_->findPoint(zone, point);
  }
}

void VCharacter::init() {
  VSuspendable::init();
  idleCountdown_ = 0;
  moving_ = false;
  VSuspendable::reset();  // the suspension flag (the wait fields were empty already)
  setFacing(2);
  setDirectionFacing(2);
  target_ = start_;
  node_ = target_;
  finalDest_ = node_;
  if (start_) setPosition({static_cast<float>(start_->x), static_cast<float>(start_->y)});
}

// ---- the walk ----

void VCharacter::queueMovePoint(ZonePoint* p) {
  if (!p) return;
  if (tutorialInEffect()) {
    if (queue_.empty()) {
      // standing on the destination, or the destination is somewhere else: a new trip
      if (node_ == finalDest_ || finalDest_ != p) {
        queue_.push_back(p);
        return;
      }
      return;
    }
    if (queue_.back() == p) return;
  }
  queue_.push_back(p);
}

bool VCharacter::gotoNextPoint() {
  if (isBusy() || queue_.empty()) return false;
  ZonePoint* p = queue_.front();
  queue_.pop_front();
  travelTo(p);
  return true;
}

void VCharacter::travelTo(ZonePoint* p) {
  if (!zones_) return;
  finalDest_ = p;
  zones_->addPath(node_, p);
  setDestination(zones_->nextPoint());
}

void VCharacter::setDestination(ZonePoint* p) {
  target_ = p;
  if (!p) return;
  if (isBusy() && !moving_) return;
  const gfx::PointF here = position();
  if (static_cast<float>(p->x) != here.x || static_cast<float>(p->y) != here.y) moving_ = true;
  else atNode();
}

void VCharacter::atNode() {
  if (!zones_ || zones_->pathEmpty()) {
    moving_ = false;
    node_ = finalDest_;
    noteNode(node_);
    atDestination();
  } else {
    node_ = target_;
    noteNode(node_);
    setDestination(zones_->nextPoint());
  }
}

void VCharacter::atDestination() {
  // ZonePoint::OnArrival is an empty hook
  gotoNextPoint();
}

void VCharacter::updatePosition() {
  if (!target_) return;
  const gfx::PointF pos = position();
  const gfx::PointF goal{static_cast<float>(target_->x), static_cast<float>(target_->y)};
  const gfx::PointF toGoal{goal.x - pos.x, goal.y - pos.y};
  setDirectionVector(toGoal);
  // vertical travel is up to a quarter of a pixel per ten slower than horizontal
  const float step = (speed_ + std::fabs(dir_.y) * -0.25f) * static_cast<float>(elapsedMs()) / 10.0f;
  gfx::PointF stepVec{dir_.x * step, dir_.y * step};
  const bool lands = lengthOf(toGoal) < lengthOf(stepVec);
  if (lands) stepVec = toGoal;
  // the clamped last step ends exactly on the point (the sum of a position and the remaining vector can miss it by a ulp)
  setPosition(lands ? goal : gfx::PointF{pos.x + stepVec.x, pos.y + stepVec.y});
  const gfx::PointF now = position();
  if (now.x == goal.x && now.y == goal.y) atNode();
}

void VCharacter::setDirectionVector(gfx::PointF v) {
  const float len = lengthOf(v);
  if (len > 0) dir_ = {v.x / len, v.y / len};
  else dir_ = v;
}

void VCharacter::setDirectionFacing(int f) {
  switch (f) {
    case 0: setDirectionVector({-1, 0}); break;
    case 1: setDirectionVector({1, 0}); break;
    case 2: setDirectionVector({0, 1}); break;
    case 3: setDirectionVector({0, -1}); break;
    default: break;
  }
  facingDirty_ = true;
}

void VCharacter::updateFacing() {
  const float before = heading_;
  const float goal = static_cast<float>(std::atan2(static_cast<double>(dir_.y), static_cast<double>(dir_.x)));
  const float diff = goal - before;
  const float turn = static_cast<float>(valueReal("player_turn_speed", 0.2));
  // the short way round, at most `turn` radians per update
  float next = before;
  bool settled = false;
  if (diff <= kPi) {
    if (diff <= 0.0f || kPi <= diff) {
      if (-kPi <= diff) {
        if (0.0f <= diff) {
          if (diff == 0.0f) settled = true;
        } else if (-kPi < diff) {
          next = std::max(diff, -turn) + before;
          heading_ = next;
          settled = true;
        }
        if (!settled) {
          // exactly half a turn: keep going the way the heading was already turning
          next = before <= previousHeading_ ? before + turn : before - turn;
          heading_ = next;
          settled = true;
        }
      } else {
        // more than half a turn clockwise the other way round: wrap
        next = turn + before;
        heading_ = next;
        if (next > kPi) {
          next -= kTwoPi;
          heading_ = next;
        }
        settled = true;
      }
    } else {
      heading_ = std::min(diff, turn) + before;
      settled = true;
    }
  } else {
    next = before - turn;
    heading_ = next;
    if (!(-kPi <= next)) {
      next += kTwoPi;
      heading_ = next;
    }
    settled = true;
  }
  if (before != heading_) previousHeading_ = before;
  const float h = heading_;
  if (std::fabs(lastEvaluated_ - h) > 0.02f || facingDirty_) {
    int f;
    if (1.0471976f < h || h < -0.7853982f) {
      if (h <= 1.0471976f || 2.0943952f <= h) f = (2.0943952f <= h || h <= -2.3561945f) ? 0 : 3;
      else f = 2;
    } else {
      f = 1;
    }
    setFacing(f);
    lastEvaluated_ = heading_;
    facingDirty_ = false;
  }
}

void VCharacter::setFacing(int f) {
  facing_ = f;
  if (f >= 0 && f <= 3) {
    switchAsset(f == 2 ? "walkfront" : (f == 3 ? "walkback" : "walkright"), "default", "default");
    setFlip(f == 0, false);
  }
  if (!moving_) stop(true);
  else play("DoAnim", true, false, 0);
}

void VCharacter::playSpecialAnim(const std::string& asset, bool loop) {
  if (!hasAsset(asset)) return;
  specialAnim_ = true;
  switchAsset(asset, "default", "default");
  play("", loop, false, 0);
}

// ---- idle ----

void VCharacter::updateIdle() {
  if (isBusy()) idleCountdown_ = valueInt("player_idle_first_cooldown", 0);
  if (idleCountdown_ > 0) {
    idleCountdown_ -= elapsedMs();
    if (idleCountdown_ < 1) playRandomIdleAnim();
  }
  if (!(idleCountdown_ > 0)) idleCountdown_ = valueInt("player_idle_cooldown", 0);
  if (idle_ && (isBusy() || !idle_->scriptIsPlaying(-1))) {
    stopCurrentIdle();
    setDirectionFacing(2);
  }
}

void VCharacter::playRandomIdleAnim() {
  if (isBusy() || idle_ || idles_.empty()) return;
  const std::size_t i = idles_.size() > 1 ? context().rng.next32() % idles_.size() : 0;
  idle_ = idles_[i];
  idle_->setVisible(true);
  setSelfVisible(false);
  idle_->play("", false, true, 0);
}

void VCharacter::stopCurrentIdle() {
  setSelfVisible(true);
  if (idle_) {
    idle_->stop(true);
    idle_->setVisible(false);
    idle_.reset();
  }
}

// ---- update ----

void VCharacter::update(bool softPaused) {
  // a callback of this update (an arrival that reloads the level) may take the character out of its manager
  const std::shared_ptr<VSprite> keep = weak_from_this().lock();
  if (!moving_) {
    if (!isBusy() && zones_ && !zones_->pathEmpty()) setDestination(zones_->nextPoint());
    else if (!isBusy() && !queue_.empty()) gotoNextPoint();
  } else {
    updatePosition();
  }
  updateFacing();
  updateIdle();
  if (specialAnim_ && !scriptIsPlaying(-1)) {
    specialAnim_ = false;
    setDirectionFacing(2);
  }
  VSuspendable::update(softPaused);
}

}  // namespace choco::games::greatcc
