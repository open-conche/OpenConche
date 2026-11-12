#pragma once

// The Great Chocolate Chase's sprite (docs/re/greatcc-sprites-and-zones.md 2, docs/design/greatcc.md 2.3): a scene node with a
// child list sorted by layer, effects, listeners, anchors, a 3x3 transform with rotation about a point and flips, and the
// Play/Stop/loop/OnAnimComplete lifecycle. It does not sit on sdk::Sprite; it shares the sheets and the animation-script host
// (through sdk::ScriptTarget). Mouse events are dispatched by the SpriteManager (hit test, capture, lockdown); the sprite holds the flags `mouse`, `leaf` and
// `clickable`, the hit tests and the event machine.
//
// Time: a sprite reads the clock of the SpriteManager it belongs to (the owning window's modal clock); a sprite outside any
// manager has none, so its elapsed time is 0 and its scripts do not run.

#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "games/greatcc/scene/Mat3.h"
#include "gfx/Renderer.h"
#include "gfx/SpriteSheet.h"
#include "gfx/Texture.h"
#include "sdk/SpriteScript.h"
#include "script/ScriptContext.h"

namespace choco::games::greatcc {

class SpriteEffect;
class SpriteManager;
class VSprite;

// TDrawSpec as composed down the tree (TDrawSpec::GetRelative): the matrix, the alpha (own times parent's), the flip bits
// (own XOR the parent's: bit 0 horizontal, bit 1 vertical) and the blend mode (the environment's when the sprite's own is -1; VSprite::DoDraw hands its children -1).
struct DrawSpec {
  Mat3 m;
  float alpha = 1;
  unsigned flip = 0;
  int blend = -1;
};

// What a hook of an effect gets while a sprite draws: the renderer, the spec the parent hands down and the sprite's own
// composed spec as of the hook.
struct DrawEnv {
  gfx::Renderer& r;
  DrawSpec parent;
  DrawSpec abs;
};

// The mouse events of VSprite::DoEvent (the ids are the original's).
enum class SpriteEvent : int { Down = 0, Move = 1, Up = 2, Click = 4, Enter = 8, Leave = 0x10, Drag = 0x20 };

// A VSpriteListener: told of the sprite's mouse events after the sprite's own handler has run. Down, Move, Up and Drag carry
// the point (relative to the sprite's position), Click, Enter and Leave do not.
class SpriteListener {
 public:
  virtual ~SpriteListener() = default;
  virtual void onMouseEnter(VSprite&) {}
  virtual void onMouseLeave(VSprite&) {}
  virtual void onMouseDown(VSprite&, gfx::PointI) {}
  virtual void onMouseMove(VSprite&, gfx::PointI) {}
  virtual void onMouseUp(VSprite&, gfx::PointI) {}
  virtual void onMouseDrag(VSprite&, gfx::PointI) {}
  virtual void onClick(VSprite&) {}
  // A dead listener is skipped and moved out of the active set on the sprite's next update.
  bool dead = false;
};

// The blend mode ids the scripts use (TRenderer::SetBlendMode): 2 is additive, everything else draws normally.
gfx::Blend blendOf(int mode);

class VSprite : public sdk::ScriptTarget, public std::enable_shared_from_this<VSprite> {
 public:
  // GetElapsed gives 0 for a step this long or longer, so a stall never produces a big jump.
  static constexpr double kMaxElapsedMs = 3000;
  static constexpr double kDefaultDelayMs = 60;

  explicit VSprite(script::ScriptContext& ctx, std::string name = {});
  ~VSprite() override;
  VSprite(const VSprite&) = delete;
  VSprite& operator=(const VSprite&) = delete;

  // ---- identity ----
  int id() const { return id_; }
  const std::string& name() const { return name_; }
  void setName(std::string n) { name_ = std::move(n); }
  virtual const char* kind() const { return "VSprite"; }
  script::ScriptContext& context() const { return ctx_; }

  // ---- tree ----
  VSprite* parent() const { return parent_; }
  const std::vector<std::shared_ptr<VSprite>>& children() const { return children_; }
  SpriteManager* manager() const { return manager_; }
  // Takes the child out of its old parent, hands it this sprite's manager, appends and re-sorts by layer, and applies this
  // sprite's propagating effects to it.
  void addChild(std::shared_ptr<VSprite> child);
  void removeChild(VSprite& child);
  void removeChildren();
  // Own name first, then the children depth first in list order; null when nothing matches.
  VSprite* findSpriteByName(const std::string& name);
  // SetManager: the sprite and its subtree join (or leave) a manager and register their effects with it.
  virtual void setManager(SpriteManager* m);

