#include "games/common/PortWindow.h"

#include <algorithm>
#include <cmath>

#include "games/common/World.h"
#include "gfx/SheetCache.h"
#include "gfx/TextureCache.h"
#include "script/ScriptContext.h"
#include "ui/DrawContext.h"
#include "ui/WindowManager.h"

namespace choco::game {

namespace {

std::string baseName(const std::string& path) {
  std::string s = path.substr(path.find_last_of('/') == std::string::npos ? 0 : path.find_last_of('/') + 1);
  if (s.size() > 4 && s.compare(s.size() - 4, 4, ".xml") == 0) s.resize(s.size() - 4);
  return s;
}

bool contains(const std::string& s, const char* part) { return s.find(part) != std::string::npos; }

}  // namespace

PortWindow::PortWindow(ui::WindowManager& wm, script::Rng& rng, script::Log& log, World* world)
    : Window(wm), rng_(rng), log_(log), world_(world) {}

PortWindow::~PortWindow() = default;

float PortWindow::introBrightness(double e) { return e < kIntroMs ? static_cast<float>(0.25 + 0.75 * e / kIntroMs) : 1.0f; }

float PortWindow::introLabelAlpha(double e) {
  return static_cast<float>(std::max(0.0, 1.0 - 2.0 * (e / kIntroMs - 0.5)));
}

PortWindow::Pulse PortWindow::pulse(double rolloverTime) {
  float f = static_cast<float>(rolloverTime / 1500.0);
  if (f > 1.0f) f = 2.0f - f;
  return {f > 0.4f ? 1.0f - 0.5f * f : 0.8f, f};
}

float PortWindow::rolloverScaleFor(int w, int h) {
  if (w <= 0 || h <= 0) return 0;
  const float fw = static_cast<float>(w), fh = static_cast<float>(h);
  return std::min({1.2f, (fw + 50) / fw, (fh + 50) / fh}) - 1.0f;
}

std::shared_ptr<gfx::Texture> PortWindow::loadTexture(const std::string& name) {
  if (name.empty() || !manager().textures()) return nullptr;
  return manager().textures()->load(name);
}

void PortWindow::init(const ui::Style& style) {
  Window::init(style);
  background_ = style.stringOr("background");
  root_ = std::make_unique<Sprite>(0, "bg");
  root_->topLeft = true;  // flag 8 with origin (0,0)
  root_->texture = loadTexture(background_);
}

void PortWindow::postChildrenInit(const ui::Style& style) {
  Window::postChildrenInit(style);
  if (!root_) init(style);
  musicDelay_ = style.numberOr("musicDelay", 25000);
  PortDef def = readScene(style);
  build(def);
  const double now = manager().nowMs();
  anim_.start(now, kTaskMs);
  if (fadeIn_) tintBackground(0.25f);
  startTime_ = lastTime_ = now;
  if (choc2Pulse_) {
    introPulse_ = true;
    sceneMs_ = hoverMs_ = 0;
    pulseHover_ = -1;
    applyPulses();
  }
  musicTime_ = now + musicDelay_;
  built_ = true;
  if (world_) world_->setPort(id());
}

PortDef PortWindow::readScene(const ui::Style& style) {
  if (auto port = style.table("port")) return parsePort(*port);
  log_.write("[port] PortWindow has no `port` table; the scene is just the background");
  return {};
}

void PortWindow::build(PortDef& def) {
  addBuildings(def);
  for (const SpriteDef& sd : def.sprites) addSprite(def, sd);
  root_->resortDirty();
}

ui::Window* PortWindow::labelFor(const std::string& name) {
  for (const auto& c : children())
    if (c->name() == name) return c.get();
  return nullptr;
}

void PortWindow::addBuildings(PortDef& def) {
  float cursorX = 50, cursorY = 100;  // the debug grid for buildings with no picture
  for (BuildingDef& b : def.buildings) {
    auto tex = b.texture ? b.texture : loadTexture(b.image.empty() ? "ports/" + def.name + "/" + b.name : b.image);
    float x = b.x, y = b.y;
    if (!tex) {
      if (warned_.insert(b.name).second) log_.write("[port] no picture for building '" + b.name + "'; using ports/temp_building");
      tex = loadTexture("ports/temp_building");
      if (cursorX + 100 > 500) {
        x = cursorX, y = cursorY;
        cursorY += 120;
        cursorX = 50;
      } else {
        x = cursorX, y = cursorY;
        cursorX += 100;
        cursorY += 20;
      }
    }
    auto sprite = std::make_unique<Sprite>(b.layer, "building:" + b.name);
    sprite->texture = tex;
    sprite->pos = {x, y};
    sprite->scale = b.scale;
    Sprite& sref = root_->addChild(std::move(sprite));

    std::unique_ptr<Sprite> hl;
    if (choc2Pulse_) hl = std::make_unique<PulseSprite>(-1, "pulse:" + b.name);
    else hl = std::make_unique<Sprite>(-1, "highlight:" + b.name);
    hl->texture = tex;
    if (choc2Pulse_ && tex && manager().textures()) {
      // The pulse is a flat white silhouette of the building (a texture made once per picture).
      if (auto sil = PulseSprite::makeSilhouette(*manager().textures(), *tex)) hl->texture = std::move(sil);
    }
    hl->visible = false;
    Sprite& href = sref.addChild(std::move(hl));

    Building info;
    info.name = b.name;
    info.sprite = &sref;
    info.highlight = &href;
    info.command = std::move(b.command);
    info.contents = std::move(b.contents);
    info.popupX = b.labelX;
    info.popupY = b.labelY;
    const int tw = tex ? tex->width() : 0, th = tex ? tex->height() : 0;
    info.rolloverScale = rolloverScaleFor(tw, th);
    if (ui::Window* label = labelFor(b.name)) {
      info.label = label->id();
      float lx = b.labelX.value_or(x - static_cast<float>(label->rect().w) / 2);
      const float ly = b.labelY.value_or(y + static_cast<float>(th) / 4);
      lx = std::max(lx, 0.0f);
      if (lx + static_cast<float>(label->rect().w) > static_cast<float>(rect().w)) lx = static_cast<float>(rect().w - label->rect().w);
      label->setPos(static_cast<int>(lx), static_cast<int>(ly));
    }
    buildings_.push_back(std::move(info));
  }
  target_ = pressed_ = -1;
}

void PortWindow::play(Sprite& s) {
  if (!manager().sheets()) return;
  SpriteContext sc{*manager().sheets(), rng_, log_};
  s.play("DoAnim", sc, manager().nowMs());
}

void PortWindow::addSprite(const PortDef& def, const SpriteDef& sd) {
  // 1. The frequency roll, for every entry.
  const unsigned freq = static_cast<unsigned>(static_cast<long long>(sd.frequency));
  if (!(static_cast<unsigned>(rng_.range(0, 99)) < freq)) return;

  // 2. The image pick.
  std::string image = sd.image, mask = sd.mask;
  if (!sd.images.empty()) {
    const int pick = rng_.range(1, static_cast<int>(sd.images.size()));
    image = sd.images[static_cast<std::size_t>(pick - 1)];
    mask = pick <= static_cast<int>(sd.masks.size()) ? sd.masks[static_cast<std::size_t>(pick - 1)] : std::string();
  }
  gfx::SheetCache* sheets = manager().sheets();
  auto warnMissing = [&](const std::string& what) {
    if (warned_.insert(what).second) log_.write("[port] cannot load '" + what + "'");
  };

  Sprite* main = nullptr;
  std::unique_ptr<Sprite> maskSprite;
  PathSprite* path = nullptr;

  if (!sd.hasPath) {
    auto s = std::make_unique<Sprite>(sd.layer, "scenery:" + baseName(image));
    if (contains(image, ".xml")) {
      s->sheet = sheets ? sheets->get(image) : nullptr;
      if (!s->sheet) warnMissing(image);
    } else {
      s->texture = loadTexture(image);
      if (!s->texture) warnMissing(image);
    }
    s->scale = sd.scale;
    s->hflip = sd.hflip;
    s->pos = {sd.x, sd.y};
    main = &root_->addChild(std::move(s));
    scenery_.push_back(main);
    if (main->sheet && main->sheet->frames.size() > 1) play(*main);
    if (!mask.empty()) {
      maskSprite = std::make_unique<Sprite>(0, "mask");
      maskSprite->texture = loadTexture(mask);
    }
  } else {
    auto p = std::make_unique<PathSprite>(sd.layer, "path:" + baseName(image));
    p->sheet = sheets ? sheets->get(image) : nullptr;
    if (!p->sheet) {
      p->texture = loadTexture(image);
      if (!p->texture) warnMissing(image);
    }
    if (!mask.empty()) {
      maskSprite = std::make_unique<Sprite>(0, "mask");
      if (sheets) maskSprite->sheet = sheets->get(image, image, mask);
      if (!maskSprite->sheet) maskSprite->texture = loadTexture(mask);
    }
    p->setCurve(gfx::Curve::fromPoints(sd.path), sd.time, sd.mode);
    p->setHold(sd.hold);
    p->headingFrames = sd.headingFrames;
    p->artFacesLeft = sd.faceLeft;
    p->choc2Timing = sd.choc2;
    p->forceHflip = sd.hflip;
    if (!sd.headingFrames && p->sheet && p->sheet->frames.size() > 1) play(*p);  // a walk cycle plays by its own script
    const float yFar = sd.yFar.value_or(def.yFar), yNear = sd.yNear.value_or(def.yNear);
    const float scaleFar = sd.scaleFar.value_or(def.scaleFar), scaleNear = sd.scaleNear.value_or(def.scaleNear);
    if (yFar != yNear) p->setScaling(yFar, yNear, scaleFar, scaleNear);
    if (sd.scale > 0 && !sd.choc2) p->setScaling(yFar, yNear, sd.scale, sd.scale);
    if (yFar != yNear) p->setSpeed(sd.speedFar, sd.speedNear);
    path = p.get();
    main = &root_->addChild(std::move(p));
    paths_.push_back(path);
    // 5. The random phase: n updates of 50 ms at once.
    if (sd.random && sd.time > 1) {
      const int n = rng_.range(0, static_cast<int>(std::floor((sd.time + sd.hold) / 50)));
      for (int i = 0; i < n; ++i) path->update(50);
    }
  }

  // 6. Horizontal tiles: four copies of the picture, as children, so they share the sprite's scale and motion.
  if (sd.htile != 0 && main) {
    for (float k : {-1.0f, -2.0f, 1.0f, 2.0f}) {
      auto t = std::make_unique<Sprite>(0, "tile");
      t->texture = main->texture;
      t->sheet = main->sheet;
      t->frame = main->frame;
      t->pos = {k * sd.htile, 0};
      main->addChild(std::move(t));
    }
  }

  // 7. The mask overlay and its tint, chosen once.
  if (maskSprite && main) {
    if (!sd.tints.empty()) maskSprite->tint = sd.tints[static_cast<std::size_t>(rng_.range(1, static_cast<int>(sd.tints.size())) - 1)];
    else maskSprite->tint = sd.tint;
    Sprite& m = main->addChild(std::move(maskSprite));
    if (path) {
      path->mask = &m;
      if (path->sheet && m.sheet) m.frame = path->frame;
    }
  }
}

void PortWindow::tintBackground(float v) {
  const gfx::ColorF c{v, v, v, 1};
  if (root_) root_->tint = c;
  for (Sprite* s : scenery_) s->tint = c;
}

void PortWindow::cancelHighlight() {
  if (target_ < 0) return;
  Building& b = buildings_[static_cast<std::size_t>(target_)];
  if (choc2Pulse_) {
    pulseHover_ = -1;
    applyPulses();
    return;
  }
  b.highlight->visible = false;
  if (ui::Window* l = manager().window(b.label)) l->setShown(false);
  rolloverTime_ = 0;
}

int PortWindow::findBuilding(gfx::PointF p) const {
  int best = -1;
  for (std::size_t i = 0; i < buildings_.size(); ++i) {
    const Sprite& s = *buildings_[i].sprite;
    const int alpha = manager().spriteHitAlpha();
    const gfx::TextureCache* textures = manager().textures();
    if (alpha > 0 && textures ? !s.hitOpaque(p, *textures, alpha) : !s.hit(p)) continue;
    if (best < 0 || buildings_[static_cast<std::size_t>(best)].sprite->layer() < buildings_[i].sprite->layer()) best = static_cast<int>(i);
  }
  return best;
}

void PortWindow::closePopup() {
  if (!popup_) return;
  if (auto* p = dynamic_cast<ui::PopupWindow*>(manager().window(popup_))) p->kill();
  popup_ = 0;
}

bool PortWindow::onMouseMove(int x, int y) {
  const int hit = findBuilding({static_cast<float>(x), static_cast<float>(y)});
  if (hit != target_) {
    cancelHighlight();
    closePopup();
    target_ = hit;
    if (hit >= 0) {
      Building& b = buildings_[static_cast<std::size_t>(hit)];
      if (!b.contents.empty()) {
        // Chocolatier 2: the building's `contents` popup, at the label position the Lua gave (window-local), kept while the
        // pointer stays over the building.
        const gfx::PointF o = ui::WindowManager::screenOrigin(*this);
        const gfx::RectF r = b.sprite->rect();
        const gfx::RectI keep{static_cast<int>(o.x + r.x), static_cast<int>(o.y + r.y), static_cast<int>(r.w), static_cast<int>(r.h)};
        const int ax = static_cast<int>(o.x + b.popupX.value_or(b.sprite->pos.x - 50));
        const int ay = static_cast<int>(o.y + b.popupY.value_or(b.sprite->pos.y));
        popup_ = ui::openContentsPopup(manager(), id(), keep, b.contents, ax, ay);
      }
      if (choc2Pulse_) {
        // The popup's Lua may have called StopPulsing just now; the hovered building pulses after it.
        if (b.command.valid()) {
          pulseHover_ = hit;
          hoverMs_ = 0;
          applyPulses();
        }
        return false;
      }
      b.highlight->visible = true;
      if (ui::Window* l = manager().window(b.label)) {
        l->setShown(true);
        ui::setSubtreeAlpha(*l, 1.0f);
      }
      rolloverTime_ = 0;
    }
  }
  return false;  // never consumed
}

bool PortWindow::onMouseDown(int x, int y) {
  pressed_ = findBuilding({static_cast<float>(x), static_cast<float>(y)});
  return pressed_ >= 0;
}

void PortWindow::activate(int index) {
  Building& b = buildings_[static_cast<std::size_t>(index)];
  ui::ScriptSink* sink = manager().scriptSink();
  if (target_ >= 0) cancelHighlight();
  closePopup();
  if (!sink) return;
  // The window may be swapped away by the closure; its deletion is deferred, so the ref stays valid for the call.
  if (b.command.valid()) {
    sink->deliver(nullptr, &b.command);
  } else if (warned_.insert("nocommand:" + b.name).second) {
    // Chocolatier 3's `<name>:OnClick()` string does not exist in Chocolatier 2's Lua.
    log_.write("[port] building '" + b.name + "' has no `command`; a click does nothing");
  }
}

bool PortWindow::keepsPopup(int sx, int sy) {
  const gfx::PointF o = ui::WindowManager::screenOrigin(*this);
  return findBuilding({static_cast<float>(sx) - o.x, static_cast<float>(sy) - o.y}) == target_;
}

void PortWindow::popupClicked() {
  if (target_ >= 0) activate(target_);
}

bool PortWindow::onMouseUp(int x, int y) {
  if (pressed_ < 0 || findBuilding({static_cast<float>(x), static_cast<float>(y)}) != pressed_) return false;
  if (const Building& pb = buildings_[static_cast<std::size_t>(pressed_)]; pb.command.valid() || !pb.contents.empty()) {
    const int index = pressed_;
    pressed_ = -1;
    activate(index);
    return true;
  }
  const std::string click = clickString(buildings_[static_cast<std::size_t>(pressed_)].name);
  // The original cancels the highlight but leaves targetBuilding set, so the same building is not highlighted again
  // until the pointer leaves it and comes back.
  if (target_ >= 0) cancelHighlight();
  ui::ScriptSink* sink = manager().scriptSink();
  pressed_ = -1;
  if (sink) sink->runString(click);  // may destroy this window: nothing is touched afterwards
  return true;
}

void PortWindow::doAnimation(double t) {
  // 2. The intro fade; one extra tick runs after 2000 ms.
  if (fadeIn_ && lastTime_ <= startTime_ + kIntroMs) {
    const double e = t - startTime_;
    tintBackground(introBrightness(e));
    if (e > 1000) {
      const float a = introLabelAlpha(e);
      for (std::size_t i = 0; i < buildings_.size(); ++i) {
        if (static_cast<int>(i) == target_) continue;
        if (ui::Window* l = manager().window(buildings_[i].label)) {
          ui::setSubtreeAlpha(*l, a);
          l->setShown(true);
        }
      }
    }
  }
  lastTime_ = t;
  // 4. The hover pulse.
  if (target_ >= 0 && !choc2Pulse_) {
    rolloverTime_ = std::fmod(rolloverTime_ + kTaskMs, 3000.0);
    const Pulse p = pulse(rolloverTime_);
    Building& b = buildings_[static_cast<std::size_t>(target_)];
    b.highlight->alpha = p.alpha;
    b.highlight->scale = 1.0f + p.f * b.rolloverScale;
  }
  // 5. Path sprites, by a fixed step whatever the real time was.
  for (PathSprite* p : paths_) p->update(static_cast<float>(kTaskMs));
  // 6. Music.
  if (portMusic_ && musicTime_ <= t) changeMusic(t);
}

float PortWindow::choc2PulsePhase(double clockMs) {
  const double m = std::fmod(clockMs, kPulsePeriodMs);
  return static_cast<float>(1.0 - std::fabs(m / (kPulsePeriodMs / 2) - 1.0));
}

void PortWindow::applyPulses() {
  for (std::size_t i = 0; i < buildings_.size(); ++i) {
    Building& b = buildings_[i];
    auto* pulse = dynamic_cast<PulseSprite*>(b.highlight);
    if (!pulse) continue;
    const bool hovered = static_cast<int>(i) == pulseHover_;
    pulse->visible = (hovered || introPulse_) && b.command.valid();
    if (pulse->visible) pulse->phase = choc2PulsePhase(hovered ? hoverMs_ : sceneMs_);
  }
}

void PortWindow::pulseTick(double dt) {
  sceneMs_ += dt;
  hoverMs_ += dt;
  if (introPulse_ && sceneMs_ >= kIntroPulseMs) stopPulsing();
  applyPulses();
}

void PortWindow::stopPulsing() {
  introPulse_ = false;
  pulseHover_ = -1;
  if (ui::Window* labels = find("buildinglabels")) labels->setShown(false);
  applyPulses();
}

void PortWindow::changeMusic(double now) {
  // docs/re/port-window.md 1.10: a playing port sting is stopped, then 10-15 s later a random port_music<n> cue starts
  // (a C3 Gameplay stem cue, docs/re/audio.md 10.4), which is stopped again after 40-60 s. The draws are the original's.
  double delay;
  if (music_) {
    music_ = false;
    if (world_) world_->sound("Stop_Music");
    delay = rng_.range(10000, 15000);
  } else {
    music_ = true;
    const auto which = rng_.range(1, 3);  // which port_music track
    if (world_) world_->sound("port_music" + std::to_string(which));
    delay = rng_.range(40000, 60000);
  }
  musicTime_ = now + delay;
}

void PortWindow::pauseAnimations(bool pause) {
  explicitPause_ = pause;
  if (pause) anim_.pause();
  else anim_.start(manager().nowMs(), kTaskMs);
}

void PortWindow::setForeground(bool fg, double nowMs) {
  if (!fg) anim_.pause();
  else if (!explicitPause_) anim_.start(nowMs, kTaskMs);
}

void PortWindow::frame(double now) {
  if (!built_) return;
  if (anim_.due(now)) doAnimation(now);
  root_->tickScripts(now);
  root_->resortDirty();
}

void PortWindow::draw(ui::DrawContext& ctx) const {
  if (!root_) return;
  root_->draw(ctx.renderer, SpriteSpec{ctx.origin, 1, 0, 1});
}

std::string PortWindow::detail() const {
  return " buildings=" + std::to_string(buildings_.size()) + " paths=" + std::to_string(paths_.size()) +
         (target_ >= 0 ? " hover=" + buildings_[static_cast<std::size_t>(target_)].name : std::string());
}

}  // namespace choco::game
