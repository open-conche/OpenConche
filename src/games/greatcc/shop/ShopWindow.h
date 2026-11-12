#pragma once

// ShopWindow (docs/re/greatcc-shop-simulation.md 1-2, 8): the window of scripts/game.lua that runs a level. It is a sprite window
// that loads each level on the script's LevelSetup() (the level file, the location's background and stations, the HUD, the
// music), runs the level's clock and end through the tick of ShopTick, and ends the level with the end-of-level dialog.
// The shell holds the zone graph (made once) and the player and stations of each level; the chocolates, machines, customers,
// upgrades, tutorials and the plot come with their own slices and leave seams here (each named where it is left).

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "games/greatcc/core/LevelData.h"
#include "games/greatcc/core/ZoneGraph.h"
#include "games/greatcc/natives/ScopedNatives.h"
#include "games/greatcc/scene/VSpriteWindow.h"
#include "games/greatcc/core/CustomerLogic.h"
#include "games/greatcc/shop/CustomerVendor.h"
#include "games/greatcc/shop/ShopHost.h"
#include "games/greatcc/shop/ShopTick.h"
#include "script/LuaState.h"

namespace choco::games::greatcc {

class GreatCCWorld;
class RActiveTextLayer;
class VBackground;
class VChocolate;
class VChocolateVendor;
class VCustomer;
class VShopHud;
class VQuestWindow;
class VPlayer;
class VStation;

class ShopWindow : public VSpriteWindow, private ShopTickEnv, private ShopHost {
 public:
  // Gives `fn` a view of the table the window was made from: the one place the location tables are read, so a test can supply a
  // MapStyle where the game supplies the Lua table.
  using StyleSource = std::function<void(const std::function<void(const ui::Style&)>& fn)>;

  ShopWindow(ui::WindowManager& wm, script::ScriptContext& ctx);
  ~ShopWindow() override;
  std::string_view typeName() const override { return "ShopWindow"; }
  // The base, then: gameLoaded := true, this window becomes its modal's default focus, the window's own style is kept for the
  // level loads, the HUD and the floating text layer are found (the layer moves in front of this window).
  void postChildrenInit(const ui::Style& style) override;
  // OnTaskAnimate: ShopTick, then the sprite update unless the level ended.
  bool onAnimate() override;
  // The sprites, then (config `zones_debug` = 1) the zone points and connections over them.
  void draw(ui::DrawContext& ctx) const override;
  // Ctrl+C opens the developer console (ungated, as in the original); every other key goes to the sprite window's debug toggles.
  bool onKeyDown(const ui::KeyEvent& ev) override;
  std::string detail() const override;

  // ---- the script natives ----
  // LevelSetup: the whole load of a level (docs/re/greatcc-shop-simulation.md 2.2).
  void levelSetup();
  // ReadyForPrelevel: the next tick shows the pre-level dialog (when the level has one) or the diary.
  void readyForPrelevel() { tick_.prelevelPending = true; }
  // DoCheatGame(type, param) (ShopWindow::DoCheat): 0 fail, 1 pass, 2 master (the first three move the level's total and end the
  // level), 3 add money, 4 go to level n, 5 set the highest level complete. Returns the message the cheats dialog shows. The
  // caller checks cheatmode.
  std::string doCheat(int type, const std::string& param);
  // The developer console (docs/re/greatcc-debug-tools.md 1): scripts/console.lua is run in a temporary child of the modal and its
  // `console_window` moves into the modal; closeConsole removes the first one the modal holds and returns the key focus to the shop.
  void openConsole();
  void closeConsole();
  // DoCommand(line) (ShopWindow::DoCommand): one of the ten console commands; unknown words, missing arguments and unknown sprites do
  // nothing, and nothing is ever reported back.
  void doCommand(const std::string& line);
  // DoDebug (ShopWindow::Debug): zones/zones.xml is read again into the zone graph (zones and points reused by name, the connections
  // rebuilt, the current path dropped, `disabled` kept). Nothing before the first level load made the graph.
  void doDebug();
  // Config `zones_debug` == 1, read when the zone graph is made (the first level load); the setter is for tests.
  bool zonesDebug() const { return zonesDebug_; }
  void setZonesDebug(bool on) { zonesDebug_ = on; }
  // GetEndlevelText(status).
  std::string endlevelText(int status) const;
  // ShopWindow::UpgradeComplete (VPrelevelWindow::Close calls it when the upgrade shop closes): the diary may follow (the plot flag),
  // the vendor takes the new upgrades, the tutorials act on this window's sprites again, every station counts its machines again and
  // the backdrop is initialised again.
  void upgradeComplete();
  // ShopWindow::RenderPrelevel (the upgrade shop's picture of the shop): the customers are hidden, the sprites are drawn at half
  // size with the scene's corner at (342, 93) from the origin of the window being drawn (`ctx.origin`: the prelevel window, bouncing), and the customers show again.
  void renderPrelevel(ui::DrawContext& ctx);
  // VUpgradeManager::IsActive for the sprites that ask through the host.
  bool upgradeActive(const std::string& name, const std::string& category) const override;

