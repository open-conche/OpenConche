#include "games/greatcc/scene/VObject.h"

#include <algorithm>
#include <cctype>

#include "games/greatcc/GreatCCWorld.h"
#include "games/greatcc/scene/SpriteManager.h"
#include "games/greatcc/scene/TextSprite.h"
#include "gfx/SheetCache.h"
#include "gfx/TextureCache.h"
#include "ui/WindowManager.h"

namespace choco::games::greatcc {

namespace {
int gObjectCount = 0;  // mObjCnt

bool startsWith(const std::string& s, const std::string& p) { return s.compare(0, p.size(), p) == 0; }
}  // namespace

// ---- VAsset ----

AssetState* VAsset::state(const std::string& name) {
  auto it = states_.find(name);
  return it == states_.end() ? nullptr : &it->second;
}
const AssetState* VAsset::state(const std::string& name) const {
  auto it = states_.find(name);
  return it == states_.end() ? nullptr : &it->second;
}

void VAsset::loadFromTable(const StyleChain& style) {
  const std::string baseDir = style.stringOr("baseDir", "");
  const int n = style.own().size();
  for (int i = 1; i <= n; ++i) {
    auto entry = style.own().element(i);
    if (!entry) continue;
    const StyleChain e(*entry, &style);
    AssetState st;
    st.name = e.stringOr("state", "default", 0);
    const std::string file = e.stringOr("file", "", 0);
    if (file.empty()) continue;  // an entry without a file is ignored
    st.file = baseDir + file;
    const std::string mask = e.stringOr("mask", "", 0);
    st.mask = mask.empty() ? mask : baseDir + mask;
    st.anim = e.boolOr("anim", true, 2);
    const auto rx = e.number("regX", 1), ry = e.number("regY", 1);
    if (rx || ry) {
      st.hasReg = true;
      st.reg = {static_cast<float>(rx.value_or(0)), static_cast<float>(ry.value_or(0))};
    }
    st.base = e.intOr("base", 0, 1);
    st.layer = e.intOr("layer", AssetState::kNoLayer, 3);
    st.play = e.stringOr("play", "", 1);
    states_[st.name] = std::move(st);
  }
}

void VAsset::resolve(AssetState& st, script::ScriptContext& ctx) const {
  if (st.resolved) return;
  st.resolved = true;
  // The game's tables name files from the install root ("assets/anim/..." on the Mac bundle's layout); the asset tree here
  // starts below that folder, so the prefix is tried last.
  std::vector<std::string> names{st.file};
  if (startsWith(st.file, "assets/")) names.push_back(st.file.substr(7));
  std::vector<std::string> masks{st.mask};
  if (startsWith(st.mask, "assets/")) masks.push_back(st.mask.substr(7));
  if (st.anim) {
    if (gfx::SheetCache* sheets = ctx.wm.sheets())
      for (const auto& n : names)
        for (const auto& m : masks)
          if (!st.sheet) st.sheet = sheets->get(n, "", m);
  } else if (gfx::TextureCache* tex = ctx.wm.textures()) {
    for (const auto& n : names)
      for (const auto& m : masks)
        if (!st.texture) st.texture = tex->load(n, m);
  }
  if (!st.sheet && !st.texture && ctx.warned.insert("greatcc-asset-" + st.file).second && (ctx.wm.sheets() || ctx.wm.textures()))
    ctx.log.write("[greatcc] asset " + name_ + "/" + st.name + ": '" + st.file + "' did not load");
}

// ---- VObject ----

VObject::VObject(script::ScriptContext& ctx, std::string name, std::string type)
    : VSprite(ctx, name.empty() ? "object_" + type + std::to_string(gObjectCount) : std::move(name)), type_(std::move(type)) {
  ++gObjectCount;
}

std::string VObject::lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

void VObject::addAsset(std::shared_ptr<VAsset> a) {
  const std::string key = lower(a->name());
  assets_[key] = std::move(a);
}
bool VObject::hasAsset(const std::string& name) const { return assets_.count(lower(name)) != 0; }
const VAsset* VObject::asset(const std::string& name) const {
  auto it = assets_.find(lower(name));
  return it == assets_.end() ? nullptr : it->second.get();
}
VObject* VObject::childObject(const std::string& key) const {
  auto it = childMap_.find(lower(key));
  return it == childMap_.end() ? nullptr : it->second.get();
}
std::shared_ptr<VObject> VObject::childShared(const std::string& key) const {
  auto it = childMap_.find(lower(key));
  return it == childMap_.end() ? nullptr : it->second;
}
TextSprite* VObject::textSprite(const std::string& key) const {
  auto it = textMap_.find(lower(key));
  return it == textMap_.end() ? nullptr : it->second.get();
}
std::string VObject::attribute(const std::string& name, const std::string& def) const {
  auto it = attrs_.find(lower(name));
  return it == attrs_.end() ? def : it->second;
}

void VObject::detachLoaded() {
  assets_.clear();
  curAsset_.clear();
  curState_.clear();
  for (auto& [k, c] : childMap_) removeChild(*c);
  childMap_.clear();
  for (auto& [k, t] : textMap_) removeChild(*t);
  textMap_.clear();
  attrs_.clear();
  inited_ = false;
}

void VObject::loadFromTable(const StyleChain& style) {
  detachLoaded();
  setLeaf(style.boolOr("leaf", leaf()));
  setVisible(style.boolOr("visible", visible()));
  setMouse(style.boolOr("mouse", mouse()));
  setClickable(style.boolOr("clickable", clickable()));
  continuous_ = style.boolOr("continuous", continuous_);
  setLayer(style.intOr("layer", layer()));
  type_ = style.stringOr("type", type_);
  setDepth(style.intOr("spriteLayer", depth(), 1));
  setName(style.stringOr("name", name()));
  attachToAnchor(style.stringOr("anchor", anchorName()));
  setPosition({static_cast<float>(style.numberOr("x", position().x, 0)), static_cast<float>(style.numberOr("y", position().y, 0))});
  setFlip(style.boolOr("hflip", false, 0), style.boolOr("vflip", false, 0));
  setBlend(style.intOr("blend", blend(), 0));
  setAlpha(static_cast<float>(style.numberOr("alpha", alpha(), 0)));
  if (auto t = style.numbers("tint", 4, 1.0, 0))
    setTint({static_cast<float>((*t)[0]), static_cast<float>((*t)[1]), static_cast<float>((*t)[2]), static_cast<float>((*t)[3])});
  if (auto r = style.numbers("rotation", 3, 0.0, 0))
    setRotation(static_cast<float>((*r)[0]), static_cast<float>((*r)[1]), static_cast<float>((*r)[2]));
  // `scale`: the original reads element 1 for both axes. F3: with the fix on, element 2 (when the table has one) is the y scale.
  constexpr double kAbsent = -1e30;
  if (auto s = style.numbers("scale", 2, kAbsent, 0)) {
    const float sx = (*s)[0] > kAbsent ? static_cast<float>((*s)[0]) : 1.0f;
    float sy = sx;
    GreatCCWorld* world = worldOf(context());
    if (world && world->settings().fixBugs && (*s)[1] > kAbsent) {
      sy = static_cast<float>((*s)[1]);
      if (sy != sx && context().warned.insert("greatcc-F3").second)
        context().log.write("[fix] F3 VObject scale: the y scale is scale[2] (the original uses scale[1] for both axes)");
    }
    setScale(sx, sy);
  }
  // children and text: this table (and its `style` base) only; the assets are looked up in every table around it (a step of a tutorial
  // takes the panel's art from the tutorial)
  if (auto assets = style.table("assets", -1)) {
    // The group table is a level of its own: a plain value in it (`children = { leaf = true, ... }`) reaches the entries.
    const StyleChain group(*assets, &style);
    assets->forEachEntry([&](const std::string& key, const ui::Style* t, const std::optional<std::string>&) {
      if (!t) return;
      const StyleChain ac(*t, &group);
      auto a = std::make_shared<VAsset>(lower(key));
      a->loadFromTable(ac);
      assets_[lower(key)] = std::move(a);
    });
  }
  if (auto children = style.table("children", 0)) {
    const StyleChain group(*children, &style);
    children->forEachEntry([&](const std::string& key, const ui::Style* t, const std::optional<std::string>&) {
      if (!t) return;
      const StyleChain cc(*t, &group);
      auto c = VObject::create(context(), lower(key), "autochild");
      c->loadFromTable(cc);
      addChild(c);
      childMap_[lower(key)] = std::move(c);
    });
  }
  if (auto texts = style.table("text", 0)) {
    const StyleChain group(*texts, &style);
    texts->forEachEntry([&](const std::string& key, const ui::Style* t, const std::optional<std::string>&) {
      if (!t) return;
      const StyleChain tc(*t, &group);
      auto ts = TextSprite::create(context());
      ts->loadFromTable(tc);
      addChild(ts);
      textMap_[lower(key)] = std::move(ts);
    });
  }
  // every plain entry of this table is a string attribute, the ones with a meaning of their own included
  style.own().forEachEntry([&](const std::string& key, const ui::Style* t, const std::optional<std::string>& text) {
    if (t || !text || key.empty()) return;
    attrs_[lower(key)] = *text;
  });
  autoAsset_ = style.stringOr("autoAsset", "", 0);
  autoState_ = style.stringOr("autoState", "", 0);
}

void VObject::showState(VAsset& asset, AssetState& st) {
  asset.resolve(st, context());
  if (st.anim && st.sheet) setSheet(st.sheet);
  else if (!st.anim && st.texture) setPlainTexture(st.texture);
  else clearImage();
}

void VObject::switchAsset(const std::string& assetName, const std::string& state, const std::string& fallback) {
  // The key is looked up as given (the map's keys were lower-cased when the tables loaded); a missing asset clears the image.
  auto it = assets_.find(assetName);
  if (it == assets_.end()) {
    clearImage();
    curAsset_.clear();
    curState_.clear();
    return;
  }
  VAsset& a = *it->second;
  // the asset becomes the current one even when neither state exists (a later SwitchState then finds it), the state and the
  // picture only when one does
  curAsset_ = assetName;
  std::string chosen = state;
  AssetState* st = a.state(state);
  if (!st) {
    st = a.state(fallback);
    chosen = fallback;
  }
  if (st) {
    if (st->hasReg) setRegOverride(st->reg);
    showState(a, *st);
    setCurrentFrame(st->base);
    if (st->layer != AssetState::kNoLayer) setLayer(st->layer);
    curState_ = chosen;
  }
  if (continuous_) play("", true, false, 0);
}

void VObject::switchState(const std::string& state, const std::string& fallback) {
  if (!curAsset_.empty()) switchAsset(curAsset_, state, fallback);
}

std::shared_ptr<VObject> VObject::addAssetLeaf(const std::string& assetName, const std::string& state, int layer) {
  const auto it = assets_.find(assetName);
  if (it == assets_.end()) return nullptr;
  auto leaf = VObject::create(context(), "", type_);
  leaf->setLeaf(true);
  leaf->setLayer(layer);
  addChild(leaf);
  leaf->addAsset(it->second);
  leaf->switchAsset(assetName, state, "default");
  std::string key = assetName;
  for (int n = 0; childMap_.count(key); ++n) key = assetName + "_" + std::to_string(n);
  childMap_[key] = leaf;
  return leaf;
}

void VObject::init() {
  if (!curAsset_.empty()) {
    switchAsset(curAsset_, curState_, "default");
  } else if (!autoAsset_.empty()) {
    switchAsset(autoAsset_, autoState_, "default");
  } else if (!assets_.empty()) {
    switchAsset(assets_.begin()->first, autoState_, "default");
  }
  if (continuous_) play("", true, false, 0);
  inited_ = true;
  for (auto& [k, c] : childMap_) c->init();
}

void VObject::copyTo(VSprite& dstSprite) {
  if (auto* dst = dynamic_cast<VObject*>(&dstSprite)) {
    dst->assets_ = assets_;
    if (!curAsset_.empty()) dst->switchAsset(curAsset_, curState_, "default");
    dst->type_ = type_;
    dst->childMap_.clear();
    dst->continuous_ = continuous_;
    dst->attrs_ = attrs_;
    for (const auto& [key, child] : childMap_) {
      if (!child) continue;
      auto c = VObject::create(context(), "", "");
      child->copyTo(*c);
      dst->childMap_[key] = c;
      dst->addChild(c);
    }
    dst->textMap_.clear();
    for (const auto& [key, text] : textMap_) {
      if (!text) continue;
      auto t = TextSprite::create(context());
      t->copyStyleFrom(*text);
      dst->textMap_[key] = t;
      dst->addChild(t);
    }
  }
  VSprite::copyTo(dstSprite);
}

void VObject::play(const std::string& name, bool loop, bool playChildren, double delayMs) {
  auto it = assets_.find(curAsset_);
  if (curAsset_.empty() || it == assets_.end()) return;
  const AssetState* st = it->second->state(curState_);
  if (!st || !st->anim) return;
  std::string fn = name;
  if (fn.empty()) {
    fn = st->play;
    if (fn.empty())
      if (const AssetState* d = it->second->state("default")) fn = d->play;
  }
  VSprite::play(fn, loop, playChildren, delayMs);
}

void VObject::stop(bool recursive) {
  VSprite::stop(recursive);
  auto it = assets_.find(curAsset_);
  if (it == assets_.end()) return;
  if (const AssetState* st = it->second->state(curState_)) setCurrentFrame(st->base);
}

}  // namespace choco::games::greatcc
