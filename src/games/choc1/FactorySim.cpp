#include "games/choc1/FactorySim.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace choco::game::choc1 {

namespace {

constexpr double kPi = 3.14159265358979323846;

// 2.8: the two 8-entry offset tables of the arm animation (the x offset of the sprite at (723,273) and at (723,323)).
constexpr double kUpperArm[8] = {-60, -110, -90, -75, -60, -50, -30, 0};
constexpr double kLowerArm[8] = {-45, -100, -80, -65, -50, -40, -20, 0};

double tableAt(const double (&t)[8], double position) {
  if (position < 0) return t[7];  // idle: the arm rests at its last entry
  const int i = std::min(7, static_cast<int>(position));
  const int j = std::min(7, i + 1);
  const double frac = position - i;
  return t[i] + (t[j] - t[i]) * frac;
}

}  // namespace

std::string FactorySim::soundName(const Event& e) {
  char buf[48];
  switch (e.kind) {
    case Event::Kind::CountdownTick: return "countdown_tick";
    case Event::Kind::StartBell: return "minigame_starting_bell";
    case Event::Kind::WarningBell: return "warning_bell";
    case Event::Kind::ProductionOver: return "mini_game_production_over";
    case Event::Kind::Fire: return "minigame_load_launch";
    case Event::Kind::Success:
      std::snprintf(buf, sizeof buf, "ingredient_success_%02d", e.arg);
      return buf;
    case Event::Kind::TemplateComplete: return "template_complete";
    case Event::Kind::Failure:
      std::snprintf(buf, sizeof buf, "ingredient_failure_%02d", e.arg);
      return buf;
    case Event::Kind::Snatch: return "factory_snatch";
    case Event::Kind::TemplateSuccess: return "template_success";
    case Event::Kind::Powerup: return "powerup";
    case Event::Kind::Ended: return "factory_shut_down";
    case Event::Kind::Paused:
    case Event::Kind::FirstMiss: break;
  }
  return {};
}

FactorySim::FactorySim(Config cfg, RandFn rand) : cfg_(std::move(cfg)), rand_(std::move(rand)) {
  cfg_.trayCount = std::max(1, cfg_.trayCount);
  if (cfg_.ingredientRate <= 0) cfg_.ingredientRate = 350;
  // The slot template: ingredient k repeated qty times, in the recipe's order (2.1 +0x34c).
  for (std::size_t k = 0; k < cfg_.quantities.size(); ++k)
    for (int n = 0; n < cfg_.quantities[k]; ++n) template_.push_back(static_cast<int>(k));
  speed_ = cfg_.startspeed;
  reset();
}

// ---- 2.4 Reset ----

void FactorySim::buildQueue() {
  queue_.clear();
  const int total = this->total();
  const int u = cfg_.trayCount * total;
  if (u <= 0) return;
  // Step 3: len = (((60000 / rate) + U) / U) * U, all integer divisions.
  const int len = (((kRunMs / cfg_.ingredientRate) + u) / u) * u;
  queue_.resize(static_cast<std::size_t>(len));
  for (int i = 0; i < len; ++i) queue_[static_cast<std::size_t>(i)] = template_[static_cast<std::size_t>(i % total)];
  // Step 4: the shuffle in blocks of U: one rand() per entry, the swap partner stays in the entry's block.
  for (int i = 0; i < len; ++i) {
    const int r = rand_();
    const int j = r % u + (i / u) * u;
    std::swap(queue_[static_cast<std::size_t>(i)], queue_[static_cast<std::size_t>(j)]);
  }
}

void FactorySim::reset() {
  // Step 1 (FUN_0040c150): every tray empty, the belt, miss, shot and product lists cleared.
  trays_.assign(static_cast<std::size_t>(cfg_.trayCount), Tray{});
  for (Tray& t : trays_) t.slots.assign(template_.size(), kEmpty);
  belt_.clear();
  missed_.clear();
  shots_.clear();
  products_.clear();
  loaded_ = -1;
  armActive_ = false;
  gunAnimMs_ = -1;
  countdownShown_ = false;
  updateBoxes();
  state_ = State::Idle;  // step 2
  saved_ = State::Idle;
  buildQueue();          // steps 2-4
  verdict_.clear();
}

