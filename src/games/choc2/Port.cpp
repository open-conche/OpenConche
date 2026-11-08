#include "games/choc2/Port.h"

#include <algorithm>
#include <functional>
#include <set>

#include "games/choc2/LogoRenderer.h"
#include "games/choc2/FireworkSprite.h"
#include "games/choc2/PortScene.h"
#include "gfx/SheetCache.h"
#include "gfx/TextureCache.h"
#include "script/ScriptContext.h"
#include "ui/DrawContext.h"
#include "ui/Layout.h"
#include "ui/Window.h"
#include "ui/WindowManager.h"

namespace choco::game::choc2 {

namespace {

// Each label Frame is `fit` around a Rectangle that has no width of its own and holds a TightText, so the width has to come from
// the text (INFERRED: the Rectangle and the Frame grow to hold their children, bottom up).
void fitDeep(ui::Window& w) {
  for (const auto& c : w.children()) fitDeep(*c);
  if (!w.children().empty()) ui::fitToChildren(w);
}

}  // namespace

void Port::init(const ui::Style& style) {
  PortWindow::init(style);
  xOffset_ = static_cast<float>(style.numberOr("xOffset", 0));
  yOffset_ = static_cast<float>(style.numberOr("yOffset", 0));
}

Port* Port::underConstruction(ui::WindowManager& wm) {
  for (ui::Window* w = wm.creationTop(); w; w = w->parent())
    if (auto* p = dynamic_cast<Port*>(w)) return p;
  return nullptr;
}

Port* Port::current(ui::WindowManager& wm) {
  if (Port* p = underConstruction(wm)) return p;
  std::function<Port*(ui::Window&)> find = [&](ui::Window& w) -> Port* {
    if (auto* p = dynamic_cast<Port*>(&w)) return p;
    for (const auto& c : w.children())
      if (Port* p = find(*c)) return p;
    return nullptr;
  };
  const auto& modals = wm.modals();
  for (auto it = modals.rbegin(); it != modals.rend(); ++it)
    if (Port* p = find(**it)) return p;
  return nullptr;
}

void Port::addSprite(const ui::Style& table) {
  if (collected_) {
    if (noted_.insert("late-addsprite").second) portLog_.write("[native-warning] bsgAddSprite after the Port was built; ignored");
    return;
  }
  gfx::TextureCache* textures = manager().textures();
  gfx::SheetCache* sheets = manager().sheets();
  const TextureInfo info = [&](const std::string& image) {
    if (image.size() > 4 && image.compare(image.size() - 4, 4, ".xml") == 0) {
      auto sheet = sheets ? sheets->get(image) : nullptr;
      return sheet && !sheet->frames.empty() ? sheet->frames[0].src.w : 0;
    }
    auto tex = textures ? textures->load(image) : nullptr;
    return tex ? tex->width() : 0;
  };
  AddedSprite a = fromAddSprite(table, order_++, info);
  if (a.isLogo) {
    // The sign is the logo drawn on the CPU into a texture made in memory (the cache re-uploads it after a device reset).
    if (!logoImages_) logoImages_.emplace(textures);
    if (textures)
      a.building.texture = textures->makeFromPixels("$logo-sign", renderLogo(a.logo, logoImages_->assets(logoFonts())));
    if (!a.building.texture) {
      portLog_.write("[port] bsgAddSprite: the player logo could not be rendered; no sign");
      return;
    }
  }
  for (const std::string& line : a.notes)
    if (noted_.insert(a.noteKey + "|" + line).second) portLog_.write(line);
  switch (a.kind) {
    case AddedSprite::Kind::Building:
      a.building.x += xOffset_;
      a.building.y += yOffset_;
      if (a.building.labelX) *a.building.labelX += xOffset_;
      if (a.building.labelY) *a.building.labelY += yOffset_;
      pending_.buildings.push_back(std::move(a.building));
      break;
    case AddedSprite::Kind::Sprite:
      a.sprite.x += xOffset_;
      a.sprite.y += yOffset_;
      pending_.sprites.push_back(std::move(a.sprite));
      break;
    case AddedSprite::Kind::Skipped: break;
  }
}

void Port::startFireworks() {
  if (collected_) {
    if (noted_.insert("late-fireworks").second)
      portLog_.write("[native-warning] StartFireworks after the Port was built; ignored");
    return;
  }
  // TPort::StartFireworks (0x271e2) records its sprite-list size and adds five TFireworks drawn at that index; the
  // engine's layer-sorted tree puts them exactly there (the next sprites' layers start after the five).
  fireworksLayer_ = order_;
  order_ += 5;
  for (int i = 0; i < 5; ++i)
    pendingFireworks_.push_back(
        std::make_unique<FireworkSprite>(fireworksLayer_ + i, std::make_unique<Firework>(rng_, -1, kC2FireworkStyle)));
  if (noted_.insert("fireworks").second)
    portLog_.write("[port] StartFireworks: five fireworks between layout items " + std::to_string(fireworksLayer_ - 1) + " and " +
                   std::to_string(fireworksLayer_));
}

PortDef Port::readScene(const ui::Style&) {
  collected_ = true;
  PortDef def = std::move(pending_);
  pending_ = {};
  def.yNear = 400;  // the default `near` (`far` is 0); Chocolatier 3's is 800
  return def;
}

void Port::postChildrenInit(const ui::Style& style) {
  PortWindow::postChildrenInit(style);
  // The labels are created visible and stay until stopPulsing() hides them.
  if (ui::Window* labels = find("buildinglabels"))
    for (const auto& label : labels->children()) fitDeep(*label);
  // The five fireworks join the scene's root at their layout slot (between the sprites around the StartFireworks call).
  for (auto& f : pendingFireworks_) {
    f->resolveTexture(manager().textures());
    fireworks_.push_back(f.get());
    rootSprite()->addChild(std::move(f));
  }
  pendingFireworks_.clear();
  manager().startWindowAnimation(id(), kTickMs, true);  // catch up: the scene clock is real time
}

bool Port::onAnimate() {
  pulseTick(kTickMs);
  // TPort's 40 ms task also updates the fireworks; a fire tick plays its sound (firework<1..6>; the sixth has no
  // shipped sfx file, so it fails silently, as in the original).
  for (FireworkSprite* f : fireworks_) {
    const int sound = f->firework()->update();
    if (sound) manager().playSound("sfx/firework" + std::to_string(sound) + ".ogg", false);
  }
  return true;
}

}  // namespace choco::game::choc2
