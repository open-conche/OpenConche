#pragma once

#include <memory>
#include <string>
#include <vector>

#include "games/common/MoneyCounter.h"
#include "gfx/Texture.h"
#include "gfx/Types.h"
#include "script/ScriptContext.h"
#include "ui/widgets/InputWidgets.h"

namespace choco::game {

// DookieDropper: one of the six factory gauges on the ledger (docs/re/ledger-and-game-natives.md section 5). A Rollover
// whose factory is its window name. Update() copies the factory's product, production count and stall flag from the
// Lua Player; the ingredient bars are read from Player.supply when they are drawn. The product icon and timer dial are
// drawn here directly, until the sprite tree exists.
//
// Keys read: itemx (18), itemy (86), ingredienty (58), barHeight (42), countx (57), county (86), and the Rollover keys.
class DookieDropper : public ui::RolloverWindow {
 public:
  DookieDropper(ui::WindowManager& wm, script::ScriptContext& ctx);
  std::string_view typeName() const override { return "DookieDropper"; }
  void init(const ui::Style& style) override;
  void postChildrenInit(const ui::Style& style) override;
  void postDraw(ui::DrawContext& ctx) const override;
  std::string detail() const override;

  // Reads Player.factories[name], the product it makes, and Player.rank.
  void update();
  // AnimateProduction: the animation clock goes back to 0.
  void startAnimation() { clockMs_ = 0; }
  // One 50 ms step of the animation (the ledger's task calls it).
  void animate();

  // ---- the gauge maths, public for tests ----
  struct Bar {
    float ratio = 0;        // weeks of supply over the weeks one column holds
    float height = 0;       // min(ratio * barHeight, barHeight)
    gfx::ColorF colour;     // green at 0.75 and over, yellow at 0.5 and over, red below
    gfx::RectI rect;        // x1, y1, width, height in window coordinates
    float centreX = 0;      // where the ingredient icon is centred
  };
  // Weeks of supply one full column stands for, by rank (4, 6, 8, 8, 8, 10, 10; ranks above 6 read as 10).
  static double weeksPerColumn(int rank);
  static gfx::ColorF barColour(double ratio);
  static float barHeightFor(double ratio, int barHeight);
  // The bars for ingredients with the given supplies (weeks) in a window `width` wide.
  static std::vector<Bar> layoutBars(const std::vector<double>& supplies, int rank, int width, int ingredientY,
                                     int barHeight);

  const std::string& product() const { return product_; }
  int production() const { return production_; }
  bool stalled() const { return stall_; }
  int playerRank() const { return rank_; }
  const std::vector<std::string>& ingredients() const { return ingredientNames_; }
  double clockMs() const { return clockMs_; }
  float productScale() const { return productScale_; }
  float handAngle() const { return handAngle_; }  // radians

 private:
  struct Layer {
    std::shared_ptr<gfx::Texture> texture;
    gfx::ColorF tint;
    bool highlight = false;
  };
  struct Icon {
    std::shared_ptr<gfx::Texture> texture;
  };
  void buildProduct(lua_State* L, int productIndex);
  std::shared_ptr<gfx::Texture> load(const std::string& name) const;

  script::ScriptContext& ctx_;
  int itemX_ = 18, itemY_ = 86, ingredientY_ = 58, barHeight_ = 42, countX_ = 57, countY_ = 86;
  std::string product_;
  int production_ = 0;
  bool stall_ = false;
  int rank_ = 1;
  std::vector<std::string> ingredientNames_;
  std::vector<Icon> ingredientIcons_;
  std::vector<Layer> layers_;
  std::shared_ptr<gfx::Texture> timerFace_, timerHand_, idle_;
  double clockMs_ = 2000;
  float productScale_ = 0.25f;
  float handAngle_ = 0;
};

// LedgerWindow: the permanent badge-and-dashboard panel (section 1). It draws nothing itself; it holds the money
// counter, the quest indicator flash and the update dispatch behind RealUpdateLedger. There is one at a time, kept by
// LedgerHost. Not a DSL type.
class LedgerWindow : public ui::BSGWindow {
 public:
  LedgerWindow(ui::WindowManager& wm, script::ScriptContext& ctx);
  std::string_view typeName() const override { return "LedgerWindow"; }

  // PrepareInstance: money and display from Player.money, the dropper list, then update("all").
  void prepareInstance();
  // RealUpdateLedger(mode, data); the modes are matched exactly.
  void update(const std::string& mode, const std::string& data);
  // SetLedgerQuestIndicator(mode).
  void setQuestIndicator(const std::string& mode);
  // AnimateProduction().
  void animateProduction();
  // The 50 ms task step: money counter, quest flash, droppers.
  void animate();

  double displayMoney() const { return money_.display; }
  double targetMoney() const { return money_.target; }
  double deltaMoney() const { return money_.delta; }
  bool flashQuest() const { return flashQuest_; }
  unsigned flashTimer() const { return flashTimer_; }
  const std::string& questImage() const { return questImage_; }  // the indicator image last chosen
  std::size_t dropperCount() const { return droppers_.size(); }

  static constexpr const char* kQuestComplete = "image/badge_button_indicator_complete";
  static constexpr const char* kQuestIncomplete = "image/badge_button_indicator_incomplete";
  static constexpr double kTickMs = 50;

 private:
  void updateMoneyText();
  void showQuestImage(bool complete);
  ui::Window* child(const char* name);
  template <class F>
  void forDroppers(F f);

  script::ScriptContext& ctx_;
  MoneyCounter money_;
  bool flashQuest_ = true;
  unsigned flashTimer_ = 0;
  std::string questImage_;
  std::vector<ui::WindowId> droppers_;
};

}  // namespace choco::game
