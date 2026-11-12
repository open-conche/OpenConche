#include "games/greatcc/shop/VConveyor.h"

#include <cmath>

#include "games/greatcc/GreatCCWorld.h"
#include "games/greatcc/shop/VCustomer.h"
#include "games/greatcc/shop/VDisposal.h"
#include "games/greatcc/shop/VPlayer.h"

namespace choco::games::greatcc {

namespace {
gfx::PointF pointOf(const ui::Style& t) { return {static_cast<float>(t.numberAt(1).value_or(0)), static_cast<float>(t.numberAt(2).value_or(0))}; }
gfx::PointF pointKey(const StyleChain& style, const char* key) {
  const auto t = style.table(key, 0);
  return t ? pointOf(*t) : gfx::PointF{0, 0};
}
constexpr float kMinGap = 0.08f;   // radians between neighbours on the belt
constexpr float kHoldBack = 0.0015f;
}  // namespace

VConveyor::VConveyor(script::ScriptContext& ctx, int id) : VStation(ctx, id, kStationConveyor) {}

void VConveyor::loadFromTable(const StyleChain& style) {
  center_ = pointKey(style, "arcCenter");
  const gfx::PointF start = pointKey(style, "arcStart"), end = pointKey(style, "arcEnd");
  // the start's offsets are absolute values, the end's are signed (the arc runs up and to the left)
  const double sx = std::fabs(start.x - center_.x), sy = std::fabs(center_.y - start.y);
  startRadius_ = static_cast<float>(std::sqrt(sx * sx + sy * sy));
  startAngle_ = static_cast<float>(std::atan2(sy, sx));
  const double ex = end.x - center_.x, ey = center_.y - end.y;
  endRadius_ = static_cast<float>(std::sqrt(ex * ex + ey * ey));
  endAngle_ = static_cast<float>(std::atan2(ey, ex));
  velocity_ = static_cast<float>(style.numberOr("arcVelocity", 0.0, 0));
  arcLength_ = (endRadius_ <= startRadius_ ? startRadius_ : endRadius_) * std::fabs(endAngle_ - startAngle_);
  zones_.clear();
  if (auto list = style.table("stations", 0)) {
    for (int i = 1; i <= list->size(); i += 2) {
      const std::string name = list->stringAt(i).value_or("");
      if (name.empty()) break;
      const auto p = list->element(i + 1);
      const gfx::PointF at = p ? pointOf(*p) : gfx::PointF{0, 0};
      const double dy = center_.y - at.y, dx = at.x - center_.x;
      Zone z;
      z.radius = static_cast<float>(std::sqrt(dy * dy + dx * dx));
      z.angle = static_cast<float>(std::atan2(dy, dx));
      zones_[name] = z;
    }
  }
  VStation::loadFromTable(style);
}

void VConveyor::init() {
  const bool on = host() && host()->upgradeActive("kConveyor1", "station");
  if (on) {
    switchAsset("base", "default", "default");
    VObject::init();
  }
  setVisible(on);
}

void VConveyor::update(bool softPaused) {
  updateBeltItems();
  VStation::update(softPaused);
}

void VConveyor::onArrival(VPlayer& player, const ZonePoint* point) {
  const std::shared_ptr<VSprite> keep = shared_from_this();
  VStation::onArrival(player, point);
  std::shared_ptr<VChocolate> choc = player.getChocolate(VPlayer::kHandA);
  int hand = VPlayer::kHandA;
  if (!choc || choc->isBucket()) {
    choc = player.getChocolate(VPlayer::kHandB);
    hand = VPlayer::kHandB;
    if (!choc || choc->isBucket()) return;
  }
  player.detachObject(hand);
  addItemToBelt(choc, 0);
}

void VConveyor::addItemToBelt(const std::shared_ptr<VObject>& item, float extra) {
  if (!item) return;
  items_.push_back({extra + startAngle_, "station07", item});
  item->setLayer(1);
  addChild(item);
  play("DoAnim", false, false, 0);
  if (GreatCCWorld* w = worldOf(context())) {
    const int size = static_cast<int>(items_.size());
    if (w->values().getInt("conveyor_max_this_level", 0) < size) w->values().setValue("conveyor_max_this_level", static_cast<double>(size), false);
  }
}

void VConveyor::getStationZones(float theta, std::string& ahead, std::string& behind) const {
  for (const auto& [name, z] : zones_) {
    const float d = z.angle - theta;
    if (0.0f < d) {
      bool keep = false;
      if (!ahead.empty()) {
        const auto it = zones_.find(ahead);
        keep = it != zones_.end() && it->second.angle - theta <= d;
      }
      if (!keep) ahead = name;
    }
    if (d < 0.0f) {
      bool keep = false;
      if (!behind.empty()) {
        const auto it = zones_.find(behind);
        keep = it != zones_.end() && d <= it->second.angle - theta;
      }
      if (!keep) behind = name;
    }
  }
}

float VConveyor::calculateRadius(float theta, const std::string& ahead, const std::string& behind) const {
  auto zone = [&](const std::string& n) {
    const auto it = zones_.find(n);
    return it == zones_.end() ? Zone{} : it->second;
  };
  const bool before = theta <= endAngle_;
  const Zone first = zone(before ? behind : ahead), second = zone(before ? ahead : behind);
  float rA = first.radius, rB = second.radius, a0 = first.angle, a1 = second.angle;
  // a zone that is not there (no name, or none passed yet) is the arc's own end on that side
  if (rA == 0.0f) rA = before ? startRadius_ : endRadius_;
  if (rB == 0.0f) rB = before ? endRadius_ : startRadius_;
  if (a0 == 0.0f) a0 = before ? startAngle_ : endAngle_;
  if (a1 == 0.0f) a1 = before ? endAngle_ : startAngle_;
  return ((theta - a0) / (a1 - a0)) * (rB - rA) + rA;
}

bool VConveyor::offerToCustomer(const BeltItem& it, const std::string& behind) {
  if (!host()) return false;
  const std::shared_ptr<VStation> station = host()->stationByName(behind);
  if (!station) return false;
  const std::shared_ptr<VCustomer> target = station->customer();
  if (!target) return false;
  // a more urgent customer at a counter further down the belt (a lower number) comes first
  for (int n = 1; n < station->stationId(); ++n) {
    const std::shared_ptr<VStation> other = host()->stationByNumber(n);
    const std::shared_ptr<VCustomer> guest = other ? other->customer() : nullptr;
    if (guest && guest->state() > 0 && guest->state() < 4 && guest->patience() < target->patience()) return false;
  }
  return target->complimentaryService(std::static_pointer_cast<VCarriable>(it.item));
}

void VConveyor::activateDisposal() {
  if (!host()) return;
  if (const auto bin = std::dynamic_pointer_cast<VDisposal>(host()->stationByName("station15"))) bin->activate();
}

void VConveyor::updateBeltItems() {
  const std::shared_ptr<VSprite> keep = shared_from_this();  // a customer's reaction may reload the level
  float previous = 0.0f;  // the angle of the item ahead of this one (0 for the first)
  for (auto it = items_.begin(); it != items_.end();) {
    std::string ahead, behind;
    getStationZones(it->theta, ahead, behind);
    if (behind != it->zone && it->item) {
      const auto choc = std::dynamic_pointer_cast<VChocolate>(it->item);
      if (choc && choc->hasDecayed()) {
        removeChild(*it->item);
        it = items_.erase(it);
        continue;
      }
      if (offerToCustomer(*it, behind)) {
        removeChild(*it->item);
        it = items_.erase(it);
        continue;
      }
      it->zone = behind;
    }
    const float radius = calculateRadius(it->theta, ahead, behind);
    const unsigned elapsed = static_cast<unsigned>(getElapsed());
    float step = ((velocity_ / radius) * (velocity_ / radius) * static_cast<float>(elapsed)) / (endAngle_ - startAngle_);
    const float theta = it->theta;
    if (theta <= endAngle_) {
      float next = step + theta;
      if (std::fabs(next - previous) < kMinGap) {
        // too close to the item ahead: slowed by a fixed amount, and standing still when that takes it all
        if (step <= 0.0f) {
          step += kHoldBack;
          next = step > 0.0f ? theta + 0.0f : theta + step;
        } else {
          step -= kHoldBack;
          next = step <= 0.0f ? theta + 0.0f : theta + step;
        }
      }
      it->theta = next;
      const double r = radius;
      it->item->setPosition({center_.x + static_cast<float>(r * std::cos(next)), center_.y + static_cast<float>(-r * std::sin(next))});
      previous = next;
      ++it;
    } else {
      it->theta = step * 5.0f + theta;
      // (the turnaround that would send it round again is never on: kConveyor2 is read by nothing)
      if (!turnaround_) {
        removeChild(*it->item);
        it = items_.erase(it);
        activateDisposal();
      } else {
        ++it;
      }
    }
  }
}

}  // namespace choco::games::greatcc
