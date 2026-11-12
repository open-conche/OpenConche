#include "games/greatcc/ui/VWorldMap.h"

#include <cstdio>
#include <cstdlib>

#include "games/greatcc/GreatCCWorld.h"
#include "games/greatcc/scene/Effects.h"
#include "games/greatcc/ui/Transitions.h"
#include "ui/WindowManager.h"

namespace choco::games::greatcc {

namespace {

VWorldMap* ownerMap(lua_State* L) {
  script::ScriptContext& ctx = script::contextOf(L);
  return dynamic_cast<VWorldMap*>(ctx.wm.window(ScopedNatives::ownerOf(L)));
}
int luaResetMap(lua_State* L) {
  if (auto* w = ownerMap(L)) w->resetMap();
  return 0;
}
int luaShowCurrentLocation(lua_State* L) {
  if (auto* w = ownerMap(L)) w->showCurrentLocation();
  return 0;
}
int luaPlayTransition(lua_State* L) {
  const char* loc = luaL_checkstring(L, 1);
  if (auto* w = ownerMap(L)) w->playTransition(loc);
  return 0;
}
int luaDoCheatMap(lua_State* L) {
  const int code = static_cast<int>(luaL_checknumber(L, 1));
  const char* arg = luaL_checkstring(L, 2);
  auto* w = ownerMap(L);
  script::ScriptContext& ctx = script::contextOf(L);
  std::string out;
  if (!cheatModeOn(ctx)) {
    ctx.log.write("[greatcc] DoCheatMap refused: cheatmode is off");
    out = "Invalid Cheat";
  } else if (w) {
    out = w->doCheat(code, arg);
  }
  lua_pushlstring(L, out.data(), out.size());
  return 1;
}

std::string lowered(std::string s) { return VObject::lower(std::move(s)); }

}  // namespace

VWorldMap::VWorldMap(ui::WindowManager& wm, script::ScriptContext& ctx) : VSpriteWindow(wm, ctx), natives_(ctx) {
  natives_.bindOwned("ResetMap", &luaResetMap, id());
  natives_.bindOwned("DoCheatMap", &luaDoCheatMap, id());
  natives_.bindOwned("PlayTransition", &luaPlayTransition, id());
  natives_.bindOwned("ShowCurrentLocation", &luaShowCurrentLocation, id());
}

void VWorldMap::postChildrenInit(const ui::Style& style) {
  if (auto pins = style.table("pins")) {
    const StyleChain group(*pins, nullptr);
    pins->forEachEntry([&](const std::string& key, const ui::Style* t, const std::optional<std::string>&) {
      if (!t) return;
      const StyleChain pc(*t, &group);
      auto pin = PushPin::create(ctx_);
      pin->loadFromTable(pc);
      pin->init();
      sprites().addObject(pin);
      pins_[lowered(key)] = pin;
    });
  }
  if (auto paths = style.table("paths")) {
    paths->forEachEntry([&](const std::string& key, const ui::Style* t, const std::optional<std::string>&) {
      if (!t) return;
      std::vector<DotInfo>& list = paths_[lowered(key)];
      for (int i = 1; i <= t->size(); ++i) {
        auto e = t->element(i);
        if (!e) continue;
        list.push_back({{static_cast<float>(e->numberOr("x", 0)), static_cast<float>(e->numberOr("y", 0))}, e->stringOr("state", "")});
      }
    });
  }
  dotTemplate_ = VObject::create(ctx_, "", "");
  if (auto dot = style.table("dot")) {
    const StyleChain dc(*dot, nullptr);
    dotTemplate_->loadFromTable(dc);
  }
  VSpriteWindow::postChildrenInit(style);
  for (ui::Window* p = parent(); p; p = p->parent())
    if (auto* modal = dynamic_cast<ui::ModalWindow*>(p)) {
      modal->defaultFocus = id();
      break;
    }
  GreatCCWorld* world = worldOf(ctx_);
  if (world && world->settings().mode != 0 && parent())
    if (ui::Window* options = parent()->find("btn_options")) options->setPos(341, options->rect().y);
}

PushPin* VWorldMap::currentLocationPin() const {
  GreatCCWorld* world = worldOf(ctx_);
  if (!world) return nullptr;
  const int highest = world->levels().highestComplete();
  for (const auto& [key, pin] : pins_) {
    const int d = highest + 1 - pin->baseLevel();
    if (pin && d >= 0 && d < 10) return pin.get();
  }
  return nullptr;
}

void VWorldMap::showCurrentLocation() {
  if (PushPin* p = currentLocationPin()) p->setOpen(true);
}

void VWorldMap::resetMap() {
  showCurrentLocation();
  for (const auto& [key, pin] : pins_)
    if (pin) pin->init();
}

void VWorldMap::playTransition(const std::string& location) {
  dotIndex_ = 0;
  transition_.clear();
  timerMs_ = 0;
  if (paths_.count(location) == 0) return;
  transition_ = location;
  timerMs_ = 1;
  sprites().addExclusiveSprite(nullptr);  // the map ignores the mouse while the trail plays
}

bool VWorldMap::onAnimate() {
  sprites().setNow(manager().modalNow(*this));
  GreatCCWorld* world = worldOf(ctx_);
  if (dotTemplate_ && !transition_.empty() && timerMs_ > 0 && world) {
    timerMs_ -= static_cast<int>(sprites().getElapsed());
    if (timerMs_ < 1) {
      const auto& path = paths_[transition_];
      if (dotIndex_ < static_cast<int>(path.size())) {
        const DotInfo& info = path[static_cast<std::size_t>(dotIndex_)];
        auto dot = VObject::create(ctx_, "", "");
        dotTemplate_->copyTo(*dot);
        dot->switchAsset("dot", info.state, "default");
        dot->setPosition(info.pos);
        sprites().addObject(dot);
        dot->addEffect(std::make_shared<AlphaEffect>(true, 1.0f, 200, -1.0f, 0));
        dot->addEffect(std::make_shared<GlowEffect>(1.0f, 400, 1.0f, 0.0f));
        ++dots_;
      } else {
        if (PushPin* pin = currentLocationPin()) {
          pin->setVisible(true);
          arrivalGlow_ = std::make_shared<GlowEffect>(1.0f, 700, 1.0f, 0.0f);
          pin->addEffect(std::make_shared<AlphaEffect>(true, 1.0f, 400, -1.0f, 0));
          pin->addEffect(arrivalGlow_);
        }
        transition_.clear();
        dotIndex_ = 0;
        timerMs_ = 0;
        sprites().clearExclusiveSprites();
      }
      ++dotIndex_;
      timerMs_ = world->values().getInt("map_dot_interval", 100);
    }
  }
  if (arrivalGlow_ && !arrivalGlow_->inProgress()) {
    arrivalGlow_.reset();
    if (PushPin* pin = currentLocationPin()) pin->setOpen(true);
  }
  return VSpriteWindow::onAnimate();
}

bool VWorldMap::onMessage(const ui::Message& m) {
  if (m.type != ui::kButtonPress) return false;
  if (m.name.size() > 10 && m.name.compare(0, 10, "level_btn_") == 0) {
    const int level = std::atoi(m.name.c_str() + 10);
    if (GreatCCWorld* world = worldOf(ctx_)) world->levels().setCurrentLevel(level);
    doLuaString(ctx_, "CloseWorldMap();");
  }
  return true;
}

std::string VWorldMap::doCheat(int code, const std::string& arg) {
  GreatCCWorld* world = worldOf(ctx_);
  if (!world) return "Invalid Cheat";
  const int n = std::atoi(arg.c_str());
  char buf[96];
  switch (code) {
    case 3: {
      if (n < 1) return "No money added";
      for (const auto& [key, pin] : pins_) {
        if (!pin || !pin->open()) continue;
        const int level = pin->baseLevel();
        if (level <= 0) break;
        world->levels().updateLevelScore(level, world->levels().locationCash(level) + n);
        world->values().addToValue("total_worth", n, 0, true);
        std::snprintf(buf, sizeof buf, "Added %i to location %i and lifetime total", n, level);
        return buf;
      }
      return "No location selected";
    }
    case 4:
      if (n < 1 || n > 60) return "Invalid Level";
      world->levels().setCurrentLevel(n);
      doLuaString(ctx_, "CloseWorldMap();");
      return "";
    case 0:
    case 1:
    case 2: return "";
    case 5:
      if (n < 1 || n > 60) return "Invalid Level";
      world->levels().setHighestComplete(n, true);
      resetMap();
      std::snprintf(buf, sizeof buf, "Set highest level complete to %i", n);
      return buf;
    default: return "Invalid Cheat";
  }
}

std::string VWorldMap::detail() const {
  std::string open;
  for (const auto& [key, pin] : pins_)
    if (pin && pin->open()) open += (open.empty() ? "" : ",") + key;
  return VSpriteWindow::detail() + " pins=" + std::to_string(pins_.size()) + " open=" + open + " trail=" + transition_ +
         " hover=" + (sprites().hover() ? sprites().hover()->name() : std::string());
}

}  // namespace choco::games::greatcc
