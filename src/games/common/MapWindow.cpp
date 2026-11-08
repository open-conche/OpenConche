#include "games/common/MapWindow.h"

#include <cstdio>

#include "app/Diagnostics.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "sdk/TextSprite.h"
#include "games/common/World.h"
#include "sdk/SpriteScript.h"
#include "gfx/TextureCache.h"
#include "ui/widgets/InputWidgets.h"
#include "gfx/SheetCache.h"
#include "gfx/text/TextLayout.h"
#include "script/ScriptContext.h"

namespace choco::game {

namespace {

constexpr int kLayerRing = 0, kLayerDot = 1, kLayerMarker = 2, kLayerLabel = 3, kLayerVehicle = 10, kLayerClouds = 11;
constexpr int kLabelW = 200, kLabelH = 20;
constexpr float kLabelSize = 20;

}  // namespace

// ---------------------------------------------------------------- parsing

std::vector<PortInfo> parsePorts(const ui::Style* allPorts) {
  std::vector<PortInfo> out;
  if (!allPorts) return out;
  allPorts->forEachTable([&](const ui::Style& p) {
    PortInfo info;
    info.name = p.stringOr("name");
    if (info.name.empty()) return;
    info.mapx = static_cast<float>(p.numberOr("mapx", 0));
    info.mapy = static_cast<float>(p.numberOr("mapy", 0));
    info.align = p.stringOr("align");
    if (auto v = p.number("popupX")) info.popupX = static_cast<int>(*v);
    if (auto v = p.number("popupY")) info.popupY = static_cast<int>(*v);
    out.push_back(std::move(info));
  });
  return out;
}

PlayerInfo parsePlayer(const ui::Style* player, const std::vector<PortInfo>& ports) {
  PlayerInfo out;
  if (!player) return out;
  out.portName = player->stringOr("portName");
  if (auto avail = player->table("portsAvailable"))
    for (const PortInfo& p : ports)
      if (auto s = avail->string(p.name.c_str())) out.available[p.name] = *s;
  return out;
}

std::optional<MarkerLook> markerLook(std::string_view state) {
  if (state == "hidden") return std::nullopt;
  if (state == "open") return MarkerLook{"image/port_visited", false, false};
  if (state == "new") return MarkerLook{"image/port_new.xml", true, true};
  if (state == "factory") return MarkerLook{"image/factory.xml", true, true};
  if (state == "factory_stall") return MarkerLook{"image/factory_idle.xml", true, true};
  if (state == "shop") return MarkerLook{"image/icon_shop", false, false};
  return MarkerLook{"image/port_locked.xml", true, false};  // locked, and any state nobody knows
}

std::vector<gfx::PointF> straightRoute(gfx::PointF a, gfx::PointF b) { return {a, a, b, b}; }

std::vector<gfx::PointF> mergeRoutePoints(const std::vector<gfx::PointF>& first, const std::vector<gfx::PointF>& second) {
  std::vector<gfx::PointF> out = first;
  for (std::size_t i = 1; i < second.size(); ++i) out.push_back(second[i]);
  return out;
}

TravelRequest parseTravelRequest(const ui::Style& t, gfx::PointF from) {
  TravelRequest r;
  if (auto path = t.table("path")) {
    std::vector<gfx::PointF> pts;
    const int n = path->size();
    for (int i = 1; i <= n; ++i)
      if (auto e = path->element(i))
        pts.push_back({static_cast<float>(e->numberAt(1).value_or(0)), static_cast<float>(e->numberAt(2).value_or(0))});
    r.curve = gfx::Curve::fromPoints(std::move(pts));
  } else {
    gfx::PointF to = from;
    if (auto dest = t.table("dest")) to = {static_cast<float>(dest->numberOr("mapx", 0)), static_cast<float>(dest->numberOr("mapy", 0))};
    r.curve = gfx::Curve::fromPoints(straightRoute(from, to));
  }
  if (auto type = t.table("type")) {
    const int n = type->size();
    for (int i = 1; i <= n; ++i)
      if (auto e = type->element(i)) {
        TravelMode m;
        m.fraction = static_cast<float>(e->numberAt(1).value_or(0));
        m.name = e->stringAt(2).value_or("travel_air");
        r.modes.push_back(std::move(m));
      }
  }
  if (auto events = t.table("events")) {
    for (int i = 1;; ++i) {
      auto e = events->element(i);
      if (!e) break;
      TravelEvent ev;
      ev.timeMs = static_cast<float>(e->numberOr("time", 0));
      if (auto f = e->function("action")) ev.action = std::move(*f);
      r.events.push_back(std::move(ev));
    }
  }
  return r;
}

// ---------------------------------------------------------------- window

MapWindow::MapWindow(ui::WindowManager& wm, MapEnv env) : Window(wm), env_(env) {}

MapWindow::~MapWindow() {
  // File log only: the window manager's logger may already be gone while the windows are torn down.
  if (travel_ && !arrivedLogged_) diag::logLine("[travel] cancelled");
}

std::shared_ptr<gfx::Texture> MapWindow::texture(std::string_view name) const {
  gfx::TextureCache* tc = manager().textures();
  return tc ? tc->load(name) : nullptr;
}

void MapWindow::init(const ui::Style& style) {
  Window::init(style);
  background_ = style.stringOr("background");
  cloudY_ = style.numberOr("cloudY", 360);
  cloudTime_ = std::clamp(style.numberOr("cloudTime", 20000), 0.0, 4294967295.0);
  scaling_.yFar = static_cast<float>(style.numberOr("yFar", 0));
  scaling_.yNear = static_cast<float>(style.numberOr("yNear", 400));
  scaling_.farScale = static_cast<float>(style.numberOr("farScale", 1));
  scaling_.nearScale = static_cast<float>(style.numberOr("nearScale", 1));

  root_ = std::make_unique<Sprite>(0, "bg");
  root_->topLeft = true;
  if (!background_.empty()) root_->texture = texture(background_);

  auto clouds = std::make_unique<Sprite>(kLayerClouds, "clouds");  // centred on its position, like the original
  clouds->texture = texture("image/worldmap_clouds");
  const float tileW = clouds->texture ? static_cast<float>(clouds->texture->width()) : 800.0f;
  for (float dx : {tileW, -tileW}) {
    auto tile = std::make_unique<Sprite>(0, "clouds-tile");
    tile->texture = clouds->texture;
    tile->pos = {dx, 0};
    clouds->addChild(std::move(tile));
  }
  clouds->pos = {0, static_cast<float>(cloudY_)};
  clouds_ = &root_->addChild(std::move(clouds));
  cloudCounter_ = (env_.rng && cloudTime_ >= 1) ? env_.rng->range(0, static_cast<int>(cloudTime_)) : 0;
  updateClouds();
}

void MapWindow::updateClouds() {
  if (!clouds_) return;
  const double width = rect().w > 0 ? rect().w : 800;
  if (cloudTime_ >= 1) {
    cloudCounter_ = std::fmod(cloudCounter_, cloudTime_);
    clouds_->pos.x = static_cast<float>(cloudCounter_ / cloudTime_ * width);
  } else {
    clouds_->pos.x = 0;
  }
}

double MapWindow::cloudX() const { return clouds_ ? clouds_->pos.x : 0; }

bool MapWindow::onAnimate() {
  cloudCounter_ += kCloudStepMs;  // a fixed step per call, not measured time (MW 3)
  updateClouds();
  return true;
}

void MapWindow::postChildrenInit(const ui::Style& style) {
  Window::postChildrenInit(style);
  std::vector<PortInfo> ports;
  PlayerInfo player;
  if (ui::ScriptSink* sink = manager().scriptSink()) {
    sink->withGlobal("_AllPorts", [&](const ui::Style* s) { ports = parsePorts(s); });
    sink->withGlobal("Player", [&](const ui::Style* s) { player = parsePlayer(s, ports); });
  }
  collectPortData(player, ports);
  manager().startWindowAnimation(id(), kCloudStepMs);
  if (env_.world) env_.world->setMap(id());
}

void MapWindow::frame(double now) {
  if (root_) root_->tickScripts(now);
  tickTravel(now);
}

void MapWindow::collectPortData(const PlayerInfo& player, const std::vector<PortInfo>& ports) {
  gfx::text::TextSystem* ts = manager().textSystem();
  std::shared_ptr<const gfx::text::Font> font = ts ? ts->fonts.get("fonts/fertigo.mvec") : nullptr;
  auto makeLabel = [&](const std::string& port, const std::string& text, int flags, int layer, gfx::ColorF color,
                       const char* prefix) {
    gfx::text::TextLayout layout;
    if (ts) {
      gfx::text::TextStyle st;
      st.font = font;
      st.fonts = &ts->fonts;
      st.size = kLabelSize;
      st.color = color;
      st.flags = flags;
      layout = gfx::text::TextLayout::build(text, st, kLabelW, kLabelH);
    }
    return std::make_unique<TextSprite>(layer, std::string(prefix) + port, std::move(layout), flags, kLabelW, kLabelH);
  };

  for (const PortInfo& p : ports) {
    auto st = player.available.find(p.name);
    const std::string state = st == player.available.end() ? std::string() : st->second;
    if (auto look = markerLook(state)) {
      auto marker = std::make_unique<Sprite>(kLayerMarker, "marker:" + p.name);
      if (look->sheet && env_.sheets) {
        marker->sheet = env_.sheets->get(look->image);
      } else if (!look->sheet) {
        marker->texture = texture(look->image);
      }
      marker->pos = {p.mapx, p.mapy};
      Sprite& placed = root_->addChild(std::move(marker));
      markers_.push_back({p.name, &placed});
      if (look->animated && placed.sheet && env_.sheets && env_.rng && env_.log) {
        SpriteContext sc{*env_.sheets, *env_.rng, *env_.log};
        placed.play("DoAnim", sc, manager().nowMs());
      }
      if (p.name == player.portName) current_ = &placed;

      std::string text = "#####";
      if (const auto& lookup = manager().labelLookup()) text = lookup(p.name);
      if (text.empty() || text == "#####") text = p.name;
      const bool left = p.mapx >= 750 || p.align == "left";
      const int flags = left ? gfx::text::kHAlignRight + gfx::text::kVAlignCenter
                             : gfx::text::kHAlignLeft + gfx::text::kVAlignCenter;
      auto black = makeLabel(p.name, text, flags, kLayerLabel, {0, 0, 0, 1}, "label:");
      black->pos = {p.mapx + (left ? -10.0f : 10.0f), p.mapy};
      auto white = makeLabel(p.name, text, flags, -1, {1, 1, 1, 1}, "label-shadow:");
      white->pos = {2, 2};  // relative to the black label; the original stores the absolute offset (see questions 2.2)
      white->alpha = 0.75f;
      black->addChild(std::move(white));
      root_->addChild(std::move(black));

      const int px = p.popupX.value_or(static_cast<int>(p.mapx) + 10);
      const int py = p.popupY.value_or(static_cast<int>(p.mapy) - 48);
      auto roll = ui::RolloverWindow::makeNative(
          manager(), {static_cast<int>(p.mapx - kRolloverHalf), static_cast<int>(p.mapy - kRolloverHalf), 2 * kRolloverHalf, 2 * kRolloverHalf},
          p.name + ":MapRolloverPopup()", p.name + ":OnClick()", px, py);
      adoptFront(std::move(roll));
    }
    // Kept from the original: the check sits inside the loop, so ports after the current one add duplicate rings.
    if (current_) {
      auto ring = std::make_unique<Sprite>(kLayerRing, "ring");
      ring->texture = texture("image/port_current");
      ring->pos = current_->pos;
      root_->addChild(std::move(ring));
    }
  }
}

Sprite* MapWindow::markerOf(std::string_view port) const {
  for (const Marker& m : markers_)
    if (m.name == port) return m.sprite;
  return nullptr;
}

int MapWindow::ringCount() const {
  int n = 0;
  for (const auto& c : root_->children()) n += c->debugName == "ring";
  return n;
}

void MapWindow::draw(ui::DrawContext& ctx) const {
  if (root_) root_->draw(ctx.renderer, SpriteSpec{ctx.origin, 1, 0, ctx.alpha});
}

void MapWindow::setVehicleMode(const std::string& mode) {
  std::string name = mode;
  if (name.find('/') == std::string::npos) name = "image/" + name;
  if (name.find('.') == std::string::npos) name += ".xml";
  if (!vehicle_ || !env_.sheets) return;
  if (auto sheet = env_.sheets->get(name)) {
    vehicle_->sheet = std::move(sheet);
    vehicle_->texture = nullptr;
  } else {
    manager().log("[map] no vehicle sheet '" + name + "'");
  }
}

bool MapWindow::initiateTravel(TravelRequest req, double now) {
  if (travel_) return false;  // the second InitiateTravel on a window is ignored (MW 6.3)
  auto vehicle = std::make_unique<Sprite>(kLayerVehicle, "vehicle");
  if (current_) {
    vehicle->pos = current_->pos;
  } else {
    manager().log("[map] InitiateTravel with no current port marker: starting from the route's first point");
    vehicle->pos = req.curve.at(0);
  }
  vehicle_ = &root_->addChild(std::move(vehicle));
  travel_ = std::make_unique<Travel>(std::move(req.curve), *vehicle_, std::move(req.modes), std::move(req.events), scaling_,
                                     now, [this](const std::string& m) { setVehicleMode(m); });
  arrivedLogged_ = false;
  const auto& pts = travel_->curve().points();
  char buf[200];
  std::snprintf(buf, sizeof buf, "[travel] started (%.0f,%.0f) -> (%.0f,%.0f) %.0f ms, %zu events",
                pts.empty() ? 0.0 : double(pts.front().x), pts.empty() ? 0.0 : double(pts.front().y),
                pts.empty() ? 0.0 : double(pts.back().x), pts.empty() ? 0.0 : double(pts.back().y), double(travel_->durationMs()),
                travel_->eventCount());
  manager().log(buf);
  return true;
}

void MapWindow::pauseTravel() {
  if (travel_ && !travel_->finished()) travel_->pause();
}

void MapWindow::resumeTravel(double now) {
  if (travel_ && !travel_->finished()) travel_->restart(now);
}

void MapWindow::setForeground(bool fg, double now) {
  if (!fg) {
    manager().stopWindowAnimation(id());
    return;
  }
  manager().startWindowAnimation(id(), kCloudStepMs);
  if (travel_ && !travel_->finished() && !travel_->paused()) travel_->restart(now);
}

void MapWindow::tickTravel(double now) {
  if (!travel_ || travel_->finished()) return;
  const ui::WindowId self = id();
  ui::WindowManager& wm = manager();
  Travel::Hooks hooks;
  hooks.setMode = [this](const std::string& m) { setVehicleMode(m); };
  hooks.addDot = [this](gfx::PointF at) {
    auto dot = std::make_unique<Sprite>(kLayerDot, "dot");
    dot->texture = texture("image/traveldot");
    dot->pos = at;
    root_->addChild(std::move(dot));
  };
  const Travel* tr = travel_.get();
  bool* arrived = &arrivedLogged_;
  hooks.fire = [&wm, self, tr, arrived](const TravelEvent& e) {
    // The window outlives the call (destruction is deferred), but `arrived` is only touched while it is still registered.
    const std::size_t i = tr->eventsFired(), n = tr->eventCount();  // already counts this event
    wm.log("[travel] event " + std::to_string(i) + "/" + std::to_string(n) + " handler start");
    if (e.action.valid())
      if (ui::ScriptSink* sink = wm.scriptSink()) sink->runInScript(e.action);
    wm.log("[travel] event " + std::to_string(i) + "/" + std::to_string(n) + " handler returned");
    if (i == n) {
      *arrived = true;
      wm.log("[travel] arrived");
    }
    return wm.window(self) != nullptr;  // an event that swaps the modal queues this window for destruction
  };
  travel_->tick(now, hooks);
  if (travel_->finished() && !arrivedLogged_) {  // a trip with no events
    arrivedLogged_ = true;
    wm.log("[travel] arrived");
  }
}

std::string MapWindow::detail() const {
  if (!travel_) return {};
  char buf[160];
  std::snprintf(buf, sizeof buf, "\n    travel t=%.2f time=%.0f paused=%d events=%zu/%zu dots=%d mode=%s", travel_->fraction(),
                travel_->timeMs(), travel_->paused() ? 1 : 0, travel_->eventsFired(), travel_->eventCount(), travel_->dots(),
                travel_->mode().c_str());
  return buf;
}

void registerMapWindow(ui::WindowManager& wm, MapEnv env) {
  wm.addBuiltinType("MapWindow", [env](ui::WindowManager& m) { return std::make_unique<MapWindow>(m, env); });
}

}  // namespace choco::game