  // ---- state ----
  const LevelData* level() const { return haveLevel_ ? &level_ : nullptr; }
  bool endless() const { return tick_.endless; }
  ShopTickState& tickState() { return tick_; }
  const ShopTickState& tickState() const { return tick_; }
  // The endless arrival schedule: the level clock's ms at which the next customer is due (0x1c8).
  int nextEndlessSpawnMs() const { return endlessNextSpawnMs_; }
  VShopHud* hud() const { return hud_; }
  RActiveTextLayer* textLayer() const { return textLayer_; }
  // ShopWindow::LevelIsComplete: the level was over, or (story) its time is up.
  bool levelIsComplete() const override;
  // The station by its lower-cased name (station01..station17), or null.
  VStation* station(const std::string& name) const;
  const std::map<std::string, std::weak_ptr<VStation>>& stations() const { return stations_; }
  std::shared_ptr<VBackground> background() const { return background_.lock(); }
  // The avatar of the level (null before the first load).
  std::shared_ptr<VPlayer> playerSprite() const { return player_.lock(); }
  // The zone graph (made by the first level load from zones/zones.xml; null before).
  const std::shared_ptr<ZoneGraph>& zones() const { return zones_; }
  // Tests: a graph without a level load.
  void setZones(std::shared_ptr<ZoneGraph> z) { zones_ = std::move(z); }
  // ShopWindow::GetChocolateVendor: made by the first level load that has a `chocolates` table, reloaded by each.
  const std::shared_ptr<VChocolateVendor>& vendor() const { return vendor_; }
  // ShopWindow::HasCenterTable and GetStationByZonePoint (the characters call them through ShopHost).
  bool centerTable() const { return hasCenterTable(); }
  std::shared_ptr<VStation> stationAt(const ZonePoint* point) const { return stationByZonePoint(point); }
  // The scripted customers that have not arrived and the ones in the shop (in the order they came).
  std::size_t pendingCustomers() const { return pending_.size(); }
  std::size_t activeCustomers() const { return active_.size(); }
  const std::vector<std::shared_ptr<VCustomer>>& activeList() const { return active_; }
  const std::vector<std::shared_ptr<VCustomer>>& pendingList() const { return pending_; }
  // The characters the level files name (the posse, the location's locals) and the pieces every customer copies.
  const CustomerVendor& roster() const { return customers_; }
  // GetAvailableCustomerStation(name): the station of that name when it has nobody, else (for an empty or unknown name) a counter
  // picked by a draw from the free ones (station01..06); null when it is taken.
  std::shared_ptr<VStation> availableCustomerStation(const std::string& name);
  // FindOrderWithLayer(c0, c1, layer, option): the first of the two chocolates that a customer's wanted chocolate is heading toward
  // with that layer (the counters in order), else `c0` when there is one, else `c1`.
  std::shared_ptr<VChocolate> findOrderWithLayer(const std::shared_ptr<VChocolate>& c0, const std::shared_ptr<VChocolate>& c1, int layer,
                                                  int option) const override;
  // AddCombo(type) and OrderComplete(price, position, customer) of the original (the customers and the deliveries call them through
  // ShopHost): the chain bonus, and the payment with its tip.
  void addCombo(int type) override;
  void orderComplete(float price, float x, float y, const VCustomer* customer) override;
  // The level's chain of events and the goal popups shown so far (0 none, 1 goal, 2 mastery).
  const ComboChain& comboChain() const { return combo_; }
  int goalPopupState() const { return goalPopup_; }
  // The tests supply the style source; the game's is made at postChildrenInit.
  void setStyleSource(StyleSource s) { styleSource_ = std::move(s); }
  // DoEndLevel (called by the tick; public so the end of a level can be driven directly).
  void doEndLevel() override;
  // The name the script gets for a finished story level: endlevel_fail, endlevel_pass or endlevel_master.
  static const char* levelPassName(int result);

