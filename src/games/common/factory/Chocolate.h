#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "games/common/Appearance.h"
#include "sdk/TextSprite.h"
#include "games/common/factory/FactoryWindow.h"
#include "games/common/SdkNames.h"

namespace choco::game {

// A table of {x, y} points under `key` as a Bezier path (the factory styles' paths); no table gives an empty curve.
gfx::Curve readStyleCurve(const ui::Style& style, const char* key);

// ---- the pure rules (docs/re/chocolate-factory.md 6 and 7), unit-tested alone ----

// CF 6.3 step 2. Returns the tray's colour after an ingredient of colour `ing` lands on a tray of colour `tray`; when the
// ingredient itself takes the tray's colour (a wild ingredient on a coloured tray) *recolourIngredientTo is that colour,
// else -1.
int trayColorAfter(int tray, int ing, int* recolourIngredientTo);
// CF 6.7: scale * speedup, capped at maxSpeed.
double speedAfterFull(double scale, double speedup, double maxSpeed);
// CF 5.4: half the speed, but never below 1.
double speedAfterMiss(double scale);
// One of the trays the harness's autoshoot can aim at.
// How long the "Nx" popup of a colour match shows, and the part of it over which it fades out (Chocolatier 2).
inline constexpr int kPopupBox = 200, kPopupFlags = gfx::text::kHAlignCenter + gfx::text::kVAlignCenter;
inline constexpr double kMatchPopupMs = 1500, kMatchPopupFadeMs = 500;

struct TrayChoice {
  bool accepts = false;  // canAccept for the ingredient at the front of the conveyor
  int fill = 0;
  int color = 0;
  gfx::PointF pos;
};
// The tray autoshoot aims at: among those that accept, one whose colour is the ingredient's or 0 first, then the fullest,
// ties to the lowest index. -1 for none.
int chooseTray(const std::vector<TrayChoice>& trays, int ingredientColor);

// Everything ChocolateFactory::init reads from the style, and the pictures and parts it builds with, as plain data. The
// defaults are Chocolatier 3's (readSpec fills them from the style); another game's subclass returns its own spec from
// readSpec (docs/design/choc2-minigames.md 2.1, 3).
struct ChocolateSpec {
  FactoryCommon common;
  std::vector<std::string> recipe;
  int colorCount = 1;
  bool rainbow = false;
  gfx::Curve conveyorPath, productPath, trayPath, recyclerPath;
  double conveyorMs = 1200, spacing = 0.2, productMs = 3000;
  double lapMs = 30000, trayEnd = 0, speedup = 1.1, maxSpeed = 8, flightMs = 250, ringMs = 500;
  int trayCount = 5;
  std::vector<AppearanceLayer> look;  // the product's picture
  std::string machine;                // the machine sprite's image
  bool recycler = false, hasRecyclerPath = false;
  double recyclerMs = 20000;
  IngredientArt ingredientArt;
  std::string trayPrefix = "image/tray";  // + the recipe size, then "_ring" for the colour ring
  std::string gunSheet = "image/gun.xml", gunBase = "image/gun_base", sparkleSheet = "image/powerup.xml";
  float gunRotationOffsetDeg = 0;         // added to the gun's aim
  // The parts a game may leave out. Fingers need the machine; the template (the tray on the machine) sits on it unless
  // templatePos gives a place of its own on the window.
  bool pistons = true, machineSprite = true, fingers = true, box = true, timerDial = true;
  std::optional<gfx::PointF> templatePos;
  bool ropes = false;                              // each tray hangs from a pulley at the top by two strands of rope
  float productScale = 0.5f;                       // the product picture on the belt and in the box
  std::string boxOpen = "image/box_open";          // the box at the belt's end: back half, front half (mask)
  std::string boxFront = "image/box_open_mask";
  gfx::PointF boxProductOffset{0, 9};              // the product picture's offset in the front half
  int ringsPerMatch = 0;                  // rings that fly on a colour match; 0: the recipe's size

