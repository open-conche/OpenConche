#include "games/greatcc/shop/ThoughtBubble.h"

#include <algorithm>
#include <cmath>

#include "games/greatcc/GreatCCWorld.h"

namespace choco::games::greatcc {

namespace {
struct BubbleValues {
  int transition = 200;
  float offset = -5.0f;
  GreatCCWorld* world = nullptr;
};
BubbleValues bubbleValues(script::ScriptContext& ctx) {
  BubbleValues v;
  v.world = worldOf(ctx);
  if (v.world) {
    v.transition = v.world->values().getInt("transition_time", 200);
    v.offset = static_cast<float>(v.world->values().getReal("thought_offset", -5.0));
  }
  return v;
}
}  // namespace

void VThoughtBubble::addOrder(const std::shared_ptr<VObject>& item, const std::string& anchor, bool enabled) {
  const BubbleValues v = bubbleValues(context());
  const int n = static_cast<int>(orders_.size());
  std::shared_ptr<VObject> leaf = addAssetLeaf("bubble_single", "default", 0);
  if (!leaf) return;  // the location has no bubble art: nothing to show
  leaf->setLayer(-n);
  Entry e;
  e.disabled = !enabled;
  e.leaf = leaf;
  orders_.push_back(e);
  leaf->setPosition({-v.offset * static_cast<float>(n), -v.offset * static_cast<float>(n)});
  if (!enabled) leaf->setTint({1.0f, 0.75f, 0.75f, 1.0f});
  leaf->addEffect(std::make_shared<AlphaEffect>(true, 1.0f, static_cast<double>((v.transition / 2) * n + v.transition), -1.0f, 0));
  std::shared_ptr<SpriteEffect> tint;
  if (n > 0) {
    const float intensity = v.world ? v.world->values().getProduct("thought_tint", std::sqrt(static_cast<float>(n)), true) : 0.0f;
    tint = std::make_shared<TintEffect>(intensity, static_cast<double>(v.transition), gfx::ColorF{enabled ? 0.0f : 1.0f, 0, 0, 1}, 0.0f);
    leaf->addEffect(tint);
  }
  tints_.push_back(tint);
  fades_.push_back(nullptr);
  if (item) {
    leaf->addChild(item);
    item->setLeaf(true);
    item->attachToAnchor(anchor);
  }
  ++live_;
}

void VThoughtBubble::setMood(int mood) {
  const char* name = "happy";
  switch (mood) {
    case 2: name = "content"; break;
    case 3: name = "neutral"; break;
    case 4: name = "annoyed"; break;
    case 5: name = "angry"; break;
    default: break;
  }
  for (const Entry& e : orders_)
    if (e.leaf) e.leaf->switchState(name, "default");
}

bool VThoughtBubble::nextBubble() {
  ++cur_;
  return cur_ < static_cast<int>(orders_.size());
}

void VThoughtBubble::clear() {
  removeChildren();
  for (const auto& f : fades_)
    if (f) f->stop();
  for (const auto& t : tints_)
    if (t) t->stop();
  orders_.clear();
  fades_.clear();
  tints_.clear();
  cur_ = 0;
  live_ = 0;
}

void VThoughtBubble::update(bool softPaused) {
  const std::shared_ptr<VSprite> keep = weak_from_this().lock();
  const BubbleValues v = bubbleValues(context());
  const int count = static_cast<int>(orders_.size());
  for (int i = 0; i < count && i < static_cast<int>(orders_.size()); ++i) {
    const std::size_t at = static_cast<std::size_t>(i);
    const std::shared_ptr<VObject> leaf = orders_[at].leaf;
    if (!leaf) continue;
    if (i < cur_) {
      if (fades_[at]) {
        if (!fades_[at]->inProgress()) {
          // faded out: the bubble goes
          leaf->cancelEffects();
          removeChild(*leaf);
          orders_[at].leaf.reset();
          --live_;
          tints_[at].reset();
          fades_[at].reset();
          continue;
        }
      } else {
        fades_[at] = std::make_shared<AlphaEffect>(false, 1.0f, static_cast<double>(v.transition), leaf->alpha(), 0);
        leaf->cancelEffects();
        leaf->addEffect(fades_[at]);
      }
    }
    if (cur_ < static_cast<int>(orders_.size())) {
      if (tints_[at]) {
        if (i == cur_) {
          // the bubble in front loses its tint
          tints_[at]->stop();
          tints_[at].reset();
        } else {
          const float behind = static_cast<float>(i - cur_);
          const float intensity = v.world ? v.world->values().getProduct("thought_tint", behind > 0 ? std::sqrt(behind) : 0.0f, true) : 0.0f;
          if (auto* tint = dynamic_cast<TintEffect*>(tints_[at].get())) tint->setIntensity(intensity);
        }
      }
      const float place = -v.offset * static_cast<float>(i - cur_);
      const gfx::PointF target{place, place};
      const gfx::PointF pos = leaf->position();
      if (pos.x != target.x || pos.y != target.y) {
        const float step = (v.offset * static_cast<float>(static_cast<unsigned>(getElapsed()))) / static_cast<float>(v.transition);
        auto clamped = [&](float to, float from) {
          const float d = to - from;
          if (to <= from) return d <= -step ? -step : d;
          return step < d ? step : d;
        };
        leaf->setPosition({pos.x + clamped(target.x, pos.x), pos.y + clamped(target.y, pos.y)});
      }
    }
  }
  VObject::update(softPaused);
}

}  // namespace choco::games::greatcc
