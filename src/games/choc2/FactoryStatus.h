#pragma once

#include <memory>
#include <string>
#include <vector>

#include "games/common/Textures.h"
#include "gfx/Texture.h"
#include "gfx/Types.h"
#include "script/ScriptContext.h"
#include "ui/Window.h"

namespace choco::game::choc2 {

// Chocolatier 2's `FactoryStatus{x y w h bars products colorLoads colorFull colorMedium colorTrouble}` in the ledger's
// Factories tab, over `image/ledger_factory` (six 83 px columns). Drawing is from the Mac binary (docs/re/choc2-ledger-binary.md
// section 8, TFactoryStatus::Draw 0x1323e).
//
// FACT: each `bars` entry is {left, top, right, bottom, fill}, `fill` being the weeks of stock over `weeksPerColumn`. A bar
// fills from its bottom up to min(fill, 1) of its height in one colour: fill >= 1 Loads, > 0.75 Full, > 0.5 Medium, else
// Trouble. Each `products` entry is {x, y, productName, rate, typeName, recipeSize}, (x, y) the centre of the column's circle:
// it draws the product `item/<name>` falling into the machine, `image/machine_<type>_small`, the `image/tray<N>_mini` rotating
// by t * 2 pi, and for a factory with rate 0 a blinking `image/missed_ingredient` at alpha 0.65 in the second half of each
// second. t = (elapsed % period) / period, the period from the rate; the fall and the rotation run only while StartProductionAnim
// is on (a trip), the blink always. INFERENCE (marked UNKNOWN there): the 4th texture argument of the falling item is its alpha.
class FactoryStatus : public ui::Window {
 public:
  struct Bar {
    gfx::RectF rect;  // left, top, width, height
    double fill = 0;
  };
  struct Product {
    gfx::PointF at;
    std::string name, type;
    double rate = 0;
    int recipeSize = 0;
  };
  // The animation period in ms for a rate (0 for none): 1000 for 1..20, 667 for 21..40, 500 for 41..60, else 400.
  static int periodMs(double rate);
  // The StartProductionAnim / StopProductionAnim flag: one global, as the binary's static.
  static void setAnimating(bool on) { animating_ = on; }
  static bool animating() { return animating_; }
  // The bands, by index into the four colours: 0 Loads, 1 Full, 2 Medium, 3 Trouble.
  static int band(double fill);
  // The height of the filled part of a bar of `rect`'s height.
  static float fillHeight(const gfx::RectF& rect, double fill);

  static constexpr double kTickMs = 50;

  FactoryStatus(ui::WindowManager& wm, script::ScriptContext& ctx) : Window(wm), tex_(wm, &ctx.log) {}
  std::string_view typeName() const override { return "FactoryStatus"; }
  void init(const ui::Style& style) override;
  void draw(ui::DrawContext& ctx) const override;
  void postChildrenInit(const ui::Style& style) override;
  bool onAnimate() override;
  std::string detail() const override;

  const std::vector<Bar>& bars() const { return bars_; }
  const std::vector<Product>& products() const { return products_; }
  const gfx::ColorF& color(int band) const { return colors_[band]; }
  double elapsedMs() const { return elapsedMs_; }

 private:
  std::vector<Bar> bars_;
  std::vector<Product> products_;
  gfx::ColorF colors_[4];
  // One set per product, null when the picture is missing.
  struct Pictures {
    std::shared_ptr<gfx::Texture> tray, machine, item;
  };
  std::vector<Pictures> pics_;
  std::shared_ptr<gfx::Texture> missed_;
  Textures tex_;
  double elapsedMs_ = 0;  // advances 50 ms per tick whether or not the flag is on
  static inline bool animating_ = false;
};

}  // namespace choco::game::choc2
