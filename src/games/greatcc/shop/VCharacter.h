#pragma once

// VCharacter (docs/re/greatcc-sprites-and-zones.md 5.1): a person who walks the zone graph. It keeps the move queue, the
// destination, the heading and the facing, the idle animations, and runs the walk: a step per update along the line to the
// next node of the route, the heading turning toward the direction of travel a fixed amount per update, the facing following
// the heading. The player and the customers derive from it.

#include <list>
#include <memory>
#include <string>
#include <vector>

#include "games/greatcc/core/ZoneGraph.h"
#include "games/greatcc/shop/VSuspendable.h"

namespace choco::games::greatcc {

class VCharacter : public VSuspendable {
 public:
  VCharacter(script::ScriptContext& ctx, std::shared_ptr<ZoneGraph> zones);
  const char* kind() const override { return "VCharacter"; }

  // VObject, then every child whose key starts with `idle_` is collected (in key order) and hidden, then the start point
  // (`startZone`, `startPoint`, searched one level up) is looked up.
  void loadFromTable(const StyleChain& style) override;
  // VObject::Init, then the idle countdown and flags reset, front facing, the current node, target and destination are the
  // start point and the sprite stands on it.
  void init() override;
  // The walk (docs/re/greatcc-sprites-and-zones.md 5.1): the next route node or queued point when standing, a step when
  // moving; then the facing, the idle animations, the end of a special animation, and VSuspendable.
  void update(bool softPaused) override;

  // ---- the walk (virtual where a derived class changes it) ----
  // moving, suspended or (never set) +0x225.
  virtual bool isBusy() const { return moving_ || suspended(); }
  // Appends to the move queue (never removed from except by GotoNextPoint); null is ignored. While a tutorial is in effect an
  // entry equal to the current destination (queue empty and still on its way) or to the queue's last is ignored.
  virtual void queueMovePoint(ZonePoint* p);
  // Pops the queue's front and travels to it, unless busy; true when it started one.
  virtual bool gotoNextPoint();
  // The destination is `p`: a route from the current node (the graph's path) and the first hop.
  virtual void travelTo(ZonePoint* p);
  // Targets `p`; unless the character is busy and standing, it starts moving, or at once reaches the node it stands on.
  virtual void setDestination(ZonePoint* p);
  virtual void atNode();
  // ZonePoint::OnArrival (empty), then the next queued point.
  virtual void atDestination();
  virtual void updatePosition();
  virtual void updateFacing();
  virtual void updateIdle();
  // SetFacing: the asset of the facing, flipped for left; stops the cycle when standing, plays it when moving.
  virtual void setFacing(int facing);
  // SetDirection(TVec2): normalised, the dirty flag untouched. SetDirection(eFacing): the unit vector of the facing, dirty.
  virtual void setDirectionVector(gfx::PointF v);
  virtual void setDirectionFacing(int facing);
  virtual void playRandomIdleAnim();
  virtual void stopCurrentIdle();
  // A tutorial is running and not finished (the tutorial manager comes with its slice).
  virtual bool tutorialInEffect() const { return false; }
  // The asset by that name, switched to `default` and played; the character faces front when it ends.
  void playSpecialAnim(const std::string& asset, bool loop);

  bool isPathEmpty() const { return queue_.empty(); }
  bool moving() const { return moving_; }
  int facing() const { return facing_; }
  float heading() const { return heading_; }
  float previousHeading() const { return previousHeading_; }
  gfx::PointF direction() const { return dir_; }
  float walkSpeed() const { return speed_; }
  void setWalkSpeed(float s) { speed_ = s; }
  ZonePoint* destination() const { return finalDest_; }
  ZonePoint* target() const { return target_; }
  ZonePoint* currentNode() const { return node_; }
  ZonePoint* startPoint() const { return start_; }
  const std::list<ZonePoint*>& queue() const { return queue_; }
  const std::shared_ptr<ZoneGraph>& zones() const { return zones_; }
  // The names of the nodes the character has reached, oldest first (the last 32), for the window dump and the tests.
  const std::vector<std::string>& nodeTrail() const { return trail_; }
  const std::vector<std::shared_ptr<VObject>>& idles() const { return idles_; }
  VObject* currentIdle() const { return idle_.get(); }
  int idleCountdownMs() const { return idleCountdown_; }

 protected:
  int elapsedMs() const { return static_cast<int>(getElapsed()); }
  // A value of values.xml (the defaults apply without a world).
  double valueReal(const char* name, double def) const;
  int valueInt(const char* name, int def) const;
  static bool isIdleName(const std::string& key) { return key.compare(0, 5, "idle_") == 0; }
  void noteNode(const ZonePoint* p);

  bool moving_ = false;
  bool specialAnim_ = false;
  bool facingDirty_ = false;
  int facing_ = -1;
  float lastEvaluated_ = -3.4028235e38f;  // -FLT_MAX
  float heading_ = 1.5707964f, previousHeading_ = 1.5707964f;
  float speed_ = 5.0f;
  int idleCountdown_ = 0;
  gfx::PointF dir_{0, 0};
  ZonePoint* finalDest_ = nullptr;
  ZonePoint* target_ = nullptr;
  ZonePoint* node_ = nullptr;
  ZonePoint* start_ = nullptr;
  std::shared_ptr<ZoneGraph> zones_;
  std::list<ZonePoint*> queue_;
  std::vector<std::shared_ptr<VObject>> idles_;
  std::shared_ptr<VObject> idle_;
  std::vector<std::string> trail_;
};

}  // namespace choco::games::greatcc