void FactorySim::start() {
  speed_ = cfg_.startspeed;
  decay_ = 0;
  angle_ = 0;
  count_ = 0;
  elapsed_ = 0;
  spawnClock_ = 0;
  nextSpawn_ = 0;
  loaded_ = -1;
  digit_ = 3;
  countdownShown_ = true;
  verdict_.clear();
  updateBoxes();
  state_ = State::Countdown;
}

void FactorySim::restart() {
  reset();  // the firstMiss flag is only cleared by the warning itself, not here
  start();
}

// ---- 2.3 TogglePause ----

void FactorySim::togglePause() {
  if (state_ == State::Over) return;
  if (state_ != State::Paused) {
    saved_ = state_;
    state_ = State::Paused;
    emit(Event::Kind::Paused, 1);
  } else {
    state_ = saved_;
    emit(Event::Kind::Paused, 0);
  }
}

// ---- geometry ----

FactorySim::Point FactorySim::trayScreen(int i, double angle) const {
  const double a = 2 * kPi * i / cfg_.trayCount;
  const double lx = kTrayRadius * std::cos(a), ly = kTrayRadius * std::sin(a);
  // The wheel matrix with y down: a decreasing angle turns the wheel counter-clockwise on the screen (2.6a, inference).
  return {kCentreX + lx * std::cos(angle) - ly * std::sin(angle), kCentreY + lx * std::sin(angle) + ly * std::cos(angle)};
}

void FactorySim::updateBoxes() {
  for (int i = 0; i < static_cast<int>(trays_.size()); ++i) {
    const Point p = trayScreen(i, angle_);
    Tray& t = trays_[static_cast<std::size_t>(i)];
    // INFERENCE: the exe's float bounds are truncated to ints (every other position in this code is).
    t.left = static_cast<int>(p.x - kTraySize / 2.0);
    t.top = static_cast<int>(p.y - kTraySize / 2.0);
    t.right = t.left + kTraySize;
    t.bottom = t.top + kTraySize;
  }
}

// 2.6a: angle -= dt * 2 pi * speed / 30000, then each tray's box; a full tray whose top crosses y = 252 upward is collected.
void FactorySim::rotate(int dt) {
  angle_ -= dt * 2 * kPi * speed_ / 30000.0;
  for (int i = 0; i < static_cast<int>(trays_.size()); ++i) {
    Tray& t = trays_[static_cast<std::size_t>(i)];
    const int oldTop = t.top;
    const Point p = trayScreen(i, angle_);
    t.left = static_cast<int>(p.x - kTraySize / 2.0);
    t.top = static_cast<int>(p.y - kTraySize / 2.0);
    t.right = t.left + kTraySize;
    t.bottom = t.top + kTraySize;
    if (oldTop >= kCollectY && t.top < kCollectY && t.filled == total() && total() > 0 && state_ != State::Over) collect(i);
  }
}

// ---- 2.7 the gun ----

void FactorySim::aim(int mx, int my) {
  float vx = static_cast<float>(mx) - static_cast<float>(kCentreX);
  float vy = static_cast<float>(my) - static_cast<float>(kCentreY);
  const float len = std::sqrt(vx * vx + vy * vy);
  if (len > 0) {  // the zero vector stays zero (a click on the pivot gives a shot that never moves)
    vx /= len;
    vy /= len;
  }
  aimX_ = vx;
  aimY_ = vy;
  gunAngle_ = std::acos(std::clamp(static_cast<double>(vy), -1.0, 1.0));
  if (vx > 0) gunAngle_ = -gunAngle_;
}

bool FactorySim::click(int mx, int my) {
  if (loaded_ < 0 || state_ != State::Running) return false;
  aim(mx, my);
  // 2.7 fire (0040de90): the loaded node leaves the belt as a shot at the pivot, the gun is empty.
  emit(Event::Kind::Fire);
  Shot s;
  s.x = static_cast<int>(kCentreX);
  s.y = static_cast<int>(kCentreY);
  s.dx = aimX_;
  s.dy = aimY_;
  s.ingredient = loaded_;
  shots_.push_back(s);
  loaded_ = -1;
  gunAnimMs_ = 0;
  return true;
}

