#pragma once

// ZoneController, Zone and ZonePoint (docs/re/greatcc-sprites-and-zones.md 6): the named points the avatar walks between, their
// connections and the search that picks a route. Pure data, no rendering and no Lua. The orders of the original's lists are
// kept because the search's tie-breaking depends on them: zones and points are inserted at the front, so they iterate newest
// first, and so do a point's connections.

#include <list>
#include <memory>
#include <string>
#include <string_view>

namespace choco::games::greatcc {

struct Zone;
struct ZonePoint;

// ZonePointConnection: the far end, its exact length and whether a zone weld made it.
struct ZoneConnection {
  ZonePoint* target = nullptr;
  double length = 0;
  bool temporary = false;
};

struct ZonePoint {
  int x = 0, y = 0;
  unsigned serial = 0;  // the order the graph made the points in (the original's sets order them by allocation)
  std::string name;
  Zone* zone = nullptr;
  std::list<ZoneConnection> connections;  // newest first
  bool disabled = false;
  // the search's own state
  bool visited = false, inFrontier = false;
  int cost = -1;  // -1 = unset
  ZonePoint* predecessor = nullptr;
  void resetPathfinding() {
    visited = inFrontier = false;
    cost = -1;
    predecessor = nullptr;
  }
};

struct Zone {
  std::string name;
  std::list<std::unique_ptr<ZonePoint>> points;  // newest first
  std::list<ZonePoint*> entrances, exits;
  void resetPathfinding() {
    for (auto& p : points) p->resetPathfinding();
  }
  void clearConnections() {
    for (auto& p : points) p->connections.clear();
  }
  ZonePoint* findPoint(const std::string& pointName) const;
};

class ZoneGraph {
 public:
  // InitializeZones: the current path is dropped, then `<zonecontroller>` is read: its `<zone name>` elements (with `<point name x
  // y isentrance isexit/>` and `<connection first second/>`, in file order: a connection needs both its points already read)
  // and `<crosszone>` blocks (`<connection firstzone firstnode secondzone secondnode/>`). A zone or point of a name that exists
  // is reused; a zone read again first loses its connections. False (and nothing changes) when the text is not XML.
  bool load(std::string_view xml);

  Zone* findZone(const std::string& name) const;
  ZonePoint* findPoint(const std::string& zone, const std::string& point) const;
  // DisablePoint: nothing when the zone or the point is unknown.
  void disablePoint(const std::string& zone, const std::string& point, bool disabled);
  // AddZone: the zone of that name, made (at the front) when it does not exist.
  Zone& addZone(const std::string& name);
  // Zone::AddPoint: a point of that name in the zone is reused (its search state reset, its position and its entrance and exit
  // membership replaced, the connections kept); otherwise a new one goes to the front, and to the entrance and exit lists
  // (also at the front) when asked.
  ZonePoint& addPoint(Zone& zone, int x, int y, const std::string& name, bool entrance, bool exit);
  // Zone::ConnectPoints: a two-way connection whose length is the points' distance computed in doubles. Both new connections go
  // to the front of their lists.
  static void connect(ZonePoint& a, ZonePoint& b, bool temporary);
  // The same by names inside the zone; nothing when either is missing.
  void connect(Zone& zone, const std::string& first, const std::string& second, bool temporary);

  // AddPath(start, dest): the route as [start, ..., dest, dest]: the destination is there twice; an unreachable destination
  // gives [dest, dest]; start == dest gives [P, P]. A null start is taken as unreachable (the original does not guard it); a
  // null destination gives an empty path.
  void addPath(ZonePoint* start, ZonePoint* dest);
  // GetNextPoint: pops the front (null when empty).
  ZonePoint* nextPoint();
  bool pathEmpty() const { return path_.empty(); }
  // Ours (fix F5): the original never drops a route.
  void clearPath() { path_.clear(); }
  const std::list<ZonePoint*>& path() const { return path_; }
  bool existsInPath(const ZonePoint* p) const;
  const std::list<std::unique_ptr<Zone>>& zones() const { return zones_; }

 private:
  void weld(Zone& a, Zone& b);
  void unweld(Zone& a, Zone& b);

  std::list<std::unique_ptr<Zone>> zones_;  // newest first
  std::list<ZonePoint*> path_;
  unsigned nextSerial_ = 0;
};

}  // namespace choco::games::greatcc
