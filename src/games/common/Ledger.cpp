#include "games/common/Ledger.h"
#include "games/common/Calendar.h"
#include "games/common/LedgerText.h"
#include "games/common/Appearance.h"
#include "script/LuaStyle.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <initializer_list>

#include "script/AudioBridge.h"
#include "script/LuaApi.h"
#include "ui/Layout.h"
#include "ui/WindowManager.h"
#include "ui/widgets/TextWidgets.h"
#include "ui/widgets/Widgets.h"

namespace choco::game {

namespace {

using ui::Window;

// Restores the Lua stack height on scope exit. The ledger reads the main state's stack, always balanced.
struct StackGuard {
  explicit StackGuard(lua_State* l) : L(l), top(lua_gettop(l)) { lua_checkstack(L, 24); }
  ~StackGuard() { lua_settop(L, top); }
  StackGuard(const StackGuard&) = delete;
  StackGuard& operator=(const StackGuard&) = delete;
  lua_State* L;
  int top;
};

// Raw reads only: a table that is not a table, or a missing key, is nil. Never raises a Lua error.
void pushField(lua_State* L, int table, const char* key) {
  if (!lua_istable(L, table)) {
    lua_pushnil(L);
    return;
  }
  lua_pushstring(L, key);
  lua_rawget(L, table < 0 ? table - 1 : table);
}

double numberField(lua_State* L, int table, const char* key, double def) {
  StackGuard g(L);
  pushField(L, table, key);
  return lua_isnumber(L, -1) ? lua_tonumber(L, -1) : def;
}

std::string stringField(lua_State* L, int table, const char* key) {
  StackGuard g(L);
  pushField(L, table, key);
  if (lua_type(L, -1) != LUA_TSTRING) return {};
  return std::string(lua_tostring(L, -1), lua_strlen(L, -1));
}

// Everything in the ledger passes through float, so values above 2^24 lose integer precision as in the original.
double viaFloat(double v) { return static_cast<double>(static_cast<float>(v)); }

unsigned clampUint(double v) {
  if (!(v > 0)) return 0;
  if (v >= 4294967295.0) return 4294967295u;
  return static_cast<unsigned>(v);
}

std::string dollars(script::ScriptContext& ctx, double v) { return script::dollars(ctx.lua.L(), v); }

std::string dateText(script::ScriptContext& ctx, double weeks) { return game::dateText(ctx.strings, weeks); }

void setText(Window* w, std::string text) { ledgerSetText(w, std::move(text)); }

void setShownAndReset(Window* w, bool shown) {
  if (!w) return;
  w->setShown(shown);
  if (auto* b = dynamic_cast<ui::ButtonWindow*>(w)) b->resetState();  // a hidden button drops capture and its pressed look
}

}  // namespace

// ================================================================ DookieDropper

DookieDropper::DookieDropper(ui::WindowManager& wm, script::ScriptContext& ctx) : RolloverWindow(wm), ctx_(ctx) {}

void DookieDropper::init(const ui::Style& style) {
  RolloverWindow::init(style);
  timerFace_ = load("image/timer_face_small");
  timerHand_ = load("image/timer_hand_small");
  idle_ = load("image/factory_idle_help");
}

void DookieDropper::postChildrenInit(const ui::Style& style) {
  RolloverWindow::postChildrenInit(style);
  itemX_ = static_cast<int>(style.numberOr("itemx", 18));
  itemY_ = static_cast<int>(style.numberOr("itemy", 86));
  ingredientY_ = static_cast<int>(style.numberOr("ingredienty", 58));
  barHeight_ = static_cast<int>(style.numberOr("barHeight", 42));
  countX_ = static_cast<int>(style.numberOr("countx", 57));
  countY_ = static_cast<int>(style.numberOr("county", 86));
}

std::string DookieDropper::detail() const {
  return RolloverWindow::detail() + " product=\"" + product_ + "\" production=" + std::to_string(production_) +
         (stall_ ? " stalled" : "");
}

std::shared_ptr<gfx::Texture> DookieDropper::load(const std::string& name) const {
  gfx::TextureCache* cache = manager().textures();
  if (!cache || cache->resolve(name).path.empty()) return nullptr;  // resolve first: a missing file would be logged
  return cache->load(name);
}

double DookieDropper::weeksPerColumn(int rank) {
  static constexpr double kWeeks[] = {4, 6, 8, 8, 8, 10, 10};
  if (rank < 0) return kWeeks[0];
  if (rank > 6) return kWeeks[6];
  return kWeeks[rank];
}

gfx::ColorF DookieDropper::barColour(double ratio) {
  if (ratio >= 0.75) return {50 / 255.0f, 205 / 255.0f, 30 / 255.0f, 1};
  if (ratio >= 0.5) return {1, 252 / 255.0f, 15 / 255.0f, 1};
  return {1, 14 / 255.0f, 14 / 255.0f, 1};
}

float DookieDropper::barHeightFor(double ratio, int barHeight) {
  return static_cast<float>(std::clamp(ratio * barHeight, 0.0, static_cast<double>(barHeight)));
}

std::vector<DookieDropper::Bar> DookieDropper::layoutBars(const std::vector<double>& supplies, int rank, int width,
                                                          int ingredientY, int barHeight) {
  std::vector<Bar> bars;
  const float cw = static_cast<float>(width) / 6.0f;
  const float n = static_cast<float>(supplies.size());
  float x = (cw + (static_cast<float>(width) - n * cw)) / 2.0f;  // the centre of the first of n columns
  const double weeks = weeksPerColumn(rank);
  for (double supply : supplies) {
    Bar b;
    b.ratio = static_cast<float>(supply / weeks);
    b.height = barHeightFor(b.ratio, barHeight);
    b.colour = barColour(b.ratio);
    const int x1 = static_cast<int>(std::max(0.0f, x - cw / 2 + 2.5f));
    const int y1 = static_cast<int>(std::max(0.0f, static_cast<float>(ingredientY) - b.height - 3));
    const int x2 = static_cast<int>(std::max(0.0f, static_cast<float>(x1) + cw - 4));
    const int y2 = static_cast<int>(static_cast<float>(y1) + b.height);
    b.rect = {x1, y1, x2 - x1, y2 - y1};
    b.centreX = x;
    bars.push_back(b);
    x += cw;
  }
  return bars;
}

// Appearance lists: a string is a layer "custom/<s>" in white, a table {name, r, g, b} one tinted (r,g,b,1) with the
// missing numbers 0.
void DookieDropper::buildProduct(lua_State* L, int product) {
  layers_.clear();
  auto addLayer = [&](const std::string& name, const gfx::ColorF& tint) {
    if (auto tex = load(name)) layers_.push_back({tex, tint, false});
    if (auto hi = load(name + "_highlight")) layers_.push_back({hi, {1, 1, 1, 1}, true});
  };
  {
    StackGuard g(L);
    const script::LuaStyle view(L, product, &ctx_.lua);
    for (const AppearanceLayer& l : loadAppearance(view)) addLayer(l.image, l.tint);
  }
  productScale_ = 0.25f;

  // The ingredient columns follow the order of next() over the recipe, which is the original's order too.
  ingredientNames_.clear();
  ingredientIcons_.clear();
  StackGuard g(L);
  pushField(L, product, "counts");
  if (!lua_istable(L, -1)) return;
  const int counts = lua_gettop(L);
  lua_pushnil(L);
  while (lua_next(L, counts) != 0) {
    lua_pop(L, 1);  // the count; only the key is used, and it is not converted in place
    if (lua_type(L, -1) == LUA_TSTRING && ingredientNames_.size() < 6) {
      ingredientNames_.emplace_back(lua_tostring(L, -1));
      ingredientIcons_.push_back({load("items/" + ingredientNames_.back())});
    }
  }
}

void DookieDropper::update() {
  lua_State* L = ctx_.lua.L();
  StackGuard g(L);
  lua_getglobal(L, "Player");
  const int player = lua_gettop(L);
  pushField(L, player, "factories");
  pushField(L, lua_gettop(L), name().c_str());
  const int info = lua_gettop(L);

  const std::string cur = stringField(L, info, "current");
  if (cur != product_) {
    product_ = cur;
    lua_getglobal(L, "_AllProducts");
    pushField(L, lua_gettop(L), cur.c_str());
    if (lua_istable(L, -1)) buildProduct(L, lua_gettop(L));  // an empty product leaves the old sprite and list as they were
  }
  production_ = static_cast<int>(std::min(clampUint(numberField(L, info, "production", 0)), 0x7fffffffu));
  if (Window* count = find("count")) setText(count, std::to_string(production_));
  {
    StackGuard s(L);
    pushField(L, info, "stall");
    stall_ = lua_toboolean(L, -1) != 0;
  }
  rank_ = static_cast<int>(std::min(clampUint(numberField(L, player, "rank", 1)), 0x7fffffffu));
}

void DookieDropper::animate() {
  if (clockMs_ < 2000) clockMs_ = std::min(2000.0, clockMs_ + 50);
  if (stall_ || production_ == 0) return;
  const double pi = 3.14159265358979323846;
  handAngle_ = static_cast<float>(2 * pi * clockMs_ / 2000);
  const int period = production_ < 10 ? 2000 : production_ < 30 ? 1000 : production_ < 50 ? 666 : 500;
  const int t = static_cast<int>(clockMs_);
  productScale_ = clockMs_ >= 2000 ? 0.25f : static_cast<float>((t % period) + 1) * 0.25f / static_cast<float>(period);
}

void DookieDropper::postDraw(ui::DrawContext& ctx) const {
  gfx::Renderer& r = ctx.renderer;
  const float ox = ctx.origin.x, oy = ctx.origin.y;
  const gfx::ColorF white{1, 1, 1, ctx.alpha};
  auto drawCentred = [&](const gfx::Texture& tex, float cx, float cy, float scale, gfx::ColorF tint, float rotationDeg = 0) {
    const float w = tex.width() * scale, h = tex.height() * scale;
    r.drawTexture(tex, {}, {ox + cx - w / 2, oy + cy - h / 2, w, h}, tint, gfx::Blend::Normal, rotationDeg, {w / 2, h / 2});
  };

  for (const Layer& l : layers_) {
    gfx::ColorF t = l.tint;
    t.a *= ctx.alpha;
    drawCentred(*l.texture, static_cast<float>(itemX_), static_cast<float>(itemY_), productScale_, t);
  }
  if (timerFace_) drawCentred(*timerFace_, static_cast<float>(countX_), static_cast<float>(countY_), 1, white);
  if (timerHand_)
    drawCentred(*timerHand_, static_cast<float>(countX_), static_cast<float>(countY_), 1, white,
                handAngle_ * 180.0f / 3.14159265f);

  // The ingredient bars and icons.
  if (!ingredientNames_.empty()) {
    lua_State* L = ctx_.lua.L();
    StackGuard g(L);
    lua_getglobal(L, "Player");
    pushField(L, lua_gettop(L), "supply");
    const int supply = lua_gettop(L);
    std::vector<double> supplies;
    for (const std::string& name : ingredientNames_) supplies.push_back(numberField(L, supply, name.c_str(), 0));
    const std::vector<Bar> bars = layoutBars(supplies, rank_, rect().w, ingredientY_, barHeight_);
    const float cw = static_cast<float>(rect().w) / 6.0f;
    for (std::size_t i = 0; i < bars.size(); ++i) {
      const Bar& b = bars[i];
      if (b.rect.w > 0 && b.rect.h > 0) {
        gfx::ColorF c = b.colour;
        c.a *= ctx.alpha;
        r.fillRect({ox + b.rect.x, oy + b.rect.y, static_cast<float>(b.rect.w), static_cast<float>(b.rect.h)}, c,
                   gfx::Blend::Normal);
      }
      if (i < ingredientIcons_.size() && ingredientIcons_[i].texture)
        drawCentred(*ingredientIcons_[i].texture, b.centreX, static_cast<float>(ingredientY_), cw * 0.03125f, white);
    }
  }

  if ((stall_ || production_ == 0) && idle_) {
    gfx::ColorF t = white;
    t.a *= 0.75f;
    drawCentred(*idle_, rect().w / 2.0f, static_cast<float>(ingredientY_ - barHeight_) + idle_->height() / 2.0f, 1, t);
  }
}

// ================================================================ LedgerWindow

LedgerWindow::LedgerWindow(ui::WindowManager& wm, script::ScriptContext& ctx) : BSGWindow(wm), ctx_(ctx) {
  fill_ = false;  // it draws nothing of its own
}

Window* LedgerWindow::child(const char* name) { return find(name); }

template <class F>
void LedgerWindow::forDroppers(F f) {
  for (ui::WindowId id : droppers_)
    if (auto* d = dynamic_cast<DookieDropper*>(manager().window(id))) f(*d);
}

void LedgerWindow::prepareInstance() {
  lua_State* L = ctx_.lua.L();
  {
    StackGuard g(L);
    lua_getglobal(L, "Player");
    money_.snap(viaFloat(numberField(L, lua_gettop(L), "money", 0)));
  }
  droppers_.clear();
  std::vector<Window*> stack{this};
  while (!stack.empty()) {  // pre-order, children in list order
    Window* w = stack.back();
    stack.pop_back();
    if (dynamic_cast<DookieDropper*>(w)) droppers_.push_back(w->id());
    for (auto it = w->children().rbegin(); it != w->children().rend(); ++it) stack.push_back(it->get());
  }
  update("all", "");
}

void LedgerWindow::updateMoneyText() {
  std::string t = dollars(ctx_, money_.display);
  const double shown = money_.display;
  const int size = shown >= 1e11 ? 20 : shown >= 1e9 ? 22 : shown >= 1e8 ? 25 : 0;
  if (size) t = "<font size='" + std::to_string(size) + "'>" + t + "</font>";
  setText(child("money"), std::move(t));
}

void LedgerWindow::update(const std::string& mode, const std::string& data) {
  lua_State* L = ctx_.lua.L();
  StackGuard g(L);
  lua_getglobal(L, "Player");
  const int player = lua_gettop(L);
  const bool full = mode == "newplayer" || mode == "all";
  const double rank = viaFloat(numberField(L, player, "rank", 1.0));

  if (full || mode == "time") setText(child("day"), dateText(ctx_, numberField(L, player, "time", 0)));

  if (full || mode == "money") {
    const double m = viaFloat(numberField(L, player, "money", 0));
    money_.setTarget(m);
    if (mode == "newplayer") updateMoneyText();  // at the current display value: it does not jump to the target
  }

  if (full || mode == "rank") {
    setText(child("rank"), ctx_.strings.lookup("rank_" + std::to_string(clampUint(rank))));
    if (Window* score = child("score")) score->setShown(rank != 1);
  }

  if (mode == "newplayer" || mode == "quest") setText(child("questText"), data);

  if (full || mode == "factory") {
    StackGuard s(L);
    pushField(L, player, "factories");
    const int factories = lua_gettop(L);
    forDroppers([&](DookieDropper& d) {
      StackGuard one(L);
      pushField(L, factories, d.name().c_str());
      if (lua_isnil(L, -1)) {
        d.setShown(false);
      } else {
        d.setShown(true);
        d.update();
      }
    });
  }

  if (mode == "portview") {
    setShownAndReset(child("map_port"), false);
    setShownAndReset(child("port_map"), true);
  } else if (mode == "mapview") {
    setShownAndReset(child("map_port"), true);
    setShownAndReset(child("port_map"), false);
  }

  if (full || mode == "time" || mode == "money") {
    const double money = viaFloat(numberField(L, player, "money", 0));
    const double time = numberField(L, player, "time", 0);
    const unsigned perWeek = time > 0 ? clampUint(std::floor(money / time)) : 0;
    setText(child("score"), ctx_.strings.sdkLookup("score_format", {dollars(ctx_, perWeek), "", "", "", ""}));
  }
  // Every mode ends with InvalidateScreen, and the whole tree is drawn every frame.
}

void LedgerWindow::showQuestImage(bool complete) {
  auto* bitmap = dynamic_cast<ui::BitmapWindow*>(child("ledger_questgoals"));
  if (!bitmap) return;
  const std::string image = complete ? kQuestComplete : kQuestIncomplete;
  if (image == questImage_) return;
  questImage_ = image;
  bitmap->setBitmap(image, bitmap->scale());
}

void LedgerWindow::setQuestIndicator(const std::string& mode) {
  if (!child("ledger_questgoals")) return;
  if (mode == "on") {
    showQuestImage(true);
    flashQuest_ = false;
    flashTimer_ = 0;
  } else if (mode == "flash") {
    showQuestImage(true);
    if (!flashQuest_) {
      flashQuest_ = true;
      flashTimer_ = 1500;
      script::soundEvent(ctx_, "quest_requirements_met");
    }
  } else {
    showQuestImage(false);
    flashQuest_ = false;
    flashTimer_ = 0;
  }
}

void LedgerWindow::animateProduction() {
  forDroppers([](DookieDropper& d) { d.startAnimation(); });
}

void LedgerWindow::animate() {
  if (money_.step()) updateMoneyText();
  if (flashQuest_) {
    flashTimer_ = flashTimer_ < 51 ? 0 : flashTimer_ - 50;
    showQuestImage(flashTimer_ % 500 < 250);
  }
  forDroppers([](DookieDropper& d) { d.animate(); });
}

}  // namespace choco::game