  // Chocolatier 2's rules (docs/re/choc2-factory-binary.md). Every default is Chocolatier 3's behaviour.
  float hitRadius = 0;                // > 0: a shot hits a tray whose centre is within this of the shot's position (no sweep)
  bool flyOutOfWindow = false;        // a shot covers the window's width per flightMs and misses when it leaves the window
  bool deliverAtWrap = false;         // a full tray is delivered when its phase wraps; trayEnd only starts the machine's turn
  int machineMs = 400;                // the machine's (and its template's) turn
  bool independentProducts = false;   // products on the belt do not wait for the one in front
  double fullBonusStaggerMs = 0;      // when every tray is full, product i starts i * this far along the belt
  bool ringsAreProducts = false;      // a colour match's rings are the product's picture on a Bezier to the box
  int ringCredit = 0;                 // > 0: the score a tick's arriving rings give together (0: 1 each)
  bool slowAbort = false;             // the abort and the end: speed 0.5 then x 0.99, and the end as soon as nothing is in flight
  bool plainSuccessSounds = false;    // `ingredient_success_n` unless the tray is colour matched (colours off: always)
  int bagRounds = 0;                  // > 0: the ingredient bag holds this many of each recipe slot, colours balanced
  gfx::PointF machinePos{695, 301};   // the machine sprite's centre
  std::string fingerImage;            // one picture for both fingers (empty: Chocolatier 3's pair)
  gfx::PointF fingerUpperPos, fingerLowerPos;  // their centres relative to the machine's centre
  float fingerUpperRestDeg = 0, fingerLowerRestDeg = 0;
  gfx::PointF boxOffset{0, 0};        // the box and its front's offset from the end of the product path
  bool matchPopup = false;            // a colour match shows "<N>x" over the tray for matchPopupMs, in the tray's colour
  std::string boxClosed;              // the box's back while no product is on the belt (empty: always boxOpen)
};

// A tray (ChocolateTray, CF 6): a layer-5 sprite `image/tray<N>` that carries its slots' ingredients as children.
class ChocolateTray : public Sprite {
 public:
  ChocolateTray(int index, std::vector<std::string> recipe, double offset, double endpoint);
  std::vector<std::string> recipe;
  std::vector<Ingredient*> slots;  // one per recipe entry, null when empty; the ingredients are children of this sprite
  int fill = 0, color = 0;
  double textMs = 0;   // the "Nx" popup's time left (0: none) and its colour
  int textColor = 0;
  double offset, endpoint, last = 0;
  float radius = 75;
  Sprite* ring = nullptr;
  Sprite* sparkle = nullptr;
  std::vector<Sprite*> pistons;  // the telescoping arm above it, set by ChocolateFactory
  TextSprite* popup = nullptr;   // the "Nx" label, a child drawn after the tray's other children (hidden unless textMs > 0)
  Sprite* pulley = nullptr;      // the ropes variant: the pulley at the top, over the tray
  std::vector<Sprite*> rope;     // and 40 px pieces of rope from it down behind the tray

  static gfx::PointF slotOffset(int i, int n);  // (sin(2 pi i / n) * 50, -cos(2 pi i / n) * 50)
  bool full() const { return fill == static_cast<int>(recipe.size()); }
  // Some slot with this ingredient's name is empty (the colour is ignored).
  bool canAccept(const Ingredient& ing) const { return firstSlotFor(ing) >= 0; }
  int firstSlotFor(const Ingredient& ing) const;
  // Did the phase move across `endpoint` between `last` and `u` (wrap-aware: when u < last the interval is [last,1) and
  // [0,u])?
  bool crossed(double u) const;
  // Recolours every ingredient on the tray, tints the ring and shows it unless c is 0.
  void setColor(int c, Textures& tex);
  // Removes the ingredients: fill 0, colour 0, ring hidden, radius 75. The sparkle plays when `animate` and a player is given.
  void clear();
};

// The chocolate factory game (ChocolateFactory, docs/design/factories.md 3.4, CF 1 to 8). Keys read: productName,
// productCode (ignored), recipe, appearance, machine, countdowntime, gametime, warntime, warnOnMiss, conveyortime,
// conveyorcount, conveyorpath, colorcount, rainbow, producttime, productpath, speedup, maxspeed, traytime, trayend, traycount,
// traypath, gunspeed, ringspeed, missed, recycler, recyclerpath, recyclertime.
class ChocolateFactory : public FactoryWindow {
 public:
  ChocolateFactory(ui::WindowManager& wm, script::ScriptContext& ctx, FillFn fill);
  ~ChocolateFactory() override;
  std::string_view typeName() const override { return "ChocolateFactory"; }
  void init(const ui::Style& style) override;
  std::string detail() const override;
  std::optional<gfx::PointF> autoAim() const override;

  // The spec for this style: Chocolatier 3's keys. A subclass for another game replaces it; init calls it once.
  virtual ChocolateSpec readSpec(const ui::Style& style) const;
  const ChocolateSpec& spec() const { return spec_; }