  // ---- state ----
  bool visible() const { return visible_; }
  void setVisible(bool v) { visible_ = v; }
  bool selfVisible() const { return selfVisible_; }
  void setSelfVisible(bool v) { selfVisible_ = v; }
  int layer() const { return layer_; }
  // SetLayer: the parent re-sorts at once.
  void setLayer(int l);
  // The top-level draw order key (Lua `spriteLayer`).
  int depth() const { return depth_; }
  void setDepth(int d) { depth_ = d; }
  bool mouse() const { return mouse_; }
  void setMouse(bool v) { mouse_ = v; }
  bool leaf() const { return leaf_; }
  void setLeaf(bool v) { leaf_ = v; }
  bool clickable() const { return clickable_; }
  void setClickable(bool v) { clickable_ = v; }

  // ---- transform ----
  gfx::PointF position() const { return pos_; }
  void setPosition(gfx::PointF p) {
    pos_ = p;
    matrixDirty_ = true;
  }
  gfx::PointF scale() const { return scale_; }
  void setScale(float sx, float sy) {
    scale_ = {sx, sy};
    matrixDirty_ = true;
  }
  // Rotation by `angle` radians about (cx, cy), in the sprite's own pixels.
  void setRotation(float cx, float cy, float angle) {
    rotation_ = Mat3::rotationAbout(cx, cy, angle);
    matrixDirty_ = true;
  }
  // UpdateMatrix: the rotation matrix with its 2x2 part scaled by (sx, sy) (the translation is not), then the position added to
  // the translation.
  const Mat3& localMatrix() const;
  float alpha() const { return alpha_; }
  void setAlpha(float a) { alpha_ = a < 0 ? 0 : (a > 1 ? 1 : a); }
  const gfx::ColorF& tint() const { return tint_; }
  void setTint(const gfx::ColorF& t) { tint_ = t; }
  // The sprite's own blend mode, for its own image only (children are handed -1); -1 (the default) draws normally.
  int blend() const { return blend_; }
  void setBlend(int b) { blend_ = b; }
  void setFlip(bool h, bool v) {
    hflip_ = h;
    vflip_ = v;
  }
  bool hflip() const { return hflip_; }
  bool vflip() const { return vflip_; }
  // IsFlipped: the flag or any ancestor's.
  bool isHFlipped() const;
  bool isVFlipped() const;
  // A registration point that replaces the frame's (a VAsset's regX/regY); the draw spec's "explicit origin" flag.
  void setRegOverride(gfx::PointF p) {
    reg_ = p;
    hasReg_ = true;
  }
  void clearRegOverride() { hasReg_ = false; }

  // ---- image ----
  const gfx::SpriteSheet* sheet() const { return sheet_.get(); }
  const std::shared_ptr<gfx::Texture>& plainTexture() const { return plain_; }
  // SetTexture: stops the animation, swaps the image and builds a new script host for the sheet.
  void setSheet(std::shared_ptr<const gfx::SpriteSheet> sheet);
  void setPlainTexture(std::shared_ptr<gfx::Texture> tex);
  // Nothing shown: neither a sheet nor a texture.
  void clearImage();
  int currentFrame() const { return frame_; }
  void setCurrentFrame(int f);
  const gfx::SheetFrame* frameInfo() const;
  // The anchor of the current frame (frame pixels from its registration point).
  bool anchorPoint(const std::string& name, gfx::PointF& out) const;

  // ---- animation (2.7) ----
  // Plays the function `name` ("" = DoAnim). It restarts only when the name differs, nothing plays or the animation is done;
  // with delayMs > 0 the start waits that long.
  virtual void play(const std::string& name, bool loop, bool playChildren, double delayMs);
  // Clears the delay, loop flag and name and ends the script; `recursive` does the same for the direct children only.
  virtual void stop(bool recursive);
  // SetDelay: stores the frame delay and runs `delay(ms);` in the script state when it exists.
  void setDelay(double ms, bool recursive);
  double delay() const { return delay_; }
  // The function currently selected (empty when none).
  const std::string& animName() const { return animName_; }
  bool animPlaying() const;
  // The anchor name; every update re-applies it.
  void attachToAnchor(const std::string& name) { anchor_ = name; }
  const std::string& anchorName() const { return anchor_; }
  // The script host, null until a sheet is set.
  sdk::SpriteScript* script() const { return script_.get(); }
  // Natives to add to every script host this sprite makes (the machines' `unlock` and `drop` on their animated children): run on
  // the current host at once and on each later one, after the common natives.
  using ScriptSetup = std::function<void(sdk::SpriteScript&)>;
  void setScriptSetup(ScriptSetup f) {
    scriptSetup_ = std::move(f);
    if (script_ && scriptSetup_) scriptSetup_(*script_);
  }