// 2.7 Shots (0040da00): 12 px a tick, positions truncated to ints, out of the playfield (unsigned test) = a miss.
void FactorySim::moveShots(int dt) {
  const double step = dt * 1200.0 / 1000.0;
  bool warn = false;
  for (std::size_t i = 0; i < shots_.size();) {
    Shot& s = shots_[i];
    s.x = static_cast<int>(static_cast<double>(s.dx) * step + s.x);
    s.y = static_cast<int>(static_cast<double>(s.dy) * step + s.y);
    if (static_cast<unsigned>(s.x) > static_cast<unsigned>(kPlayfieldW) || static_cast<unsigned>(s.y) > static_cast<unsigned>(kPlayfieldH)) {
      // Literal: the exe indexes the sound objects by misses * 8 + 0x404 unbounded, so a ninth miss (two shots leaving in one
      // tick) plays the next object, template_complete.
      if (misses() + 1 > kMissLimit) emit(Event::Kind::TemplateComplete);
      else emit(Event::Kind::Failure, misses() + 1);
      missed_.push_back(s.ingredient);
      speed_ = (speed_ - cfg_.startspeed) * 0.5 + cfg_.startspeed;
      if (cfg_.firstMiss) warn = true;
      shots_.erase(shots_.begin() + static_cast<std::ptrdiff_t>(i));
      continue;
    }
    ++i;
  }
  // The first-waste warning: pause the game (unless paused or idle) and ask the Lua to show the character's dialog.
  if (warn) {
    if (state_ != State::Paused && state_ != State::Idle) togglePause();
    cfg_.firstMiss = false;
    emit(Event::Kind::FirstMiss);
  }
}

bool FactorySim::trayNeeds(const Tray& t, int ingredient) const {
  if (t.filled >= total()) return false;
  for (int k = 0; k < total(); ++k)
    if (template_[static_cast<std::size_t>(k)] == ingredient && t.slots[static_cast<std::size_t>(k)] == kEmpty) return true;
  return false;
}

// 2.7 hit test (0040e480).
void FactorySim::hitTest() {
  const int tot = total();
  for (int ti = 0; ti < static_cast<int>(trays_.size()); ++ti) {
    Tray& t = trays_[static_cast<std::size_t>(ti)];
    for (std::size_t si = 0; si < shots_.size();) {
      const Shot& s = shots_[si];
      const long dx = s.x - t.cx(), dy = s.y - t.cy();
      if (t.filled < tot && dx * dx + dy * dy <= kHitRadiusSq) {
        int slot = -1;
        for (int k = 0; k < tot; ++k)
          if (template_[static_cast<std::size_t>(k)] == s.ingredient && t.slots[static_cast<std::size_t>(k)] == kEmpty) {
            slot = k;
            break;
          }
        if (slot >= 0) {
          // The sound is indexed by the slots already filled; the last slot's entry was overwritten with template_complete.
          if (t.filled == tot - 1) emit(Event::Kind::TemplateComplete);
          else emit(Event::Kind::Success, std::min(t.filled, 4) + 1);
          t.slots[static_cast<std::size_t>(slot)] = s.ingredient;
          ++t.filled;
          // The only speed-up event: a tray completed.
          if (t.filled == tot) speed_ = std::min(speed_ * cfg_.speedup, cfg_.maxspeed);
          shots_.erase(shots_.begin() + static_cast<std::ptrdiff_t>(si));
          continue;
        }
      }
      ++si;
    }
  }
  // Every tray full at once: the power-up, all collected together.
  if (!trays_.empty() && tot > 0 &&
      std::all_of(trays_.begin(), trays_.end(), [tot](const Tray& t) { return t.filled == tot; })) {
    emit(Event::Kind::Powerup);
    for (int i = 0; i < static_cast<int>(trays_.size()); ++i) collect(i);
  }
}

// ---- 2.8 belt, spawning, collection, output conveyor ----

