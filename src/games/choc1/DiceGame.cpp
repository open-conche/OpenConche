#include "games/choc1/DiceGame.h"

#include <cstdio>

#include "audio/Audio.h"
#include "games/choc1/Choc1AudioNatives.h"
#include "games/choc1/Choc1World.h"
#include "ui/WindowManager.h"
#include "ui/widgets/Widgets.h"

namespace choco::game::choc1 {

// ---- DiceSim (docs/re/choc-dice-game.md 1.5, 1.6) ----

int DiceSim::remainingOf(int n, int elapsedMs) {
  // The exe: remaining = N; if (2000 < elapsed) remaining = N - (elapsed - 2000)/750 (unsigned integer division).
  if (elapsedMs <= kSpinMs) return n;
  const int r = n - (elapsedMs - kSpinMs) / kStepMs;
  return r > 0 ? r : 0;
}

void DiceSim::rollDice(std::string bet) {
  bet_ = std::move(bet);
  elapsed_ = 0;
  for (int i = 0; i < n_; ++i) final_[static_cast<std::size_t>(i)] = rand_() % 6;
}

DiceSim::Faces DiceSim::redraw(int remaining) {
  // 00408910: one rand()%6 per die, always, in die order (a stopped die's draw is discarded); the shown face is the draw
  // while the die spins (i < remaining), otherwise its fixed final face. FACT: face value v shows picture slot v of the
  // six loaded die1..die6 pictures, so v = 0..5 maps to image/die<v+1>.
  Faces f;
  f.remaining = remaining;
  for (int i = 0; i < n_; ++i) {
    const int drawn = rand_() % 6;
    f.face[static_cast<std::size_t>(i)] = i < remaining ? drawn : final_[static_cast<std::size_t>(i)];
  }
  return f;
}

DiceSim::Faces DiceSim::tick() {
  elapsed_ += kTickMs;
  const int r = remaining();
  Faces f = redraw(r);
  if (r == 0) {
    // 00408b40 when remaining hit 0: A = final[0]+final[1] (the player), B = final[2]+final[3] (the character).
    f.outcome = characterTotal() < playerTotal()   ? Outcome::Win
                : playerTotal() < characterTotal() ? Outcome::Lose
                                                   : Outcome::Tie;
  }
  return f;
}

DiceSim::Faces DiceSim::stopDie() {
  const int r = remaining();
  if (r == 0) return {};  // 004089e0: nothing once every die has stopped
  Faces f = redraw(r);
  elapsed_ = (n_ - (r - 1)) * kStepMs + kStopBias;
  return f;
}

const char* DiceSim::messageOf(Outcome o) {
  switch (o) {
    case Outcome::Win: return "win_message";
    case Outcome::Lose: return "lose_message";
    default: return "draw_message";
  }
}

const char* DiceSim::soundOf(Outcome o) {
  switch (o) {
    case Outcome::Win: return "dice_game_win";
    case Outcome::Lose: return "dice_game_lose";
    default: return "dice_game_tie";
  }
}

std::string DiceSim::moneyOf(Outcome o, const std::string& bet) {
  // 1.5: the DoString statement is built after the outcome sound; a tie pays nothing.
  switch (o) {
    case Outcome::Win: return "gSim:AdjustMoney(" + bet + ",'dice_logwin')";
    case Outcome::Lose: return "gSim:AdjustMoney(-" + bet + ",'dice_logloss')";
    default: return {};
  }
}

// ---- the window ----

void DiceGame::postChildrenInit(const ui::Style& style) {
  Window::postChildrenInit(style);
  // 1.3 Init: count the die1..dieN Bitmap children (stop at the first missing or non-Bitmap), re-image each counted die
  // to picture slot 0 (image/die1, whatever body art it was built with), zero the final faces, then disable win_message,
  // lose_message, draw_message and stop_label. The `stop` button itself is left alone (it is covered by the
  // later-created `roll` button until the roll starts).
  int n = 0;
  while (n < DiceSim::kMaxDice) {
    auto* b = dynamic_cast<ui::BitmapWindow*>(find("die" + std::to_string(n + 1)));
    if (!b) break;
    b->setBitmap("image/die1", 1.0);  // face 0 is picture slot 0; keeps shown_ (all zero) in step with the dice
    ++n;
  }
  sim_.setDiceCount(n);
  showChild("win_message", false);
  showChild("lose_message", false);
  showChild("draw_message", false);
  showChild("stop_label", false);
}

void DiceGame::showChild(const char* child, bool shown) {
  // The exe flips these by name through the UtilEnableWindow / UtilDisableWindow path (00408b40), not by child pointer,
  // so the port takes the first window of that name in the modal chain too.
  if (ui::Window* w = manager().findInChain(child)) w->setShown(shown);
}

void DiceGame::playSound(const char* name) { script::choc1PlaySound(ctx_, name); }

// 1.5 step 5: the tick that stops the last die stops the roll sound (the exe's FUN_00433df0 on its sound handle). The
// engine's PlaySound path has no handle, so Audio::stopFile stops the playing file by name (added for this slice; see
// the note in docs/re/choc-dice-game.md 1.8).
void DiceGame::stopRollSound() {
  if (!ctx_.audio) return;
  const std::string path = script::choc1SoundPath("dice_game_roll");
  if (!path.empty()) ctx_.audio->stopFile(path);
}

// 1.5 / 1.7: the exe runs the statement with DoString right after the outcome sound; ScriptSink::runString is the
// engine's DoLuaString, run here at the same point in the tick (World and the Chocolatier 2 map use it in-tick too).
void DiceGame::runMoney(const std::string& statement) {
  if (ui::ScriptSink* sink = manager().scriptSink()) sink->runString(statement);
}

int DiceGame::dieDraw() {
  // The dice share Chocolatier 1's one CRT rand() stream (the factory game's); a test without a world gets the window's
  // own seed-1 generator.
  if (auto* world = choc1WorldOf(ctx_)) return world->crtRand.next();
  return localRand_.next();
}

void DiceGame::rollDice(std::string bet) {
  outcome_ = DiceSim::Outcome::None;
  sim_.rollDice(std::move(bet));
  playSound("dice_game_roll");
  manager().startWindowAnimation(id(), DiceSim::kTickMs, true);  // the repeating 62 ms task, on the modal's clock
}

void DiceGame::stopDie() {
  const DiceSim::Faces f = sim_.stopDie();
  if (f.remaining > 0) applyFaces(f);
}

void DiceGame::applyFaces(const DiceSim::Faces& f) {
  for (int i = 0; i < sim_.diceCount(); ++i) {
    const int face = f.face[static_cast<std::size_t>(i)];
    if (face == shown_[static_cast<std::size_t>(i)]) continue;
    shown_[static_cast<std::size_t>(i)] = face;
    if (auto* b = dynamic_cast<ui::BitmapWindow*>(find("die" + std::to_string(i + 1))))
      b->setBitmap("image/die" + std::to_string(face + 1), 1.0);
  }
}

bool DiceGame::onAnimate() {
  const DiceSim::Faces f = sim_.tick();
  applyFaces(f);
  if (f.outcome) {
    outcome_ = *f.outcome;
    stopRollSound();
    showChild("stop", false);
    showChild("stop_label", false);
    showChild("intro", false);  // the tie branch disables it too (00408b40)
    showChild("leave", true);
    showChild("leave_label", true);
    showChild(DiceSim::messageOf(*f.outcome), true);
    playSound(DiceSim::soundOf(*f.outcome));
    if (const std::string money = DiceSim::moneyOf(*f.outcome, sim_.bet()); !money.empty()) runMoney(money);
    return false;
  }
  return true;  // remaining != 0: the task runs again
}

std::string DiceGame::detail() const {
  std::string out = " dice=";
  for (int i = 0; i < sim_.diceCount(); ++i) out += (i ? "," : "") + std::to_string(shown_[static_cast<std::size_t>(i)]);
  const char* outcome = outcome_ == DiceSim::Outcome::Win     ? "win"
                        : outcome_ == DiceSim::Outcome::Lose ? "lose"
                        : outcome_ == DiceSim::Outcome::Tie  ? "tie"
                                                              : "none";
  char buf[64];
  std::snprintf(buf, sizeof buf, " rolling=%d elapsed=%d outcome=%s", sim_.rolling() ? 1 : 0, sim_.elapsed(), outcome);
  out += buf;
  out += " bet=" + (sim_.bet().empty() ? std::string("-") : sim_.bet());
  return out;
}

// ---- the window in the chain ----

namespace {
DiceGame* findIn(ui::Window& w) {
  if (auto* g = dynamic_cast<DiceGame*>(&w)) return g;
  for (const auto& c : w.children())
    if (auto* g = findIn(*c)) return g;
  return nullptr;
}
}  // namespace

DiceGame* DiceGame::inChain(ui::WindowManager& wm) {
  const auto& modals = wm.modals();
  for (auto it = modals.rbegin(); it != modals.rend(); ++it)
    if (auto* g = findIn(**it)) return g;
  return nullptr;
}

}  // namespace choco::game::choc1