  // ---- update ----
  // 0 without a clock, when the clock is not past the last update, or when the step reaches kMaxElapsedMs. The first call
  // seeds the last-update time from the clock, so it returns 0.
  double getElapsed() const;
  virtual void update(bool softPaused);
  // The time the soft pause began, 0 when not paused (read by VSuspendable later).
  double softPauseStart() const { return softPauseStart_; }
  // Called with the finished (non-looping) function's name. Listeners are not told (nothing sends them the event).
  virtual void onAnimComplete(const std::string& anim);

  // ---- effects and listeners ----
  void addEffect(const std::shared_ptr<SpriteEffect>& e);
  void removeEffect(const std::shared_ptr<SpriteEffect>& e);
  // RemovedFromSprite on each; Stop is not called.
  virtual void cancelEffects();
  bool hasEffect(int effectId) const { return effects_.count(effectId) != 0; }
  std::size_t effectCount() const { return effects_.size(); }
  // The effects in ascending id order (a snapshot).
  std::vector<std::shared_ptr<SpriteEffect>> effectList() const;
  // Sets: adding a listener takes it out of the pending removals and the reverse, so the last call in a frame wins. Both
  // take effect at the next update (removals first).
  void addListener(std::shared_ptr<SpriteListener> l) {
    pendingRemove_.erase(std::remove(pendingRemove_.begin(), pendingRemove_.end(), l), pendingRemove_.end());
    if (std::find(pendingAdd_.begin(), pendingAdd_.end(), l) == pendingAdd_.end()) pendingAdd_.push_back(std::move(l));
  }
  void removeListener(const std::shared_ptr<SpriteListener>& l);
  std::size_t listenerCount() const { return listeners_.size(); }
  // The active set as it is now (a snapshot, dead ones included): the callers that tell a kind of listener something that is not
  // a mouse event (VStation::OnArrival and VSuspendable tell the tutorial listeners) walk it themselves.
  std::vector<std::shared_ptr<SpriteListener>> activeListeners() const { return listeners_; }
  // VSprite::CleanUpListeners: the pending removals take effect, then the pending additions.
  void cleanUpListeners() { housekeepListeners(); }

  // ---- mouse (docs/re/greatcc-sprites-and-zones.md 2.5) ----
  // VSprite::HitTest: only a visible sprite with `mouse` or `leaf`. Its own image first (rectangle, then with a threshold >= 0
  // the pixel's alpha as drawn: texel x sprite alpha x tint alpha; the frame's xform and alpha scale are ignored); then, when
  // depth != 0, the children back to front (highest layer first) if the point is inside the union rectangle. `parent` is the
  // parent's spec; a point is a whole pixel.
  virtual bool hitTest(gfx::PointI p, const DrawSpec& parent, int threshold, int depth) const;
  // VSprite::GetChildAt: the sprite that should get an event at `p`: children at layer >= 0 back to front, then this sprite's
  // own image (when selfVisible), then the children below layer 0. A sprite found below this one is returned as it is, unless it
  // is a `leaf`, which hands the event to the container. Null when nothing is hit.
  std::shared_ptr<VSprite> getChildAt(gfx::PointI p, const DrawSpec& parent, int threshold);
  // VSprite::DoEvent: the sprite's handler, then its flags (Enter/Leave: hovered; Down/Up: pressed; an Up on a `clickable`
  // sprite sends Click at once), then the listeners that are not dead. Safe against the sprite dying in a handler.
  void doEvent(SpriteEvent ev, gfx::PointI p);
  // +0xd4: the pointer is over it (set after the Enter handler, cleared after the Leave handler; the manager also writes it
  // while the sprite is captured). +0xd5: pressed on it.
  bool hovered() const { return hovered_; }
  void setHovered(bool h) { hovered_ = h; }
  bool pressed() const { return pressed_; }
  // The handlers (all empty here). Points are relative to the sprite's position, truncated to whole pixels.
  virtual void onMouseEnter() {}
  virtual void onMouseLeave() {}
  virtual void onMouseDown(gfx::PointI) {}
  virtual void onMouseMove(gfx::PointI) {}
  virtual void onMouseUp(gfx::PointI) {}
  virtual void onMouseDrag(gfx::PointI) {}
  // Called every update with the milliseconds the pointer has stayed on the hover target.
  virtual void onMouseHover(double) {}
  virtual void onClick() {}

  // ---- copy ----
  // VSprite::CopyTo: layer, depth, the mouse flags, anchor and the draw state (transform, alpha, tint, flips, blend,
  // registration override). A nameless destination is named "<name>(copy NN)".
  virtual void copyTo(VSprite& dst);

