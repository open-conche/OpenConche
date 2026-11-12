#include "games/greatcc/shop/ShopWindow.h"

#include <climits>
#include <cstdio>
#include <cstdlib>

#include "games/greatcc/GreatCCWorld.h"
#include "games/greatcc/core/AssetsXml.h"
#include "games/greatcc/core/EndlessLogic.h"
#include "games/greatcc/debug/DebugCommand.h"
#include "games/greatcc/meta/VQuestWindow.h"
#include "games/greatcc/shop/VBackground.h"
#include "games/greatcc/shop/FloatingTexts.h"
#include "games/greatcc/shop/VChocolate.h"
#include "games/greatcc/shop/VCustomer.h"
#include "games/greatcc/shop/VConveyor.h"
#include "games/greatcc/shop/VDelivery.h"
#include "games/greatcc/shop/VDisplay.h"
#include "games/greatcc/shop/VSauceDrink.h"
#include "games/greatcc/shop/VPlayer.h"
#include "games/greatcc/shop/VShopHud.h"
#include "games/greatcc/shop/VStation.h"
#include "games/greatcc/ui/RActiveTextLayer.h"
#include "games/greatcc/ui/RTimer.h"
#include "games/greatcc/ui/Transitions.h"
#include "script/LuaStyle.h"
#include "script/Loaders.h"
#include "script/RunFile.h"
#include "ui/WindowManager.h"

namespace choco::games::greatcc {

namespace {

constexpr double kTickMs = 16;     // OBJECT_UPDATE_INTERVAL
constexpr int kPreLevelMs = 1000;  // LevelSetup's countdown

ShopWindow* ownerShop(lua_State* L) {
  script::ScriptContext& ctx = script::contextOf(L);
  return dynamic_cast<ShopWindow*>(ctx.wm.window(ScopedNatives::ownerOf(L)));
}
int luaLevelSetup(lua_State* L) {
  if (auto* w = ownerShop(L)) w->levelSetup();
  return 0;
}
int luaReadyForPrelevel(lua_State* L) {
  if (auto* w = ownerShop(L)) w->readyForPrelevel();
  return 0;
}
int luaGetEndlevelText(lua_State* L) {
  const int status = static_cast<int>(luaL_checknumber(L, 1));
  auto* w = ownerShop(L);
  const std::string text = w ? w->endlevelText(status) : std::string();
  lua_pushlstring(L, text.data(), text.size());
  return 1;
}

int luaDoCheatGame(lua_State* L) {
  const int type = static_cast<int>(luaL_checknumber(L, 1));
  const char* param = luaL_optstring(L, 2, "");
  script::ScriptContext& ctx = script::contextOf(L);
  std::string out;
  if (!cheatModeOn(ctx)) {
    ctx.log.write("[greatcc] DoCheatGame refused: cheatmode is off");
    out = "Invalid Cheat";
  } else if (auto* w = ownerShop(L)) {
    out = w->doCheat(type, param);
  }
  lua_pushlstring(L, out.data(), out.size());
  return 1;
}

int luaDoCommand(lua_State* L) {
  const char* line = luaL_optstring(L, 1, "");
  if (auto* w = ownerShop(L)) w->doCommand(line);
  return 0;
}
int luaDoDebug(lua_State* L) {
  if (auto* w = ownerShop(L)) w->doDebug();
  return 0;
}

std::string lowered(std::string s) { return VObject::lower(std::move(s)); }

// What the upgrade manager does to the level's sprites (UpgradeSprites): they are looked up by name in the shop's sprite manager at
// each call, through the window's id, so a manager that outlives the window finds nothing and does nothing.
class ShopUpgradeSprites : public UpgradeSprites {
 public:
  ShopUpgradeSprites(script::ScriptContext& ctx, ui::WindowId window) : ctx_(ctx), window_(window) {}
  bool exists(const std::string& name) override { return find(name) != nullptr; }
  void init(const std::string& name) override {
    if (VObject* o = find(name)) o->init();
  }
  void switchAsset(const std::string& name, const std::string& asset, const std::string& state) override {
    if (VObject* o = find(name)) o->switchAsset(asset, state, "default");
  }
  void switchState(const std::string& name, const std::string& state) override {
    if (VObject* o = find(name)) o->switchState(state, "default");
  }