  // ---- for tests and the dump ----
  const std::vector<ChocolateTray*>& trays() const { return trays_.list; }
  double traySpeed() const { return trays_.clock.scale; }
  std::size_t shots() const { return gun_.flights.size(); }
  std::size_t productsOnBelt() const { return products_.list.size(); }
  std::size_t ringsFlying() const { return rings_.list.size(); }
  const IngredientQueue* conveyor() const { return conveyor_.get(); }
  gfx::PointF gunPos() const { return gun_.pos; }
  const Sprite* gunSprite() const { return gun_.sprite; }
  bool hasRecycler() const { return recycler_.has_value(); }
  gfx::PointF recyclerPos() const { return recycler_->pos; }
  int recycled() const { return recycled_; }
  // Where a sprite's centre lands when it is turned `deg` clockwise about its anchor: `anchor` is the anchor's position,
  // `centre` the centre's offset from the anchor at rest (8.3).
  struct Pivot {
    gfx::PointF anchor, centre;
  };
  static gfx::PointF pivotedPos(const Pivot& pv, float deg);
  const Sprite* upperFinger() const { return armUpper_; }
  const Sprite* lowerFinger() const { return armLower_; }
  const Sprite* trayTemplate() const { return template_; }
  // Test hooks: put a ready ingredient at the gun, or an ingredient straight onto a tray as a hit would.
  void forceHit(ChocolateTray& tray, Ingredient& ing);

 protected:
  void enterState(State s) override;
  void resetParts() override;
  void pauseParts() override;
  void restartParts() override;
  bool fire(gfx::PointF target) override;
  void aim(gfx::PointF mouse) override;
  ScalingTask& speedTask() override { return trays_.clock; }
  const char* firstMissKey() const override { return "choco_first_missed"; }
  const char* gameOverFile() const override { return "ui/chocolate_gameover.lua"; }
  const char* introSound() const override { return "chocolate_making_game_intro"; }
  void stepOnce() override;
  // A tray has just filled with one colour (the matching ingredient is in, the rings fly, the product is made, the tray is
  // cleared). Default nothing. Runs inside the step, like a first-miss pause.
  virtual void onColorMatch() {}
  // A tray that is not yet full has just taken its second ingredient with its colours still matching (Chocolatier 2's hint).
  virtual void onSecondMatch() {}

 private:
  struct Gun {
    ScalingTask clock;
    double flightMs = 250;
    Sprite* sprite = nullptr;
    gfx::PointF pos;
    std::vector<Flight> flights;
  };
  struct TrayTask {
    ScalingTask clock;
    double lapMs = 30000, speedup = 1.1, maxSpeed = 8;
    gfx::Curve path;
    std::vector<ChocolateTray*> list;
  };
  struct Recycler {
    ScalingTask clock;
    double lapMs = 20000;
    gfx::Curve path;
    Sprite* bin = nullptr;
    gfx::PointF pos;
  };
  struct Rings {
    struct Ring {
      Sprite* s;
      gfx::Curve c;
      double t = 0;
    };
    ScalingTask clock;
    double flightMs = 500;
    std::vector<Ring> list;
  };
  struct Belt {
    struct P {
      Sprite* s;
      double t = 0;
    };
    ScalingTask clock;
    double tripMs = 3000;
    gfx::Curve path;
    std::vector<AppearanceLayer> look;
    std::vector<P> list;
  };

  void tickRecycler();
  void tickTrays();
  void placeTrayPistons(ChocolateTray& tray);
  void tickConveyor();
  void tickGun();
  void tickRings();
  void tickProducts();
  void updateBox();
  void endRow();
  void machineStep();
  void setMachineAngle(float deg);
  void hitTray(ChocolateTray& tray, Ingredient& ing);
  void startRings(const ChocolateTray& tray);
  void produce(double t0 = 0);
  void manufacture(ChocolateTray& tray);
  bool anyTrayFull() const;
  bool allTraysFull() const;
  void playAnim(Sprite& s);
  void showPopup(ChocolateTray& tray, int color);
  static void hidePopup(ChocolateTray& t) {
    if (t.popup) t.popup->visible = false;
  }
  void clearTray(ChocolateTray& tray, bool animate);
  void dropFlights();

  ChocolateSpec spec_;
  std::shared_ptr<const IngredientArt> art_;
  std::vector<std::string> recipe_;
  int colorCount_ = 1;
  bool rainbow_ = false;
  std::unique_ptr<IngredientQueue> conveyor_;
  Gun gun_;
  TrayTask trays_;
  std::optional<Recycler> recycler_;
  Rings rings_;
  Belt products_;
  Sprite* machine_ = nullptr;
  Sprite* armUpper_ = nullptr;
  Sprite* armLower_ = nullptr;
  Sprite* template_ = nullptr;
  Sprite* boxBack_ = nullptr;
  float fingerUpperRest_ = 0, fingerLowerRest_ = 0;
  float productScale_ = 0.5f;
  Pivot pivotUpper_, pivotLower_;
  Sprite* gunBase_ = nullptr;
  int machineTime_ = 0;
  int recycled_ = 0;
  gfx::PointF ringTarget_{689, 555};
};

}  // namespace choco::game