 private:
  // ShopHost
  std::shared_ptr<VPlayer> player() const override { return player_.lock(); }
  bool hasCenterTable() const override;
  std::shared_ptr<VStation> stationByZonePoint(const ZonePoint* point) const override;
  bool tutorialInEffect() const override;
  std::shared_ptr<VChocolateVendor> chocolateVendor() const override;
  bool isEndless() const override { return tick_.endless; }
  int levelElapsedMs() const override;
  std::shared_ptr<VStation> stationByName(const std::string& name) const override;
  std::shared_ptr<VStation> stationByNumber(int n) const override;
  void waitOnZeppelin(bool wait) override { tick_.waitingOnZeppelin = wait; }
  void addSceneSprite(const std::shared_ptr<VSprite>& sprite) override;
  const std::vector<OrderInfo>* zeppelinOrders() const override { return haveLevel_ && !tick_.endless ? &level_.zeppelin : nullptr; }

  // ShopTickEnv
  int windowElapsedMs() override;
  bool hasLevel() override { return haveLevel_; }
  bool tutorialInEffect() override { return static_cast<const ShopHost&>(*this).tutorialInEffect(); }
  bool showTutorials() override;
  bool hasTimer() override;
  float percentElapsed() override;
  bool levelInLockDown(float percent) override { return level_.inLockDown(percent); }
  void setLockDown(bool on) override { sprites().setLockDown(on); }
  void startLevel() override;
  void updateCustomers() override;
  void updateHud() override;
  bool levelNeedsPrelevel() override { return level_.needPrelevel; }
  void showPrelevel() override;
  bool levelNeedsPlot() override { return level_.needPlot(); }
  void launchPlotDialog() override;
  int timerElapsedMs() override;
  int timerDurationMs() override;
  bool activeCustomersEmpty() override { return active_.empty(); }
  bool pendingCustomersEmpty() override { return pending_.empty(); }
  void updateEndlessMode() override;
  unsigned angryThisLevel() override;
  unsigned angryLimit() override;

  void withWindowStyle(const std::function<void(const ui::Style&)>& fn) const;
  void withLocation(const std::string& location, const std::function<void(const ui::Style&)>& fn) const;
  void oneTimeInit();
  void loadChocolates(const ui::Style& location);
  void loadStations(const ui::Style& location);
  void loadCharacters(const ui::Style& location);
  void loadBackground(const ui::Style& location);
  void preloadSounds();
  // The popups of the goal and the mastery: the first time the level's total passes each, at most once.
  void checkGoalPopups(float total);
  void loadQuests(GreatCCWorld& world);

  ScopedNatives natives_;
  StyleSource styleSource_;
  ShopTickState tick_;
  LevelData level_;
  bool haveLevel_ = false;
  bool oneTimeDone_ = false;
  bool zonesDebug_ = false;
  VShopHud* hud_ = nullptr;
  VQuestWindow* questWindow_ = nullptr;
  RActiveTextLayer* textLayer_ = nullptr;
  CustomerVendor customers_;
  std::vector<std::shared_ptr<VCustomer>> pending_, active_;  // 0x158, 0x160
  std::map<std::string, std::weak_ptr<VStation>> stations_;
  std::weak_ptr<VBackground> background_;
  std::weak_ptr<VPlayer> player_;
  std::shared_ptr<ZoneGraph> zones_;
  std::shared_ptr<VChocolateVendor> vendor_;  // 0x180: made once, reloaded at each level
  ComboChain combo_;  // 0x1bc, 0x1c0
  int goalPopup_ = 0;  // 0x1d8
  int endlessNextSpawnMs_ = 0, endlessLastCheckMs_ = 0;  // 0x1c8, 0x1cc
};

}  // namespace choco::games::greatcc
