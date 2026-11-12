#pragma once

// VObject and VAsset (docs/re/greatcc-sprites-and-zones.md 4.2-4.4): a sprite built from a Lua table. Its `assets` are named
// groups of states (each a sheet or texture with a resting frame, layer and play function), its `children` are objects built
// the same way, and its `text` entries are text sprites. Lookups run through the StyleChain, so a child reads what it lacks
// from the object around it.

#include <climits>
#include <map>
#include <memory>
#include <string>

#include "games/greatcc/scene/StyleChain.h"
#include "games/greatcc/scene/VSprite.h"

namespace choco::games::greatcc {

class TextSprite;

// One state of an asset: which file it shows and how it is shown.
struct AssetState {
  static constexpr int kNoLayer = INT_MIN;
  std::string name;
  std::string file;  // the path with baseDir applied, no extension
  std::string mask;
  bool anim = true;
  bool hasReg = false;
  gfx::PointF reg;
  int base = 0;  // the resting frame (0-based)
  int layer = kNoLayer;
  std::string play;  // the function to play ("" = DoAnim)
  bool resolved = false;
  std::shared_ptr<const gfx::SpriteSheet> sheet;
  std::shared_ptr<gfx::Texture> texture;
};

// VAsset: the states of one named asset.
class VAsset {
 public:
  explicit VAsset(std::string name) : name_(std::move(name)) {}
  const std::string& name() const { return name_; }
  // `style` is the asset table: `baseDir` (any depth) and an array of entry tables, one state each.
  void loadFromTable(const StyleChain& style);
  AssetState* state(const std::string& name);
  const AssetState* state(const std::string& name) const;
  const std::map<std::string, AssetState>& states() const { return states_; }
  // Loads the state's sheet or texture once (null and logged once when the file is missing).
  void resolve(AssetState& st, script::ScriptContext& ctx) const;

 private:
  std::string name_;
  std::map<std::string, AssetState> states_;
};

class VObject : public VSprite {
 public:
  // An empty name becomes "object_<type><counter>".
  VObject(script::ScriptContext& ctx, std::string name, std::string type);
  static std::shared_ptr<VObject> create(script::ScriptContext& ctx, std::string name, std::string type) {
    return std::make_shared<VObject>(ctx, std::move(name), std::move(type));
  }
  const char* kind() const override { return "VObject"; }
  const std::string& type() const { return type_; }
  void setType(std::string t) { type_ = std::move(t); }

  // The `VObject` table keys. Loading again first detaches what the previous load made.
  virtual void loadFromTable(const StyleChain& style);
  // Selects the starting asset (the current one, else autoAsset/autoState, else the first by name), plays when `continuous`,
  // and initialises the children. Idempotent.
  virtual void init();

  void addAsset(std::shared_ptr<VAsset> a);
  bool hasAsset(const std::string& name) const;
  const VAsset* asset(const std::string& name) const;
  const std::string& currentAsset() const { return curAsset_; }
  const std::string& currentState() const { return curState_; }
  // SwitchAsset: the asset's `state`, else its `fallback`; when both are missing the asset is still the current one but the picture
  // and the state stay, and a missing asset (the name is not lower-cased) clears the image and the current asset.
  virtual void switchAsset(const std::string& asset, const std::string& state, const std::string& fallback);
  // The current asset with another state.
  void switchState(const std::string& state, const std::string& fallback);
  // AddAssetLeaf: a leaf child (type of this object, layer `layer`) that shares this object's asset `asset` and shows its `state`
  // (else `default`); null when this object has no such asset. It is kept in the child map under the asset's name, or
  // `<asset>_0`, `<asset>_1` ... when that key is taken.
  std::shared_ptr<VObject> addAssetLeaf(const std::string& asset, const std::string& state, int layer);

  VObject* childObject(const std::string& key) const;
  // The same child, shared (null when there is none).
  std::shared_ptr<VObject> childShared(const std::string& key) const;
  // The child objects by lower-cased key, in key order (VCharacter walks them for its `idle_` children).
  const std::map<std::string, std::shared_ptr<VObject>>& childObjects() const { return childMap_; }
  TextSprite* textSprite(const std::string& key) const;
  // The text sprites by lower-cased key, in key order (VPushPin::SetOpen walks them).
  const std::map<std::string, std::shared_ptr<TextSprite>>& textSprites() const { return textMap_; }
  // A loose table entry (a non-table value without a meaning of its own), looked up by lower-cased key.
  std::string attribute(const std::string& name, const std::string& def = {}) const;
  void setAttribute(const std::string& name, std::string value) { attrs_[lower(name)] = std::move(value); }
  bool continuous() const { return continuous_; }

  void play(const std::string& name, bool loop, bool playChildren, double delayMs) override;
  void stop(bool recursive) override;
  // VObject::CopyTo: the destination shares this object's assets (and switches to the current one), takes the type, the
  // `continuous` flag and the attributes, gets a copy of each child object and of each text sprite's style; then
  // VSprite::CopyTo. The destination's own earlier assets, children map and attributes are replaced.
  void copyTo(VSprite& dst) override;

  static std::string lower(std::string s);

 protected:
  void detachLoaded();
  // The sheet or texture of the asset state, loaded now.
  void showState(VAsset& asset, AssetState& st);

 private:
  std::string type_;
  bool continuous_ = false;
  bool inited_ = false;
  std::string autoAsset_, autoState_;
  std::string curAsset_, curState_;
  std::map<std::string, std::shared_ptr<VAsset>> assets_;
  std::map<std::string, std::shared_ptr<VObject>> childMap_;
  std::map<std::string, std::shared_ptr<TextSprite>> textMap_;
  std::map<std::string, std::string> attrs_;
};

}  // namespace choco::games::greatcc
