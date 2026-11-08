#include "games/choc1/MapScreen.h"

#include <cmath>
#include <cstdio>

#include "games/choc1/Choc1AudioNatives.h"
#include "games/choc1/Choc1World.h"
#include "gfx/SheetCache.h"
#include "gfx/TextureCache.h"
#include "script/LuaStyle.h"
#include "ui/DrawContext.h"
#include "ui/ScriptSink.h"
#include "ui/Style.h"
#include "ui/WindowManager.h"

namespace choco::game::choc1 {

// ---- MapTrip ----

int MapTrip::legDurationMs(double weeks) { return static_cast<int>(std::nearbyint(weeks * kWeekMs)); }

const std::string& MapTrip::vehicle() const {
  static const std::string none;
  return idx_ < legs_.size() ? legs_[idx_].vehicle : none;
}

void MapTrip::start(std::string endName, std::vector<Leg> legs, long long nowMs) {
  // Step 1 (3.2): everything but the legs and dots starts again (the exe appends to the old lists; the game builds a fresh
  // window per trip, so this engine replaces them).
  elapsed_ = 0;
  trail_ = 0;
  idx_ = 0;
  lastSound_.clear();
  dots_.clear();
  end_ = std::move(endName);
  legs_ = std::move(legs);
  lastMs_ = nowMs;  // step 5
  active_ = !legs_.empty();
}

// 3.3 step 3: a dot for every full 200 ms between the trail clock and min(elapsed, duration), at the path's position then.
void MapTrip::sampleDots(const Leg& leg) {
  const long long t = std::min<long long>(elapsed_, leg.path.timeMs());
  while (trail_ + kDotMs <= t) {
    trail_ += kDotMs;
    const PolylinePath::Point p = leg.path.at(trail_);
    dots_.push_back({static_cast<float>(p.x), static_cast<float>(p.y)});
  }
}

MapTrip::Step MapTrip::update(long long nowMs) {
  Step s;
  if (!active_) return s;  // step 1
  // Step 2.
  const long long old = elapsed_;
  elapsed_ += nowMs - lastMs_;
  lastMs_ = nowMs;
  const long long weekTicks = elapsed_ / kWeekMs - old / kWeekMs;
  // Steps 3 and 4: dots, then the leg walk. A finished leg that is not the last hands its overshoot to the next.
  const std::size_t last = legs_.size() - 1;
  sampleDots(legs_[idx_]);
  bool done = elapsed_ >= legs_[idx_].path.timeMs();
  while (done && idx_ < last) {
    elapsed_ -= legs_[idx_].path.timeMs();
    ++idx_;
    trail_ = 0;
    sampleDots(legs_[idx_]);
    done = elapsed_ >= legs_[idx_].path.timeMs();
  }
  const Leg& leg = legs_[idx_];
  const PolylinePath::Point p = leg.path.at(elapsed_);
  pos_ = {static_cast<float>(p.x), static_cast<float>(p.y)};
  heading_ = leg.path.heading(elapsed_);
  // Step 5: the last second of the last leg (unsigned in the exe: false once elapsed is past the duration).
  if (idx_ == last && elapsed_ <= leg.path.timeMs() && leg.path.timeMs() - elapsed_ < kFadeLeftMs) s.fade = true;
  // Step 7: the vehicle sound, when the leg's vehicle differs from the last one played (the first update always plays).
  if (leg.vehicle != lastSound_) {
    lastSound_ = leg.vehicle;
    s.sound = leg.vehicle;
  }
  // Step 8: at most one tick per update, however many boundaries it crossed.
  if (weekTicks >= 1) {
    s.command = done ? Step::Command::TickSting : Step::Command::Tick;
  } else if (done) {
    active_ = false;
    s.command = Step::Command::Arrive;
  }
  return s;
}

// ---- MapScreen ----

MapScreen::~MapScreen() { releaseGate(); }

void MapScreen::releaseGate() {
  if (!gate_) return;
  gate_ = false;
  if (auto* world = choc1WorldOf(ctx_)) --world->mapTrips;
}

void MapScreen::init(const ui::Style& style) {
  Window::init(style);  // the plain TWindow init: x y w h and name only
  if (manager().sheets()) {
    boat_ = manager().sheets()->get("image/icon_boat.xml");
    train_ = manager().sheets()->get("image/icon_train.xml");
    airship_ = manager().sheets()->get("image/icon_airship.xml");
  }
  if (manager().textures()) dot_ = manager().textures()->load("image/traveldot");
}

void MapScreen::postChildrenInit(const ui::Style& style) {
  Window::postChildrenInit(style);
  manager().startWindowAnimation(id(), kUpdateMs);  // PostInit: the window's own 62 ms timer
}

void MapScreen::runCommand(const std::string& code) {
  if (ui::ScriptSink* sink = manager().scriptSink()) sink->runString(code);
}

namespace {
// The Lua string literal for `s` in single quotes.
std::string quoted(const std::string& s) {
  std::string out = "'";
  for (const char c : s) {
    if (c == '\'' || c == '\\') out += '\\';
    out += c;
  }
  return out + "'";
}
}  // namespace

// 3.2 steps 2-6.
void MapScreen::animateTravel(lua_State* L, int index) {
  const script::LuaStyle route = script::LuaStyle::plainView(L, index, &ctx_.lua);
  const std::string end = route.stringOr("endName");
  const auto legsTable = route.table("legs");
  if (!legsTable) return;  // step 3: no legs, nothing happens
  std::vector<MapTrip::Leg> legs;
  for (int i = 1; i <= legsTable->size(); ++i) {
    const auto leg = legsTable->element(i);
    if (!leg) break;
    const auto path = leg->table("path");
    if (!path) continue;  // a leg without a path adds neither a path nor a vehicle
    std::vector<PolylinePath::Point> pts;
    for (int k = 1; k <= path->size(); ++k)
      if (auto e = path->element(k)) pts.push_back({e->numberAt(1).value_or(0), e->numberAt(2).value_or(0)});
    // The leg's time (weeks, default 1) replaces the path's own time.
    const int ms = MapTrip::legDurationMs(leg->numberOr("time", 1.0));
    legs.push_back({PolylinePath(ms, false, std::move(pts)), leg->stringOr("vehicle", "boat")});
  }
  if (legs.empty()) return;
  const long long now = static_cast<long long>(manager().modalNow(*this));
  trip_.start(end, std::move(legs), now);
  // Step 6: the Rollover gate (DAT_005330a0) goes up once per window.
  if (!gate_)
    if (auto* world = choc1WorldOf(ctx_)) {
      ++world->mapTrips;
      gate_ = true;
    }
}

// Section 2: PauseTravel detaches the window's timer (the trip clock stops); ResumeTravel resets the clock and re-arms it.
void MapScreen::pause() { manager().stopWindowAnimation(id()); }
void MapScreen::resume() {
  trip_.resetClock(static_cast<long long>(manager().modalNow(*this)));
  manager().startWindowAnimation(id(), kUpdateMs);
}

bool MapScreen::onAnimate() {
  const MapTrip::Step s = trip_.update(static_cast<long long>(manager().modalNow(*this)));
  const std::string end = trip_.endName();
  if (s.fade)
    if (auto* world = choc1WorldOf(ctx_); world && ctx_.audio && world->music.state() == Choc1Music::State::Playing) {
      AudioMusicSink sink(*ctx_.audio);
      world->music.start(sink, "", world->nowMs());  // the 1000 ms fade; INFERENCE: StartMusic("") is the same state change
    }
  if (!s.sound.empty()) script::choc1PlaySound(ctx_, s.sound);
  switch (s.command) {
    case MapTrip::Step::Command::Tick:
      runCommand("table.insert(gCommandQueue, function() gSim:TravelTick() end)");
      break;
    case MapTrip::Step::Command::TickSting:
      runCommand("table.insert(gCommandQueue, function() gSim:TravelTick(" + quoted(end) + ") end)");
      break;
    case MapTrip::Step::Command::Arrive:
      runCommand("table.insert(gCommandQueue, function() gSim:ArriveAtPort(" + quoted(end) + ") end)");
      releaseGate();
      break;
    case MapTrip::Step::Command::None:
      break;
  }
  return true;
}

// 3.4: nothing before the first update has accumulated time; dots first, then the vehicle frame mirrored about its
// registration point when moving right.
void MapScreen::draw(ui::DrawContext& ctx) const {
  if (trip_.elapsedMs() == 0 || !trip_.started()) return;
  if (dot_)
    for (const MapTrip::Dot& d : trip_.dots()) {
      gfx::RectF r = SimpleAnimation::stillRect(d.x, d.y, dot_->width(), dot_->height(), 1);  // INFERENCE: centred
      r.x += ctx.origin.x;
      r.y += ctx.origin.y;
      ctx.renderer.drawTexture(*dot_, {}, r, {1, 1, 1, ctx.alpha}, gfx::Blend::Normal, 0, {}, false);
    }
  const std::string& v = trip_.vehicle();
  const auto& sheet = v == "train" ? train_ : v == "airship" ? airship_ : boat_;
  if (!sheet || !sheet->image || sheet->frames.empty()) return;
  const gfx::SheetFrame& f = sheet->frames[static_cast<std::size_t>(frameAt(trip_.elapsedMs(), static_cast<int>(sheet->frames.size())))];
  const bool mirror = SimpleAnimation::mirrored(true, true, trip_.heading());
  const gfx::RectF src{static_cast<float>(f.src.x), static_cast<float>(f.src.y), static_cast<float>(f.src.w), static_cast<float>(f.src.h)};
  const gfx::RectF dst{ctx.origin.x + trip_.pos().x - static_cast<float>(mirror ? f.src.w - f.reg.x : f.reg.x),
                       ctx.origin.y + trip_.pos().y - static_cast<float>(f.reg.y), src.w, src.h};
  ctx.renderer.drawTexture(*sheet->image, src, dst, {1, 1, 1, ctx.alpha}, gfx::Blend::Normal, 0, {}, mirror);
}

std::string MapScreen::detail() const {
  char buf[256];
  std::snprintf(buf, sizeof buf, " trip=%s end=\"%s\" leg=%zu/%zu elapsed=%lld dots=%zu vehicle=%s pos=%.0f,%.0f mirror=%d",
                trip_.active() ? "active" : trip_.started() ? "done" : "none", trip_.endName().c_str(), trip_.legIndex() + 1,
                trip_.legCount(), trip_.elapsedMs(), trip_.dots().size(), trip_.vehicle().c_str(), trip_.pos().x, trip_.pos().y,
                SimpleAnimation::mirrored(true, true, trip_.heading()) ? 1 : 0);
  return buf;
}

namespace {
MapScreen* findIn(ui::Window& w) {
  if (auto* m = dynamic_cast<MapScreen*>(&w)) return m;
  for (const auto& c : w.children())
    if (auto* m = findIn(*c)) return m;
  return nullptr;
}
}  // namespace

MapScreen* MapScreen::inChain(ui::WindowManager& wm) {
  const auto& modals = wm.modals();
  for (auto it = modals.rbegin(); it != modals.rend(); ++it)
    if (auto* m = findIn(**it)) return m;
  return nullptr;
}

}  // namespace choco::game::choc1
