#include "games/greatcc/ui/Transitions.h"

#include <algorithm>

#include "games/greatcc/ui/RBounceWindow.h"
#include "games/greatcc/ui/VTintWindow.h"
#include "script/Loaders.h"
#include "ui/WindowManager.h"

namespace choco::games::greatcc {

// RUtil::DoLuaString: the text becomes a command-queue entry, run the next time the script waits for a message.
void doLuaString(script::ScriptContext& ctx, const std::string& text) {
  if (text.empty()) return;
  lua_State* L = ctx.lua.L();
  const int top = lua_gettop(L);
  const std::string code = "table.insert( gCommandQueue, function() " + text + " end );";
  if (script::loadBuffer(L, code, "DoLuaString") != 0 || lua_pcall(L, 0, 0, 0) != 0) {
    const char* msg = lua_tostring(L, -1);
    ctx.log.write(std::string("[lua-error] ") + (msg ? msg : "DoLuaString"));
  }
  lua_settop(L, top);
}

namespace {

constexpr double kMonitorMs = 16;
constexpr float kScreenW = 800, kScreenH = 600;

// The windows of a type under the top modal (the modal's own subtree), each once. The original's spiders visit nested ones
// twice; the repeated calls are no-ops there.
template <class T>
std::vector<T*> collect(ui::WindowManager& wm) {
  std::vector<T*> out;
  auto walk = [&](auto&& self, ui::Window& w) -> void {
    if (auto* t = dynamic_cast<T*>(&w))
      if (std::find(out.begin(), out.end(), t) == out.end()) out.push_back(t);
    for (const auto& c : w.children()) self(self, *c);
  };
  walk(walk, *wm.topModal());
  return out;
}

// RColorFade: a full-screen quad whose alpha ramps linearly with the progress.
class ColorFade : public Overlay {
 public:
  ColorFade(bool fadeIn, gfx::ColorF colour) : fadeIn_(fadeIn), colour_(colour) { durationMs = 500; }
  void draw(ui::DrawContext& ctx, double nowMs) override {
    Overlay::draw(ctx, nowMs);
    const float p = progress();
    gfx::ColorF c = colour_;
    c.a = fadeIn_ ? p * colour_.a : colour_.a - p * colour_.a;
    ctx.renderer.fillRect({0, 0, kScreenW, kScreenH}, c, gfx::Blend::Normal);
    if (elapsedMs >= durationMs) done = true;
  }

 private:
  bool fadeIn_;
  gfx::ColorF colour_;
};

// RBounceOff: construction retargets every RBounceWindow of the top modal; the overlay itself draws nothing.
class BounceOff : public Overlay {
 public:
  BounceOff(ui::WindowManager& wm, int mode) {
    durationMs = 0;
    for (RBounceWindow* w : collect<RBounceWindow>(wm)) {
      durationMs = std::max(durationMs, static_cast<double>(w->bounceLenMs()));
      const BounceSide side = bounceSide(w->entryX(), w->entryY());
      const BouncePoint t = bounceOffTarget(mode, side, w->entryX(), w->entryY(), w->targetX(), w->targetY());
      w->setTargetPos(t.x, t.y);
    }
  }
  void draw(ui::DrawContext& ctx, double nowMs) override {
    Overlay::draw(ctx, nowMs);
    if (elapsedMs >= durationMs) done = true;
  }
};

// RTintOff: construction starts the fade-out of every VTintWindow of the top modal; the duration is the longest `time`.
class TintOff : public Overlay {
 public:
  explicit TintOff(ui::WindowManager& wm) {
    durationMs = 0;
    for (VTintWindow* w : collect<VTintWindow>(wm)) {
      durationMs = std::max(durationMs, w->timeMs());
      w->tintOff();
    }
  }
  void draw(ui::DrawContext& ctx, double nowMs) override {
    Overlay::draw(ctx, nowMs);
    if (elapsedMs >= durationMs) done = true;
  }
};

// RCrossFade: construction captures the top modal and hides it. Render targets do not exist yet (gfx::Renderer::createTarget),
// so the snapshot is not drawn: the modal is simply absent for the 250 ms, and a fade-in shows it again at the end. No shipped
// script runs it (its only uses are commented out), so this is a logged gap rather than a feature.
class CrossFade : public Overlay {
 public:
  CrossFade(ui::WindowManager& wm, script::ScriptContext& ctx, bool fadeIn) : wm_(wm), fadeIn_(fadeIn) {
    durationMs = 250;
    modal_ = wm.topModal()->id();
    wm.topModal()->setShown(false);
    ctx.log.write("[unimplemented] RCrossFade snapshot (no render targets): the window is hidden for the fade instead");
  }
  void draw(ui::DrawContext& ctx, double nowMs) override {
    Overlay::draw(ctx, nowMs);
    if (elapsedMs >= durationMs) {
      done = true;
      if (fadeIn_)
        if (ui::Window* w = wm_.window(modal_)) w->setShown(true);
    }
  }

