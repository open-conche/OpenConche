#pragma once

#include <deque>
#include <memory>
#include <string>
#include <vector>

#include "games/common/Textures.h"
#include "sdk/Sprite.h"
#include "gfx/Curve.h"
#include "gfx/Types.h"
#include "script/ScriptContext.h"
#include "games/common/SdkNames.h"

namespace choco::game {

// The ring colours (kFactoryColors, docs/re/chocolate-factory.md 5.1): 0 is wild (the rainbow ring picture), 1 red,
// 2 blue, 3 yellow, 4 white (a mismatch).
inline constexpr int kColorWild = 0;
inline constexpr int kColorMismatch = 4;
const gfx::ColorF& factoryColor(int c);

// TScalingTask, exactly (CF 2.2): a clock that runs at `scale` times the real step and can be paused. Every part of a
// factory owns one. Its owner calls tick(10) once per logical step and, when that returns true, animate().
class ScalingTask {
 public:
  // time = lastTime = taskClock = lastClock = 0, the given scale, paused.
  void reset(double scale);
  // The trays' and the recycler's Reset: only the scale and lastClock change, so their position carries on.
  void softReset(double scale);
  // lastClock = taskClock; paused = false.
  void restart();
  void pause() { paused = true; }
  // Paused: false. Else taskClock += ms and true (the owner then animates).
  bool tick(int ms);
  // lastTime = time; time += (taskClock - lastClock) * scale; lastClock = taskClock.
  void animate();
  // GetT: the fractional part of time / full.
  double phase(double full) const;
  // animate(), then the fraction of one `full`-long period that elapsed (the same as the phase change across the call,
  // wrap-aware, whenever less than a whole period passes in one step).
  double animateDelta(double full);

  double scale = 1;
  bool paused = true;
  double time() const { return time_; }
  double lastTime() const { return lastTime_; }
  int taskClock() const { return taskClock_; }

 private:
  double time_ = 0, lastTime_ = 0;
  int taskClock_ = 0, lastClock_ = 0;
};

// The names of the pictures an ingredient uses. The defaults are Chocolatier 3's; another game's spec replaces them
// (docs/design/choc2-minigames.md 2.1).
struct IngredientArt {
  std::string itemPrefix = "items/";                    // + the ingredient's name
  std::string ring = "image/item_ring";                 // the colour ring, tinted
  std::string rainbowRing = "image/item_ring_rainbow";  // the ring of the wild colour 0 (empty: the plain ring)
  std::string plate;                                    // the holder drawn under a conveyor item (empty: none)
  std::string pole;                                     // a rod tiled from the holder down to the bottom edge (empty: none)
};

// A shot ingredient or a conveyor item: a layer-4 sprite `items/<name>` with a lazily made ring child (layer -1).
class Ingredient : public Sprite {
 public:
  // `art` names the ring pictures; null gives the defaults of IngredientArt.
  explicit Ingredient(std::string name, std::shared_ptr<const IngredientArt> art = {});
  // CF 5.1: the ring texture is the rainbow one for colour 0, and `item_ring` for a non-zero colour only when the
  // previous colour was 0. The ring is tinted with factoryColor(c).
  void setColor(int c, Textures& tex);
  int color() const { return color_; }
  // The rod under a conveyor item: 32 px pieces from the holder to y = 600 (screen), hidden when `show` is false.
  void placePole(Textures& tex, bool show);
  const std::string& name() const { return name_; }

 private:
  std::string name_;
  std::shared_ptr<const IngredientArt> art_;
  int color_ = 0;
  Sprite* ring_ = nullptr;
  std::vector<Sprite*> pole_;
};

struct BagEntry {
  std::string name;
  int color = 0;
};

// IngredientQueue (CF 5.1): the train of ingredients that rides the conveyor to the gun.
class IngredientQueue {
 public:
  // `base` is the sprite the ingredients are added to while they ride; it outlives the queue.
  // `spacing` is the distance between neighbours along the path, in units of the path (Chocolatier 3 passes 1.0 / count).
  // `art` null gives the default pictures; `pistons` false leaves the arms and fingers out.
  IngredientQueue(std::vector<std::string> recipe, int colorCount, bool rainbow, gfx::Curve path, double spacing, double tripMs,
                  Sprite& base, std::shared_ptr<const IngredientArt> art = {}, bool pistons = true, int bagRounds = 0);

