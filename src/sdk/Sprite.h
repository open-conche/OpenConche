#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "gfx/Renderer.h"
#include "gfx/SpriteSheet.h"
#include "gfx/Texture.h"
#include "gfx/TextureCache.h"
#include "gfx/Types.h"

namespace choco::sdk {

class SpriteScript;
struct SpriteContext;

// The transform a parent hands its children: where its origin is on screen, and the scale, rotation (clockwise
// degrees) and alpha accumulated down the tree.
struct SpriteSpec {
  gfx::PointF origin;
  float scale = 1;
  float rotationDeg = 0;
  float alpha = 1;
};

// One node of a window's sprite tree (TSprite + TAnimatedSprite, docs/re/port-window.md 1.6). The window owns the root,
// the root owns everything below. Sprites outside the tree (the lists a window keeps for hit tests and updates) are
// non-owning and live in the same window, declared after the root.
class Sprite {
 public:
  explicit Sprite(int layer = 0, std::string debugName = {});
  virtual ~Sprite();
  Sprite(const Sprite&) = delete;
  Sprite& operator=(const Sprite&) = delete;

  // Children stay sorted by layer, ascending; a new child goes after those with the same layer, so the order is
  // stable. Returns the child.
  Sprite& addChild(std::unique_ptr<Sprite> child);
  // Detaches a direct child and hands it back; the pointer to it stays valid. Null when `child` is not a direct child.
  // Never call while this tree runs tickScripts or resortDirty.
  std::unique_ptr<Sprite> release(Sprite& child);
  // release() and destroy: the child's subtree goes with it. Nothing happens for a sprite that is not a direct child.
  void removeChild(Sprite& child);
  // Changes the layer. The parent re-sorts when resortDirty() next runs, never at once, so a sprite may change its
  // layer while its parent iterates.
  void setLayer(int layer);
  // Re-sorts (stably) every child list under this sprite that setLayer marked.
  void resortDirty();
  Sprite* parent() const { return parent_; }

  // Layer < 0 children first, then this sprite's own image, then the rest. Hidden and removed sprites draw nothing,
  // and neither do their children.
  virtual void draw(gfx::Renderer& r, const SpriteSpec& parent) const;

  // The sprite's own image rectangle in its parent's space, ignoring children (the building hit area).
  gfx::RectF rect() const;
  // visible, not removed, and the point inside rect(): left and top edges inclusive, right and bottom exclusive.
  bool hit(gfx::PointF p) const;
  // Chocolatier 2's TSprite::HitTest (docs/re/choc2-port-binary.md 1.3): the sprite and all its children are composited at
  // `p` (a point in the parent's space, so the sprite's own position, scale, rotation, flip and alpha apply) and the result
  // hits when its alpha is at least `threshold` (0..255). A texture whose pixels cannot be read counts as opaque, so such a
  // sprite hits on its rectangle alone.
  bool hitOpaque(gfx::PointF p, const gfx::TextureCache& textures, int threshold) const;

  // Sprites in this subtree that are visible (all the way up from here) and not removed, whose debug name starts with
  // `prefix`. An empty prefix matches every name.
  int countVisible(std::string_view prefix) const;
  // One line per sprite, children indented, for --dump-windows and the harness `dump` command.
  void dump(std::string& out, int indent) const;
  std::string dumpLine() const;

  int layer() const { return layer_; }
  const std::vector<std::unique_ptr<Sprite>>& children() const { return children_; }

  std::string debugName;
  gfx::PointF pos;
  float scale = 1;
  float rotationDeg = 0;
  gfx::ColorF tint{1, 1, 1, 1};  // not inherited
  float alpha = 1;               // inherited (multiplies down the tree)
  bool visible = true;
  bool hflip = false;    // mirrored left to right about the anchor; not inherited
  bool topLeft = false;  // anchored at the image's top-left instead of its centre
  bool removed = false;
  std::shared_ptr<gfx::Texture> texture;         // a plain sprite
  std::shared_ptr<const gfx::SpriteSheet> sheet;  // an animated one: anchored at the frame's registration point
  int frame = 0;

  // Runs the function `fn` (normally "DoAnim") of this sprite's sheet script, making the sprite's own Lua state first if
  // it has none. Needs a sheet. False (and a log line) when there is no sheet or the function is missing.
  bool play(const char* fn, SpriteContext& ctx, double now);
  // Gives every script in this subtree its turn (once per frame, with the script clock).
  void tickScripts(double now);
  // The sprite's script, or null if it never played.
  SpriteScript* scriptOf() const { return script.get(); }

  // The current image size and the anchor in image pixels; {0,0,0,0} size when the sprite has no image.
  gfx::RectF imageSize() const;
  gfx::PointF anchor() const;

 private:
  int layer_;
  bool sortDirty_ = false;
  Sprite* parent_ = nullptr;
  std::vector<std::unique_ptr<Sprite>> children_;
  const gfx::SheetFrame* currentFrame() const;
  std::unique_ptr<SpriteScript> script;  // declared last so it dies first: its natives point at this sprite
};

}  // namespace choco::sdk