 private:
  ui::WindowManager& wm_;
  bool fadeIn_;
  ui::WindowId modal_ = 0;
};

}  // namespace

// ---- Overlay ----

void Overlay::account(double nowMs) {
  if (!started_) {
    lastMs_ = nowMs;
    started_ = true;
  }
  elapsedMs += nowMs - lastMs_;
  lastMs_ = nowMs;
}

void Overlay::draw(ui::DrawContext&, double nowMs) { account(nowMs); }

float Overlay::progress() const {
  if (durationMs <= 0) return 1.0f;
  return static_cast<float>(std::min(1.0, elapsedMs / durationMs));
}

// ---- TransitionManager ----

TransitionManager::TransitionManager(script::ScriptContext& ctx) : ctx_(ctx) {}

std::unique_ptr<Overlay> TransitionManager::make(int id, std::string luaString) {
  std::unique_ptr<Overlay> o;
  switch (id) {
    case kFadeToBlack: o = std::make_unique<ColorFade>(true, gfx::ColorF{0, 0, 0, 1}); break;
    case kFadeFromBlack: o = std::make_unique<ColorFade>(false, gfx::ColorF{0, 0, 0, 1}); break;
    case kFadeToWhite: o = std::make_unique<ColorFade>(true, gfx::ColorF{1, 1, 1, 1}); break;
    case kFadeFromWhite: o = std::make_unique<ColorFade>(false, gfx::ColorF{1, 1, 1, 1}); break;
    case kCrossFadeIn: o = std::make_unique<CrossFade>(ctx_.wm, ctx_, true); break;
    case kCrossFadeOut: o = std::make_unique<CrossFade>(ctx_.wm, ctx_, false); break;
    case kBounceBack:
    case kBounceThrough:
    case kBounceClock:
    case kBounceCounter: o = std::make_unique<BounceOff>(ctx_.wm, id - kBounceBack); break;
    case kTintOff: o = std::make_unique<TintOff>(ctx_.wm); break;
    default: return nullptr;
  }
  o->id = id;
  o->luaString = std::move(luaString);
  return o;
}

bool TransitionManager::submit(std::unique_ptr<Overlay> overlay) {
  if (!overlay) return false;
  if (!current_) {
    current_ = std::move(overlay);
    return true;
  }
  pending_.push_back(std::move(overlay));
  return false;
}

bool TransitionManager::onMessage(const ui::Message& m) {
  if (m.type < kFirstTransitionMessage || m.type > kLastTransitionMessage) return false;
  submit(make(m.type, m.name));  // 1012 and 1013 make nothing
  return true;
}

bool TransitionManager::handleEvent(const ui::InputEvent& ev) const {
  if (!active()) return false;
  return ev.type != ui::InputEvent::Type::Text;
}

void TransitionManager::tick(double nowMs) {
  if (!ticker_.running()) ticker_.start(nowMs, kMonitorMs);
  else if (!ticker_.due(nowMs)) return;
  monitor();
}

void TransitionManager::monitor() {
  retired_.clear();
  if (!current_ || !current_->done) return;
  // The overlay stays current while its Lua string runs and its coroutine resumes, so a DoTransition made from there queues
  // behind it, as in the original.
  const std::string text = current_->luaString;
  const auto waiter = current_->waiter;
  doLuaString(ctx_, text);
  if (waiter && ctx_.host) ctx_.host->resume(*waiter, true);
  retired_.push_back(std::move(current_));
  if (!pending_.empty()) {
    current_ = std::move(pending_.front());
    pending_.erase(pending_.begin());
  }
}

void TransitionManager::draw(ui::DrawContext& ctx) {
  // A finished overlay stays on screen until the next monitor tick drops it (a fade to black holds its black).
  for (const auto& o : retired_) o->draw(ctx, ctx_.wm.nowMs());
  if (current_) current_->draw(ctx, ctx_.wm.nowMs());
}

}  // namespace choco::games::greatcc