  // FillQueue(5): five copies of every recipe entry (so recipe.size() * 5 names, in recipe order modulo its size), a
  // colour for each when colorCount > 1 drawn as RandRange(rainbow ? 0 : 1, colorCount), then a swap shuffle: each
  // element i swaps with an index drawn from [0, n-1]. The colours are all drawn first, then the shuffle.
  // Chocolatier 2 (docs/re/choc2-factory-binary.md 4): `rounds` > 0 makes recipe.size() * rounds entries (the tray count) with the
  // colour of entry i being i % colorCount, and the types and the colours are shuffled apart (colour range 0 .. colorCount - 1).
  static std::vector<BagEntry> makeBag(const std::vector<std::string>& recipe, int colorCount, bool rainbow, script::Rng& rng,
                                       int rounds = 0);

  // dt = clock.animateDelta(tripMs): moves the head, places the train and spawns new items at the tail.
  void update(double dt, script::Rng& rng, Textures& tex);
  // The front item when the head is exactly 1.0 (and then the head drops by one spacing), else null.
  Ingredient* pop();
  Ingredient* peek() const { return belt_.empty() ? nullptr : belt_.front(); }
  bool ready() const { return !belt_.empty() && head_ >= 1.0; }
  // Removes every sprite on the belt, empties the reserve, puts the head at 0 and resets the clock to scale 1.
  void reset();

  ScalingTask clock;
  double tripMs;
  double head() const { return head_; }
  double spacing() const { return spacing_; }
  std::size_t onBelt() const { return belt_.size(); }
  std::size_t reserve() const { return reserve_.size(); }
  // The pistons made so far (root sprite and its arm child), for tests; they are never removed.
  std::size_t pistonCount() const { return pistons_.size(); }
  const Sprite& pistonRoot(std::size_t k) const { return *pistons_[k].root; }
  const Sprite& pistonArm(std::size_t k) const { return *pistons_[k].arm; }

 private:
  void place(Ingredient& ing, double t);
  std::vector<std::string> recipe_;
  int colorCount_;
  bool rainbow_;
  gfx::Curve path_;
  double spacing_;
  double head_ = 0;
  Sprite& base_;
  std::shared_ptr<const IngredientArt> art_;
  bool pistonsOn_;
  int bagRounds_ = 0;
  std::vector<BagEntry> reserve_;  // consumed from the back
  std::deque<Ingredient*> belt_;   // in the tree under base_, front = at the gun

  // IngredientPiston (docs/re/factories-questions.md 8.2): the arm and two fingers holding each belt ingredient. Made on
  // demand, never removed.
  struct Piston {
    Sprite* root = nullptr;
    Sprite* arm = nullptr;
  };
  void updatePistons(Textures& tex);
  Textures* queueTex_ = nullptr;  // the textures of the last update(), for the rods
  std::vector<Piston> pistons_;
};

// FlyingIngredient (CF 5.2): a shot. `ing` is owned by the sprite tree.
struct Flight {
  Ingredient* ing = nullptr;
  gfx::PointF start, dir;  // dir is the whole flight: length 1000 for the chocolate gun
  double distance = 0;     // 0: the flight is the whole of dir in one step of t
  bool unbounded = false;  // Chocolatier 2: t is not capped; the owner ends the flight when it leaves the window
  double t = 0;
  // t = min(1, t + dt); u = distance > 0 ? min(1, t * 1000 / distance) : t. Moves the ingredient to start + dir * u and
  // returns u (exactly 1 at the end of the flight).
  double tick(double dt);
};

// The gun sprite's clockwise rotation in degrees for a mouse position (CF 5.2): d = normalise(mouse - gun),
// acos(d.y), negated if d.x > 0, plus pi. (1,0) gives 90, (0,1) gives 180, (0,-1) gives 0.
float gunRotationDeg(gfx::PointF mouse, gfx::PointF gun);

// Squared distance from c to the segment a-b is at most r squared (CF 5.3).
bool segmentHitsCircle(gfx::PointF a, gfx::PointF b, gfx::PointF c, float r);

}  // namespace choco::game
