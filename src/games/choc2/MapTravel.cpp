#include "games/choc2/MapTravel.h"

#include <cstdio>
#include <functional>

#include "audio/Audio.h"
#include "games/choc2/TravelSounds.h"
#include "games/common/sprite/PathSprite.h"
#include "games/choc2/Choc2AudioNatives.h"
#include "gfx/TextureCache.h"
#include "ui/DrawContext.h"

namespace choco::game::choc2 {

void MapTravel::init(const ui::Style& style) {
  Window::init(style);
  xOffset_ = static_cast<float>(style.numberOr("xOffset", 0));
  yOffset_ = static_cast<float>(style.numberOr("yOffset", 0));
}

void MapTravel::postChildrenInit(const ui::Style& style) {
  Window::postChildrenInit(style);
  root_ = std::make_unique<Sprite>(0, "travel");
  root_->topLeft = true;
  if (gfx::TextureCache* textures = manager().textures()) dotTexture_ = textures->load("image/traveldot");
}

void MapTravel::draw(ui::DrawContext& ctx) const {
  if (root_) root_->draw(ctx.renderer, SpriteSpec{{ctx.origin.x + xOffset_, ctx.origin.y + yOffset_}, 1, 0, ctx.alpha});
}

void MapTravel::loadIcon(const std::string& type) {
  type_ = type;
  if (!vehicle_) return;
  vehicle_->texture = nullptr;
  gfx::TextureCache* textures = manager().textures();
  const std::string name = "image/icon_" + type;
  if (!textures) return;
  if (textures->resolve(name).path.empty()) {
    if (!warnedIcon_) ctx_.log.write("[choc2] MapTravel: no vehicle image '" + name + "'; the trip is drawn without one");
    warnedIcon_ = true;
    return;
  }
  vehicle_->texture = textures->load(name);
}

void MapTravel::place() {
  if (!vehicle_) return;
  gfx::PointF tangent;
  vehicle_->pos = mover_.curve().at(mover_.fraction(), &tangent);
  // Art faces left; keep the last facing when the curve gives no direction (a leg with no length).
  if (tangent.x != 0 || tangent.y != 0) vehicle_->hflip = PathSprite::mirrorForTangent(tangent);
}

bool MapTravel::initiateTravel(std::vector<gfx::PointF> points, int weeks, const std::string& type) {
  if (points.size() < 4) {
    ctx_.log.write("[choc2] InitiateTravel: a leg needs at least 4 control points, got " + std::to_string(points.size()));
    return false;
  }
  if (!root_) return false;
  if (vehicle_) root_->removeChild(*vehicle_);
  auto vehicle = std::make_unique<Sprite>(kLayerVehicle, "vehicle");
  vehicle->visible = false;  // until the first week moves
  vehicle_ = &root_->addChild(std::move(vehicle));
  mover_.begin(gfx::Curve::fromPoints(std::move(points)), weeks);
  loadIcon(type);
  place();
  return true;
}

bool MapTravel::travelOneWeek(const std::string& type) {
  if (!mover_.begun()) {
    ctx_.log.write("[choc2] TravelOneWeek: no leg was started with InitiateTravel");
    return false;
  }
  if (type != type_) loadIcon(type);
  playTravelSounds(type);
  if (!mover_.startWeek(manager().modalNow(*this))) return false;
  if (vehicle_) vehicle_->visible = true;
  place();
  manager().startWindowAnimation(id(), kTickMs);
  return true;
}

// The ambient bed and the type's one-shot (docs/re/choc2-map-binary.md 1.4), through the same audio paths as the Lua natives.
void MapTravel::playTravelSounds(const std::string& type) {
  const TravelAudio a = travelAudio(type, soundType_);
  soundType_ = a.remember;
  if (!ctx_.audio) return;
  if (a.ambient) ctx_.audio->setAmbientFile(*a.ambient);
  if (!a.oneShot.empty()) ctx_.audio->playFile(script::choc2SoundPath(a.oneShot));
}

bool MapTravel::onAnimate() {
  std::vector<gfx::PointF> dots;
  const bool going = mover_.step(manager().modalNow(*this), &dots);
  for (const gfx::PointF& at : dots) {
    auto dot = std::make_unique<Sprite>(kLayerDot, "dot");
    dot->texture = dotTexture_;
    dot->pos = at;
    root_->addChild(std::move(dot));
  }
  place();
  if (!going && vehicle_) vehicle_->visible = false;  // drawn only while a week runs
  return going;
}

int MapTravel::dotSprites() const {
  int n = 0;
  if (root_)
    for (const auto& c : root_->children()) n += c->debugName == "dot";
  return n;
}

MapTravel* MapTravel::current(ui::WindowManager& wm) {
  std::function<MapTravel*(ui::Window&)> find = [&](ui::Window& w) -> MapTravel* {
    if (auto* t = dynamic_cast<MapTravel*>(&w)) return t;
    for (const auto& c : w.children())
      if (MapTravel* t = find(*c)) return t;
    return nullptr;
  };
  const auto& modals = wm.modals();
  for (auto it = modals.rbegin(); it != modals.rend(); ++it)
    if (MapTravel* t = find(**it)) return t;
  return nullptr;
}

std::string MapTravel::detail() const {
  char buf[200];
  std::snprintf(buf, sizeof buf, " leg=%s week=%d/%d t=%.3f travelling=%d dots=%d%s", type_.empty() ? "-" : type_.c_str(),
                mover_.weeksDone(), mover_.weeks(), double(mover_.fraction()), mover_.active() ? 1 : 0, mover_.dots(),
                hasVehicleIcon() ? "" : " noicon");
  return buf;
}

}  // namespace choco::game::choc2
