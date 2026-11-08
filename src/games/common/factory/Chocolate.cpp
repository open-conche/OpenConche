#include "games/common/factory/Chocolate.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <tuple>

#include "sdk/SpriteScript.h"
#include "gfx/SheetCache.h"
#include "ui/WindowManager.h"

namespace choco::game {

namespace {

constexpr float kTwoPi = 6.28318530717958f;

double frac(double x) { return x - std::floor(x); }

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

float dist(gfx::PointF a, gfx::PointF b) { return std::hypot(a.x - b.x, a.y - b.y); }

}  // namespace

gfx::Curve readStyleCurve(const ui::Style& style, const char* key) { return readCurve(style, key); }

// ------------------------------------------------------------------ the pure rules

int trayColorAfter(int tray, int ing, int* recolour) {
  if (recolour) *recolour = -1;
  if (tray == ing) return tray;
  if (tray != 0) {
    if (ing == 0) {
      if (recolour) *recolour = tray;
      return tray;
    }
    return kColorMismatch;
  }
  return ing;  // an uncoloured tray adopts the ingredient's colour
}

double speedAfterFull(double scale, double speedup, double maxSpeed) {
  scale *= speedup;
  return scale > maxSpeed ? maxSpeed : scale;
}

double speedAfterMiss(double scale) { return std::max(1.0, scale * 0.5); }

int chooseTray(const std::vector<TrayChoice>& trays, int ingredientColor) {
  int best = -1, bestKey = -1;
  for (std::size_t i = 0; i < trays.size(); ++i) {
    const TrayChoice& t = trays[i];
    if (!t.accepts) continue;
    const bool compatible = t.color == ingredientColor || t.color == 0;
    const int key = (compatible ? 1000 : 0) + t.fill;
    if (key > bestKey) bestKey = key, best = static_cast<int>(i);
  }
  return best;
}

// ------------------------------------------------------------------ ChocolateTray

ChocolateTray::ChocolateTray(int index, std::vector<std::string> r, double off, double end)
    : Sprite(5, "tray:" + std::to_string(index)), recipe(std::move(r)), slots(recipe.size(), nullptr), offset(off), endpoint(end) {}

gfx::PointF ChocolateTray::slotOffset(int i, int n) {
  const float a = kTwoPi * static_cast<float>(i) / static_cast<float>(std::max(n, 1));
  return {std::sin(a) * 50.0f, -std::cos(a) * 50.0f};
}

int ChocolateTray::firstSlotFor(const Ingredient& ing) const {
  for (std::size_t i = 0; i < recipe.size(); ++i)
    if (!slots[i] && recipe[i] == ing.name()) return static_cast<int>(i);
  return -1;
}

bool ChocolateTray::crossed(double u) const {
  if (u < last) return endpoint >= last || endpoint <= u;
  return last <= endpoint && endpoint <= u;
}

void ChocolateTray::setColor(int c, Textures& tex) {
  for (Ingredient* i : slots)
    if (i) i->setColor(c, tex);
  color = c;
  if (ring) {
    ring->tint = factoryColor(c);
    ring->visible = c != 0;
  }
}

void ChocolateTray::clear() {
  for (Ingredient*& i : slots) {
    if (i) removeChild(*i);
    i = nullptr;
  }
  fill = 0;
  color = 0;
  if (ring) ring->visible = false;
  radius = 75;
}

// ------------------------------------------------------------------ construction

ChocolateFactory::ChocolateFactory(ui::WindowManager& wm, script::ScriptContext& ctx, FillFn fill)
    : FactoryWindow(wm, ctx, std::move(fill)) {}

ChocolateFactory::~ChocolateFactory() = default;

ChocolateSpec ChocolateFactory::readSpec(const ui::Style& style) const {
  ChocolateSpec sp;
  sp.common = parseCommon(style);
  sp.recipe = readStrings(style, "recipe");
  sp.colorCount = static_cast<int>(std::clamp(style.numberOr("colorcount", 1), 0.0, 255.0));
  sp.rainbow = style.booleanOr("rainbow", false);
  sp.conveyorPath = readCurve(style, "conveyorpath");
  sp.conveyorMs = style.numberOr("conveyortime", 1200);
  sp.spacing = 1.0 / std::max(static_cast<int>(style.numberOr("conveyorcount", 5)), 1);
  sp.productMs = style.numberOr("producttime", 3000);
  sp.productPath = readCurve(style, "productpath");
  if (auto a = style.table("appearance")) sp.look = loadAppearance(*a);
  sp.speedup = style.numberOr("speedup", 1.1);
  sp.maxSpeed = style.numberOr("maxspeed", 8);
  sp.lapMs = style.numberOr("traytime", 30000);
  sp.trayPath = readCurve(style, "traypath");
  sp.trayEnd = style.numberOr("trayend", 0);
  sp.trayCount = static_cast<int>(std::clamp(style.numberOr("traycount", 5), 0.0, 64.0));
  sp.machine = style.stringOr("machine");
  sp.flightMs = style.numberOr("gunspeed", 250);
  sp.ringMs = style.numberOr("ringspeed", 500);
  sp.recycler = style.booleanOr("recycler", false);
  sp.hasRecyclerPath = static_cast<bool>(style.table("recyclerpath"));
  if (sp.hasRecyclerPath) {
    sp.recyclerMs = style.numberOr("recyclertime", 20000);
    sp.recyclerPath = readCurve(style, "recyclerpath");
  }
  return sp;
}

void ChocolateFactory::init(const ui::Style& style) {
  FactoryWindow::init(style);
  spec_ = readSpec(style);
  const ChocolateSpec& sp = spec_;
  applyCommon(sp.common);
  recipe_ = sp.recipe;
  fingerUpperRest_ = sp.fingerUpperRestDeg;
  fingerLowerRest_ = sp.fingerLowerRestDeg;
  colorCount_ = sp.colorCount;
  rainbow_ = sp.rainbow;
  art_ = std::make_shared<const IngredientArt>(sp.ingredientArt);
  const int n = static_cast<int>(recipe_.size());

  // The conveyor.
  conveyor_ = std::make_unique<IngredientQueue>(recipe_, colorCount_, rainbow_, sp.conveyorPath, sp.spacing, sp.conveyorMs, *root_,
                                                art_, sp.pistons, sp.bagRounds);

  // The product belt and its picture.
  productScale_ = sp.productScale;
  products_.tripMs = sp.productMs;
  products_.path = sp.productPath;
  products_.look = sp.look;

  // The trays.
  trays_.speedup = sp.speedup;
  trays_.maxSpeed = sp.maxSpeed;
  trays_.lapMs = sp.lapMs;
  trays_.path = sp.trayPath;
  const double trayEnd = sp.trayEnd;
  const int trayCount = sp.trayCount;
  const std::string trayImage = sp.trayPrefix + std::to_string(n);
  for (int i = 0; i < trayCount; ++i) {
    auto tray = std::make_unique<ChocolateTray>(i, recipe_, static_cast<double>(i) / trayCount, trayEnd);
    tray->texture = tex_.texture(trayImage);
    auto ring = std::make_unique<Sprite>(1, "tray_ring");
    ring->texture = tex_.texture(trayImage + "_ring");
    ring->visible = false;
    tray->ring = &tray->addChild(std::move(ring));
    auto sparkle = std::make_unique<Sprite>(20, "sparkle");
    sparkle->sheet = tex_.sheet(sp.sparkleSheet);
    if (sparkle->sheet) sparkle->frame = static_cast<int>(sparkle->sheet->frames.size()) - 1;
    tray->sparkle = &tray->addChild(std::move(sparkle));
    if (sp.matchPopup) {
      auto label = std::make_unique<TextSprite>(100, "tray_text", gfx::text::TextLayout{}, kPopupFlags, kPopupBox, kPopupBox);
      label->visible = false;
      tray->popup = static_cast<TextSprite*>(&tray->addChild(std::move(label)));
    }
    const double u = frac(tray->offset);
    tray->pos = trays_.path.at(static_cast<float>(u));
    tray->last = u;
    if (sp.pistons) {
      for (int k = 4; k >= 1; --k) {  // tray_arm_4 (narrowest) first, as BuildPistons does
        auto pst = std::make_unique<Sprite>(-100, "tray_piston");
        pst->texture = tex_.texture("image/tray_arm_" + std::to_string(k));
        tray->pistons.push_back(&root_->addChild(std::move(pst)));
      }
    }
    if (sp.ropes) {
      auto pl = std::make_unique<Sprite>(-90, "tray_pulley");
      pl->sheet = tex_.sheet("image/rope_guide.xml");
      tray->pulley = &root_->addChild(std::move(pl));
      playAnim(*tray->pulley);
      for (int k = 0; k < 16; ++k) {
        auto r = std::make_unique<Sprite>(-100, "tray_rope");
        r->texture = tex_.texture("image/rope");
        tray->rope.push_back(&root_->addChild(std::move(r)));
      }
    }
    trays_.list.push_back(tray.get());
    placeTrayPistons(*tray);
    root_->addChild(std::move(tray));
  }

  // The machinery on the right: the machine, two fingers and the full tray on it (CF 3.2 step 6).
  {
    Sprite* templateParent = nullptr;
    if (sp.machineSprite) {
      auto m = std::make_unique<Sprite>(6, "machine");
      m->texture = tex_.texture(sp.machine);
      m->pos = sp.machinePos;
      machine_ = &root_->addChild(std::move(m));
      templateParent = machine_;
    }
    // Layer -1: behind the machine (8.3). The registration point sits at `offset`; a sprite here is anchored at its centre,
    // so the pivot is kept in the rig and the centre is recomputed when the finger turns.
    auto finger = [&](const char* image, const char* name, gfx::PointF anchor, gfx::PointF offset) {
      auto f = std::make_unique<Sprite>(-1, name);
      f->texture = tex_.texture(image);
      const gfx::RectF size = f->imageSize();
      Pivot pv{offset, {size.w * 0.5f - anchor.x, size.h * 0.5f - anchor.y}};
      f->pos = pivotedPos(pv, 0);
      Sprite* sp2 = &machine_->addChild(std::move(f));
      return std::pair{sp2, pv};
    };
    if (machine_ && !sp.fingerImage.empty()) {
      // Chocolatier 2: one picture twice, turned about its centre.
      auto one = [&](const char* name, gfx::PointF at) {
        auto f = std::make_unique<Sprite>(-1, name);
        f->texture = tex_.texture(sp.fingerImage);
        f->pos = at;
        return &machine_->addChild(std::move(f));
      };
      armUpper_ = one("finger_upper", sp.fingerUpperPos);
      armLower_ = one("finger_lower", sp.fingerLowerPos);
      pivotUpper_ = {sp.fingerUpperPos, {0, 0}};
      pivotLower_ = {sp.fingerLowerPos, {0, 0}};
      armUpper_->rotationDeg = fingerUpperRest_;
      armLower_->rotationDeg = fingerLowerRest_;
    } else if (machine_ && sp.fingers) {
      std::tie(armUpper_, pivotUpper_) = finger("image/machine_stack_finger_upper", "finger_upper", {41, 14}, {-74, -51});
      std::tie(armLower_, pivotLower_) = finger("image/machine_stack_finger_lower", "finger_lower", {41, 119}, {-74, 50});
    }
    auto tmpl = std::make_unique<Sprite>(0, "tray_template");
    tmpl->texture = tex_.texture(trayImage);
    tmpl->pos = sp.templatePos ? *sp.templatePos : gfx::PointF{0, -2};
    template_ = &(templateParent ? *templateParent : *root_).addChild(std::move(tmpl));
    for (int i = 0; i < n; ++i) {
      auto icon = std::make_unique<Sprite>(0, "icon:" + recipe_[static_cast<std::size_t>(i)]);
      icon->texture = tex_.texture(sp.ingredientArt.itemPrefix + recipe_[static_cast<std::size_t>(i)]);
      icon->pos = ChocolateTray::slotOffset(i, n);
      template_->addChild(std::move(icon));
    }
  }

  // The box at the end of the belt, with the product picture between its halves.
  ringTarget_ = {689, 555};
  if (sp.box) {
    const gfx::PointF end = products_.path.at(1.0f);
    const gfx::PointF boxAt{end.x + sp.boxOffset.x, end.y + sp.boxOffset.y};
    auto open = std::make_unique<Sprite>(1, "box");
    open->texture = tex_.texture(sp.boxClosed.empty() ? sp.boxOpen : sp.boxClosed);
    open->pos = boxAt;
    boxBack_ = &root_->addChild(std::move(open));
    auto mask = std::make_unique<Sprite>(3, "box_mask");
    mask->texture = tex_.texture(sp.boxFront);
    mask->pos = boxAt;
    // The picture is a child of the mask (layer 0, offset (0, 9)), so it draws over the box front; cases slide between.
    auto pic = std::make_unique<Sprite>(0, "box_product");
    pic->pos = sp.boxProductOffset;
    buildItemAppearance(*pic, products_.look, sp.productScale, tex_);
    mask->addChild(std::move(pic));
    root_->addChild(std::move(mask));
  }

  // The gun at the end of the conveyor.
  gun_.flightMs = sp.flightMs;
  gun_.pos = conveyor_ ? sp.conveyorPath.at(1.0f) : gfx::PointF{};
  {
    auto base = std::make_unique<Sprite>(0, "gun_base");
    base->texture = tex_.texture(sp.gunBase);
    base->pos = gun_.pos;
    gunBase_ = &root_->addChild(std::move(base));
    auto gun = std::make_unique<Sprite>(10, "gun");
    gun->sheet = tex_.sheet(sp.gunSheet);
    gun->pos = gun_.pos;
    gun->rotationDeg = sp.gunRotationOffsetDeg;
    gun_.sprite = &root_->addChild(std::move(gun));
  }

  rings_.flightMs = sp.ringMs;

  // The recycler, when the factory has the powerup and the table is there.
  if (sp.recycler) {
    if (sp.hasRecyclerPath) {
      Recycler r;
      r.lapMs = sp.recyclerMs;
      r.path = sp.recyclerPath;
      r.pos = r.path.at(0.0f);
      auto bin = std::make_unique<Sprite>(3, "recycler");
      bin->sheet = tex_.sheet("image/recycler.xml");
      bin->pos = r.pos;
      r.bin = &root_->addChild(std::move(bin));
      recycler_ = std::move(r);
    } else {
      ctx_.log.write("[factory] recycler is on but there is no recyclerpath; no recycler");
    }
  }

  if (sp.timerDial) buildTimer();
}

// ------------------------------------------------------------------ states (CF 4.2)

bool ChocolateFactory::anyTrayFull() const {
  return std::any_of(trays_.list.begin(), trays_.list.end(), [](const ChocolateTray* t) { return t->full(); });
}

bool ChocolateFactory::allTraysFull() const {
  return !trays_.list.empty() && std::all_of(trays_.list.begin(), trays_.list.end(), [](const ChocolateTray* t) { return t->full(); });
}

void ChocolateFactory::enterState(State s) {
  switch (s) {
    case State::Init: break;
    case State::Countdown:
      trays_.clock.restart();
      if (recycler_) recycler_->clock.restart();
      conveyor_->clock.pause();
      gun_.clock.pause();
      rings_.clock.pause();
      products_.clock.pause();
      break;
    case State::Playing:
      sound("chocolate_making_game");
      trays_.clock.restart();
      conveyor_->clock.restart();
      gun_.clock.restart();
      rings_.clock.restart();
      products_.clock.restart();
      if (recycler_) recycler_->clock.restart();
      break;
    case State::Ending:
      sound("game_over");
      sound("chocolate_making_game_outro");
      conveyor_->clock.pause();
      conveyor_->reset();
      if (anyTrayFull()) trays_.clock.scale = 5.0;
      break;
    case State::Aborted:
      setCursor("cursor");
      sound("game_over");
      sound("chocolate_making_game_outro");
      conveyor_->clock.pause();
      conveyor_->reset();
      gun_.clock.pause();
      if (spec_.slowAbort) trays_.clock.scale = 0.5;  // TFactoryGame::GameAbort
      break;
    case State::Over:
      setCursor("cursor");
      pauseParts();
      conveyor_->reset();
      manager().stopWindowAnimation(id());
      showGameOver();
      break;
  }
}

void ChocolateFactory::pauseParts() {
  trays_.clock.pause();
  conveyor_->clock.pause();
  gun_.clock.pause();
  rings_.clock.pause();
  products_.clock.pause();
  if (recycler_) recycler_->clock.pause();
}

void ChocolateFactory::restartParts() {
  trays_.clock.restart();
  conveyor_->clock.restart();
  gun_.clock.restart();
  rings_.clock.restart();
  products_.clock.restart();
  if (recycler_) recycler_->clock.restart();
}

void ChocolateFactory::dropFlights() {
  for (Flight& f : gun_.flights)
    if (f.ing) root_->removeChild(*f.ing);
  gun_.flights.clear();
}

// FACT (docs/re/choc2-factory-binary.md 10): "<N>x", N the recipe size, in the tray's colour, fertigo size 80, centred on the tray.
void ChocolateFactory::showPopup(ChocolateTray& tray, int color) {
  tray.textMs = kMatchPopupMs;
  tray.textColor = color;
  if (!tray.popup) return;
  if (gfx::text::TextSystem* ts = manager().textSystem()) {
    gfx::text::TextStyle st;
    st.font = ts->fonts.get("fonts/fertigo.mvec");
    st.fonts = &ts->fonts;
    st.size = 80;
    st.color = factoryColor(color);
    st.flags = kPopupFlags;
    tray.popup->setLayout(gfx::text::TextLayout::build(std::to_string(recipe_.size()) + "x", st, kPopupBox, kPopupBox));
  }
  tray.popup->alpha = 1;
  tray.popup->visible = true;
}

void ChocolateFactory::clearTray(ChocolateTray& tray, bool animate) {
  tray.clear();
  if (animate && tray.sparkle) playAnim(*tray.sparkle);
}

void ChocolateFactory::resetParts() {
  trays_.clock.softReset(1.0);
  for (ChocolateTray* t : trays_.list) clearTray(*t, false), t->textMs = 0, hidePopup(*t);
  if (recycler_) recycler_->clock.softReset(1.0);
  conveyor_->reset();
  dropFlights();
  gun_.clock.reset(1.0);
  for (Rings::Ring& r : rings_.list) root_->removeChild(*r.s);
  rings_.list.clear();
  rings_.clock.reset(1.0);
  for (Belt::P& p : products_.list) root_->removeChild(*p.s);
  products_.list.clear();
  products_.clock.reset(1.0);
  updateBox();
  machineTime_ = 0;
  recycled_ = 0;
  if (template_) template_->rotationDeg = 0;
  setMachineAngle(0);
}

// ------------------------------------------------------------------ input

void ChocolateFactory::aim(gfx::PointF mouse) {
  if (gun_.sprite) gun_.sprite->rotationDeg = gunRotationDeg(mouse, gun_.pos) + spec_.gunRotationOffsetDeg;
}

bool ChocolateFactory::fire(gfx::PointF target) {
  Ingredient* ing = conveyor_->pop();
  if (!ing) return false;  // the click is lost
  ing->setLayer(8);
  Flight f;
  f.ing = ing;
  f.start = ing->pos;
  const float dx = target.x - gun_.pos.x, dy = target.y - gun_.pos.y;
  const float len = std::hypot(dx, dy);
  const float reach = spec_.flyOutOfWindow ? static_cast<float>(rect().w) : 1000.0f;  // the window's width per flightMs in Chocolatier 2
  f.dir = len > 0 ? gfx::PointF{dx / len * reach, dy / len * reach} : gfx::PointF{0, -reach};
  f.unbounded = spec_.flyOutOfWindow;
  gun_.flights.push_back(f);
  if (gun_.sprite) playAnim(*gun_.sprite);
  sound("launch");
  return true;
}

std::optional<gfx::PointF> ChocolateFactory::autoAim() const {
  if (state_ != State::Playing || paused_ || !conveyor_ || !conveyor_->ready() || !gun_.flights.empty()) return std::nullopt;
  const Ingredient* ing = conveyor_->peek();
  std::vector<TrayChoice> choices;
  for (const ChocolateTray* t : trays_.list) choices.push_back({t->canAccept(*ing), t->fill, t->color, t->pos});
  const int pick = chooseTray(choices, ing->color());
  if (pick < 0) return std::nullopt;
  return choices[static_cast<std::size_t>(pick)].pos;
}

// ------------------------------------------------------------------ the 10 ms step (docs/design/factories.md 4)

void ChocolateFactory::playAnim(Sprite& s) {
  if (!manager().sheets() || !s.sheet) return;
  SpriteContext sc{*manager().sheets(), ctx_.rng, ctx_.log};
  s.play("DoAnim", sc, manager().nowMs());
}

void ChocolateFactory::stepOnce() {
  // The original's order (docs/re/factories-questions.md 1): end-state decay, clock, the ticks, then the machine angle.
  endRow();
  stepClock();
  tickRecycler();
  tickTrays();
  for (ChocolateTray* t : trays_.list) {
    t->textMs = std::max(0.0, t->textMs - kStepMs);
    if (!t->popup) continue;
    t->popup->visible = t->textMs > 0;
    t->popup->alpha = static_cast<float>(std::min(1.0, t->textMs / kMatchPopupFadeMs));
  }
  tickConveyor();
  tickGun();
  tickRings();
  tickProducts();
  machineStep();
  updateBox();
}

void ChocolateFactory::updateBox() {
  if (!boxBack_ || spec_.boxClosed.empty()) return;
  const std::string& want = products_.list.empty() ? spec_.boxClosed : spec_.boxOpen;
  if (!boxBack_->texture || boxBack_->texture != tex_.texture(want)) boxBack_->texture = tex_.texture(want);
}

void ChocolateFactory::endRow() {
  if (spec_.slowAbort) {
    // TFactoryGame::OnTaskAnimate: the abort slows the trays (x 0.99, stopped below 0.3); the end comes as soon as no shot and no
    // product is out (and, when the time ran out, no tray is full). Rings in flight do not count.
    if (state_ != State::Ending && state_ != State::Aborted) return;
    if (state_ == State::Aborted) {
      const double s = trays_.clock.scale * 0.99;
      trays_.clock.scale = s > 0 && s < 0.3 ? 0 : s;
    }
    if (!gun_.flights.empty() || !products_.list.empty()) return;
    if (state_ == State::Ending && anyTrayFull()) return;
    setState(State::Over);
    return;
  }
  if (state_ == State::Ending) {
    if (!rings_.list.empty() || !gun_.flights.empty() || !products_.list.empty() || anyTrayFull()) return;
  } else if (state_ != State::Aborted) {
    return;
  }
  const double s = trays_.clock.scale * 0.95;
  if (s < 0.05) setState(State::Over);
  else trays_.clock.scale = s;
}

void ChocolateFactory::machineStep() {
  if (machineTime_ == 0) return;
  machineTime_ = machineTime_ + 10 < spec_.machineMs ? machineTime_ + 10 : 0;
  const float deg = 360.0f * static_cast<float>(machineTime_) / static_cast<float>(spec_.machineMs);
  setMachineAngle(deg);
}

// A sprite turned by `deg` (clockwise) about its anchor while the anchor stays put (8.3): the centre swings round it.
gfx::PointF ChocolateFactory::pivotedPos(const Pivot& pv, float deg) {
  const float r = deg * 3.14159265358979f / 180.0f, c = std::cos(r), s = std::sin(r);
  return {pv.anchor.x + pv.centre.x * c - pv.centre.y * s, pv.anchor.y + pv.centre.x * s + pv.centre.y * c};
}

void ChocolateFactory::setMachineAngle(float deg) {
  if (template_) template_->rotationDeg = deg;
  if (armUpper_) {
    armUpper_->rotationDeg = fingerUpperRest_ - deg;
    armUpper_->pos = pivotedPos(pivotUpper_, -deg);
  }
  if (armLower_) {
    armLower_->rotationDeg = fingerLowerRest_ + deg;
    armLower_->pos = pivotedPos(pivotLower_, deg);
  }
}

void ChocolateFactory::tickRecycler() {
  if (!recycler_ || !recycler_->clock.tick(kStepInt)) return;
  Recycler& r = *recycler_;
  r.clock.animate();
  r.pos = r.path.at(static_cast<float>(r.clock.phase(r.lapMs)));
  if (r.bin) r.bin->pos = r.pos;
}

void ChocolateFactory::manufacture(ChocolateTray& tray) {
  if (machineTime_ == 0) machineTime_ = 1;
  sound("snatch");
  produce();
  clearTray(tray, true);
}

// ChocolateTray::Update (8.1): piston i sits at (x, (4 - i) * (y - 50) / 4) with its anchor at the bottom centre, 2 px up.
void ChocolateFactory::placeTrayPistons(ChocolateTray& tray) {
  if (tray.pulley) {
    // The pulley rides along the top at the tray's x; rope hangs from under it to the tray's centre (the tray hides the end).
    tray.pulley->pos = {tray.pos.x, 15};
    // TFactoryGame::Draw: 40 px pieces tiled up from the tray's position, the first centred 20 px above it, while the centre is >= -20.
    for (std::size_t k = 0; k < tray.rope.size(); ++k) {
      const float cy = tray.pos.y - 20.0f - 40.0f * static_cast<float>(k);
      tray.rope[k]->visible = cy >= -20.0f;
      tray.rope[k]->pos = {tray.pos.x, cy};
    }
  }
  for (std::size_t i = 0; i < tray.pistons.size(); ++i) {
    Sprite& s = *tray.pistons[i];
    const gfx::RectF size = s.imageSize();
    const float anchorX = std::floor(size.w / 2), anchorY = size.h - 2;
    const float bx = tray.pos.x, by = static_cast<float>(4 - static_cast<int>(i)) * (tray.pos.y - 50) / 4;
    s.pos = {bx + size.w * 0.5f - anchorX, by + size.h * 0.5f - anchorY};
  }
}

void ChocolateFactory::tickTrays() {
  if (!trays_.clock.tick(kStepInt)) return;
  trays_.clock.animate();
  const double t = trays_.clock.phase(trays_.lapMs);
  for (ChocolateTray* tray : trays_.list) {
    const double u = frac(t + tray->offset);
    tray->pos = trays_.path.at(static_cast<float>(u));
    placeTrayPistons(*tray);
    const bool cross = tray->crossed(u);
    const bool wrapped = u < tray->last;
    tray->last = u;
    if (spec_.deliverAtWrap) {
      // TFactoryGame::TickTrays: a full tray starts the machine's turn at trayEnd and is taken at the wrap.
      if (cross && tray->full()) machineTime_ = 1;
      if (wrapped && tray->full()) manufacture(*tray);
    } else if (cross && tray->full()) {
      manufacture(*tray);
    }
  }
  if (allTraysFull()) {
    sound("ingredient_complete_full_bonus");
    double i = 0;
    for (ChocolateTray* tray : trays_.list) {
      clearTray(*tray, true);
      produce(spec_.fullBonusStaggerMs > 0 && products_.tripMs > 0 ? i * spec_.fullBonusStaggerMs / products_.tripMs : 0);
      ++i;
    }
  }
}

void ChocolateFactory::tickConveyor() {
  if (!conveyor_->clock.tick(kStepInt)) return;
  const double dt = conveyor_->clock.animateDelta(conveyor_->tripMs);
  conveyor_->update(dt, ctx_.rng, tex_);
}

void ChocolateFactory::tickGun() {
  if (!gun_.clock.tick(kStepInt)) return;
  const double dt = gun_.clock.animateDelta(gun_.flightMs);
  for (std::size_t i = 0; i < gun_.flights.size();) {
    Flight& f = gun_.flights[i];
    Ingredient* ing = f.ing;
    const gfx::PointF old = ing->pos;
    const double u = f.tick(dt);
    bool gone = false;
    const gfx::PointF at = ing->pos;
    const bool ended = spec_.flyOutOfWindow
                           ? at.x < 0 || at.x >= static_cast<float>(rect().w) || at.y < 0 || at.y >= static_cast<float>(rect().h)
                           : u >= 1.0;
    if (!ended) {
      for (ChocolateTray* tray : trays_.list) {
        const bool near = spec_.hitRadius > 0 ? dist(at, tray->pos) <= spec_.hitRadius
                                              : segmentHitsCircle(old, at, tray->pos, tray->radius + 16);
        if (near && tray->canAccept(*ing)) {
          hitTray(*tray, *ing);
          gone = true;
          break;
        }
      }
      if (!gone && recycler_ && dist(ing->pos, recycler_->pos) <= 88.0f) {
        if (recycler_->bin) {
          const SpriteScript* sc = recycler_->bin->scriptOf();
          if (!sc || !sc->playing()) playAnim(*recycler_->bin);  // CF 8: only if it isn't already playing
        }
        root_->removeChild(*ing);
        sound("recycler");
        ++recycled_;
        gone = true;
      }
    } else {
      missed(*ing);
      gone = true;
    }
    if (gone) gun_.flights.erase(gun_.flights.begin() + static_cast<std::ptrdiff_t>(i));
    else ++i;
  }
}

void ChocolateFactory::tickRings() {
  if (!rings_.clock.tick(kStepInt)) return;
  const double dt = rings_.clock.animateDelta(rings_.flightMs);
  unsigned arrived = 0;
  for (std::size_t i = 0; i < rings_.list.size();) {
    Rings::Ring& r = rings_.list[i];
    r.t = std::min(1.0, r.t + dt);
    if (r.t > 1.0 - 1e-9) r.t = 1.0;
    r.s->pos = r.c.at(static_cast<float>(r.t));
    if (r.t >= 1.0) {
      root_->removeChild(*r.s);
      rings_.list.erase(rings_.list.begin() + static_cast<std::ptrdiff_t>(i));
      ++arrived;
      if (spec_.ringCredit <= 0) addScore(1, true);
    } else {
      ++i;
    }
  }
  if (spec_.ringCredit > 0 && arrived > 0) addScore(static_cast<unsigned>(spec_.ringCredit), true);  // once per tick (TickBonuses)
}

void ChocolateFactory::tickProducts() {
  if (!products_.clock.tick(kStepInt)) return;
  const double dt = products_.clock.animateDelta(products_.tripMs);
  // ChocolateProductQueueTask::Animate (CF 7): products update in order; the next one waits while the previous one is
  // still within 0.1 of the start. A counted product resets the kept value to 1.
  double prevT = 1.0;
  for (std::size_t i = 0; i < products_.list.size();) {
    if (!spec_.independentProducts && prevT < 0.1) break;
    Belt::P& p = products_.list[i];
    p.t = std::min(1.0, p.t + dt);
    if (p.t > 1.0 - 1e-9) p.t = 1.0;
    p.s->pos = products_.path.at(static_cast<float>(p.t));
    if (p.t >= 1.0) {
      root_->removeChild(*p.s);
      products_.list.erase(products_.list.begin() + static_cast<std::ptrdiff_t>(i));
      addScore(1, false);
      sound("chocolate_counted");
      prevT = 1.0;
    } else {
      prevT = p.t;
      ++i;
    }
  }
}

// ------------------------------------------------------------------ hits, products, rings (CF 6.3, 7)

void ChocolateFactory::produce(double t0) {
  auto s = std::make_unique<Sprite>(2, "product");
  s->pos = products_.path.at(static_cast<float>(t0));
  buildItemAppearance(*s, products_.look, productScale_, tex_);
  Sprite& ref = root_->addChild(std::move(s));
  products_.list.push_back({&ref, t0});
}

void ChocolateFactory::startRings(const ChocolateTray& tray) {
  const int n = static_cast<int>(tray.recipe.size());
  const int count = spec_.ringsPerMatch > 0 ? spec_.ringsPerMatch : n;
  for (int i = 0; i < count; ++i) {
    const gfx::PointF slot = ChocolateTray::slotOffset(i % std::max(n, 1), n);
    const gfx::PointF a{tray.pos.x + slot.x, tray.pos.y + slot.y};
    auto s = std::make_unique<Sprite>(15, "ring");
    if (spec_.ringsAreProducts) {
      // TFactoryGame::AddBonus: the product's picture (0.8 x 0.75; a sprite has one scale) from its slot, over a Bezier through
      // (400, 150 - 100 i) and (the belt's start x, its y - 50 i) to the box.
      buildItemAppearance(*s, products_.look, 0.775f, tex_);
      const gfx::PointF s0 = products_.path.at(0.0f), s1 = products_.path.at(1.0f);
      const float fi = static_cast<float>(i);
      s->pos = a;
      Sprite& ref = root_->addChild(std::move(s));
      rings_.list.push_back({&ref, gfx::Curve::fromPoints({a, {400, 150 - 100 * fi}, {s0.x, s0.y - 50 * fi}, s1}), 0.0});
      continue;
    }
    s->texture = tex_.texture(art_->ring);
    s->tint = factoryColor(tray.color);
    s->pos = a;
    Sprite& ref = root_->addChild(std::move(s));
    rings_.list.push_back({&ref, gfx::Curve::fromPoints({a, a, ringTarget_, ringTarget_}), 0.0});
  }
}

void ChocolateFactory::forceHit(ChocolateTray& tray, Ingredient& ing) { hitTray(tray, ing); }

void ChocolateFactory::hitTray(ChocolateTray& tray, Ingredient& ing) {
  const int slot = tray.firstSlotFor(ing);
  if (slot < 0) return;
  std::unique_ptr<Sprite> owned = root_->release(ing);
  if (!owned) return;
  tray.slots[static_cast<std::size_t>(slot)] = &ing;
  ++tray.fill;
  ing.setLayer(4);
  ing.pos = ChocolateTray::slotOffset(slot, static_cast<int>(tray.recipe.size()));
  ing.scale = 1;
  tray.addChild(std::move(owned));

  int recolour = -1;
  const int newColor = trayColorAfter(tray.color, ing.color(), &recolour);
  if (recolour >= 0) ing.setColor(recolour, tex_);
  if (newColor != tray.color) tray.setColor(newColor, tex_);

  if (tray.full()) {
    trays_.clock.scale = speedAfterFull(trays_.clock.scale, trays_.speedup, trays_.maxSpeed);
    const bool matched = tray.color != 0 && tray.color != kColorMismatch;
    sound(matched ? "ingredient_complete_colormatch" : "ingredient_complete");
    if (matched) {
      startRings(tray);
      produce();
      if (spec_.matchPopup) showPopup(tray, tray.color);  // before clearTray resets the colour
      clearTray(tray, true);
      onColorMatch();
    }
  } else {
    const std::string n = std::to_string(tray.fill);
    const bool matched = tray.color != 0 && tray.color != kColorMismatch;
    const bool plain = spec_.plainSuccessSounds ? !matched : tray.color == kColorMismatch;
    sound((plain ? "ingredient_success_" : "ingredient_color_match_") + n);
    if (tray.fill == 2 && matched) onSecondMatch();
  }
}

std::string ChocolateFactory::detail() const {
  char buf[96];
  std::snprintf(buf, sizeof buf, " speed=%.2f shots=%zu", trays_.clock.scale, gun_.flights.size());
  std::string s = FactoryWindow::detail() + buf + " trays=";
  for (std::size_t i = 0; i < trays_.list.size(); ++i) {
    const ChocolateTray* t = trays_.list[i];
    if (i) s += ",";
    s += std::to_string(i) + ":" + std::to_string(t->fill) + "/" + std::to_string(t->recipe.size()) + ":" + std::to_string(t->color);
  }
  s += " products=" + std::to_string(products_.list.size()) + " rings=" + std::to_string(rings_.list.size());
  return s;
}

}  // namespace choco::game