void FactorySim::moveBelt(int dt) {
  const int budget = dt * 600 / 1200;  // 0.5 px per ms, whole pixels
  int move = budget;
  // INFERENCE: when the lead node reaches the gun, the belt stops at that moment: every node moves only as far as the lead
  // did, and the spawn clock advances by the time spent moving (a full tick less twice the overshoot).
  if (!belt_.empty()) move = std::min(budget, kBeltLength - belt_.front().s);
  for (BeltNode& n : belt_) n.s += move;
  if (!belt_.empty() && belt_.front().s >= kBeltLength) {
    loaded_ = belt_.front().ingredient;
    belt_.erase(belt_.begin());
  }
  spawnClock_ += dt - (budget - move) * 2;
  while (spawnClock_ >= nextSpawn_) {
    if (!queue_.empty()) {  // INFERENCE: an exhausted queue spawns nothing (the exe reads past the vector)
      belt_.push_back({queue_.back(), (spawnClock_ - nextSpawn_) * 600 / 1200});
      queue_.pop_back();
    }
    nextSpawn_ += cfg_.ingredientRate;
  }
}

void FactorySim::collect(int tray) {
  // 0040e1d0: the arm starts (its sound only when idle), the tray is emptied, a product rides the conveyor.
  if (!armActive_) emit(Event::Kind::Snatch);
  armActive_ = true;
  armClock_ = 0;
  Tray& t = trays_[static_cast<std::size_t>(tray)];
  std::fill(t.slots.begin(), t.slots.end(), kEmpty);
  t.filled = 0;
  t.powerupMs = 0;
  products_.push_back(kOutStartY);
}

void FactorySim::armTick(int dt) {
  armClock_ += static_cast<unsigned>(dt * speed_);
  if (armClock_ * 7 / 1000 >= 8) armActive_ = false;  // 0x40a8f3: f is truncated and the arm ends at f >= 8 (clock 1143)
}

double FactorySim::armPosition() const { return armActive_ ? std::min(7.0, armClock_ * 7.0 / 1000.0) : -1; }
double FactorySim::armUpperOffset(double position) { return tableAt(kUpperArm, position); }
double FactorySim::armLowerOffset(double position) { return tableAt(kLowerArm, position); }

// 0040c360: each product rides 216 px in 2000 ms; past 516 it is counted.
void FactorySim::conveyor(int dt) {
  bool arrived = false;
  for (std::size_t i = 0; i < products_.size();) {
    products_[i] += dt * (kOutEndY - kOutStartY) / 2000.0;
    if (products_[i] > kOutEndY) {
      products_.erase(products_.begin() + static_cast<std::ptrdiff_t>(i));
      ++count_;
      arrived = true;
      continue;
    }
    ++i;
  }
  if (arrived) emit(Event::Kind::TemplateSuccess);
}

// ---- 2.9 the end ----

std::string FactorySim::verdictKey(int count, int total, int ingredientRate, bool aborted) {
  if (aborted) return "production_stopped";
  const int maxTrays = total > 0 && ingredientRate > 0 ? (kRunMs / ingredientRate) / total : 0;
  const double r = maxTrays > 0 ? static_cast<double>(count) / maxTrays : 0.0;
  if (r < 0.11) return "production_low";
  if (r < 0.19) return "production_med";
  const bool awesome = total <= 3 ? r >= 0.65 : r >= 0.55;
  return awesome ? "production_awesome" : "production_high";
}

void FactorySim::end(bool aborted) {
  state_ = State::Over;
  decay_ = speed_ / 300;
  verdict_ = verdictKey(count_, total(), cfg_.ingredientRate, aborted);
  emit(Event::Kind::Ended, aborted ? 1 : 0);
}

std::string FactorySim::timerText() const {
  const int s = elapsed_ < kRunMs ? (kRunMs - elapsed_) / 1000 : 0;
  char buf[16];
  std::snprintf(buf, sizeof buf, "%d:%02d", s / 60, s % 60);
  return buf;
}

// ---- 2.6 the tick ----

