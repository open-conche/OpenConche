#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "games/common/Appearance.h"
#include "sdk/TextSprite.h"
#include "games/common/factory/FactoryWindow.h"
#include "games/common/SdkNames.h"

namespace choco::game {

// ---- the pure rules (docs/re/coffee-factory-and-recipes.md 1.9 to 1.11), unit-tested without sprites ----

// One cell of the coffee grid: the ingredient's name (empty for a free cell) and its sprite, if there is one.
struct GridSlot {
  std::string name;
  Ingredient* ing = nullptr;
};

// A slot a flood clear emptied.
struct ClearedSlot {
  int index = 0;
  std::string name;
  Ingredient* ing = nullptr;
};

// CoffeeIngredientGrid's logic (CO 1.9). The cells are row-major (index = row * cols + col). `pos` is the grid's current
// top-left corner in window coordinates; it moves down as the belt scrolls, so every window-to-cell mapping uses it.
class CoffeeGrid {
 public:
  void configure(int cols, int rows, float dx, float dy, gfx::PointF origin);

  int cols = 9, rows = 9;
  float dx = 50, dy = 50;
  gfx::PointF pos;
  std::vector<GridSlot> slots;
  ScalingTask clock;     // CoffeeGridTask: its scale is the scroll speed
  double previousT = 0;  // CoffeeGridTask's: the scroll phase of the last tick (never reset, see factories-questions 9.3)

  int slotIndex(gfx::PointF window) const;  // -1 outside [pos, pos + cols*dx) x [pos, pos + rows*dy): right and bottom exclusive
  gfx::PointF slotCentre(int idx) const;    // grid-local
  gfx::PointF slotCentreWindow(int idx) const {
    const gfx::PointF c = slotCentre(idx);
    return {c.x + pos.x, c.y + pos.y};
  }
  bool empty(int idx) const { return idx >= 0 && idx < static_cast<int>(slots.size()) && slots[static_cast<std::size_t>(idx)].name.empty(); }
  int filled() const;
  // The length of the run of equal names through the slot (counting it), 0 for a free or missing slot.
  int countHorizontal(int idx) const;
  int countVertical(int idx) const;
  // ClearGrid (CO 1.9): the 4-neighbour group of equal names through `idx`, in the original's visiting order: left, up,
  // self, right, down. The slots are emptied. Nothing happens for a free slot.
  std::vector<ClearedSlot> floodClear(int idx);
  // ShiftRows (CO 1.9): every row moves down one (the bottom row wraps to the top) and the sprites snap to their cells.
  // The wrapped row is then emptied (its ingredients are returned for the caller to remove; no miss is counted) and
  // refilled: for each column RandRange(0, 2N), an entry of `recipe` when below N, else a free cell. `spawn` makes the new
  // ingredient's sprite (it may be empty in tests, then only the name is set).
  std::vector<Ingredient*> shiftRows(script::Rng& rng, const std::vector<std::string>& recipe,
                                     const std::function<Ingredient*(const std::string&)>& spawn);
  void clear();  // every name and pointer forgotten; the sprites are the caller's
};

// CoffeeHopper's counters (CO 1.10): one per recipe entry (duplicates allowed), each with a needle that rises by 4.8 px per
// unit up to 10.
class Hopper {
 public:
  explicit Hopper(std::vector<std::string> names = {}) : names_(std::move(names)), counts_(names_.size(), 0) {}
  // Among the entries with this name the one with the smallest count gets it, ties to the later entry. Unknown: ignored.
  bool add(const std::string& name);
  // All or nothing: when every counter is at least 1, takes one from each and returns true.
  bool release();
  void reset();
  static float needleY(int count) { return 24.0f - 4.8f * static_cast<float>(count < 10 ? count : 10); }
  const std::vector<std::string>& names() const { return names_; }
  const std::vector<int>& counts() const { return counts_; }
  std::vector<Sprite*> needles;  // set by the window; updated after every change
  void updateNeedles();

