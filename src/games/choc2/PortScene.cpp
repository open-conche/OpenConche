#include "games/choc2/PortScene.h"

#include <algorithm>

namespace choco::game::choc2 {

namespace {

std::string baseName(const std::string& path) {
  std::string s = path.substr(path.find_last_of('/') == std::string::npos ? 0 : path.find_last_of('/') + 1);
  if (s.size() > 4 && s.compare(s.size() - 4, 4, ".xml") == 0) s.resize(s.size() - 4);
  return s;
}

// The building's name from `contents`, "(LBuilding:ByName('<name>')):NameRollover()"; the image's name when it has none.
std::string buildingName(const std::string& contents, const std::string& image) {
  const std::string key = "ByName('";
  const auto at = contents.find(key);
  if (at != std::string::npos) {
    const auto end = contents.find('\'', at + key.size());
    if (end != std::string::npos) return contents.substr(at + key.size(), end - at - key.size());
  }
  return baseName(image);
}

std::optional<float> opt(const ui::Style& t, const char* k) {
  if (auto v = t.number(k)) return static_cast<float>(*v);
  return std::nullopt;
}

}  // namespace

AddedSprite fromAddSprite(const ui::Style& t, int order, const TextureInfo& textureInfo) {
  AddedSprite out;
  const std::string image = t.stringOr("image");
  out.noteKey = image;
  if (image == ".LOGO") {
    auto logo = t.table("logo");
    if (!logo) {
      out.notes.push_back("[port] bsgAddSprite: .LOGO has no `logo` table; no sign");
      return out;
    }
    out.isLogo = true;
    out.logo = LogoSpec::fromStyle(*logo);
  }
  // Keys the native may read but nothing here does (docs/re/choc2-port-and-ledger.md 8.2, 8.6).
  for (const char* key : {"wait"})
    if (t.number(key) || t.boolean(key)) out.notes.push_back(std::string("[port] bsgAddSprite: `") + key + "` on '" + image + "' is not read");
  const float x = static_cast<float>(t.numberOr("x", 0)), y = static_cast<float>(t.numberOr("y", 0));

  if (t.string("contents") || t.function("command")) {
    out.kind = AddedSprite::Kind::Building;
    BuildingDef& b = out.building;
    b.image = image;
    b.contents = t.stringOr("contents");
    b.name = buildingName(b.contents, image);
    b.x = x;
    b.y = y;
    b.layer = order;
    const float bs = static_cast<float>(t.numberOr("scale", 1));  // the default of both perspective ends, as for sprites
    const float nearScale = opt(t, "nearscale").value_or(bs), farScale = opt(t, "farscale").value_or(bs);
    const float far = opt(t, "far").value_or(0), near = opt(t, "near").value_or(400);
    b.scale = nearScale;  // TPort::Draw runs the perspective block for buildings too (docs/re/choc2-port-binary.md 3.1):
    if (nearScale != farScale && near != far)                 // equal ends: that scale; else the clamped lerp by the
      b.scale = std::max(0.0f, farScale + (nearScale - farScale) * ((y - far) / (near - far)));  // layout y
    b.labelX = opt(t, "labelx");
    b.labelY = opt(t, "labely");
    if (auto f = t.function("command")) b.command = std::move(*f);
    return out;
  }

  out.kind = AddedSprite::Kind::Sprite;
  SpriteDef& d = out.sprite;
  d.image = image;
  d.x = x;
  d.y = y;
  d.layer = order;
  if (auto path = t.table("path")) {
    d.hasPath = true;
    for (int i = 1, n = path->size(); i <= n; ++i)
      if (auto p = path->element(i))
        d.path.push_back({static_cast<float>(p->numberAt(1).value_or(0)), static_cast<float>(p->numberAt(2).value_or(0))});
  }
  if (d.hasPath) {
    d.time = static_cast<float>(t.numberOr("time", kDefaultPathMs));
  }
  d.choc2 = true;
  d.hflip = t.booleanOr("hflip", false);
  d.mode = t.booleanOr("bounce", false) ? PathSprite::Mode::Bounce : PathSprite::Mode::Loop;
  d.hold = static_cast<float>(t.numberOr("hold", 0));
  d.scale = static_cast<float>(t.numberOr("scale", d.hasPath ? -1 : 1));
  d.yFar = opt(t, "far");
  d.yNear = opt(t, "near");
  d.scaleFar = opt(t, "farscale");
  d.scaleNear = opt(t, "nearscale");
  // `scale` is only the default of nearscale and farscale; the explicit ones win.
  if (d.scale > 0) {
    if (!d.scaleFar) d.scaleFar = d.scale;
    if (!d.scaleNear) d.scaleNear = d.scale;
  }
  // TPort::Draw applies the perspective to every sprite, static ones too (docs/re/choc2-port-binary.md 3.1): with the two scales
  // different, s = max(0, farscale + (nearscale - farscale) * ((y - far) / (near - far))) with `y` in layout coordinates (before
  // the Port's offset, which AddSprite adds and Draw takes off again); with equal scales it is that scale. near defaults to 400.
  if (!d.hasPath) {
    const float nearScale = d.scaleNear.value_or(d.scale), farScale = d.scaleFar.value_or(d.scale);
    const float far = d.yFar.value_or(0), near = d.yNear.value_or(400);
    d.scale = nearScale;
    if (nearScale != farScale && near != far) d.scale = std::max(0.0f, farScale + (nearScale - farScale) * ((d.y - far) / (near - far)));
  }
  if (auto n = t.number("htile")) d.htile = static_cast<float>(*n);
  else if (t.booleanOr("htile", false) && textureInfo) d.htile = static_cast<float>(textureInfo(image));
  // A sheet on a path sprite is a walk cycle (car_01.xml frames{1,2}), not a set of headings.
  d.faceLeft = true;  // every vehicle, plane, boat and camel in the sheets faces left
  d.headingFrames = image.size() < 4 || image.compare(image.size() - 4, 4, ".xml") != 0;
  return out;
}

}  // namespace choco::game::choc2
