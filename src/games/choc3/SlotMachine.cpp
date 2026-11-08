#include "games/choc3/SlotMachine.h"

#include <cmath>
#include <cstdio>

#include "gfx/TextureCache.h"
#include "script/AudioBridge.h"
#include "script/Bind.h"
#include "script/LuaApi.h"
#include "script/ScriptHost.h"
#include "ui/ScriptSink.h"
#include "ui/Style.h"
#include "ui/WindowManager.h"

namespace choco::game {

namespace {
SlotMachine* gActive = nullptr;
}

SlotMachine::SlotMachine(ui::WindowManager& wm, script::ScriptContext& ctx) : Window(wm), ctx_(ctx), sparkle_(ctx.rng) { gActive = this; }

SlotMachine::~SlotMachine() {
  if (gActive == this) gActive = nullptr;
}

SlotMachine* SlotMachine::active() { return gActive; }

void SlotMachine::init(const ui::Style& style) {
  Window::init(style);
  onStopped_ = style.function("onstopped");
  onPayout_ = style.function("onpayout");
  // options: the symbol names; wheel1..wheel4: the strip positions (a missing table leaves 0, 0).
  options_.clear();
  if (auto opts = style.table("options")) {
    const int n = opts->size();
    for (int i = 1; i <= n; ++i) options_.push_back(opts->stringAt(i).value_or(""));
  }
  for (int k = 0; k < 4; ++k) {
    SlotWheel& w = wheelState_[k];
    w = SlotWheel{};
    if (auto t = style.table(("wheel" + std::to_string(k + 1)).c_str())) {
      w.x = t->numberOr("x", 0);
      w.y = t->numberOr("y", 0);
    }
    baseY_[k] = w.y;
    w.stripY = w.y;
  }
  // The constructor gives every strip symbol a random option: wheel by wheel, symbol 0..3 (16 draws).
  if (!options_.empty()) {
    const int top = static_cast<int>(options_.size()) - 1;
    for (SlotWheel& w : wheelState_)
      for (std::string& name : w.names) name = options_[ctx_.rng.range(0, top)];
  }
}

long long SlotMachine::winnings(double bet, double payout) { return static_cast<long long>(std::floor(bet * payout + 0.9)); }

void SlotMachine::sound(script::ScriptContext& ctx, const char* name) {
  script::soundEvent(ctx, name);
}

void SlotMachine::call(const std::optional<script::LuaRef>& fn) {
  if (!fn) return;
  if (ui::ScriptSink* sink = manager().scriptSink()) {
    sink->callProtected(*fn);  // pcall on the main state while the script's coroutine is suspended
    return;
  }
  lua_State* L = ctx_.lua.L();  // no script host (unit tests): the same pcall here
  const int top = lua_gettop(L);
  fn->push(L);
  if (lua_pcall(L, 0, 0, 0) != 0) {
    if (ctx_.host) ctx_.host->logLuaError(L);
    else ctx_.log.write(std::string("[lua-error] ") + (lua_tostring(L, -1) ? lua_tostring(L, -1) : "?"));
  }
  lua_settop(L, top);
}

void SlotMachine::spin(const int wheels[4], double payout) {
  for (int i = 0; i < 4; ++i) {
    wheels_[i] = wheels[i];
    SlotWheel& w = wheelState_[i];
    w.target = wheels[i] - 1;
    w.stepsLeft = ctx_.rng.range(20, 50);  // steps first, then speed
    w.speed = ctx_.rng.range(90, 110);
  }
  payout_ = payout;
  ++spins_;
  allStopped_ = false;
  payoutDone_ = false;
  manager().startWindowAnimation(id(), kSpinMs, true);  // restarting drops an unfinished earlier spin's tick
}

void SlotMachine::stopOne() {
  for (SlotWheel& w : wheelState_) {
    if (w.stepsLeft == 0) continue;
    if (w.stepsLeft > 3) w.stepsLeft = 3;
    return;  // one wheel per call
  }
}

void SlotMachine::tickWheel(int i) {
  SlotWheel& w = wheelState_[i];
  if (w.stepsLeft == 0) return;
  w.phase += w.speed;
  if (w.phase >= 200) {
    if (w.stepsLeft == 1) sound(ctx_, "spin_stop");
    // The new top symbol is drawn on every step, including the last two (which then overwrite it with the target).
    std::string top;
    if (!options_.empty()) top = options_[ctx_.rng.range(0, static_cast<int>(options_.size()) - 1)];
    w.names[3] = w.names[2];
    w.names[2] = w.names[1];
    w.names[1] = w.names[0];
    w.names[0] = top;
    if (--w.stepsLeft == 2 && w.target >= 0 && w.target < static_cast<int>(options_.size())) w.names[0] = options_[w.target];
  }
  w.phase %= 200;
  // Between steps the strip drifts down by phase/200 of a symbol pitch (in strip units, so 60 * 0.425 px is one symbol on
  // screen); at rest it is exactly on its base y.
  // Intended deviation: the original adds phase*60/200 window px (no kStripScale), jumping back each step.
  w.stripY = w.stepsLeft == 0 ? baseY_[i] : baseY_[i] + w.phase * kSymbolPitch / 200.0 * kStripScale;
}

bool SlotMachine::onAnimate() {
  if (allStopped_ && payoutDone_) return false;
  script::ScriptContext& ctx = ctx_;
  // A reference of its own for onpayout: onstopped may destroy the window, and `this` is not touched after that.
  std::optional<script::LuaRef> payoutFn;
  if (onPayout_) {
    lua_State* L = ctx.lua.L();
    onPayout_->push(L);
    payoutFn.emplace(ctx.lua, L, -1);
    lua_pop(L, 1);
  }
  if (!allStopped_) {
    allStopped_ = true;
    for (int i = 0; i < 4; ++i) {
      tickWheel(i);
      if (wheelState_[i].stepsLeft != 0) allStopped_ = false;
    }
    if (allStopped_) {
      const ui::WindowId wid = id();
      ui::WindowManager& wm = manager();
      sparkle_.arm(payout_);  // before onstopped, which may close the dialog
      if (onStopped_) {
        sound(ctx, "Stop_machine_spin");
        call(onStopped_);
        if (!wm.window(wid)) {
          sound(ctx, "Stop_win_money");
          call(payoutFn);
          return false;
        }
      }
    }
  }
  if (allStopped_ && !payoutDone_) {
    // A payout of 0 or less is finished at once; otherwise tick the particles until gSparkleTime <= 0.
    if (payout_ <= 0 || sparkle_.tick()) {
      payoutDone_ = true;
      if (payoutFn) {
        sound(ctx, "Stop_win_money");
        call(payoutFn);
      }
    }
  }
  return !(allStopped_ && payoutDone_);
}

const gfx::Texture* SlotMachine::symbolTexture(const std::string& name) const {
  if (name.empty()) return nullptr;
  auto it = symbols_.find(name);
  if (it == symbols_.end()) {
    std::shared_ptr<gfx::Texture> tex;
    gfx::TextureCache* cache = manager().textures();
    const std::string path = "items/" + name + "_big";
    if (cache && !cache->resolve(path).path.empty()) tex = cache->load(path);
    it = symbols_.emplace(name, std::move(tex)).first;
  }
  return it->second.get();
}

void SlotMachine::draw(ui::DrawContext& ctx) const {
  // The wheels (strips in order 1..4; each strip's children follow its transform), then the sparkle, as the original.
  for (const SlotWheel& w : wheelState_) {
    for (int k = 0; k < 4; ++k) {
      const gfx::Texture* tex = symbolTexture(w.names[k]);
      if (!tex) continue;
      const float tw = static_cast<float>(tex->width() * kStripScale), th = static_cast<float>(tex->height() * kStripScale);
      const float cx = ctx.origin.x + static_cast<float>(w.x);
      const float cy = ctx.origin.y + static_cast<float>(w.stripY + (k - 2) * kSymbolPitch * kStripScale);
      ctx.renderer.drawTexture(*tex, {}, {cx - tw / 2, cy - th / 2, tw, th}, {1, 1, 1, ctx.alpha}, gfx::Blend::Normal);
    }
  }
  if (sparkle_.particles().active().empty()) return;
  if (!dollarsTried_) {
    dollarsTried_ = true;
    gfx::TextureCache* cache = manager().textures();
    if (cache && !cache->resolve("glitz/dollars_big").path.empty()) dollars_ = cache->load("glitz/dollars_big");
  }
  sparkle_.draw(ctx.renderer, dollars_.get(), ctx.origin, ctx.alpha);
}

std::string SlotMachine::detail() const {
  char buf[192];
  std::snprintf(buf, sizeof buf, " spins=%d wheels=%d,%d,%d,%d payout=%g sparkle=%d steps=%d,%d,%d,%d", spins_, wheels_[0], wheels_[1], wheels_[2], wheels_[3],
                payout_, static_cast<int>(sparkle_.particles().active().size()), wheelState_[0].stepsLeft, wheelState_[1].stepsLeft,
                wheelState_[2].stepsLeft, wheelState_[3].stepsLeft);
  return buf;
}

namespace {

int nativeStartSlots(lua_State* L) {
  script::ScriptContext& ctx = script::contextOf(L);
  SlotMachine* m = SlotMachine::active();
  if (!m) {
    if (ctx.stubs.hit("StartSlots")) ctx.log.write("[unimplemented] StartSlots (no SlotMachine window is open)");
    return 0;
  }
  int w[4];
  for (int i = 0; i < 4; ++i) w[i] = static_cast<int>(luaL_optnumber(L, i + 1, 0));
  m->spin(w, luaL_optnumber(L, 5, 0));
  return 0;
}

int nativeStopSlots(lua_State* L) {
  script::ScriptContext& ctx = script::contextOf(L);
  if (SlotMachine* m = SlotMachine::active()) m->stopOne();
  else if (ctx.stubs.hit("StopSlots")) ctx.log.write("[unimplemented] StopSlots (no SlotMachine window is open)");
  return 0;
}

}  // namespace

void registerStubNatives(lua_State* L, script::ScriptContext& ctx) {
  script::ScriptContext* c = &ctx;
  ctx.wm.addWindowType("SlotMachine", [c](ui::WindowManager& wm) { return std::make_unique<SlotMachine>(wm, *c); });
  // SignEditor and CurveEditor are unreachable in the shipped data (docs/re/api-globals.md, resolved): no vanilla
  // or Reforged quest awards the "sign" key that opens ui/sign_basic.lua (whose own default text is "BASIC SIGN DESIGNER: NOT
  // YET IMPLEMENTED"), and CurveEditor is a dev/dev_animation.lua tool retail never runs. They stay placeholders BY DESIGN:
  // future third-party content shows the logged placeholder instead of a crash.
  for (const char* name : {"SignEditor", "CurveEditor"}) {
    const std::string n = name;
    ctx.wm.addWindowType(n, [n](ui::WindowManager& wm) { return std::make_unique<ui::PlaceholderWindow>(wm, n); });
  }
  script::bindRaw(L, ctx, "StartSlots", &nativeStartSlots);
  script::bindRaw(L, ctx, "StopSlots", &nativeStopSlots);
}

}  // namespace choco::game
