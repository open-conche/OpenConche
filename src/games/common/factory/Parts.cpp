#include "games/common/factory/Parts.h"

#include <algorithm>
#include <cmath>

namespace choco::game {

namespace {

constexpr gfx::ColorF kColors[5] = {{1, 1, 1, 1},
                                    {0.9216f, 0.0549f, 0.0549f, 1},
                                    {0.1765f, 0.2196f, 0.7725f, 1},
                                    {1.0f, 0.8196f, 0.0902f, 1},
                                    {1, 1, 1, 1}};

constexpr double kEps = 1e-9;

}  // namespace

const gfx::ColorF& factoryColor(int c) { return kColors[std::clamp(c, 0, 4)]; }

// ------------------------------------------------------------------ ScalingTask

void ScalingTask::reset(double s) {
  time_ = lastTime_ = 0;
  taskClock_ = lastClock_ = 0;
  scale = s;
  paused = true;
}

void ScalingTask::softReset(double s) {
  scale = s;
  lastClock_ = taskClock_;
}

void ScalingTask::restart() {
  lastClock_ = taskClock_;
  paused = false;
}

bool ScalingTask::tick(int ms) {
  if (paused) return false;
  taskClock_ += ms;
  return true;
}

void ScalingTask::animate() {
  lastTime_ = time_;
  time_ += static_cast<double>(taskClock_ - lastClock_) * scale;
  lastClock_ = taskClock_;
}

double ScalingTask::phase(double full) const {
  if (full <= 0) return 0;
  const double x = time_ / full;
  return x - std::floor(x);
}

double ScalingTask::animateDelta(double full) {
  animate();
  return full > 0 ? (time_ - lastTime_) / full : 0;
}

// ------------------------------------------------------------------ Ingredient

Ingredient::Ingredient(std::string name, std::shared_ptr<const IngredientArt> art)
    : Sprite(4, "ingredient:" + name), name_(std::move(name)), art_(art ? std::move(art) : std::make_shared<const IngredientArt>()) {}

void Ingredient::setColor(int c, Textures& tex) {
  const int prev = color_;
  // A game with no rainbow ring picture shows its plain ring for the wild colour too.
  const std::string& wild = art_->rainbowRing.empty() ? art_->ring : art_->rainbowRing;
  if (!ring_) {
    auto r = std::make_unique<Sprite>(-1, "ring");
    ring_ = &addChild(std::move(r));
    ring_->texture = tex.texture(c == 0 ? wild : art_->ring);
  } else if (c == 0) {
    ring_->texture = tex.texture(wild);
  } else if (prev == 0) {
    ring_->texture = tex.texture(art_->ring);
  }
  color_ = c;
  ring_->tint = factoryColor(c);
}

// ------------------------------------------------------------------ IngredientQueue

IngredientQueue::IngredientQueue(std::vector<std::string> recipe, int colorCount, bool rainbow, gfx::Curve path, double spacing,
                                 double tripMs_, Sprite& base, std::shared_ptr<const IngredientArt> art, bool pistons, int bagRounds)
    : tripMs(tripMs_),
      recipe_(std::move(recipe)),
      colorCount_(colorCount),
      rainbow_(rainbow),
      path_(std::move(path)),
      spacing_(spacing),
      base_(base),
      art_(art ? std::move(art) : std::make_shared<const IngredientArt>()),
      pistonsOn_(pistons),
      bagRounds_(bagRounds) {}

std::vector<BagEntry> IngredientQueue::makeBag(const std::vector<std::string>& recipe, int colorCount, bool rainbow,
                                               script::Rng& rng, int rounds) {
  std::vector<BagEntry> bag;
  if (recipe.empty()) return bag;
  if (rounds > 0) {
    // TFactoryGame::RandomizeIngredientQueue: types recipe[i % size], colours i % colorCount, each vector shuffled by its own
    // swaps (for each i the type swap, then the colour swap).
    const std::size_t m = recipe.size() * static_cast<std::size_t>(rounds);
    std::vector<std::string> types;
    std::vector<int> colors;
    for (std::size_t i = 0; i < m; ++i) {
      types.push_back(recipe[i % recipe.size()]);
      if (colorCount > 1) colors.push_back(static_cast<int>(i % static_cast<std::size_t>(colorCount)) + 1);
    }
    for (std::size_t i = 0; i < m; ++i) {
      std::swap(types[i], types[static_cast<std::size_t>(rng.range(0, static_cast<int>(m) - 1))]);
      if (colorCount > 1) std::swap(colors[i], colors[static_cast<std::size_t>(rng.range(0, static_cast<int>(m) - 1))]);
    }
    for (std::size_t i = 0; i < m; ++i) bag.push_back({types[i], colorCount > 1 ? colors[i] : 0});
    return bag;
  }
  const std::size_t n = recipe.size() * 5;
  bag.reserve(n);
  for (std::size_t i = 0; i < n; ++i) {
    BagEntry e{recipe[i % recipe.size()], 0};
    if (colorCount > 1) e.color = rng.range(rainbow ? 0 : 1, colorCount);
    bag.push_back(std::move(e));
  }
  for (std::size_t i = 0; i < n; ++i) {
    const std::size_t j = static_cast<std::size_t>(rng.range(0, static_cast<int>(n) - 1));
    std::swap(bag[i], bag[j]);
  }
  return bag;
}

void Ingredient::placePole(Textures& tex, bool show) {
  if (art_->pole.empty()) return;
  const int count = 20;
  while (static_cast<int>(pole_.size()) < count) {
    auto p = std::make_unique<Sprite>(-3, "pole");
    p->texture = tex.texture(art_->pole);
    pole_.push_back(&addChild(std::move(p)));
  }
  for (int k = 0; k < count; ++k) {
    const float y = 16.0f + 32.0f * static_cast<float>(k);
    pole_[static_cast<std::size_t>(k)]->visible = show && pos.y + y <= 600.0f;  // the tile's centre inside the window
    pole_[static_cast<std::size_t>(k)]->pos = {0, y + 16.0f};
  }
}

void IngredientQueue::place(Ingredient& ing, double t) {
  ing.pos = path_.at(static_cast<float>(t));
  if (queueTex_) ing.placePole(*queueTex_, true);
}

void IngredientQueue::updatePistons(Textures& tex) {
  // Sprites here are anchored at their centre, the original's pistons at the points in the table of 8.2; each offset
  // below is the original's anchor-relative position converted to a centre position.
  while (pistons_.size() < belt_.size()) {
    Piston pi;
    auto root = std::make_unique<Sprite>(-100, "piston");
    root->texture = tex.texture("image/ingredient_arm_2");
    pi.root = &base_.addChild(std::move(root));
    auto arm = std::make_unique<Sprite>(0, "piston_arm");
    arm->texture = tex.texture("image/ingredient_arm_1");
    pi.arm = &pi.root->addChild(std::move(arm));
    for (float dx : {-6.0f, 6.0f}) {
      auto f = std::make_unique<Sprite>(2, "piston_finger");
      f->texture = tex.texture(dx < 0 ? "image/ingredient_finger_1" : "image/ingredient_finger_2");
      f->pos = {dx, -78};
      pi.root->addChild(std::move(f));
    }
    pistons_.push_back(pi);
  }
  for (std::size_t k = 0; k < belt_.size() && k < pistons_.size(); ++k) {
    const gfx::PointF p = belt_[k]->pos;
    pistons_[k].root->pos = {p.x, p.y + 78};  // anchor (7.5, -16) on a 15 x 124 image
    pistons_[k].arm->pos = {0, (600 - p.y) / 2 + 16 - 16};  // anchor (9.5, 0) on 19 x 124, relative to the root's anchor
  }
}

void IngredientQueue::update(double dt, script::Rng& rng, Textures& tex) {
  queueTex_ = &tex;
  const bool moving = head_ < 1.0;
  struct PistonGuard {
    IngredientQueue& q;
    Textures& tex;
    bool run;
    ~PistonGuard() {
      if (run) q.updatePistons(tex);
    }
  } guard{*this, tex, moving && pistonsOn_};
  if (head_ < 1.0) {
    head_ = std::min(1.0, head_ + dt);
    if (head_ > 1.0 - kEps) head_ = 1.0;
  }
  for (std::size_t k = 0; k < belt_.size(); ++k) place(*belt_[k], head_ - static_cast<double>(k) * spacing_);
  if (recipe_.empty()) return;
  double rem = head_ - static_cast<double>(belt_.size()) * spacing_;
  while (rem > kEps) {
    if (reserve_.empty()) reserve_ = makeBag(recipe_, colorCount_, rainbow_, rng, bagRounds_);
    BagEntry e = std::move(reserve_.back());
    reserve_.pop_back();
    auto ing = std::make_unique<Ingredient>(e.name, art_);
    ing->texture = tex.texture(art_->itemPrefix + e.name);
    if (colorCount_ > 1) ing->setColor(e.color, tex);
    if (!art_->plate.empty()) {
      auto plate = std::make_unique<Sprite>(-2, "plate");
      plate->texture = tex.texture(art_->plate);
      plate->pos = {0, 16};
      plate->pos.y += plate->imageSize().h * 0.5f;  // its top edge 16 px under the ingredient's centre
      ing->addChild(std::move(plate));
    }
    Ingredient& ref = *ing;
    base_.addChild(std::move(ing));
    belt_.push_back(&ref);
    place(ref, rem);
    rem -= spacing_;
  }
}

Ingredient* IngredientQueue::pop() {
  if (!ready()) return nullptr;
  Ingredient* front = belt_.front();
  if (queueTex_) front->placePole(*queueTex_, false);
  belt_.pop_front();
  head_ -= spacing_;
  return front;
}

void IngredientQueue::reset() {
  for (Ingredient* i : belt_) base_.removeChild(*i);
  belt_.clear();
  reserve_.clear();
  head_ = 0;
  clock.reset(1.0);
}

// ------------------------------------------------------------------ shots

double Flight::tick(double dt) {
  if (unbounded) {
    t += dt;
    if (ing) ing->pos = {start.x + dir.x * static_cast<float>(t), start.y + dir.y * static_cast<float>(t)};
    return t;
  }
  t = std::min(1.0, t + dt);
  if (t > 1.0 - kEps) t = 1.0;
  double u = t;
  if (distance > 0) {
    u = std::min(1.0, t * 1000.0 / distance);
    if (u > 1.0 - kEps) u = 1.0;
  }
  if (ing) ing->pos = {start.x + dir.x * static_cast<float>(u), start.y + dir.y * static_cast<float>(u)};
  return u;
}

float gunRotationDeg(gfx::PointF mouse, gfx::PointF gun) {
  const float dx = mouse.x - gun.x, dy = mouse.y - gun.y;
  const float len = std::sqrt(dx * dx + dy * dy);
  if (len <= 0) return 180.0f;
  const float ny = std::clamp(dy / len, -1.0f, 1.0f);
  float angle = std::acos(ny);
  if (dx / len > 0) angle = -angle;
  return (angle + 3.14159265358979f) * 180.0f / 3.14159265358979f;
}

bool segmentHitsCircle(gfx::PointF a, gfx::PointF b, gfx::PointF c, float r) {
  const double abx = b.x - a.x, aby = b.y - a.y;
  const double len2 = abx * abx + aby * aby;
  double t = 0;
  if (len2 > 0) t = std::clamp(((c.x - a.x) * abx + (c.y - a.y) * aby) / len2, 0.0, 1.0);
  const double px = a.x + abx * t - c.x, py = a.y + aby * t - c.y;
  return px * px + py * py <= static_cast<double>(r) * r;
}

}  // namespace choco::game
