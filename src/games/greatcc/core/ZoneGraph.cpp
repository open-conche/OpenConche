#include "games/greatcc/core/ZoneGraph.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>

#include "assets/Xml.h"

namespace choco::games::greatcc {

namespace {
int toInt(const char* s) { return std::atoi(s); }
bool lowerEquals(std::string s, const char* want) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s == want;
}
}  // namespace

ZonePoint* Zone::findPoint(const std::string& pointName) const {
  for (const auto& p : points)
    if (p->name == pointName) return p.get();
  return nullptr;
}

Zone* ZoneGraph::findZone(const std::string& name) const {
  for (const auto& z : zones_)
    if (z->name == name) return z.get();
  return nullptr;
}

ZonePoint* ZoneGraph::findPoint(const std::string& zone, const std::string& point) const {
  Zone* z = findZone(zone);
  return z ? z->findPoint(point) : nullptr;
}

void ZoneGraph::disablePoint(const std::string& zone, const std::string& point, bool disabled) {
  if (ZonePoint* p = findPoint(zone, point)) p->disabled = disabled;
}

Zone& ZoneGraph::addZone(const std::string& name) {
  if (Zone* z = findZone(name)) return *z;
  auto z = std::make_unique<Zone>();
  z->name = name;
  zones_.push_front(std::move(z));
  return *zones_.front();
}

ZonePoint& ZoneGraph::addPoint(Zone& zone, int x, int y, const std::string& name, bool entrance, bool exit) {
  if (ZonePoint* existing = zone.findPoint(name)) {
    existing->resetPathfinding();
    existing->x = x;
    existing->y = y;
    auto sync = [&](std::list<ZonePoint*>& list, bool member) {
      const auto it = std::find(list.begin(), list.end(), existing);
      if (!member) {
        if (it != list.end()) list.remove(existing);
      } else if (it == list.end()) {
        list.push_back(existing);
      }
    };
    sync(zone.entrances, entrance);
    sync(zone.exits, exit);
    return *existing;
  }
  auto p = std::make_unique<ZonePoint>();
  p->x = x;
  p->y = y;
  p->name = name;
  p->zone = &zone;
  p->serial = nextSerial_++;
  ZonePoint* raw = p.get();
  zone.points.push_front(std::move(p));
  if (entrance) zone.entrances.push_front(raw);
  if (exit) zone.exits.push_front(raw);
  return *raw;
}

void ZoneGraph::connect(ZonePoint& a, ZonePoint& b, bool temporary) {
  const int dx = a.x - b.x, dy = a.y - b.y;
  const double length = std::sqrt(static_cast<double>(dx * dx) + static_cast<double>(dy * dy));
  a.connections.push_front({&b, length, temporary});
  b.connections.push_front({&a, length, temporary});
}

void ZoneGraph::connect(Zone& zone, const std::string& first, const std::string& second, bool temporary) {
  ZonePoint* a = zone.findPoint(first);
  ZonePoint* b = zone.findPoint(second);
  if (a && b) connect(*a, *b, temporary);
}

// ---- loading ----

bool ZoneGraph::load(std::string_view xml) {
  assets::Xml doc;
  if (!doc.parse(xml)) return false;
  path_.clear();
  pugi::xml_node root = doc.root();
  if (!root || !lowerEquals(root.name(), "zonecontroller")) return true;
  for (pugi::xml_node n : root.children()) {
    const std::string kind = n.name();
    if (kind == "zone") {
      Zone& zone = addZone(n.attribute("name").as_string());
      zone.clearConnections();
      zone.resetPathfinding();
      for (pugi::xml_node c : n.children()) {
        const std::string ck = c.name();
        if (ck == "point") {
          addPoint(zone, toInt(c.attribute("x").as_string()), toInt(c.attribute("y").as_string()), c.attribute("name").as_string(),
                   toInt(c.attribute("isentrance").as_string()) == 1, toInt(c.attribute("isexit").as_string()) == 1);
        } else if (ck == "connection") {
          connect(zone, c.attribute("first").as_string(), c.attribute("second").as_string(), false);
        }
      }
    } else if (kind == "crosszone") {
      for (pugi::xml_node c : n.children()) {
        if (std::string(c.name()) != "connection") continue;
        Zone* z = findZone(c.attribute("firstzone").as_string());
        if (!z) continue;
        ZonePoint* a = findPoint(c.attribute("firstzone").as_string(), c.attribute("firstnode").as_string());
        ZonePoint* b = findPoint(c.attribute("secondzone").as_string(), c.attribute("secondnode").as_string());
        if (a && b) connect(*a, *b, false);
      }
    }
  }
  return true;
}

// ---- the search ----

void ZoneGraph::weld(Zone& a, Zone& b) {
  for (ZonePoint* exit : a.exits)
    for (ZonePoint* entrance : b.entrances) connect(*exit, *entrance, true);
}

// A temporary connection leaves a point of either zone when it reaches a point of another zone.
void ZoneGraph::unweld(Zone& a, Zone& b) {
  for (Zone* z : {&a, &b})
    for (auto& p : z->points)
      p->connections.remove_if([&](const ZoneConnection& c) { return c.target && c.target->zone != z && c.temporary; });
}

void ZoneGraph::addPath(ZonePoint* start, ZonePoint* dest) {
  path_.clear();
  if (!dest) return;
  Zone* zoneA = start ? start->zone : nullptr;
  Zone* zoneB = dest->zone;
  for (auto& z : zones_) z->resetPathfinding();
  const bool welded = start && zoneA && zoneB && zoneA != zoneB;
  if (welded) weld(*zoneA, *zoneB);
  if (start) {
    start->cost = 0;
    start->visited = true;
    std::list<ZonePoint*> frontier;
    ZonePoint* cur = start;
    for (;;) {
      for (const ZoneConnection& c : cur->connections) {
        ZonePoint* n = c.target;
        if (!n || n->disabled || n->visited) continue;
        if (!n->inFrontier) {
          frontier.push_front(n);
          n->inFrontier = true;
        }
        // the comparison is exact, the stored cost truncates the edge length
        if (n->cost == -1 || static_cast<double>(n->cost) > static_cast<double>(cur->cost) + c.length) {
          n->cost = static_cast<int>(c.length) + cur->cost;
          n->predecessor = cur;
        }
      }
      cur->visited = true;
      int best = -1;
      for (ZonePoint* f : frontier)
        if (!f->visited && (best == -1 || f->cost < best)) {
          best = f->cost;
          cur = f;
        }
      if (best == -1) break;
    }
  }
  path_.push_back(dest);
  ZonePoint* cur = dest;
  while (cur && cur != start) {
    path_.push_front(cur);
    cur = cur->predecessor;
  }
  if (cur) path_.push_front(cur);
  if (welded) unweld(*zoneA, *zoneB);
}

ZonePoint* ZoneGraph::nextPoint() {
  if (path_.empty()) return nullptr;
  ZonePoint* p = path_.front();
  path_.pop_front();
  return p;
}

bool ZoneGraph::existsInPath(const ZonePoint* p) const { return std::find(path_.begin(), path_.end(), p) != path_.end(); }

}  // namespace choco::games::greatcc