  // ---- draw ----
  // DoDraw: PreDraw hooks, the children below layer 0, the image (PreRender, draw, PostRender), blend reset, the other
  // children, PostDraw hooks. Invisible sprites draw nothing, their subtree with them.
  virtual void doDraw(gfx::Renderer& r, const DrawSpec& parent);
  // The spec this sprite draws with under `parent`.
  DrawSpec composeSpec(const DrawSpec& parent) const;
  // The image alone, under `spec` (the glow effects draw it again with a changed spec). The frame's own alpha scale and xform
  // still apply.
  virtual void drawImage(gfx::Renderer& r, const DrawSpec& spec) const;
  // Screen rectangle of the sprite's image under `parent` (frame size, registration point, scale and rotation; the frame's
  // xform and alpha scale are ignored), unioned with every visible descendant when depth != 0. Empty without an image. The
  // sprite's own `visible` flag is not read.
  gfx::RectF getRect(const DrawSpec& parent, int depth) const;
  // The sprite's absolute matrix as of the last draw (for tests).
  const Mat3& lastAbs() const { return lastAbs_; }

  // ---- sdk::ScriptTarget ----
  const gfx::SpriteSheet* scriptSheet() const override { return sheet_.get(); }
  void scriptSetSheet(std::shared_ptr<const gfx::SpriteSheet> sheet) override { setSheet(std::move(sheet)); }
  void scriptSetFrame(int i) override { setCurrentFrame(i); }
  void scriptSetAlpha(float a) override { setAlpha(a); }
  void scriptSetLayer(int l) override { setLayer(l); }
  void scriptRemove() override;
  void scriptPause(bool pause, int depth, double now) override;
  bool scriptIsPlaying(int depth) const override;

 protected:
  // The image's own hit test under the composed spec `spec`: its rectangle, then the pixel's alpha against `threshold` (a
  // negative threshold takes the rectangle alone). False without an image.
  virtual bool imageHit(gfx::PointI p, const DrawSpec& spec, int threshold) const;
  // A sheet changed: a new script host. Subclasses add their natives to it (unlock, drop).
  virtual void newScript();
  double clockNow() const;
  bool hasClock() const { return manager_ != nullptr; }
  bool startAnim(const std::string& fn);
  // The image's rectangle under `spec` (the box of a text sprite, the size of a nine-slice).
  virtual gfx::RectF frameRect(const DrawSpec& spec) const;

 private:
  void resortChildren();
  enum class Hook { PreDraw, PreRender, PostRender, PostDraw };
  void runEffects(Hook hook, DrawEnv& env);
  void releaseEffect(const std::shared_ptr<SpriteEffect>& e);
  void housekeepListeners();
  void reapEffects();
  void pushEffectsToChild(VSprite& c);

  script::ScriptContext& ctx_;
  int id_;
  std::string name_;
  VSprite* parent_ = nullptr;
  SpriteManager* manager_ = nullptr;
  std::vector<std::shared_ptr<VSprite>> children_;

  bool visible_ = true, selfVisible_ = true, mouse_ = true, leaf_ = false, clickable_ = false;
  bool hovered_ = false, pressed_ = false;
  int copyCount_ = 1;
  int layer_ = 0, depth_ = 0;

  gfx::PointF pos_{0, 0}, scale_{1, 1};
  Mat3 rotation_;
  mutable Mat3 local_;
  mutable bool matrixDirty_ = true;
  float alpha_ = 1;
  gfx::ColorF tint_{1, 1, 1, 1};
  int blend_ = -1;
  bool hflip_ = false, vflip_ = false;
  bool hasReg_ = false;
  gfx::PointF reg_{0, 0};
  Mat3 lastAbs_;

  std::shared_ptr<const gfx::SpriteSheet> sheet_;
  std::shared_ptr<gfx::Texture> plain_;
  int frame_ = 0;
  std::unique_ptr<sdk::SpriteScript> script_;
  ScriptSetup scriptSetup_;

  std::string anchor_;
  std::string animName_;
  bool loop_ = false;
  double playDelay_ = 0;
  double delay_ = kDefaultDelayMs;
  double softPauseStart_ = 0;
  mutable double lastUpdate_ = 0;

  struct EffectEntry {
    std::shared_ptr<SpriteEffect> effect;
    int count = 0;
  };
  std::map<int, EffectEntry> effects_;
  std::vector<std::shared_ptr<SpriteListener>> listeners_, pendingAdd_;
  std::vector<std::shared_ptr<SpriteListener>> pendingRemove_;
  // Script hosts replaced while one of their natives may still be running (Load): freed at the next update.
  std::vector<std::unique_ptr<sdk::SpriteScript>> retired_;
};

}  // namespace choco::games::greatcc
