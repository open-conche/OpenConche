#pragma once

// VSpriteManager (docs/re/greatcc-sprites-and-zones.md 2.4, 2.6): the top-level sprite list of a sprite window, the global
// effects, the per-frame update order, the depth-sorted draw and the mouse: hit test of the top-level sprites back to front,
// the hover target and its Enter/Leave/Move events, the pressed (captured) sprite, and lockdown with the exclusive set that
// decide which sprites may react. A plain object, owned by its window; the sprites it holds drop their link to it when it goes.
// Points are in the window's own coordinates (the manager's root spec is the identity).

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "games/greatcc/scene/Effects.h"
#include "games/greatcc/scene/VSprite.h"
#include "ui/Message.h"

namespace choco::games::greatcc {

// VSpriteSpider: visits sprites; returning false stops the walk.
using SpriteSpider = std::function<bool(VSprite&)>;

class SpriteManager {
 public:
  explicit SpriteManager(script::ScriptContext& ctx) : ctx_(ctx) {}
  ~SpriteManager();
  SpriteManager(const SpriteManager&) = delete;
  SpriteManager& operator=(const SpriteManager&) = delete;

  script::ScriptContext& context() const { return ctx_; }
  // The window name the manager was given (GetParentWindow looks it up in the top modal).
  const std::string& windowName() const { return windowName_; }
  void setWindowName(std::string n) { windowName_ = std::move(n); }

  // ---- clock ----
  // The modal-clock time of the current frame, set by the window before it updates. A change of clock source is the
  // window's business: the manager only reads this.
  double now() const { return now_; }
  void setNow(double ms) { now_ = ms; }
  // The time since the previous updateObjects (0 on the first).
  double getElapsed() const { return haveLast_ ? now_ - lastUpdate_ : 0; }

  // ---- objects ----
  // AddObject: appended, every global effect applied to it.
  void addObject(std::shared_ptr<VSprite> s);
  // Takes it out of the list (and clears its manager link) at once.
  void removeObject(VSprite& s);
  void removeAllObjects();
  // Reset (VSpriteWindow::Reset at a level load): every object goes (ours only: the original's Reset removes nothing and
  // leaves that to the owners of the sprites), hover and pressed with them, the pointer position is far
  // away, the hover time and the update clock start over and the soft pause ends. Lockdown, the exclusive set and the effects
  // already registered are not touched.
  void reset();
  // Queued; the sprite leaves the list at the start of the next update (RemoveSprite from a script, effects).
  void queueRemoval(VSprite& s);
  const std::vector<std::shared_ptr<VSprite>>& objects() const { return objects_; }
  VSprite* findSpriteByName(const std::string& name) const;
  // Visits the top-level sprites in list order (their children too when `recurse`).
  void forEachSprite(const SpriteSpider& spider, bool recurse) const;

  // ---- effects ----
  // Once per effect: added to the global list and applied to every current object (and later ones).
  void addGlobalEffect(const std::shared_ptr<SpriteEffect>& e);
  // Registers an effect that a sprite holds, so the manager updates it (VSprite does this).
  void registerEffect(const std::shared_ptr<SpriteEffect>& e);
  std::size_t effectCount() const { return effects_.size(); }
  std::size_t globalEffectCount() const { return globalEffects_.size(); }

  // ---- mouse (docs/re/greatcc-sprites-and-zones.md 2.5, 2.6) ----
  // RecalcMouse: the next update runs the hit test again. AddObject, RemoveObject, AddExclusiveSprite and the child tree
  // changes call it.
  void recalcMouse() { mouseDirty_ = true; }
  bool mouseDirty() const { return mouseDirty_; }
  // OnMouseMove: remembers the point. With a pressed sprite, that sprite's `hovered` is whether the point still hits it and it
  // gets a Drag; true. Otherwise the hit test is queued and the result is whether a hover target exists now.
  bool onMouseMove(gfx::PointI p);
  // OnMouseDown: the hover target (when valid) gets Down and becomes the pressed sprite; true. Otherwise any capture ends.
  bool onMouseDown(gfx::PointI p);
  // OnMouseUp: the pressed sprite (when still valid) gets Up (and Click when clickable); the capture always ends. True when
  // it was valid.
  bool onMouseUp(gfx::PointI p);
  // DoHitTest: the top-level sprites back to front, visible and `mouse` ones whose union rectangle holds the point; the first
  // GetChildAt result that is valid wins. The old hover target gets Leave, the new one Enter then Move (or a Move when it did not
  // change). True when there is a target.
  bool doHitTest(gfx::PointI p);
  VSprite* hover() const { return hover_.get(); }
  VSprite* pressedSprite() const { return pressed_.get(); }
  gfx::PointI lastMouse() const { return lastMouse_; }
  // The milliseconds the pointer has stayed on the hover target (reset by every change of target).
  double hoverMs() const { return hoverMs_; }
  // Lockdown and the exclusive set: when the set is non-empty or lockdown is on, only a sprite in the set is valid. A null
  // entry is allowed (it keeps everything locked out). A null sprite is never valid.
  void setLockDown(bool on) { lockDown_ = on; }
  bool lockDown() const { return lockDown_; }
  void addExclusiveSprite(std::shared_ptr<VSprite> s);
  void clearExclusiveSprites() { exclusive_.clear(); }
  std::size_t exclusiveCount() const { return exclusive_.size(); }
  bool isValidSprite(const VSprite* s) const;
  // GetParentWindow: the window under the top modal that has the manager's window name (0 when none).
  ui::WindowId parentWindowId() const;

