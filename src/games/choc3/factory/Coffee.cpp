#include "games/choc3/factory/Coffee.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "sdk/SpriteScript.h"
#include "gfx/SheetCache.h"
#include "gfx/text/TextLayout.h"
#include "ui/WindowManager.h"

namespace choco::game {

namespace {

gfx::Curve readCurve(const ui::Style& style, const char* key) {
  std::vector<gfx::PointF> pts;
  if (auto t = style.table(key)) {
    for (int i = 1; i <= t->size(); ++i) {
      if (auto e = t->element(i))
        pts.push_back({static_cast<float>(e->numberAt(1).value_or(0)), static_cast<float>(e->numberAt(2).value_or(0))});
    }
  }
  return gfx::Curve::fromPoints(std::move(pts));
}

std::vector<std::string> readStrings(const ui::Style& style, const char* key) {
  std::vector<std::string> out;
  if (auto t = style.table(key)) {
    for (int i = 1; i <= t->size(); ++i)
      if (auto s = t->stringAt(i)) out.push_back(*s);
  }
  return out;
}

gfx::PointF readPoint(const ui::Style& style, const char* key) {
  if (auto t = style.table(key)) return {static_cast<float>(t->numberAt(1).value_or(0)), static_cast<float>(t->numberAt(2).value_or(0))};
  return {0, 0};
}

constexpr double kEps = 1e-9;
constexpr float kHopperDx = -75.0f;  // the collection flights aim this far left of the hopper (CO 1.10)

}  // namespace

// ------------------------------------------------------------------ the pure rules

int matchBonusFor(const std::vector<int>& table, int n) {
  if (table.empty() || n < 0) return 0;
  return table[static_cast<std::size_t>(std::min(n, static_cast<int>(table.size()) - 1))];
}

bool caseSpawnReady(double lastT, double spacingMs, double tripMs) { return tripMs > 0 && lastT > spacingMs / tripMs; }

int chooseSlot(const std::vector<std::string>& names, int cols, int rows, const std::string& name) {
  int best = -1, bestRun = 0;
  auto at = [&](int c, int r) -> const std::string* {
    if (c < 0 || c >= cols || r < 0 || r >= rows) return nullptr;
    return &names[static_cast<std::size_t>(r * cols + c)];
  };
  for (int r = 1; r <= rows - 2; ++r) {
    for (int c = 0; c < cols; ++c) {
      if (!at(c, r)->empty()) continue;
      auto run = [&](int dc, int dr) {
        int n = 1;
        for (int k = 1;; ++k) {
          const std::string* s = at(c + dc * k, r + dr * k);
          if (!s || *s != name) break;
          ++n;
        }
        for (int k = 1;; ++k) {
          const std::string* s = at(c - dc * k, r - dr * k);
          if (!s || *s != name) break;
          ++n;
        }
        return n;
      };
      const int len = std::max(run(1, 0), run(0, 1));
      if (len > bestRun) bestRun = len, best = r * cols + c;
    }
  }
  return best;
}

// ------------------------------------------------------------------ CoffeeGrid

void CoffeeGrid::configure(int c, int r, float cellW, float cellH, gfx::PointF origin) {
  cols = std::max(c, 1);
  rows = std::max(r, 1);
  dx = cellW;
  dy = cellH;
  pos = origin;
  slots.assign(static_cast<std::size_t>(cols) * static_cast<std::size_t>(rows), GridSlot{});
}

int CoffeeGrid::slotIndex(gfx::PointF p) const {
  const float lx = p.x - pos.x, ly = p.y - pos.y;
  if (lx < 0 || ly < 0 || lx >= static_cast<float>(cols) * dx || ly >= static_cast<float>(rows) * dy) return -1;
  const int c = std::min(static_cast<int>(lx / dx), cols - 1), r = std::min(static_cast<int>(ly / dy), rows - 1);
  return r * cols + c;
}

gfx::PointF CoffeeGrid::slotCentre(int idx) const {
  const int c = idx % cols, r = idx / cols;
  return {(static_cast<float>(c) + 0.5f) * dx, (static_cast<float>(r) + 0.5f) * dy};
}

int CoffeeGrid::filled() const {
  return static_cast<int>(std::count_if(slots.begin(), slots.end(), [](const GridSlot& s) { return !s.name.empty(); }));
}

int CoffeeGrid::countHorizontal(int idx) const {
  if (idx < 0 || idx >= static_cast<int>(slots.size()) || slots[static_cast<std::size_t>(idx)].name.empty()) return 0;
  const std::string& n = slots[static_cast<std::size_t>(idx)].name;
  const int c = idx % cols, base = idx - c;
  int run = 1;
  for (int k = c - 1; k >= 0 && slots[static_cast<std::size_t>(base + k)].name == n; --k) ++run;
  for (int k = c + 1; k < cols && slots[static_cast<std::size_t>(base + k)].name == n; ++k) ++run;
  return run;
}

int CoffeeGrid::countVertical(int idx) const {
  if (idx < 0 || idx >= static_cast<int>(slots.size()) || slots[static_cast<std::size_t>(idx)].name.empty()) return 0;
  const std::string& n = slots[static_cast<std::size_t>(idx)].name;
  const int c = idx % cols, r = idx / cols;
  int run = 1;
  for (int k = r - 1; k >= 0 && slots[static_cast<std::size_t>(k * cols + c)].name == n; --k) ++run;
  for (int k = r + 1; k < rows && slots[static_cast<std::size_t>(k * cols + c)].name == n; ++k) ++run;
  return run;
}

std::vector<ClearedSlot> CoffeeGrid::floodClear(int idx) {
  std::vector<ClearedSlot> out;
  if (idx < 0 || idx >= static_cast<int>(slots.size()) || slots[static_cast<std::size_t>(idx)].name.empty()) return out;
  const std::string name = slots[static_cast<std::size_t>(idx)].name;
  // The original removes a cell after visiting its left and upper neighbours, so a neighbour may reach it again first;
  // a cell is taken once, at whichever visit comes first.
  std::function<void(int, int)> visit = [&](int c, int r) {
    if (c < 0 || c >= cols || r < 0 || r >= rows) return;
    GridSlot& s = slots[static_cast<std::size_t>(r * cols + c)];
    if (s.name != name) return;
    visit(c - 1, r);
    visit(c, r - 1);
    if (s.name == name) {
      out.push_back({r * cols + c, s.name, s.ing});
      s.name.clear();
      s.ing = nullptr;
    }
    visit(c + 1, r);
    visit(c, r + 1);
  };
  visit(idx % cols, idx / cols);
  return out;
}

std::vector<Ingredient*> CoffeeGrid::shiftRows(script::Rng& rng, const std::vector<std::string>& recipe,
                                               const std::function<Ingredient*(const std::string&)>& spawn) {
  std::rotate(slots.rbegin(), slots.rbegin() + cols, slots.rend());  // every row down one; the bottom row becomes row 0
  for (std::size_t i = 0; i < slots.size(); ++i)
    if (slots[i].ing) slots[i].ing->pos = slotCentre(static_cast<int>(i));
  std::vector<Ingredient*> fallen;
  const int n = static_cast<int>(recipe.size());
  for (int c = 0; c < cols; ++c) {
    GridSlot& s = slots[static_cast<std::size_t>(c)];
    if (!s.name.empty()) {
      if (s.ing) fallen.push_back(s.ing);
      s.name.clear();
      s.ing = nullptr;
    }
    if (n == 0) continue;
    const int pick = rng.range(0, 2 * n);
    if (pick < n) {
      s.name = recipe[static_cast<std::size_t>(pick)];
      if (spawn) {
        s.ing = spawn(s.name);
        if (s.ing) s.ing->pos = slotCentre(c);
      }
    }
  }
  return fallen;
}

void CoffeeGrid::clear() {
  for (GridSlot& s : slots) s = GridSlot{};
}

// ------------------------------------------------------------------ Hopper

bool Hopper::add(const std::string& name) {
  int best = -1;
  for (std::size_t i = 0; i < names_.size(); ++i)
    if (names_[i] == name && (best < 0 || counts_[i] <= counts_[static_cast<std::size_t>(best)])) best = static_cast<int>(i);
  if (best < 0) return false;
  ++counts_[static_cast<std::size_t>(best)];
  updateNeedles();
  return true;
}

bool Hopper::release() {
  if (counts_.empty() || std::any_of(counts_.begin(), counts_.end(), [](int c) { return c < 1; })) return false;
  for (int& c : counts_) --c;
  updateNeedles();
  return true;
}

void Hopper::reset() {
  std::fill(counts_.begin(), counts_.end(), 0);
  updateNeedles();
}

void Hopper::updateNeedles() {
  for (std::size_t i = 0; i < needles.size() && i < counts_.size(); ++i)
    if (needles[i]) needles[i]->pos.y = needleY(counts_[i]);
}

// ------------------------------------------------------------------ construction

CoffeeFactory::CoffeeFactory(ui::WindowManager& wm, script::ScriptContext& ctx, FillFn fill) : FactoryWindow(wm, ctx, std::move(fill)) {}

CoffeeFactory::~CoffeeFactory() = default;

void CoffeeFactory::init(const ui::Style& style) {
  FactoryWindow::init(style);
  readCommon(style);
  recipe_ = readStrings(style, "recipe");
  const int n = static_cast<int>(recipe_.size());
  if (auto t = style.table("matchBonus")) {
    matchBonus_.push_back(0);
    for (int i = 1; i <= t->size(); ++i) matchBonus_.push_back(static_cast<int>(t->numberAt(i).value_or(0)));
  }
  task_.speedup = style.numberOr("speedup", 1.1);
  task_.maxSpeed = style.numberOr("maxspeed", 8);
  task_.rowMs = style.numberOr("gridspeed", 3000);

  // The ingredient belt.
  const gfx::Curve conveyorPath = readCurve(style, "conveyorpath");
  conveyor_ = std::make_unique<IngredientQueue>(recipe_, static_cast<int>(std::clamp(style.numberOr("colorcount", 1), 0.0, 255.0)), false,
                                                conveyorPath, 1.0 / std::max(static_cast<int>(style.numberOr("conveyorcount", 5)), 1),
                                                style.numberOr("conveyortime", 1200), *root_);

  // The finished cases.
  cases_.tripMs = style.numberOr("producttime", 3000);
  cases_.spacingMs = style.numberOr("productspacing", 1000);
  cases_.path = readCurve(style, "productpath");
  if (auto a = style.table("appearance")) cases_.look = loadAppearance(*a);

  // The hopper with one meter per recipe entry (CO 1.10).
  hopper_ = Hopper(recipe_);
  {
    auto h = std::make_unique<Sprite>(9, "hopper");
    h->texture = tex_.texture(n == 3 ? "image/machine_2" : "image/machine_5");
    h->pos = hopperPos_;
    hopperSprite_ = &root_->addChild(std::move(h));
    for (int i = 0; i < n; ++i) {
      const std::string& name = recipe_[static_cast<std::size_t>(i)];
      gfx::PointF at{0, 0};  // more than 5 entries: the extras stay at the hopper's centre
      if (n == 3) {
        at = {-6.5f, -53.0f + 65.0f * static_cast<float>(i)};
      } else if (i < 4) {
        at = {static_cast<float>(i % 2) * 106.0f - 56.5f, static_cast<float>(i / 2) * 81.0f - 32.0f};
      } else if (i == 4) {
        at = {-3.5f, 11.0f};
      } else if (ctx_.stubs.hit("coffee.hopper.extra")) {
        ctx_.log.write("[factory] a coffee recipe with more than 5 entries: the extra meters sit at the hopper's centre");
      }
      auto meter = std::make_unique<Sprite>(1, "meter:" + name);
      meter->texture = tex_.texture("image/machine_meter");
      meter->pos = at;
      auto icon = std::make_unique<Sprite>(0, "meter_icon:" + name);
      icon->texture = tex_.texture("items/" + name);
      icon->pos = {-1.5f, -1.0f};
      meter->addChild(std::move(icon));
      auto needle = std::make_unique<Sprite>(2, "needle");
      needle->texture = tex_.texture("image/machine_needle");
      needle->pos = {25.5f, Hopper::needleY(0)};
      hopper_.needles.push_back(&meter->addChild(std::move(needle)));
      hopperSprite_->addChild(std::move(meter));
    }
  }

  // The box at the end of the case belt, with the case picture between its halves.
  {
    const gfx::PointF end = cases_.path.at(1.0f);
    auto open = std::make_unique<Sprite>(1, "box");
    open->texture = tex_.texture("image/box_open");
    open->pos = end;
    root_->addChild(std::move(open));
    auto mask = std::make_unique<Sprite>(3, "box_mask");
    mask->texture = tex_.texture("image/box_open_mask");
    mask->pos = end;
    auto pic = std::make_unique<Sprite>(0, "box_product");
    pic->pos = {0, 9};
    buildItemAppearance(*pic, cases_.look, 0.5f, tex_);
    mask->addChild(std::move(pic));
    root_->addChild(std::move(mask));
  }

  // The two hoods that hide the grid's overflow above and below the belt (CO 1.4 step 7).
  {
    const gfx::PointF bottom = readPoint(style, "bottom_hood"), top = readPoint(style, "top_hood");
    auto lower = std::make_unique<Sprite>(7, "hood_lower");
    lower->texture = tex_.texture("image/conveyor_base_mask_lower");
    lower->pos = bottom;
    root_->addChild(std::move(lower));
    auto upper = std::make_unique<Sprite>(7, "hood_upper");
    upper->texture = tex_.texture("image/conveyor_base_mask_upper");
    upper->pos = top;
    root_->addChild(std::move(upper));
  }

  // The grid: a sprite at the grid's origin that moves down as it scrolls; its cells are in grid-local coordinates.
  {
    const int cols = static_cast<int>(style.numberOr("columns", 9)), rows = static_cast<int>(style.numberOr("rows", 9));
    const gfx::PointF origin{static_cast<float>(style.numberOr("gridx", 10)), static_cast<float>(style.numberOr("gridy", 10))};
    grid_.configure(cols, rows, static_cast<float>(style.numberOr("griddx", 50)), static_cast<float>(style.numberOr("griddy", 50)), origin);
    task_.rootY = origin.y;
    auto g = std::make_unique<Sprite>(4, "grid");
    g->topLeft = true;
    g->pos = origin;
    gridSprite_ = &root_->addChild(std::move(g));
    const char* seamImage = grid_.cols == 8 ? "image/conveyor_belt_wide_seam" : "image/conveyor_belt_narrow_seam";
    for (int i = 0; i < 2; ++i) {
      auto seam = std::make_unique<Sprite>(0, "seam");
      seam->texture = tex_.texture(seamImage);
      seam->pos = {seam->imageSize().w * 0.5f, static_cast<float>(i * (grid_.rows / 2)) * grid_.dy};
      seam_[i] = &gridSprite_->addChild(std::move(seam));
    }
    auto marker = std::make_unique<Sprite>(0, "marker");
    marker->texture = tex_.texture("image/conveyor_belt_gridtarget");
    marker->visible = false;
    auto preview = std::make_unique<Sprite>(-1, "marker_image");
    preview->alpha = 0.5f;
    markerImage_ = &marker->addChild(std::move(preview));
    marker_ = &gridSprite_->addChild(std::move(marker));
  }

  // The gun at the end of the conveyor.
  gun_.flightMs = style.numberOr("gunspeed", 250);
  gun_.pos = conveyorPath.at(1.0f);
  {
    auto base = std::make_unique<Sprite>(0, "gun_base");
    base->texture = tex_.texture("image/gun_base");
    base->pos = gun_.pos;
    root_->addChild(std::move(base));
    auto gun = std::make_unique<Sprite>(10, "gun");
    gun->sheet = tex_.sheet("image/gun.xml");
    gun->pos = gun_.pos;
    gun->rotationDeg = 0;
    gun_.sprite = &root_->addChild(std::move(gun));
  }

  buildTimer();
}

// ------------------------------------------------------------------ states (CO 1.6)

void CoffeeFactory::enterState(State s) {
  switch (s) {
    case State::Init: break;
    case State::Countdown:  // all five tasks pause (SetGameState 0x1479e falls through to 0x14923)
      sound("Stop_conveyor_belt_loop");
      grid_.clock.pause();
      conveyor_->clock.pause();
      gun_.clock.pause();
      cases_.clock.pause();
      collect_.clock.pause();
      break;
    case State::Playing:
      sound("coffee_making_game");
      conveyor_->clock.restart();
      gun_.clock.restart();
      cases_.clock.restart();
      collect_.clock.restart();
      grid_.clock.reset(1.0);
      grid_.clock.restart();
      break;
    case State::Ending:
      showTarget(false);
      sound("coffee_making_game_outro");
      sound("game_over");
      conveyor_->clock.pause();
      conveyor_->reset();
      grid_.clock.pause();
      sound("Stop_conveyor_belt_loop");
      break;
    case State::Aborted:
      showTarget(false);
      setCursor("cursor");
      sound("coffee_making_game_outro");
      sound("game_over");
      conveyor_->clock.pause();
      conveyor_->reset();
      grid_.clock.pause();
      sound("Stop_conveyor_belt_loop");
      break;
    case State::Over:
      showTarget(false);
      setCursor("cursor");
      pauseParts();
      conveyor_->reset();
      for (Flight& f : collect_.list)
        if (f.ing) gridSprite_->removeChild(*f.ing);
      collect_.list.clear();
      collect_.clock.reset(1.0);
      hopper_.reset();
      manager().stopWindowAnimation(id());
      showGameOver();
      break;
  }
}

void CoffeeFactory::pauseParts() {
  conveyor_->clock.pause();
  gun_.clock.pause();
  cases_.clock.pause();
  collect_.clock.pause();
  if (!grid_.clock.paused) sound("Stop_conveyor_belt_loop");
  grid_.clock.pause();
}

void CoffeeFactory::restartParts() {
  conveyor_->clock.restart();
  gun_.clock.restart();
  cases_.clock.restart();
  collect_.clock.restart();
  grid_.clock.restart();
  sound("conveyor_belt_loop");
}

void CoffeeFactory::dropFlights() {
  for (Flight& f : gun_.flights)
    if (f.ing) root_->removeChild(*f.ing);
  gun_.flights.clear();
}

void CoffeeFactory::clearPopups() {
  for (Bonus& b : popups_)
    if (b.s) gridSprite_->removeChild(*b.s);
  popups_.clear();
}

void CoffeeFactory::resetParts() {
  emptyGrid();
  for (Flight& f : collect_.list)
    if (f.ing) gridSprite_->removeChild(*f.ing);
  collect_.list.clear();
  clearPopups();
  hopper_.reset();
  conveyor_->reset();
  dropFlights();
  gun_.clock.reset(1.0);
  // The original's case belt reset leaves the cases on it, and they score in the next run after Try Again; ours clears them.
  for (CaseBelt::P& p : cases_.list) root_->removeChild(*p.s);
  cases_.list.clear();
  cases_.clock.reset(1.0);
  collect_.clock.reset(1.0);
  grid_.clock.reset(1.0);
  delivered_ = 0;
  showTarget(false);
}

// ------------------------------------------------------------------ input

void CoffeeFactory::showTarget(bool on) {
  if (!marker_) return;
  if (!on && markerOn_) setCursor("cursor");
  marker_->visible = on;
  markerOn_ = on;
}

void CoffeeFactory::setTargetImage() {
  if (!markerImage_) return;
  const Ingredient* next = conveyor_ ? conveyor_->peek() : nullptr;
  markerImage_->texture = next ? next->texture : nullptr;
}

void CoffeeFactory::setTarget(gfx::PointF mouse) {
  const int idx = state_ == State::Playing ? grid_.slotIndex(mouse) : -1;
  if (idx < 0) {
    showTarget(false);
    return;
  }
  if (!markerOn_) {
    setCursor("blank_cursor");
    marker_->visible = true;
    markerOn_ = true;
  }
  marker_->pos = grid_.slotCentre(idx);
  setTargetImage();
}

void CoffeeFactory::aim(gfx::PointF mouse) {
  if (gun_.sprite) gun_.sprite->rotationDeg = gunRotationDeg(mouse, gun_.pos);
  setTarget(mouse);
}

bool CoffeeFactory::fire(gfx::PointF target) {
  Ingredient* ing = conveyor_->pop();
  if (!ing) return false;  // the click is lost
  setTargetImage();
  ing->setLayer(8);
  Flight f;
  f.ing = ing;
  f.start = gun_.pos;
  const int idx = grid_.slotIndex(target);
  if (idx < 0 || !grid_.empty(idx)) {
    const float dx = target.x - gun_.pos.x, dy = target.y - gun_.pos.y;
    const float len = std::hypot(dx, dy);
    f.dir = len > 0 ? gfx::PointF{dx / len * 1000.0f, dy / len * 1000.0f} : gfx::PointF{0, -1000};
    f.distance = 1000;
  } else {
    const gfx::PointF c = grid_.slotCentreWindow(idx);
    f.dir = {c.x - gun_.pos.x, c.y - gun_.pos.y};
    f.distance = std::hypot(f.dir.x, f.dir.y);
  }
  gun_.flights.push_back(f);
  if (gun_.sprite) playAnim(*gun_.sprite);
  sound("launch");
  return true;
}

std::optional<gfx::PointF> CoffeeFactory::autoAim() const {
  if (state_ != State::Playing || paused_ || !conveyor_ || !conveyor_->ready() || !gun_.flights.empty()) return std::nullopt;
  if (grid_.clock.phase(task_.rowMs) > 0.9) return std::nullopt;
  const Ingredient* ing = conveyor_->peek();
  std::vector<std::string> names;
  names.reserve(grid_.slots.size());
  for (const GridSlot& s : grid_.slots) names.push_back(s.name);
  const int pick = chooseSlot(names, grid_.cols, grid_.rows, ing->name());
  if (pick < 0) return std::nullopt;
  return grid_.slotCentreWindow(pick);
}

// ------------------------------------------------------------------ the 10 ms step (docs/design/factories.md 4)

void CoffeeFactory::playAnim(Sprite& s) {
  if (!manager().sheets() || !s.sheet) return;
  SpriteContext sc{*manager().sheets(), ctx_.rng, ctx_.log};
  s.play("DoAnim", sc, manager().nowMs());
}

void CoffeeFactory::stepOnce() {
  endRow();
  stepClock();
  tickConveyor();
  tickGun();
  tickCases();
  tickCollection();
  tickGrid();
  tickPopups();
}

void CoffeeFactory::endRow() {
  if (state_ == State::Aborted) setState(State::Over);
  else if (state_ == State::Ending && gun_.flights.empty() && cases_.list.empty()) setState(State::Over);
}

void CoffeeFactory::tickConveyor() {
  if (!conveyor_->clock.tick(kStepInt)) return;
  const double dt = conveyor_->clock.animateDelta(conveyor_->tripMs);
  conveyor_->update(dt, ctx_.rng, tex_);
}

bool CoffeeFactory::canAccept(const Ingredient& ing) const {
  const int idx = grid_.slotIndex(ing.pos);
  return idx >= 0 && grid_.empty(idx);
}

void CoffeeFactory::tickGun() {
  if (!gun_.clock.tick(kStepInt)) return;
  const double dt = gun_.clock.animateDelta(gun_.flightMs);
  for (std::size_t i = 0; i < gun_.flights.size();) {
    Flight& f = gun_.flights[i];
    Ingredient* ing = f.ing;
    if (f.tick(dt) < 1.0) {
      ++i;
      continue;
    }
    gun_.flights.erase(gun_.flights.begin() + static_cast<std::ptrdiff_t>(i));  // before acting: missed() may pause the game
    if (canAccept(*ing)) addToGrid(*ing);
    else missed(*ing);
  }
}

void CoffeeFactory::addToGrid(Ingredient& ing) {
  const int idx = grid_.slotIndex(ing.pos);
  std::unique_ptr<Sprite> owned = root_->release(ing);
  if (!owned) return;
  GridSlot& slot = grid_.slots[static_cast<std::size_t>(idx)];
  slot.name = ing.name();
  slot.ing = &ing;
  ing.setLayer(5);
  ing.pos = grid_.slotCentre(idx);
  ing.scale = 1;
  ing.debugName = "cell:" + ing.name();
  gridSprite_->addChild(std::move(owned));
  if (grid_.countHorizontal(idx) >= 3 || grid_.countVertical(idx) >= 3) collected(grid_.floodClear(idx));
}

bool CoffeeFactory::dropInto(int slot, const std::string& name) {
  if (slot < 0 || slot >= static_cast<int>(grid_.slots.size())) return false;
  auto ing = std::make_unique<Ingredient>(name);
  ing->texture = tex_.texture("items/" + name);
  ing->pos = grid_.slotCentreWindow(slot);
  Ingredient& ref = static_cast<Ingredient&>(root_->addChild(std::move(ing)));
  if (!canAccept(ref)) {
    root_->removeChild(ref);
    return false;
  }
  addToGrid(ref);
  return true;
}

// CO 1.11 message 1010.
void CoffeeFactory::collected(std::vector<ClearedSlot> list) {
  const int n = static_cast<int>(list.size());
  grid_.clock.scale = std::min(task_.maxSpeed, grid_.clock.scale * task_.speedup);
  const int bonus = matchBonusFor(matchBonus_, n);
  if (bonus != 0) {
    addScore(static_cast<unsigned>(bonus), true);
    sound("ingredient_match_" + std::to_string(std::min(bonus, 5)));
    addPopup(bonus, list);
  } else {
    sound("ingredient_match_0");
  }
  const gfx::PointF target{hopperPos_.x + kHopperDx, hopperPos_.y};
  for (ClearedSlot& c : list) {
    if (!c.ing) continue;
    c.ing->setLayer(7);
    Flight f;
    f.ing = c.ing;
    f.start = c.ing->pos;
    f.dir = {target.x - f.start.x, target.y - f.start.y};
    collect_.list.push_back(f);
  }
}

void CoffeeFactory::addPopup(int bonus, const std::vector<ClearedSlot>& list) {
  gfx::PointF mean{0, 0};
  int k = 0;
  for (const ClearedSlot& c : list) {
    if (!c.ing) continue;
    mean.x += c.ing->pos.x, mean.y += c.ing->pos.y, ++k;
  }
  if (k == 0) {
    mean = grid_.slotCentre(list.empty() ? 0 : list.front().index);
  } else {
    mean.x /= static_cast<float>(k), mean.y /= static_cast<float>(k);
  }
  const int flags = gfx::text::kHAlignCenter + gfx::text::kVAlignCenter;
  gfx::text::TextLayout layout;
  if (gfx::text::TextSystem* ts = manager().textSystem()) {
    gfx::text::TextStyle st;
    st.font = ts->fonts.get("fonts/fertigo.mvec");
    st.fonts = &ts->fonts;
    st.size = 80;
    st.color = {1, 1, 1, 1};
    st.flags = flags;
    layout = gfx::text::TextLayout::build("+" + std::to_string(bonus), st, 120, 120);
  }
  auto s = std::make_unique<TextSprite>(10, "bonus", std::move(layout), flags, 120, 120);
  s->pos = mean;
  TextSprite& ref = static_cast<TextSprite&>(gridSprite_->addChild(std::move(s)));
  popups_.push_back({&ref, mean, 0});
}

void CoffeeFactory::tickPopups() {
  for (std::size_t i = 0; i < popups_.size();) {
    Bonus& b = popups_[i];
    const double f = b.ageMs / 2000.0;
    b.s->pos = {b.start.x, b.start.y - static_cast<float>(150.0 * f)};
    b.s->alpha = static_cast<float>(1.0 - 0.8 * f);
    b.ageMs += kStepMs;
    if (b.ageMs <= 1999) {
      ++i;
    } else {
      gridSprite_->removeChild(*b.s);
      popups_.erase(popups_.begin() + static_cast<std::ptrdiff_t>(i));
    }
  }
}

void CoffeeFactory::tickCases() {
  if (!cases_.clock.tick(kStepInt)) return;
  const double dt = cases_.clock.animateDelta(cases_.tripMs);
  double last = 1.0;  // the progress of the last case looked at; a removed one counts as 1
  for (std::size_t i = 0; i < cases_.list.size();) {
    CaseBelt::P& p = cases_.list[i];
    p.t = std::min(1.0, p.t + dt);
    if (p.t > 1.0 - kEps) p.t = 1.0;
    p.s->pos = cases_.path.at(static_cast<float>(p.t));
    last = p.t;
    if (p.t >= 1.0) {
      root_->removeChild(*p.s);
      cases_.list.erase(cases_.list.begin() + static_cast<std::ptrdiff_t>(i));
      ++delivered_;
      addScore(1, false);
      sound("chocolate_counted");
      last = 1.0;
      continue;
    }
    if (p.t < 0.1) break;
    ++i;
  }
  if (caseSpawnReady(last, cases_.spacingMs, cases_.tripMs) && hopper_.release()) {
    auto s = std::make_unique<Sprite>(2, "case");
    s->pos = cases_.path.at(0.0f);
    buildItemAppearance(*s, cases_.look, 0.5f, tex_);
    Sprite& ref = root_->addChild(std::move(s));
    cases_.list.push_back({&ref, 0.0});
  }
}

void CoffeeFactory::tickCollection() {
  if (!collect_.clock.tick(kStepInt)) return;
  const double dt = collect_.clock.animateDelta(collect_.flightMs);
  for (std::size_t i = 0; i < collect_.list.size();) {
    Flight& f = collect_.list[i];
    Ingredient* ing = f.ing;
    if (f.tick(dt) < 1.0) {
      ++i;
      continue;
    }
    const std::string name = ing->name();
    collect_.list.erase(collect_.list.begin() + static_cast<std::ptrdiff_t>(i));
    gridSprite_->removeChild(*ing);
    hopper_.add(name);
  }
}

void CoffeeFactory::shiftGrid() {
  auto spawn = [this](const std::string& name) -> Ingredient* {
    auto ing = std::make_unique<Ingredient>(name);
    ing->texture = tex_.texture("items/" + name);
    ing->debugName = "cell:" + name;
    return &static_cast<Ingredient&>(gridSprite_->addChild(std::move(ing)));
  };
  for (Ingredient* gone : grid_.shiftRows(ctx_.rng, recipe_, spawn)) gridSprite_->removeChild(*gone);
  const float full = static_cast<float>(grid_.rows) * grid_.dy;
  for (Sprite* seam : seam_)
    if (seam) {
      seam->pos.y += grid_.dy;
      if (seam->pos.y > full) seam->pos.y -= full;  // strict, as the original: exactly `full` stays
    }
}

void CoffeeFactory::forceShift() { shiftGrid(); }

void CoffeeFactory::emptyGrid() {
  for (GridSlot& s : grid_.slots)
    if (s.ing) gridSprite_->removeChild(*s.ing);
  grid_.clear();
}

void CoffeeFactory::tickGrid() {
  if (!grid_.clock.tick(kStepInt)) return;
  grid_.clock.animate();
  const double t = grid_.clock.phase(task_.rowMs);
  if (t < grid_.previousT) shiftGrid();
  grid_.pos.y = static_cast<float>(t * grid_.dy + task_.rootY);
  if (gridSprite_) gridSprite_->pos = grid_.pos;
  grid_.previousT = t;
}

std::string CoffeeFactory::detail() const {
  char buf[96];
  std::snprintf(buf, sizeof buf, " speed=%.2f shots=%zu", grid_.clock.scale, gun_.flights.size());
  std::string s = FactoryWindow::detail() + buf;
  s += " cases=" + std::to_string(cases_.list.size()) + " delivered=" + std::to_string(delivered_) + " hopper=";
  for (std::size_t i = 0; i < hopper_.names().size(); ++i) {
    if (i) s += ",";
    s += hopper_.names()[i] + ":" + std::to_string(hopper_.counts()[i]);
  }
  std::snprintf(buf, sizeof buf, " grid=%d/%zu row=%.2f", grid_.filled(), grid_.slots.size(), grid_.previousT);
  return s + buf;
}

}  // namespace choco::game