void FactorySim::tick() {
  // Step 1: the clocks, unless paused.
  if (state_ != State::Paused) {
    elapsed_ += kStepMs;                                                  // 1.1
    speed_ -= decay_;                                                     // 1.2
    if (speed_ < 0) {
      speed_ = 0;
      decay_ = 0;
    }
    if (state_ != State::Over && (kRunMs - elapsed_) / 1000 < 8 && (kRunMs - (elapsed_ - kStepMs)) / 1000 > 7)
      emit(Event::Kind::WarningBell);                                     // 1.3: once, at elapsed 52010
    if (armActive_) armTick(kStepMs);                                     // 1.4
    if (misses() == kMissLimit && state_ != State::Over) end(true);       // 1.5
    if (elapsed_ > kRunMs && state_ == State::Running) {                  // 1.6
      emit(Event::Kind::ProductionOver);
      state_ = State::Finishing;
      if (speed_ < 5.0) speed_ = 5.0;
    }
    // Cosmetic clocks (the gun sheet and the tray power-up sheet).
    if (gunAnimMs_ >= 0) gunAnimMs_ += kStepMs;
    for (Tray& t : trays_)
      if (t.powerupMs >= 0) t.powerupMs += kStepMs;
  }
  // Step 2: the countdown. On the bell tick the exe's dt is 0 for the rest of the tick (0x410cd9: the elapsed time left after
  // the subtraction is 0), so the wheel, belt, spawn clock and conveyor all skip that tick.
  int dt = kStepMs;
  if (state_ == State::Countdown) {
    if (elapsed_ < kCountdownMs) {
      const int n = (kCountdownMs - elapsed_) / 1000 + 1;
      const int before = (kCountdownMs - (elapsed_ - kStepMs)) / 1000 + 1;
      digit_ = n;
      if (n != before) emit(Event::Kind::CountdownTick);  // at elapsed 410 and 1410
    } else {
      emit(Event::Kind::StartBell);
      countdownShown_ = false;
      elapsed_ -= kCountdownMs;
      dt = elapsed_;
      state_ = State::Running;
      nextSpawn_ = 0;
    }
  }
  // Step 3: the wheel, then the hit test (not in state 5).
  if (state_ == State::Countdown || state_ == State::Running || state_ == State::Finishing || state_ == State::Over) {
    rotate(dt);
    if (state_ != State::Over) hitTest();
  }
  // Step 4: shots, belt and spawn, then the hit test again. The belt only runs while the gun is empty.
  if (state_ == State::Running || state_ == State::Finishing) {
    moveShots(dt);
    if (loaded_ < 0) moveBelt(dt);
    if (state_ != State::Over) hitTest();
  }
  // Step 5: the output conveyor.
  if (state_ != State::Paused) conveyor(dt);
  // Step 6: finishing ends once nothing is in flight or on the conveyor and no tray is full and uncollected.
  if (state_ == State::Finishing && shots_.empty() && products_.empty()) {
    const bool fullLeft = std::any_of(trays_.begin(), trays_.end(), [this](const Tray& t) { return total() > 0 && t.filled == total(); });
    if (!fullLeft) end(false);
  }
}

std::vector<FactorySim::Event> FactorySim::takeEvents() {
  std::vector<Event> out;
  out.swap(events_);
  return out;
}

// ---- the harness aim ----

std::optional<FactorySim::Point> FactorySim::bestAim() const {
  if (state_ != State::Running || loaded_ < 0) return std::nullopt;
  const Tray* best = nullptr;
  int bestIndex = -1;
  for (int i = 0; i < static_cast<int>(trays_.size()); ++i) {
    const Tray& t = trays_[static_cast<std::size_t>(i)];
    if (!trayNeeds(t, loaded_)) continue;
    if (!best || t.filled > best->filled) {
      best = &t;
      bestIndex = i;
    }
  }
  if (!best) return std::nullopt;
  // The shot flies 12 px a tick from the pivot; the tray moves on while it does: iterate to the meeting time (in ticks).
  const double perTick = 2 * kPi * speed_ / 3000.0;
  Point p = trayScreen(bestIndex, angle_);
  for (int it = 0; it < 6; ++it) {
    const double ticks = std::hypot(p.x - kCentreX, p.y - kCentreY) / 12.0;
    p = trayScreen(bestIndex, angle_ - ticks * perTick);
  }
  return p;
}

}  // namespace choco::game::choc1