  // ---- soft pause ----
  void softPause(bool on) { softPaused_ = on; }
  bool softPaused() const { return softPaused_; }

  // ---- per frame ----
  // Removal queue, each top-level sprite's update (children recursively) in list order, then the effects.
  void updateObjects();
  // The top-level list sorted by depth (ties: lower position y first, so lower on screen draws later), each visible sprite
  // drawn under a translation to `origin`. The sort is stable.
  // `scale` shrinks the whole scene about `origin` (the upgrade shop's picture of the shop).
  void draw(gfx::Renderer& r, gfx::PointF origin, float scale = 1.0f);
  // The drawn order of the last draw, as sprite names (for tests).
  const std::vector<VSprite*>& lastDrawOrder() const { return drawOrder_; }

  // ---- debug overlays (docs/re/greatcc-debug-tools.md 3) ----
  static constexpr unsigned kDebugAssets = 1, kDebugMouse = 2;
  // OnKeyDown: with ctrl, `t` toggles the asset text and `s` the mouse text (each key tests on its own; both may be on). Never consumes.
  bool onKeyDown(const std::string& key, bool ctrl);
  unsigned debugFlags() const { return debugFlags_; }
  void setDebugFlags(unsigned f) { debugFlags_ = f; }
  // The text DrawDebug shows ("" with no flag). Ctrl+S replaces Ctrl+T when both are on.
  std::string debugText() const;
  // DrawDebug: the text in red 12 pt myriad_pro at (5, 5) of `origin` on an opaque black panel, after the sprites.
  void drawDebug(gfx::Renderer& r, gfx::PointF origin) const;
  // VSprite::GetChildAt reports the sprite that had a descendant under the pointer (the outermost one after a whole hit test).
  void setTraceSprite(const std::string& name) { traceSprite_ = name, haveTrace_ = true; }
  // The textures of the sprites drawn this frame (VAssetManager::SetUsed); Draw clears the set first (ClearUsed).
  void noteTextureUsed(const gfx::Texture& tex);

 private:
  void updateEffects();
  struct TexSize {
    int w = 0, h = 0;
  };
  std::string assetStatus() const;

  script::ScriptContext& ctx_;
  std::shared_ptr<VSprite> hover_, pressed_;
  std::vector<std::shared_ptr<VSprite>> exclusive_;
  gfx::PointI lastMouse_{-10000, -10000};
  double hoverMs_ = 0;
  bool mouseDirty_ = false, lockDown_ = false;
  std::string windowName_;
  double now_ = 0, lastUpdate_ = 0;
  bool haveLast_ = false;
  bool softPaused_ = false;
  std::vector<std::shared_ptr<VSprite>> objects_;
  std::vector<std::shared_ptr<VSprite>> removals_;
  std::vector<std::shared_ptr<SpriteEffect>> globalEffects_;
  std::vector<std::shared_ptr<SpriteEffect>> effects_;
  std::vector<VSprite*> drawOrder_;
  unsigned debugFlags_ = 0;
  std::string traceSprite_;
  bool haveTrace_ = false;
  // The textures of this frame and every one the manager has drawn (by identity, with the size they had), and the peak of the frame sums.
  std::map<const gfx::Texture*, TexSize> usedNow_, usedEver_;
  mutable long long peakBytes_ = 0;
};

}  // namespace choco::games::greatcc
