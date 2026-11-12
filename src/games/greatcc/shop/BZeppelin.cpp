#include "games/greatcc/shop/BZeppelin.h"

#include <cmath>

#include "games/greatcc/GreatCCWorld.h"
#include "games/greatcc/shop/VMachine.h"
#include "gfx/SheetCache.h"

namespace choco::games::greatcc {

namespace {
constexpr const char* kBase = "anim/machines/delivery/";
gfx::PointF operator*(float k, gfx::PointF p) { return {k * p.x, k * p.y}; }
gfx::PointF operator+(gfx::PointF a, gfx::PointF b) { return {a.x + b.x, a.y + b.y}; }
}  // namespace

BZeppelin::BZeppelin(script::ScriptContext& ctx, std::string name) : VObject(ctx, std::move(name), "zeppelin") {
  setDepth(3);
  if (gfx::SheetCache* sheets = ctx.wm.sheets()) {
    pickupSheet_ = sheets->get(std::string(kBase) + "zepp_pickup", "", "");
    withBox_ = sheets->get(std::string(kBase) + "zepp_with_box", "", "");
    noBox_ = sheets->get(std::string(kBase) + "zepp_no_box", "", "");
  }
}

BZeppelin::~BZeppelin() {
  if (voice_ != 0)
    if (GreatCCWorld* w = worldOf(context())) w->sound().stopVoice(voice_);
}

float BZeppelin::rand(float lo, float hi) const { return lo + static_cast<float>((hi - lo) * context().rng.fxUniform()); }

void BZeppelin::init() {
  VObject::init();
  if (!body_) {
    body_ = std::make_shared<VSprite>(context(), "zeppelin");
    addChild(body_);
  }
  if (noBox_) body_->setSheet(noBox_);
  duration_ = approachMs_;
  p0_ = {-117.0f, rand(0.0f, 300.0f)};
  pos_ = p0_;
  p3_ = gfx::PointF{43.0f, 39.0f} + gfx::PointF{590.0f, 106.0f};
  body_->setPosition(p0_);
  const float y1 = rand(0.0f, 500.0f), x1 = rand(50.0f, 450.0f);
  p1_ = {x1, y1};
  const float y2 = rand(0.0f, 200.0f), x2 = rand(550.0f, 650.0f);
  p2_ = {x2, y2};
  approaching_ = false;
  leaving_ = false;
  state_ = kIdle;
  setVisible(false);
  if (voice_ != 0) {
    if (GreatCCWorld* w = worldOf(context())) w->sound().stopVoice(voice_);
    voice_ = 0;
  }
}

float BZeppelin::updatePosition() {
  const float t = static_cast<float>(static_cast<unsigned>(clockNow() - start_)) / static_cast<float>(static_cast<unsigned>(duration_));
  const double dt = t, u = 1.0 - dt;
  pos_ = static_cast<float>(std::pow(u, 3.0)) * p0_ + static_cast<float>(dt * dt * 3.0 * u) * p2_ +
         static_cast<float>(static_cast<double>(t * 3.0f) * u * u) * p1_ + static_cast<float>(std::pow(dt, 3.0)) * p3_;
  if (body_) body_->setPosition(pos_);
  return t;
}

void BZeppelin::waitForChocolate() {
  duration_ = 1000;
  p0_ = {590.0f, 106.0f};
  pos_ = p0_;
  p3_ = {590.0f, 106.0f};
  if (body_) body_->setPosition(p0_);
  const float y1 = rand(84.0f, 104.0f), x1 = rand(651.0f, 671.0f);
  p1_ = {x1, y1};
  approaching_ = false;
}

void BZeppelin::pickup() {
  if (approaching_) return;
  init();
  setVisible(true);
  approaching_ = true;
  p0_ = {-116.0f, rand(0.0f, 300.0f)};
  pos_ = p0_;
  start_ = clockNow();
  if (GreatCCWorld* w = worldOf(context())) voice_ = w->sound().playSfxVoice("audio/sfx/zepplin_loop.ogg", true);
}

void BZeppelin::readyForPickup(const std::shared_ptr<VMachine>& machine) {
  machine_ = machine;
  ready_ = true;
}

void BZeppelin::grabBox() {
  if (body_) {
    body_->setPosition({0.0f, 0.0f});
    if (pickupSheet_) body_->setSheet(pickupSheet_);
    body_->play("", false, false, 0);
  }
  state_ = kGrabbing;
  if (GreatCCWorld* w = worldOf(context())) w->sound().playSfx("audio/sfx/pickup_zepplin.ogg", false);
}

void BZeppelin::leave() {
  if (leaving_) return;
  if (body_ && withBox_) body_->setSheet(withBox_);
  setVisible(true);
  leaving_ = true;
  p0_ = gfx::PointF{43.0f, 39.0f} + gfx::PointF{590.0f, 106.0f};
  pos_ = p0_;
  const float y1 = rand(0.0f, 200.0f), x1 = rand(550.0f, 650.0f);
  p1_ = {x1, y1};
  const float y2 = rand(0.0f, 500.0f), x2 = rand(50.0f, 450.0f);
  p2_ = {x2, y2};
  p3_ = {-116.0f, rand(0.0f, 300.0f)};
  start_ = clockNow();
  duration_ = leaveMs_;
}

void BZeppelin::update(bool softPaused) {
  // keep the sprite alive across the machine's reset, which may reload the level
  const std::shared_ptr<VSprite> keep = shared_from_this();
  VObject::update(softPaused);
  if (approaching_ && updatePosition() >= 1.0f) {
    approaching_ = false;
    if (!ready_) {
      state_ = kWaiting;
      waitForChocolate();
    } else {
      grabBox();
    }
  }
  if (state_ == kWaiting && updatePosition() >= 1.0f) {
    if (!ready_) waitForChocolate();
    else grabBox();
  }
  if (state_ == kGrabbing) {
    if (body_ && body_->currentFrame() > 7) {
      if (const std::shared_ptr<VMachine> m = machine_.lock()) m->reset();
    }
    if (body_ && !body_->animPlaying()) leave();
  }
  if (leaving_ && updatePosition() >= 1.0f) {
    leaving_ = false;
    state_ = kIdle;
    init();
  }
}

}  // namespace choco::games::greatcc
