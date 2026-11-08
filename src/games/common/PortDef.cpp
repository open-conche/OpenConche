#include "games/common/PortDef.h"

namespace choco::game {

namespace {

gfx::ColorF colorAt(const ui::Style& s, gfx::ColorF def) {
  const float r = static_cast<float>(s.numberAt(1).value_or(def.r)), g = static_cast<float>(s.numberAt(2).value_or(def.g)),
              b = static_cast<float>(s.numberAt(3).value_or(def.b)), a = static_cast<float>(s.numberAt(4).value_or(def.a));
  return {r, g, b, a};
}

std::vector<std::string> stringArray(const ui::Style& s, const char* key) {
  std::vector<std::string> out;
  if (auto t = s.table(key))
    for (int i = 1, n = t->size(); i <= n; ++i) out.push_back(t->stringAt(i).value_or(""));
  return out;
}

BuildingDef parseBuilding(const ui::Style& b) {
  BuildingDef d;
  d.name = b.stringOr("name");
  d.x = static_cast<float>(b.numberOr("x", 0));
  d.y = static_cast<float>(b.numberOr("y", 0));
  d.layer = static_cast<int>(b.numberOr("layer", 0));
  if (auto v = b.number("labelx")) d.labelX = static_cast<float>(*v);
  if (auto v = b.number("labely")) d.labelY = static_cast<float>(*v);
  return d;
}

SpriteDef parseSprite(const ui::Style& s) {
  SpriteDef d;
  d.frequency = s.numberOr("frequency", 100);
  d.images = stringArray(s, "images");
  d.masks = stringArray(s, "masks");
  d.image = s.stringOr("image");
  d.mask = s.stringOr("mask");
  d.layer = static_cast<int>(s.numberOr("layer", 0));
  if (auto behind = s.table("behind")) {
    const int n = static_cast<int>(behind->numberOr("layer", 0));
    d.layer = n == 0 ? 0 : n - 1;
  }
  if (auto path = s.table("path")) {
    d.hasPath = true;
    for (int i = 1, n = path->size(); i <= n; ++i)
      if (auto p = path->element(i))
        d.path.push_back({static_cast<float>(p->numberAt(1).value_or(0)), static_cast<float>(p->numberAt(2).value_or(0))});
  }
  d.time = static_cast<float>(s.numberOr("time", 0));
  const std::string motion = s.stringOr("motion", "loop");
  d.mode = motion == "oneway"   ? PathSprite::Mode::OneWay
           : motion == "bounce" ? PathSprite::Mode::Bounce
                                : PathSprite::Mode::Loop;
  d.x = static_cast<float>(s.numberOr("x", 0));
  d.y = static_cast<float>(s.numberOr("y", 0));
  d.scale = static_cast<float>(s.numberOr("scale", d.hasPath ? -1 : 1));
  auto opt = [&](const char* k) -> std::optional<float> {
    if (auto v = s.number(k)) return static_cast<float>(*v);
    return std::nullopt;
  };
  d.yFar = opt("yFar");
  d.yNear = opt("yNear");
  d.scaleFar = opt("scaleFar");
  d.scaleNear = opt("scaleNear");
  d.speedFar = static_cast<float>(s.numberOr("speedFar", 1));
  d.speedNear = static_cast<float>(s.numberOr("speedNear", 1));
  d.random = s.booleanOr("random", true);
  d.htile = static_cast<float>(s.numberOr("htile", 0));
  if (auto c = s.table("tint")) d.tint = colorAt(*c, d.tint);
  if (auto ts = s.table("tints"))
    for (int i = 1, n = ts->size(); i <= n; ++i)
      if (auto c = ts->element(i)) d.tints.push_back(colorAt(*c, {1, 1, 1, 1}));
  return d;
}

}  // namespace

PortDef parsePort(const ui::Style& port) {
  PortDef d;
  d.name = port.stringOr("name");
  d.yFar = static_cast<float>(port.numberOr("yFar", 0));
  d.yNear = static_cast<float>(port.numberOr("yNear", 800));
  d.scaleFar = static_cast<float>(port.numberOr("scaleFar", 1));
  d.scaleNear = static_cast<float>(port.numberOr("scaleNear", 1));
  if (auto bs = port.table("buildings"))
    for (int i = 1, n = bs->size(); i <= n; ++i)
      if (auto b = bs->element(i)) d.buildings.push_back(parseBuilding(*b));
  if (auto ss = port.table("sprites"))
    for (int i = 1, n = ss->size(); i <= n; ++i)
      if (auto s = ss->element(i)) d.sprites.push_back(parseSprite(*s));
  return d;
}

}  // namespace choco::game
