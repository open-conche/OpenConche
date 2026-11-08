#include "games/choc2/DiceGame.h"

#include <algorithm>
#include <cstdio>

#include "script/LuaApi.h"
#include "ui/WindowManager.h"
#include "ui/widgets/Widgets.h"

namespace choco::game::choc2 {

namespace {
DiceGame* gActive = nullptr;
}

int DiceRoll::remaining() const {
  int n = 0;
  for (int d : dice)
    if (!stopped[d]) ++n;
  return n;
}

void DiceRoll::start(std::vector<int> d, script::Rng& rng) {
  dice = std::move(d);
  elapsed = 0;
  for (int die : dice) {
    face[die] = rng.range(1, 6);
    stopped[die] = false;
  }
}

std::vector<int> DiceRoll::tick() {
  std::vector<int> out;
  const int n = static_cast<int>(dice.size());
  if (remaining() == 0) return out;
  elapsed += kTickMs;
  const int want = elapsed <= kSpinMs ? n : std::max(0, n - (elapsed - kSpinMs) / kStepMs);
  for (int d : dice) {
    if (remaining() <= want) break;
    if (stopped[d]) continue;
    stopped[d] = true;
    out.push_back(d);
  }
  return out;
}

void DiceRoll::stopOne() {
  const int r = remaining();
  if (r == 0) return;
  elapsed = (static_cast<int>(dice.size()) - (r - 1)) * kStepMs + kStopBias;
}

int DiceRoll::total(int from, int to) const {
  int sum = 0;
  for (int d = from; d < to; ++d)
    if (stopped[d]) sum += face[d];
  return sum;
}

DiceGame::DiceGame(ui::WindowManager& wm, script::ScriptContext& ctx) : Window(wm), ctx_(ctx) { gActive = this; }

DiceGame::~DiceGame() {
  if (gActive == this) gActive = nullptr;
}

DiceGame* DiceGame::active() {
  // Destruction is deferred: a window whose own flag or an ancestor's is set is already gone.
  for (const ui::Window* w = gActive; w; w = w->parent())
    if (w->isDestroyed()) return nullptr;
  return gActive;
}

void DiceGame::show(int die, int face) {
  auto* b = dynamic_cast<ui::BitmapWindow*>(find("die_" + std::to_string(die)));
  if (!b) return;
  b->setBitmap(face >= 1 && face <= 6 ? "image/die" + std::to_string(face) : std::string("image/die_unknown"), 1.0);
}

int DiceGame::spinFace() {
  spin_ ^= spin_ << 13;
  spin_ ^= spin_ >> 17;
  spin_ ^= spin_ << 5;
  return static_cast<int>(spin_ % 6) + 1;
}

void DiceGame::bumpRollCount() {
  lua_State* L = ctx_.lua.L();
  lua_getglobal(L, "gDiceRollCount");
  const double n = lua_isnumber(L, -1) ? lua_tonumber(L, -1) : 0;
  lua_pop(L, 1);
  lua_pushnumber(L, n + 1);
  lua_setglobal(L, "gDiceRollCount");
}

void DiceGame::startRoll(std::vector<int> dice) {
  if (roll_.rolling()) return;
  ++rolls_;
  spin_ = 0x9E3779B9u * static_cast<std::uint32_t>(rolls_) | 1u;
  roll_.start(std::move(dice), ctx_.rng);
  for (int d : roll_.dice) show(d, spinFace());
  manager().startWindowAnimation(id(), DiceRoll::kTickMs, true);
}

bool DiceGame::onAnimate() {
  for (int d : roll_.tick()) {
    show(d, roll_.face[d]);
    bumpRollCount();
  }
  for (int d : roll_.dice)
    if (!roll_.stopped[d]) show(d, spinFace());
  return roll_.rolling();
}

std::string DiceGame::detail() const {
  std::string s = " dice=";
  for (int d = 0; d < DiceRoll::kDice; ++d) {
    if (d) s += ',';
    s += roll_.stopped[d] ? std::to_string(roll_.face[d]) : "-";
  }
  char buf[96];
  std::snprintf(buf, sizeof buf, " rolls=%d rolling=%d elapsed=%d player=%d character=%d", rolls_, roll_.rolling() ? 1 : 0,
                roll_.elapsed, roll_.playerTotal(), roll_.characterTotal());
  return s + buf;
}

}  // namespace choco::game::choc2
