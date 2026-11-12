#include "games/greatcc/shop/VStation.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "games/greatcc/GreatCCWorld.h"
#include "games/greatcc/shop/VConveyor.h"
#include "games/greatcc/shop/VDisplay.h"
#include "games/greatcc/shop/VCustomer.h"
#include "games/greatcc/shop/VDisposal.h"
#include "games/greatcc/shop/VPlayer.h"
#include "games/greatcc/tutorial/TutorialSpider.h"

namespace choco::games::greatcc {

namespace {
std::string stationName(int id) {
  char buf[24];
  std::snprintf(buf, sizeof buf, "station%02i", id);
  return buf;
}
}  // namespace

VStation::VStation(script::ScriptContext& ctx, int id, int type) : VSuspendable(ctx, stationName(id), "station"), id_(id), type_(type) {
  setClickable(true);
}

std::shared_ptr<VStation> VStation::create(script::ScriptContext& ctx, int id, int type) {
  std::shared_ptr<VStation> station;
  if (type == kStationDisposal) {
    station = std::make_shared<VDisposal>(ctx, id);
    station->addEffect(std::make_shared<HoverGlow>(0.18f, true, true, false));
  } else if (type == kStationDisplay) {
    station = std::make_shared<VDisplay>(ctx, id);
    station->addEffect(std::make_shared<HoverGlow>(0.18f, false, true, true));
  } else if (type == kStationConveyor) {
    station = std::make_shared<VConveyor>(ctx, id);
    station->addEffect(std::make_shared<HoverGlow>(0.18f, false, true, true));
  } else {
    station = std::make_shared<VStation>(ctx, id, type);
    if (type == kStationCounter) station->addEffect(std::make_shared<HoverGlow>(0.18f, false, true, true));
  }
  return station;
}

void VStation::loadFromTable(const StyleChain& style) {
  machines_.clear();
  single_.reset();
  removeChildren();
  playerPoints_.clear();
  singleForm_ = false;
  custPoint_.clear();
  money_ = {};
  id_ = style.intOr("stationID", 0);
  type_ = style.intOr("stationType", 0);
  if (auto table = style.table("machines", 0)) {
    const StyleChain group(*table, &style);
    table->forEachEntry([&](const std::string& key, const ui::Style* t, const std::optional<std::string>&) {
      if (!t) return;
      const StyleChain mc(*t, &group);
      const int machineType = mc.intOr("stationType", type_);
      std::shared_ptr<VMachine> machine = VMachine::create(context(), machineType);
      if (!machine) {
        if (context().warned.insert("greatcc-machine-" + std::to_string(machineType)).second)
          context().log.write("[greatcc] a machine of station type " + std::to_string(machineType) + " has no machine class; it is left out");
        return;
      }
      machine->setHost(host_);
      machine->loadFromTable(mc);
      machine->init();
      machines_[VObject::lower(key)] = machine;
      addChild(machine);
    });
  }
  if (auto points = style.table("playerPoint", 0)) {
    for (int i = 1; i <= points->size(); i += 2) {
      PlayerPoint p;
      p.zonePoint = points->stringAt(i).value_or("");
      p.facing = static_cast<int>(points->numberAt(i + 1).value_or(kFaceFront));
      playerPoints_.push_back(std::move(p));
    }
  } else if (auto single = style.own().string("playerPoint")) {
    // no facing is stored for it: the lookup finds the map's default, left
    singleForm_ = true;
    playerPoints_.push_back({*single, kFaceLeft});
  }
  if (type_ == kStationCounter) {
    custPoint_ = style.own().stringOr("custPoint");
    if (custPoint_.empty()) {
      char buf[24];
      std::snprintf(buf, sizeof buf, "customer%02i", id_);
      custPoint_ = buf;
    }
    if (auto m = style.table("money", 0)) money_ = {static_cast<float>(m->numberAt(1).value_or(0)), static_cast<float>(m->numberAt(2).value_or(0))};
  }
  VObject::loadFromTable(style);
}

void VStation::resolvePoints(const ZoneGraph& zones) {
  points_.clear();
  facingByPoint_.clear();
  custZone_ = nullptr;
  auto add = [&](ZonePoint* p) {
    if (p && std::find(points_.begin(), points_.end(), p) == points_.end()) points_.push_back(p);
  };
  if (singleForm_ && !playerPoints_.empty()) {
    ZonePoint* p = zones.findPoint("avatar", playerPoints_.front().zonePoint);
    if (!p) p = zones.findPoint("avatar", name());
    add(p);
  } else {
    for (const PlayerPoint& pp : playerPoints_)
      if (ZonePoint* p = zones.findPoint("avatar", pp.zonePoint)) {
        add(p);
        facingByPoint_[p->name] = pp.facing;
      }
  }
  std::stable_sort(points_.begin(), points_.end(), [](const ZonePoint* a, const ZonePoint* b) { return a->serial < b->serial; });
  if (type_ == kStationCounter) {
    custZone_ = zones.findPoint("customers", custPoint_);
    if (!custZone_) {
      char buf[24];
      std::snprintf(buf, sizeof buf, "customer%02i", id_);
      custZone_ = zones.findPoint("customers", buf);
    }
  }
}

bool VStation::hasPoint(const ZonePoint* p) const { return p && std::find(points_.begin(), points_.end(), p) != points_.end(); }

int VStation::facingAt(const std::string& pointName) const {
  auto it = facingByPoint_.find(pointName);
  return it == facingByPoint_.end() ? kFaceLeft : it->second;
}

void VStation::onClick() {
  // (TGameState's ++TotalValidClicks only feeds the analytics and is not kept.)
  const std::shared_ptr<VPlayer> player = host_ ? host_->player() : nullptr;
  if (!player || points_.empty()) return;
  const gfx::PointF here = player->position();
  auto distance = [&](const ZonePoint* p) {
    const float dx = static_cast<float>(p->x) - here.x, dy = static_cast<float>(p->y) - here.y;
    return std::sqrt(dx * dx + dy * dy);
  };
  ZonePoint* best = points_.front();
  float bestLen = distance(best);
  for (std::size_t i = 1; i < points_.size(); ++i) {
    const float len = distance(points_[i]);
    if (len < bestLen) {
      best = points_[i];
      bestLen = len;
    }
  }
  player->queueMovePoint(best);
}

void VStation::onArrival(VPlayer& player, const ZonePoint* point) {
  // The machines (or the customer) may reload the level or take this station out of the scene: the call finishes on a live one.
  const std::shared_ptr<VSprite> keep = shared_from_this();
  if (!suspended()) {
    if (single_) {
      const std::shared_ptr<VMachine> machine = single_;
      machine->onArrival(player);
    } else if (machines_.size() > 1) {
      // the clicked buttons' machines, in click order (the list is copied: an arrival may click again)
      const std::list<std::string> clicked = pendingClicks_;
      for (const std::string& name : clicked) {
        const auto it = machines_.find(name);
        if (it != machines_.end() && it->second) {
          const std::shared_ptr<VMachine> machine = it->second;
          machine->onArrival(player);
        }
      }
    }
    // the customer at the counter (the call may take it away, or reload the level)
    if (const std::shared_ptr<VCustomer> guest = customer_.lock()) guest->onArrival(player);
    // the tutorial listeners of this sprite are told the player arrived
    tellTutorialsOfArrival(*this);
  }
  pendingClicks_.clear();
  if (player.isPathEmpty()) player.setDirectionFacing(point ? facingAt(point->name) : kFaceFront);
}

bool VStation::attachCustomer(const std::shared_ptr<VCustomer>& customer) {
  if (!customer || customer_.lock() || type_ != kStationCounter) return false;
  customer_ = customer;
  setMouse(true);
  return true;
}

std::shared_ptr<VCustomer> VStation::detachCustomer() {
  const std::shared_ptr<VCustomer> guest = customer_.lock();
  if (!guest) return nullptr;
  if (guest->menu()) removeChild(*guest->menu());
  if (guest->money()) removeChild(*guest->money());
  guest->setStation(nullptr);
  customer_.reset();
  setMouse(machineCount_ > 0);
  if (machineCount_ <= 0 && hovered()) doEvent(SpriteEvent::Leave, {});
  return guest;
}

void VStation::init() {
  VObject::init();
  // the tabletop: the second aesthetic upgrade's art, else the first's, else the plain one
  // (with no host nothing is active: the plain art, as the original, which switches the tabletop whatever is active)
  const bool two = host_ && host_->upgradeActive("kStation2", "aesthetic");
  const bool one = host_ && host_->upgradeActive("kStation1", "aesthetic");
  switchAsset("tabletop", two ? "upgrade2" : (one ? "upgrade1" : "default"), "default");
  updateAvailable();
  setMouse(machineCount_ > 0);
}

void VStation::updateAvailable() {
  single_.reset();
  machineCount_ = 0;
  for (const auto& [key, m] : machines_) {
    if (!m || !m->enabled()) continue;
    ++machineCount_;
    if (machineCount_ < 2) single_ = m;
    else single_.reset();
  }
  const std::shared_ptr<VStation> self = std::static_pointer_cast<VStation>(shared_from_this());
  for (const auto& [key, m] : machines_)
    if (m) m->setStation(self);
}

void VStation::onMachineClick(const std::string& machineName) {
  pendingClicks_.push_back(machineName);
  doEvent(SpriteEvent::Click, {});
}

void VStation::onMouseEnter() {
  if (single_) single_->doEvent(SpriteEvent::Enter, {});
}

void VStation::onMouseLeave() {
  if (single_) single_->doEvent(SpriteEvent::Leave, {});
}

}  // namespace choco::games::greatcc