 private:
  VObject* find(const std::string& name) const {
    auto* shop = dynamic_cast<ShopWindow*>(ctx_.wm.window(window_));
    return shop ? dynamic_cast<VObject*>(shop->sprites().findSpriteByName(name)) : nullptr;
  }
  script::ScriptContext& ctx_;
  ui::WindowId window_;
};

}  // namespace

ShopWindow::ShopWindow(ui::WindowManager& wm, script::ScriptContext& ctx) : VSpriteWindow(wm, ctx), natives_(ctx), customers_(ctx) {
  natives_.bindOwned("LevelSetup", &luaLevelSetup, id());
  natives_.bindOwned("ReadyForPrelevel", &luaReadyForPrelevel, id());
  natives_.bindOwned("GetEndlevelText", &luaGetEndlevelText, id());
  natives_.bindOwned("DoCheatGame", &luaDoCheatGame, id());
  natives_.bindOwned("DoCommand", &luaDoCommand, id());
  natives_.bindOwned("DoDebug", &luaDoDebug, id());
  // DoMetrics is a logged stub: it only writes analytics (design 9).
}

// The window is destroyed with its modal, after the world has died at shutdown: it touches the world only through worldOf.
ShopWindow::~ShopWindow() {
  if (GreatCCWorld* w = worldOf(ctx_)) {
    w->settings().gameLoaded = false;
    if (w->shopWindowId() == id()) w->setShopWindowId(0);
    // the plot and the quests are the level screen's (the original's destructor clears both)
    w->plots().clear();
    w->quests().clear();
    // the tutorials of a screen that is gone: nothing runs and nothing refers to the window
    if (w->tutorials().levelWindow() == id()) {
      w->tutorials().setLevelWindow(0);
      w->tutorials().resetTutorials();
    }
  }
}

void ShopWindow::postChildrenInit(const ui::Style& style) {
  VSpriteWindow::postChildrenInit(style);
  if (GreatCCWorld* w = worldOf(ctx_)) {
    w->settings().gameLoaded = true;
    w->setShopWindowId(id());
  }
  for (ui::Window* p = parent(); p; p = p->parent())
    if (auto* modal = dynamic_cast<ui::ModalWindow*>(p)) {
      modal->defaultFocus = id();
      break;
    }
  if (!styleSource_) {
    // The location tables are read at each level load, long after this call: keep the window's table in the registry.
    if (const auto* lua = dynamic_cast<const script::LuaStyle*>(&style)) {
      auto ref = std::make_shared<script::LuaRef>(lua->tableRef());
      script::ScriptContext* ctx = &ctx_;
      if (ref->valid())
        styleSource_ = [ctx, ref](const std::function<void(const ui::Style&)>& fn) {
          lua_State* L = ctx->lua.L();
          const int top = lua_gettop(L);
          ref->push(L);
          {
            script::LuaStyle view = script::LuaStyle::plainView(L, lua_gettop(L), &ctx->lua);
            fn(view);
          }
          lua_settop(L, top);
        };
    }
  }
  hud_ = dynamic_cast<VShopHud*>(find("shop_hud"));
  questWindow_ = dynamic_cast<VQuestWindow*>(find("quest_window"));
  if (parent())
    if (ui::Window* t = parent()->find("text_window")) {
      textLayer_ = dynamic_cast<RActiveTextLayer*>(t);
      if (textLayer_) textLayer_->bringToFront();  // SetWindowDepth(textLayer, this): the floating texts draw over the sprites
    }
}

void ShopWindow::withWindowStyle(const std::function<void(const ui::Style&)>& fn) const {
  if (styleSource_) styleSource_(fn);
}

void ShopWindow::withLocation(const std::string& location, const std::function<void(const ui::Style&)>& fn) const {
  withWindowStyle([&](const ui::Style& root) {
    if (auto t = root.table(("location_" + location).c_str())) fn(*t);
  });
}

void ShopWindow::oneTimeInit() {
  // OneTimeInit: the zone controller reads zones/zones.xml once (a level load never reads it again, so a route left over from the
  // last level stays in the graph); the customer vendor loads the posse (the names of the twelve characters) from the window's
  // `posse` table; a story game reads locations/quests.lua and the player's quests, an endless one has none.
  zones_ = std::make_shared<ZoneGraph>();
  zonesDebug_ = std::strtol(ctx_.config.get("zones_debug").c_str(), nullptr, 10) == 1;  // GetIntConfig == 1, read once
  if (const auto xml = ctx_.fs.readText("zones/zones.xml")) {
    if (!zones_->load(*xml)) ctx_.log.write("[greatcc] zones/zones.xml did not parse; the avatar has no zones");
  } else {
    ctx_.log.write("[greatcc] zones/zones.xml is missing; the avatar has no zones");
  }
  withWindowStyle([&](const ui::Style& root) {
    if (auto posse = root.table("posse")) customers_.loadPosse(*posse);
  });
  // the tutorials table and the sprite window the tutorials act on
  if (GreatCCWorld* world = worldOf(ctx_)) {
    loadQuests(*world);
    world->tutorials().setLevelWindow(id());
    withWindowStyle([&](const ui::Style& root) {
      if (auto tutorials = root.table("tutorials")) world->tutorials().loadFromTable(*tutorials);
    });
  }
}

// ---- LevelSetup ----

// locations/quests.lua is run in the script state and its `quests` table read (the original runs it in a script of its own); the
// global is dropped again.
void ShopWindow::loadQuests(GreatCCWorld& world) {
  if (tick_.endless) {
    world.quests().clear();
    return;
  }
  const auto text = ctx_.fs.readText("locations/quests.lua");
  if (!text) {
    ctx_.log.write("[greatcc] locations/quests.lua is missing; there are no quests");
    return;
  }
  lua_State* L = ctx_.lua.L();
  const int top = lua_gettop(L);
  if (script::loadBuffer(L, *text, "locations/quests.lua") != 0 || lua_pcall(L, 0, 0, 0) != 0) {
    ctx_.log.write(std::string("[greatcc] locations/quests.lua: ") + lua_tostring(L, -1));
  } else {
    lua_getglobal(L, "quests");
    if (lua_istable(L, -1)) {
      script::LuaStyle view = script::LuaStyle::plainView(L, lua_gettop(L), &ctx_.lua);
      world.quests().loadFromTable(view);
    }
    lua_pushnil(L);
    lua_setglobal(L, "quests");
  }
  lua_settop(L, top);
}

void ShopWindow::preloadSounds() {
  GreatCCWorld* world = worldOf(ctx_);
  if (!world) return;
  const auto xml = ctx_.fs.readText("assets.xml");
  if (!xml) {
    ctx_.log.write("[greatcc] assets.xml is missing");
    return;
  }
  for (const AssetSound& s : parseAssetSounds(*xml)) world->sound().preload(s.file);
}

void ShopWindow::levelSetup() {
  GreatCCWorld* world = worldOf(ctx_);
  if (!world) return;
  Settings& settings = world->settings();
  ValueManager& values = world->values();
  LevelManager& levels = world->levels();
  const bool endless = settings.mode != 0;
  tick_.endless = endless;
  world->sound().purgeSounds();
  sprites().setNow(manager().modalNow(*this));
  sprites().reset();  // VSpriteWindow::Reset
  preloadSounds();    // assets.xml
  if (!oneTimeDone_) {
    oneTimeDone_ = true;
    oneTimeInit();
  }
  pending_.clear();
  active_.clear();
  combo_.reset();
  tick_.levelOverStamp = 0;
  goalPopup_ = 0;
  values.reload();  // re-reads values.xml: every value goes back to its default
  if (endless) {
    // The difficulty (not the mode number) picks the modifier.
    double m = 0;
    if (settings.difficulty == 1) m = values.getReal("endless_medium_modifier", 0);
    else if (settings.difficulty == 2) m = values.getReal("endless_hard_modifier", 0);
    values.setValue("endless_diff_modifier", static_cast<float>(m), false);
  }
  // The level number: story plays the current level; an endless run starts at the first level of the current location.
  const int current = levels.currentLevel();
  int number = current;
  if (current == 0) {
    levels.setCurrentLevel(1);
    number = 1;
  } else if (endless && current % 10 != 1) {
    const int base = current % 10 == 0 ? current - 10 : current;
    number = base - base % 10 + 1;
    levels.setCurrentLevel(number);
  }
  // SetCurrentLevel on the quest manager, then VQuestWindow::LevelInit: the panel shows only while a quest is active.
  world->quests().setCurrentLevel(number);
  if (questWindow_) questWindow_->levelInit();
  char path[48];
  std::snprintf(path, sizeof path, "levels/level%02i.xml", number);
  const auto xml = ctx_.fs.readText(path);
  LevelData::Result r;
  if (xml) r = LevelData::load(number, *xml, settings.fixBugs, level_);
  else r.error = "the file is missing";
  haveLevel_ = true;  // the level object exists even when the file did not load (CSLevel::Create makes it before reading)
  if (!r.parsed) {
    ctx_.log.write(std::string("[greatcc] ") + path + " did not load: " + r.error + "; the level has no customers and no length");
    level_ = LevelData{};
    level_.number = number;
    level_.needPrelevel = number > 3;
  }
  if (r.fixedF1 && ctx_.warned.insert("greatcc-F1").second)
    ctx_.log.write(std::string("[fix] F1 ") + path + ": an <order> that is never closed (the yachtsman's, the last customer's) is closed before the <station> that follows it");
  if (r.partial && ctx_.warned.insert(std::string("greatcc-partial-") + path).second)
    ctx_.log.write(std::string("[greatcc] ") + path + " does not parse (" + r.error + "); the part before the error is used");
  if (r.fixedF2 && ctx_.warned.insert("greatcc-F2").second)
    ctx_.log.write("[fix] F2 kTruffler: an order's shape is read as kTruffle (the original takes it as unspecified)");
  if (!endless) {
    values.setValue("current_worth", static_cast<float>(levels.currentScore()), true);
    if (level_.chocolateDecay != 0) values.setValue("chocolate_decay_rate", 100.0f / static_cast<float>(level_.chocolateDecay), false);
    if (level_.customerDecay > 0) values.setValue("level_customer_decay", level_.customerDecay + 1.0f, false);
  }
  // The upgrades of the location (VUpgradeManager::SetCurrentLevel with the level): the file is read again, what the player owns is
  // loaded, and the upgrades the level has reached are granted or unlocked. The sprites an upgrade is bound to are looked for
  // below, once they exist.
  {
    UpgradeManager& upgrades = world->upgrades();
    upgrades.setNoSave(endless);
    upgrades.setSprites(std::make_shared<ShopUpgradeSprites>(ctx_, id()));
    const auto upgradesXml = ctx_.fs.readText("locations/" + level_.location + "/upgrades.xml");
    if (!upgradesXml) ctx_.log.write("[greatcc] locations/" + level_.location + "/upgrades.xml is missing; the location has no upgrades");
    upgrades.setCurrentLevel(level_.location, number, std::string_view(upgradesXml ? *upgradesXml : std::string()), -1,
                             levels.currentLevel(), !endless, level_.upgrades);
    if (upgradesXml && upgrades.loadFailed())
      ctx_.log.write("[greatcc] locations/" + level_.location + "/upgrades.xml did not parse");
  }
  // The tutorials start over for this level (they are given the level's number).
  world->tutorials().setLevelWindow(id());
  world->tutorials().resetTutorials();
  world->tutorials().setCurrentLevel(number);
  // The location's content: the plot pages, the chocolates, the stations, the locals' names and the background.
  stations_.clear();
  background_.reset();
  player_.reset();
  customers_.clearLocals();
  world->plots().clear();  // ClearPlotItems, then the location's plot table when it has one (LoadPlotItems)
  withLocation(level_.location, [&](const ui::Style& location) {
    if (auto plot = location.table("plot")) world->plots().loadFromTable(*plot);
    loadChocolates(location);
    loadStations(location);
    loadCharacters(location);
    loadBackground(location);
  });
  world->upgrades().resolveBoundObjects();
  world->plots().setCurrentLevel(level_.plots);  // the level's pages are the active ones
  if (vendor_) {
    // UpdateCapability(true); a story level limits the layers of an order (maxlayers), the counter that names chocolates restarts
    vendor_->updateCapability(true, [this](const char* name) { return upgradeActive(name, "station"); });
    if (!endless) vendor_->setMaxLayers(level_.maxLayers);
    vendor_->resetChocolateCount();
  }
  if (!endless) {
    // The scripted customers of the level, in file order, that the vendor knows; the others give nothing.
    for (const auto& data : level_.customers)
      if (std::shared_ptr<VCustomer> customer = customers_.getCustomer(data->type, this)) {
        customer->setCustomerData(data);
        pending_.push_back(std::move(customer));
      }
  } else {
    endlessLastCheckMs_ = timerElapsedMs();
    endlessNextSpawnMs_ = kEndlessFirstSpawnMs;
  }
  if (hud_) hud_->levelInit(&level_, endless);
  if (textLayer_) textLayer_->clearText();
  withLocation(level_.location, [&](const ui::Style& location) {
    const std::string music = location.stringOr("music_01");
    if (!music.empty()) world->sound().playMusic(music, true, true);
  });
  sprites().updateObjects();
  // InitMetrics: the level's game states start over.
  {
    GameStateStore& gs = world->gameStates();
    gs.set("LevelNumber", std::to_string(number));
    gs.set("PlayTime_" + std::to_string(gs.nextPlay()), "1");
    gs.set("TutorialEnd", "-1");
    for (const char* key : {"TotalCustomers", "TotalCustomersLost", "OrderCompleted", "MenuDeliveryChainPointsEarned",
                            "ServiceChainPointsEarned", "CashPickupChainPointsEarned", "DisplayUsed", "TrashUsed", "TotalClicks",
                            "TotalValidClicks"})
      gs.set(key, "0");
    gs.set("TrashUsedItemID", "-1");
  }
  // Arm the start: a second of pre-level time with the sprites locked and the soft pause on, the clock not running.
  tick_.startCountdownMs = kPreLevelMs;
  tick_.waitingToStart = true;
  if (hud_ && hud_->timer()) hud_->timer()->timer().running = false;
  sprites().setLockDown(true);
  sprites().softPause(true);
  tick_.waitingOnZeppelin = false;
  manager().startWindowAnimation(id(), kTickMs);
}

void ShopWindow::loadChocolates(const ui::Style& location) {
  auto table = location.table("chocolates");
  if (!table) return;
  if (!vendor_) {
    vendor_ = VChocolateVendor::create(ctx_);
    vendor_->setUpgradeProbe([this](const std::string& name, const std::string& category) { return upgradeActive(name, category); });
  }
  const StyleChain chain(*table, nullptr);
  vendor_->loadFromTable(chain);
}

std::shared_ptr<VChocolateVendor> ShopWindow::chocolateVendor() const { return vendor_; }

void ShopWindow::loadStations(const ui::Style& location) {
  stations_.clear();
  auto table = location.table("stations");
  if (!table) return;
  const StyleChain group(*table, nullptr);
  table->forEachEntry([&](const std::string& key, const ui::Style* t, const std::optional<std::string>&) {
    if (!t || key.compare(0, 8, "station_") != 0) return;
    const StyleChain sc(*t, &group);
    const std::string name = lowered(t->stringOr("name"));
    auto station = VStation::create(ctx_, 0, static_cast<int>(t->numberOr("stationType", 0)));
    station->setHost(this);  // the machines read their upgrades and the vendor through it while the table loads
    station->loadFromTable(sc);
    if (zones_) station->resolvePoints(*zones_);
    station->init();
    stations_[name] = station;
    sprites().addObject(station);
  });
  if (zones_) zones_->disablePoint("avatar", "middle_center", true);
}

void ShopWindow::loadCharacters(const ui::Style& location) {
  // the bubble, menu, money and orderables every customer copies, then the locals (what a level file's customers are looked up by)
  if (auto objects = location.table("objects")) customers_.loadObjects(*objects);
  if (auto locals = location.table("customers")) customers_.loadLocals(*locals);
  // the avatar: made with the zone graph, loaded, initialised and added
  if (auto table = location.table("player")) {
    // F5: the graph outlives the level, and a route cut off by the level's end would be walked by the new avatar.
    if (GreatCCWorld* world = worldOf(ctx_); world && world->settings().fixBugs && zones_ && !zones_->pathEmpty()) {
      zones_->clearPath();
      ctx_.log.write("[fix] F5: dropped the previous level's unfinished route");
    }
    const StyleChain chain(*table, nullptr);
    auto avatar = VPlayer::create(ctx_, zones_);
    avatar->setHost(this);
    avatar->loadFromTable(chain);
    avatar->init();
    // a decayed chocolate in her hands is dropped (VPlayer::Update)
    avatar->setDropCheck([](const VCarriable& item) {
      const auto* choc = dynamic_cast<const VChocolate*>(&item);
      return choc && choc->hasDecayed();
    });
    player_ = avatar;
    sprites().addObject(avatar);
  }
}

bool ShopWindow::hasCenterTable() const {
  const VStation* s = station("station16");
  return s && s->visible();
}

std::shared_ptr<VStation> ShopWindow::stationByZonePoint(const ZonePoint* point) const {
  if (!point) return nullptr;
  for (const auto& [name, weak] : stations_)
    if (auto s = weak.lock(); s && s->hasPoint(point)) return s;
  return nullptr;
}

void ShopWindow::loadBackground(const ui::Style& location) {
  auto table = location.table("background");
  if (!table) return;
  const StyleChain chain(*table, nullptr);
  auto bg = VBackground::create(ctx_);
  background_ = bg;
  sprites().addObject(bg);
  bg->loadFromTable(chain);
  bg->init();
}

std::shared_ptr<VStation> ShopWindow::stationByName(const std::string& name) const {
  const auto it = stations_.find(name);
  return it == stations_.end() ? nullptr : it->second.lock();
}

std::shared_ptr<VStation> ShopWindow::stationByNumber(int n) const {
  char key[16];
  std::snprintf(key, sizeof key, "station%02i", n);
  return stationByName(key);
}

void ShopWindow::addSceneSprite(const std::shared_ptr<VSprite>& sprite) {
  if (sprite && sprite->manager() != &sprites()) sprites().addObject(sprite);
}

VStation* ShopWindow::station(const std::string& name) const {
  auto it = stations_.find(name);
  if (it == stations_.end()) return nullptr;
  auto s = it->second.lock();
  return s.get();
}

// ---- the tick ----

bool ShopWindow::onAnimate() {
  claimKeyFocus();
  sprites().setNow(manager().modalNow(*this));
  if (runShopTick(tick_, *this) == TickResult::Stopped) return false;
  return VSpriteWindow::onAnimate();
}

int ShopWindow::windowElapsedMs() { return static_cast<int>(sprites().getElapsed()); }

bool ShopWindow::showTutorials() {
  GreatCCWorld* w = worldOf(ctx_);
  return w && w->tutorials().shouldShowTutorials();
}

bool ShopWindow::tutorialInEffect() const {
  GreatCCWorld* w = worldOf(ctx_);
  return w && w->tutorials().tutorialInEffect();
}

int ShopWindow::levelElapsedMs() const { return hud_ && hud_->timer() ? hud_->timer()->timer().elapsedMs : 0; }

bool ShopWindow::hasTimer() { return hud_ && hud_->timer(); }
float ShopWindow::percentElapsed() { return hud_->timer()->timer().percentElapsed(); }
int ShopWindow::timerElapsedMs() { return hasTimer() ? hud_->timer()->timer().elapsedMs : 0; }
int ShopWindow::timerDurationMs() { return hasTimer() ? hud_->timer()->timer().durationMs : 0; }

void ShopWindow::updateEndlessMode() {
  GreatCCWorld* w = worldOf(ctx_);
  if (!tick_.endless || !w || !hasTimer()) return;
  const int elapsed = hud_->timer()->timer().elapsedMs;
  if (kEndlessUpdateIntervalMs >= static_cast<unsigned>(elapsed - endlessLastCheckMs_)) return;
  endlessLastCheckMs_ = elapsed;
  const int level = w->levels().currentLevel();
  char key[32];
  std::snprintf(key, sizeof key, "endless_threshold_%i", level % 10);
  const float threshold = static_cast<float>(w->values().getReal(key, 0));
  if (!endlessThresholdReached(level, threshold, static_cast<unsigned>(elapsed))) return;
  // the next pseudo-level: its upgrades unlock, the machines can make more
  w->levels().setCurrentLevel(level + 1);
  w->upgrades().setCurrentLevel(level + 1, std::nullopt, -1, -1);
  if (vendor_) {
    // VChocolateVendor::UpdateMaxLayers(minutes): three layers from endless_three_layers, one below endless_two_layers, else two
    const float minutes = static_cast<float>(static_cast<unsigned>(elapsed) / 60000);
    if (static_cast<float>(w->values().getReal("endless_three_layers", 0)) <= minutes) vendor_->setMaxLayers(3);
    else vendor_->setMaxLayers(minutes < static_cast<float>(w->values().getReal("endless_two_layers", 0)) ? 1 : 2);
    vendor_->updateCapability(true, [this](const char* name) { return upgradeActive(name, "station"); });
  }
  w->tutorials().checkTutorial("tutorial_endless_2");
}

void ShopWindow::startLevel() {
  if (hud_ && hud_->timer()) hud_->timer()->resumeFromNow();
  sprites().softPause(false);
  sprites().setLockDown(false);
  // the tutorial trigger of the endless run
  if (GreatCCWorld* w = worldOf(ctx_)) w->tutorials().checkTutorialsByTrigger("endless_start", "");
}

void ShopWindow::updateCustomers() {
  if (sprites().softPaused()) return;
  if (!tick_.endless) {
    // the arrivals: each pending customer whose counter is free and whose time has come (the draw for a counter is made every tick
    // for a customer that names none, whether or not its time has come)
    if (hasTimer() && hud_->timer()->timer().running && hud_->timer()->timer().elapsedMs < hud_->timer()->timer().durationMs) {
      for (std::size_t i = 0; i < pending_.size();) {
        const std::shared_ptr<VCustomer> customer = pending_[i];
        if (!customer || !customer->customerData()) {
          pending_.erase(pending_.begin() + static_cast<std::ptrdiff_t>(i));
          continue;
        }
        const std::shared_ptr<VStation> station = availableCustomerStation(customer->customerData()->station);
        if (!station || !customer->customerData()->inRange(percentElapsed())) {
          ++i;
          continue;
        }
        sprites().addObject(customer);
        active_.push_back(customer);
        customer->setActive(station, 100.0f);
        if (GreatCCWorld* w = worldOf(ctx_)) {
          w->sound().playSfx("audio/sfx/customer_arrives.ogg", false);
          w->gameStates().set("++TotalCustomers");
        }
        pending_.erase(pending_.begin() + static_cast<std::ptrdiff_t>(i));
      }
    }
  } else if (hasTimer() && endlessNextSpawnMs_ <= hud_->timer()->timer().elapsedMs) {
    // the endless arrivals: a random character at a random free counter, then the next one is scheduled whether or not it came
    if (const std::shared_ptr<VStation> station = availableCustomerStation("")) {
      if (const std::shared_ptr<VCustomer> customer = customers_.getRandomCustomer(this)) {
        sprites().addObject(customer);
        active_.push_back(customer);
        customer->setActive(station, 100.0f);
      }
    }
    if (GreatCCWorld* w = worldOf(ctx_)) {
      ValueManager& v = w->values();
      const int now = hud_->timer()->timer().elapsedMs;
      const float modifier = v.getProduct("customer_delay_modifier", static_cast<float>(now / 1000), true);
      endlessNextSpawnMs_ = endlessNextSpawnMs(now, ctx_.rng.next32(), v.getUint("customer_delay", 0), v.getUint("customer_delay_variance", 0), modifier);
    }
  }
  // the customers in the shop: their update, and the ones that are done leave
  for (std::size_t i = 0; i < active_.size();) {
    const std::shared_ptr<VCustomer> customer = active_[i];
    customer->update(sprites().softPaused());
    if (!customer->shouldDelete()) {
      ++i;
      continue;
    }
    if (const std::shared_ptr<VStation> station = customer->station()) station->detachCustomer();
    sprites().removeObject(*customer);
    for (std::size_t k = 0; k < active_.size(); ++k)
      if (active_[k] == customer) {
        active_.erase(active_.begin() + static_cast<std::ptrdiff_t>(k));
        break;
      }
  }
}

std::shared_ptr<VStation> ShopWindow::availableCustomerStation(const std::string& name) {
  std::shared_ptr<VStation> found;
  if (!name.empty()) {
    const auto it = stations_.find(name);
    if (it != stations_.end()) found = it->second.lock();
  }
  if (!found) {
    std::vector<std::shared_ptr<VStation>> free;
    for (int i = 1; i <= 6; ++i) {
      char key[16];
      std::snprintf(key, sizeof key, "station%02i", i);
      const auto it = stations_.find(key);
      const std::shared_ptr<VStation> s = it == stations_.end() ? nullptr : it->second.lock();
      if (s && s->stationType() == kStationCounter && !s->customer()) free.push_back(s);
    }
    if (!free.empty()) found = free[ctx_.rng.next32() % free.size()];
  }
  if (found && !found->customer()) return found;
  return nullptr;
}

std::shared_ptr<VChocolate> ShopWindow::findOrderWithLayer(const std::shared_ptr<VChocolate>& c0, const std::shared_ptr<VChocolate>& c1,
                                                           int layer, int option) const {
  for (int i = 1; i <= 6; ++i) {
    char key[16];
    std::snprintf(key, sizeof key, "station%02i", i);
    const auto it = stations_.find(key);
    const std::shared_ptr<VStation> s = it == stations_.end() ? nullptr : it->second.lock();
    if (!s || s->stationType() != kStationCounter) continue;
    const std::shared_ptr<VCustomer> guest = s->customer();
    if (!guest || !guest->wanted()) continue;
    const auto wanted = std::dynamic_pointer_cast<VChocolate>(guest->wanted());
    if (!wanted) continue;
    if (wanted->compareWithLayer(c0.get(), layer, option)) return c0;
    if (wanted->compareWithLayer(c1.get(), layer, option)) return c1;
  }
  return c0 ? c0 : c1;
}

// ---- scoring ----

void ShopWindow::checkGoalPopups(float total) {
  GreatCCWorld* w = worldOf(ctx_);
  if (!w || !textLayer_) return;
  const int current = w->levels().currentLevel();
  gfx::text::TextSystem* text = ctx_.wm.textSystem();
  if (goalPopup_ < 2 && total >= static_cast<float>(w->levels().mastery(current))) {
    textLayer_->adoptText(makeGoalText(text, 2, ctx_.strings.sdkLookup("reached_master")));
    goalPopup_ = 2;
    w->sound().playSfx("audio/sfx/master.ogg", false);
    w->values().setValue("mastery_this_level", 1.0, false);
    return;
  }
  if (goalPopup_ < 1 && static_cast<float>(w->levels().goal(current)) <= total) {
    textLayer_->adoptText(makeGoalText(text, 1, ctx_.strings.sdkLookup("reached_goal")));
    goalPopup_ = 1;
    w->sound().playSfx("audio/sfx/goal.ogg", false);
  }
}

void ShopWindow::addCombo(int type) {
  GreatCCWorld* w = worldOf(ctx_);
  if (!w || !combo_.add(type)) return;
  ValueManager& values = w->values();
  const float bonus = values.getProduct(comboBonusValue(type), static_cast<float>(combo_.counter), false);
  values.addToValue("current_level_total", bonus, 0, false);
  const float total = static_cast<float>(values.getReal("current_level_total", 0));
  if (tick_.endless) {
    values.addToValue("current_worth", bonus, 0, false);
    values.addToValue("endless_level_counter", bonus, 0, false);
  }
  if (textLayer_) {
    int x = 400, y = 300;
    if (const std::shared_ptr<VPlayer> p = player_.lock()) {
      x = static_cast<int>(p->position().x);
      y = static_cast<int>(p->position().y) - 75;
    }
    textLayer_->adoptText(makeComboText(ctx_.wm.textSystem(), bonus, combo_.counter + 1, ctx_.strings.sdkLookup(comboLabelKey(type)), x, y));
    if (!tick_.endless) checkGoalPopups(total);
  }
  if (const char* name = comboMultiplierValue(type))
    if (values.getInt(name, 0) < combo_.counter + 1) values.setValue(name, static_cast<double>(combo_.counter + 1), false);
  if (const char* state = comboPointsState(type)) {
    const std::string held = w->gameStates().get(state);
    w->gameStates().set(state, std::to_string(std::atoi(held.c_str()) + static_cast<int>(bonus)));
  }
}

void ShopWindow::orderComplete(float price, float x, float y, const VCustomer* customer) {
  GreatCCWorld* w = worldOf(ctx_);
  if (!w) return;
  ValueManager& values = w->values();
  const float sale = values.getProduct("salemodifier", price, false);
  const float tip = customer ? values.getProduct("tipmodifier", tipBase(customer->patience(), customer->tip()), false) : 0.0f;
  const float amount = sale + tip;
  values.addToValue("current_level_total", amount, 0, false);
  const float total = static_cast<float>(values.getReal("current_level_total", 0));
  if (tick_.endless) {
    values.addToValue("current_worth", amount, 0, false);
    values.addToValue("endless_level_counter", amount, 0, false);
  }
  if (textLayer_) {
    textLayer_->adoptText(makePaymentText(ctx_.wm.textSystem(), std::floor(amount), static_cast<int>(x), static_cast<int>(y)));
    if (!tick_.endless) checkGoalPopups(total);
  }
  addCombo(kComboPayment);
}

void ShopWindow::updateHud() {
  if (hud_) hud_->updateHud(sprites().softPaused(), tutorialInEffect(), tick_.endless);
}

void ShopWindow::showPrelevel() {
  doLuaString(ctx_, "DoModal ( 'scripts/prelevel.lua' );");
  level_.needPrelevel = false;  // once per level load
}

bool ShopWindow::upgradeActive(const std::string& name, const std::string& category) const {
  const GreatCCWorld* world = worldOf(ctx_);
  return world && world->upgradeActive(name, category);
}

void ShopWindow::upgradeComplete() {
  if (vendor_) {
    tick_.plotPending = true;
    vendor_->updateCapability(true, [this](const char* name) { return upgradeActive(name, "station"); });
  }
  if (GreatCCWorld* world = worldOf(ctx_)) world->tutorials().setLevelWindow(id());
  for (const auto& [name, station] : stations_)
    if (auto st = station.lock()) st->updateAvailable();
  if (auto bg = background_.lock()) bg->init();
}

void ShopWindow::renderPrelevel(ui::DrawContext& ctx) {
  for (const auto& c : active_)
    if (c) c->setVisible(false);
  sprites().draw(ctx.renderer, {ctx.origin.x + 342.0f, ctx.origin.y + 93.0f}, 0.5f);  // relative to the prelevel window (the viewport push)
  for (const auto& c : active_)
    if (c) c->setVisible(true);
}

void ShopWindow::launchPlotDialog() {
  if (GreatCCWorld* world = worldOf(ctx_)) world->plots().launchPlotDialog();
}

unsigned ShopWindow::angryThisLevel() {
  GreatCCWorld* w = worldOf(ctx_);
  return w ? w->values().getUint("angry_this_level", 0) : 0;
}
unsigned ShopWindow::angryLimit() {
  GreatCCWorld* w = worldOf(ctx_);
  return w ? w->values().getUint("angry_customer_limit", 5) : 5;
}

bool ShopWindow::levelIsComplete() const {
  if (tick_.levelOverStamp != 0) return true;
  if (tick_.endless || !hud_ || !hud_->timer()) return false;
  const LevelTimer& t = hud_->timer()->timer();
  return !(t.elapsedMs < t.durationMs);
}

// ---- the end of a level ----

const char* ShopWindow::levelPassName(int result) {
  return result == 2 ? "endlevel_master" : (result == 1 ? "endlevel_pass" : "endlevel_fail");
}

void ShopWindow::doEndLevel() {
  GreatCCWorld* world = worldOf(ctx_);
  if (!world) return;
  ValueManager& values = world->values();
  LevelManager& levels = world->levels();
  char text[200];
  if (!tick_.endless) {
    const int number = level_.number;
    const int statusBefore = levels.status(number);
    const int levelBefore = levels.currentLevel();
    const int total = values.getInt("current_level_total", 0);
    const LevelManager::Outcome outcome = levels.levelComplete(number, total);
    const int result = outcome.result;
    if (result != 0) {
      if (statusBefore == 0) values.addToValue("completed_" + level_.location, 1, 0, true);
      if (result == 2 && statusBefore != 2) values.addToValue("num_levels_mastered", 1, 0, true);
      world->quests().postLevelProcess();  // the quests of the level are checked and their rewards given
      values.addToValue("total_worth", outcome.delta, 0, true);
      values.addToValue("current_worth", outcome.delta, 0, true);
      levels.updateCurrentScore(values.getUint("current_worth", 0));
      levels.incrementLevel();
      const int now = levels.currentLevel();
      if (now % 10 == 1 && levels.highestComplete() < now) values.setValue("do_map_transition", 1, false);
    }
    values.saveAll();  // even for a failed level
    if (levelBefore > 59 && result != 0) world->achievements().checkAll("endgame");
    world->achievements().checkAll("adventure");
    // ShopWindow::LogScore(total_worth, highest level complete): only a passed level is logged; the game data is the highest level
    // and the medal xml goes with it.
    if (result != 0)
      world->logScore(ctx_, values.getInt("total_worth", 0), std::to_string(levels.highestComplete()), world->achievements().medalXml());
    std::snprintf(text, sizeof text, "DisplayDialog { 'scripts/endlevel.lua', level_pass = '%s', level_num=%i, level_status=%i };",
                  levelPassName(result), number, result);
    doLuaString(ctx_, text);
    // DoMetrics(result == 0, result == 2) only writes analytics.
  } else {
    const unsigned seconds = static_cast<unsigned>(timerElapsedMs()) / 1000;
    values.setValue("endless_time", static_cast<double>(seconds), false);
    world->achievements().checkAll("endless");
    world->achievements().checkAll("adventure");
    // ShopWindow::LogScore(current_worth, seconds): the game data is the play time in seconds, no medals.
    world->logScore(ctx_, values.getInt("current_worth", 0), std::to_string(seconds), std::string());
    ctx_.log.write("[greatcc] LogScore endless score=" + std::to_string(values.getInt("current_worth", 0)) + " seconds=" + std::to_string(seconds) +
                   " difficulty=" + std::to_string(world->settings().difficulty) + " table=" + world->settings().hiscoreMode());
    const std::string shown = hasTimer() ? hud_->timer()->timer().text() : std::string();
    std::snprintf(text, sizeof text,
                  "DisplayDialog { 'scripts/endlevel.lua', level_pass = 'endlevel_endless', time = '%s', level_status=0 };", shown.c_str());
    doLuaString(ctx_, text);
  }
}

std::string ShopWindow::doCheat(int type, const std::string& param) {
  GreatCCWorld* world = worldOf(ctx_);
  if (!world) return "Invalid Cheat";
  ValueManager& values = world->values();
  LevelManager& levels = world->levels();
  const int n = std::atoi(param.c_str());
  const int level = levels.currentLevel();
  char buf[96];
  switch (type) {
    case 0:
      if (!tick_.endless) {
        const int goal = levels.goal(level);
        values.setValue("current_level_total", std::min(values.getInt("current_level_total", 0), goal - 1), false);
      } else {
        values.addToValue("angry_this_level", 10, 0, false);
        values.setValue("served_this_level", 5, false);
      }
      break;
    case 1: {
      const int goal = levels.goal(level);
      const int mastery = levels.mastery(level);
      int total = std::min(values.getInt("current_level_total", 0), mastery - 1);
      if (total <= goal) total = goal;
      values.setValue("current_level_total", total, false);
      break;
    }
    case 2: {
      const int mastery = levels.mastery(level);
      values.setValue("current_level_total", std::max(values.getInt("current_level_total", 0), mastery), false);
      break;
    }
    case 3:
      if (n > 0) {
        values.addToValue("current_level_total", n, 0, true);
        if (!tick_.endless) {
          levels.updateCurrentScore(levels.currentScore() + n);
          values.addToValue("current_worth", n, 0, true);
          values.addToValue("total_worth", n, 0, true);
        }
      }
      std::snprintf(buf, sizeof buf, "Added %i to current and lifetime total", n);
      return buf;
    case 4:
      if (n < 1 || n > 60) return "Invalid Level";
      levels.setCurrentLevel(n);
      doLuaString(ctx_, "DoModal('scripts/loading.lua');");
      return "";
    case 5:
      if (n < 1 || n > 60) return "Invalid Level";
      levels.setHighestComplete(n, true);
      std::snprintf(buf, sizeof buf, "Set highest level complete to %i", n);
      return buf;
    default: return "Invalid Cheat";
  }
  doEndLevel();  // fail, pass and master end the level at once
  return "";
}

std::string ShopWindow::endlevelText(int status) const {
  if (tick_.endless) return ctx_.strings.sdkLookup("endlevel_endless_blurb_1");
  // the quests' text comes first when there is any
  if (GreatCCWorld* world = worldOf(ctx_)) {
    const std::string quests = world->quests().postLevelText();
    if (!quests.empty()) return quests;
  }
  return ctx_.strings.sdkLookup(status == 0 ? "endlevel_blurb_2" : "endlevel_blurb_1");
}

std::string ShopWindow::detail() const {
  char buf[256];
  std::snprintf(buf, sizeof buf, " level=%d endless=%d countdown=%d waiting=%d over=%d pending=%zu stations=%zu", haveLevel_ ? level_.number : 0,
                tick_.endless ? 1 : 0, tick_.startCountdownMs, tick_.waitingToStart ? 1 : 0, tick_.levelOverStamp, pending_.size(),
                stations_.size());
  std::string out = VSpriteWindow::detail() + buf;
  out += " active=" + std::to_string(active_.size());
  if (const auto p = player_.lock()) {
    // the avatar: where she is, which way she faces, whether she walks, her queue and destination, the nodes she has reached
    std::snprintf(buf, sizeof buf, " player=%.1f,%.1f face=%d idle=%s moving=%d queue=%zu dest=%s trail=", p->position().x, p->position().y,
                  p->facing(), p->currentIdle() ? p->currentIdle()->name().c_str() : "-", p->moving() ? 1 : 0, p->queue().size(), p->destination() ? p->destination()->name.c_str() : "-");
    out += buf;
    const auto& trail = p->nodeTrail();
    for (std::size_t i = 0; i < trail.size(); ++i) out += (i ? "," : "") + trail[i];
    if (trail.empty()) out += "-";
    // what she holds: slot A then slot B
    auto held = [](const std::shared_ptr<VCarriable>& c) {
      if (!c) return std::string("-");
      if (const auto* choc = dynamic_cast<const VChocolate*>(c.get())) return choc->name() + choc->toString();
      return c->name();
    };
    out += " hands=" + held(p->handA()) + "|" + held(p->handB());
  }
  // the customers in the shop: name at its counter, state, mood, patience, the money owed, the orders still to come, what it wants now
  if (!active_.empty()) {
    std::string customers;
    for (const auto& c : active_) {
      const auto st = c->station();
      std::string wanted = "-";
      if (c->wanted()) {
        const auto* choc = dynamic_cast<const VChocolate*>(c->wanted().get());
        wanted = c->wanted()->type() + (choc ? choc->toString() : std::string());
      }
      std::snprintf(buf, sizeof buf, "@%s:s=%d,m=%d,p=%.1f,$=%.0f,o=%zu,w=", st ? st->name().c_str() : "-", c->state(), c->mood(),
                    static_cast<double>(c->patience()), static_cast<double>(c->price()), c->orders().size());
      customers += std::string(customers.empty() ? "" : ";") + c->name() + buf + wanted;
    }
    out += " customers=" + customers;
  }
  if (const VSprite* h = sprites().hover()) out += " hover=" + h->name();
  {
    GreatCCWorld* w = worldOf(ctx_);
    std::snprintf(buf, sizeof buf, " total=%.1f combo=%d/%d popup=%d", w ? w->values().getReal("current_level_total", 0) : 0.0, combo_.lastType,
                  combo_.counter, goalPopup_);
    out += buf;
  }
  // the machines that are on: state, choice, locked, processed and what they hold
  std::string machines;
  for (const auto& [name, weak] : stations_) {
    const auto station = weak.lock();
    if (!station) continue;
    for (const auto& [key, m] : station->machines()) {
      if (!m || !m->enabled()) continue;
      std::snprintf(buf, sizeof buf, "%s/%s:s=%d,o=%d,L=%d,P=%d,held=", name.c_str(), key.c_str(), m->state(), m->option(), m->locked() ? 1 : 0,
                    m->processed() ? 1 : 0);
      machines += std::string(machines.empty() ? "" : ";") + buf + (m->chocolate() ? m->chocolate()->toString() : std::string("-"));
      if (const auto* sd = dynamic_cast<const VSauceDrink*>(m.get())) {
        // a sauce or drink machine: the cooldown left and the bottle it has
        std::snprintf(buf, sizeof buf, ",cd=%d,bottle=%s", sd->cooldownMs(), sd->bottle() ? sd->bottle()->type().c_str() : "-");
        machines += buf;
      }
      if (const auto* d = dynamic_cast<const VDelivery*>(m.get())) {
        // the delivery: its own state, the countdown, the order's patience and what is wanted, the airship's state
        std::snprintf(buf, sizeof buf, ",ds=%d,t=%d,pat=%.1f,want=", d->deliveryState(), d->timerMs(), static_cast<double>(d->patience()));
        machines += buf;
        machines += d->wanted() ? d->wanted()->toString() : std::string("-");
        std::snprintf(buf, sizeof buf, ",zep=%d", d->zeppelin() ? d->zeppelin()->state() : -1);
        machines += buf;
      }
    }
  }
  if (!machines.empty()) out += " machines=" + machines;
  // the upgrades the scene shows: the backdrop's and the first counter's tabletop art, and the shop's own (not hidden) upgrades that are on
  if (const auto bg = background_.lock()) out += " floor=" + bg->currentState();
  if (const auto it = stations_.find("station01"); it != stations_.end())
    if (const auto st = it->second.lock()) out += " table=" + st->currentState();
  if (const GreatCCWorld* w = worldOf(ctx_)) {
    std::string bought;
    for (const char* category : {"aesthetic", "station", "gameplay"})
      for (const auto& [name, u] : w->upgrades().typeMap(category))
        if (u && !u->hidden && u->isActive()) bought += std::string(bought.empty() ? "" : ",") + name + (u->state == kUpgradePreview ? "(preview)" : "");
    if (!bought.empty()) out += " bought=" + bought;
  }
  // the conveyor: the angle of each item on the belt, front first
  if (const auto it = stations_.find("station17"); it != stations_.end())
    if (const auto belt = std::dynamic_pointer_cast<VConveyor>(it->second.lock()); belt && belt->visible()) {
      out += " belt=";
      std::string items;
      for (const auto& item : belt->items()) {
        std::snprintf(buf, sizeof buf, "%.3f@%s", static_cast<double>(item.theta), item.zone.c_str());
        items += std::string(items.empty() ? "" : "|") + buf;
      }
      out += items.empty() ? "-" : items;
    }
  // the display case: shown or not, how many things are on it, and which
  if (const auto it = stations_.find("station16"); it != stations_.end())
    if (const auto table = std::dynamic_pointer_cast<VDisplay>(it->second.lock())) {
      std::snprintf(buf, sizeof buf, " display=%d,%d/%d,", table->visible() ? 1 : 0, table->count(), table->capacity());
      out += buf;
      std::string items;
      for (const auto& item : table->slots()) {
        if (!item) continue;
        const auto* choc = dynamic_cast<const VChocolate*>(item.get());
        items += std::string(items.empty() ? "" : "|") + item->type() + (choc ? choc->toString() : std::string());
      }
      out += items.empty() ? "-" : items;
    }
  if (GreatCCWorld* w = worldOf(ctx_)) {
    const std::string trash = w->gameStates().get("TrashUsed");
    if (!trash.empty() && trash != "0") out += " trash=" + trash + "," + (w->gameStates().get("TrashUsedItemID").empty() ? std::string("-") : w->gameStates().get("TrashUsedItemID"));
  }
  return out;
}

}  // namespace choco::games::greatcc