 private:
  std::vector<std::string> names_;
  std::vector<int> counts_;
};

// CoffeeFactory's match bonus (CO 1.11): the table with a 0 put in front; `n` past the end reads the last entry, and an
// absent (empty) table gives 0.
int matchBonusFor(const std::vector<int>& table, int n);
// The case belt's spawn test: the last case's progress must be past productspacing / producttime.
bool caseSpawnReady(double lastT, double spacingMs, double tripMs);

// One policy answer for the harness's autoshoot: a free slot in rows 1 to rows-2 with the longest run of `name` through it
// (a neighbour of the same name counts, horizontally or vertically), ties to the lowest index; -1 for none.
int chooseSlot(const std::vector<std::string>& names, int cols, int rows, const std::string& name);

// The coffee factory game (CoffeeFactory, docs/design/factories.md 3.5, CO 1). Keys read: productName, productCode (ignored),
// recipe, appearance, matchBonus, countdowntime, gametime, warntime, warnOnMiss, conveyorpath, conveyorcount, conveyortime,
// colorcount, productpath, producttime, productspacing, top_hood, bottom_hood, speedup, maxspeed, columns, rows, gridx, gridy,
// griddx, griddy, gridspeed, gunspeed, missed.
class CoffeeFactory : public FactoryWindow {
 public:
  CoffeeFactory(ui::WindowManager& wm, script::ScriptContext& ctx, FillFn fill);
  ~CoffeeFactory() override;
  std::string_view typeName() const override { return "CoffeeFactory"; }
  void init(const ui::Style& style) override;
  std::string detail() const override;
  std::optional<gfx::PointF> autoAim() const override;

  // ---- for tests and the dump ----
  const CoffeeGrid& grid() const { return grid_; }
  Hopper& hopper() { return hopper_; }
  const IngredientQueue* conveyor() const { return conveyor_.get(); }
  double gridSpeed() const { return grid_.clock.scale; }
  std::size_t shots() const { return gun_.flights.size(); }
  std::size_t casesOnBelt() const { return cases_.list.size(); }
  unsigned delivered() const { return delivered_; }
  std::size_t collectionFlights() const { return collect_.list.size(); }
  std::size_t popups() const { return popups_.size(); }
  gfx::PointF gunPos() const { return gun_.pos; }
  const Sprite* gunSprite() const { return gun_.sprite; }
  const Sprite* marker() const { return marker_; }
  // Test hook: an ingredient arrives at a slot as a shot would (CanAccept, AddIngredient with matching). False when refused.
  bool dropInto(int slot, const std::string& name);
  // Test hooks: one scroll shift now; every ingredient out of the grid.
  void forceShift();
  void emptyGrid();

 protected:
  void enterState(State s) override;
  void resetParts() override;
  void pauseParts() override;
  void restartParts() override;
  bool fire(gfx::PointF target) override;
  void aim(gfx::PointF mouse) override;
  ScalingTask& speedTask() override { return grid_.clock; }
  const char* firstMissKey() const override { return "coffee_first_missed"; }
  const char* gameOverFile() const override { return "ui/chocolate_gameover.lua"; }
  const char* introSound() const override { return "coffee_making_game_intro"; }
  void stepOnce() override;

 private:
  struct Gun {
    ScalingTask clock;
    double flightMs = 250;
    Sprite* sprite = nullptr;
    gfx::PointF pos;
    std::vector<Flight> flights;
  };
  struct Collection {
    ScalingTask clock;
    double flightMs = 250;
    std::vector<Flight> list;  // children of the grid sprite, in grid-local coordinates
  };
  struct CaseBelt {
    struct P {
      Sprite* s;
      double t = 0;
    };
    ScalingTask clock;
    double tripMs = 3000, spacingMs = 1000;
    gfx::Curve path;
    std::vector<AppearanceLayer> look;
    std::vector<P> list;
  };
  struct Bonus {
    TextSprite* s = nullptr;
    gfx::PointF start;
    double ageMs = 0;
  };

  void endRow();
  void tickConveyor();
  void tickGun();
  void tickCases();
  void tickCollection();
  void tickGrid();
  void tickPopups();
  void shiftGrid();
  bool canAccept(const Ingredient& ing) const;
  void addToGrid(Ingredient& ing);
  void collected(std::vector<ClearedSlot> list);
  void addPopup(int bonus, const std::vector<ClearedSlot>& list);
  void setTarget(gfx::PointF mouse);
  void showTarget(bool on);
  void setTargetImage();
  void playAnim(Sprite& s);
  void dropFlights();
  void clearPopups();

  std::vector<std::string> recipe_;
  std::vector<int> matchBonus_;  // 0 prepended
  std::unique_ptr<IngredientQueue> conveyor_;
  Gun gun_;
  CoffeeGrid grid_;
  struct GridTask {
    double rowMs = 5000, speedup = 1.1, maxSpeed = 8, rootY = 0;
  } task_;
  Sprite* gridSprite_ = nullptr;
  Sprite* seam_[2] = {nullptr, nullptr};
  Sprite* marker_ = nullptr;
  Sprite* markerImage_ = nullptr;
  Hopper hopper_;
  Sprite* hopperSprite_ = nullptr;
  gfx::PointF hopperPos_{695, 301};
  Collection collect_;
  CaseBelt cases_;
  std::vector<Bonus> popups_;
  unsigned delivered_ = 0;
  bool markerOn_ = false;
};

}  // namespace choco::game
