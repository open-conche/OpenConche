#include "games/choc3/factory/RecipeDesigner.h"

#include <algorithm>
#include <cctype>

#include "assets/AssetFS.h"
#include "script/AudioBridge.h"
#include "ui/ScriptSink.h"
#include "ui/WindowManager.h"
#include "ui/widgets/Widgets.h"

namespace choco::game {

namespace {

std::vector<RecipeDesigner*>& liveDesigners() {
  static std::vector<RecipeDesigner*> v;
  return v;
}

std::string lowered(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

bool endsWith(const std::string& s, std::string_view suffix) {
  return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

constexpr std::string_view kCustom = "custom/";

}  // namespace

RecipeDesigner::RecipeDesigner(ui::WindowManager& wm, script::ScriptContext& ctx) : Window(wm), ctx_(ctx), tex_(wm, &ctx.log) {
  liveDesigners().push_back(this);
}

RecipeDesigner::~RecipeDesigner() { std::erase(liveDesigners(), this); }

RecipeDesigner* RecipeDesigner::active() { return liveDesigners().empty() ? nullptr : liveDesigners().back(); }

void RecipeDesigner::sound(std::string_view name) const {
  script::soundEvent(ctx_, std::string(name));
}

void RecipeDesigner::setRecipeType(std::string_view category) {
  for (auto& l : lists_) l.clear();
  // The indices are kept, as in the original (SetRecipeType 0x32e32 only clears the lists): ui_kitchen.lua calls
  // SetRecipeType on every DesignMode() and expects GetLayerImage to still match what the windows show.
  keep_.clear();
  const std::string cat = lowered(std::string(category));
  for (const std::string& rel : ctx_.fs.list(std::string(kCustom) + cat)) {  // sorted, bytewise on the lowercased key
    const std::string name = lowered(rel);
    if (name.find("_highlight") != std::string::npos || !endsWith(name, ".png")) continue;
    const std::string base = name.substr(0, name.size() - 4);
    for (std::size_t n = 0; n < lists_.size(); ++n) {
      if (base.find("layer" + std::to_string(n + 1)) != std::string::npos) {
        lists_[n].push_back(base);
        if (auto t = tex_.optionalTexture(base)) keep_.push_back(std::move(t));
        break;
      }
    }
  }
  lists_[1].emplace_back();
  if (cat != "beverage") lists_[2].emplace_back();
  lists_[3].emplace_back();
}

int RecipeDesigner::layerSize(int layer) const {
  return layer >= 0 && layer < kLayers ? static_cast<int>(lists_[static_cast<std::size_t>(layer)].size()) : 0;
}

int RecipeDesigner::layerIndex(int layer) const { return layer >= 0 && layer < kLayers ? index_[static_cast<std::size_t>(layer)] : 0; }

std::string RecipeDesigner::entry(int layer, int index) const {
  if (index < 0 || index >= layerSize(layer)) return {};
  return lists_[static_cast<std::size_t>(layer)][static_cast<std::size_t>(index)];
}

std::string RecipeDesigner::layerImage(int layer) const {
  const std::string e = entry(layer, layerIndex(layer));
  return e.size() > kCustom.size() ? e.substr(kCustom.size()) : std::string();
}

int RecipeDesigner::setLayer(int layer, int index) {
  const int size = layerSize(layer);
  if (size == 0) return 0;
  const int applied = ((index % size) + size) % size;
  index_[static_cast<std::size_t>(layer)] = applied;
  showLayer(layer);
  return applied;
}

// Option pictures at scale 0.5, display pictures at 1.0 (TImage::SetTexture's third argument is a scale). An empty entry
// hides all four windows; a missing `_highlight` hides only the highlight pair.
void RecipeDesigner::showLayer(int layer) {
  const std::string n = std::to_string(layer);
  const std::string image = entry(layer, layerIndex(layer));
  const bool hasLayer = !image.empty();
  const bool hasHighlight = hasLayer && ctx_.fs.probe(image + "_highlight", assets::kTextureExtensions).has_value();
  auto apply = [&](const std::string& name, bool visible, const std::string& pic, double scale) {
    auto* w = dynamic_cast<ui::BitmapWindow*>(find(name));
    if (!w) return;
    w->setShown(visible);
    if (visible) w->setBitmap(pic, scale);
  };
  apply("option_layer" + n, hasLayer, image, 0.5);
  apply("option_highlight" + n, hasHighlight, image + "_highlight", 0.5);
  apply("display_layer" + n, hasLayer, image, 1.0);
  apply("display_highlight" + n, hasHighlight, image + "_highlight", 1.0);
}

void RecipeDesigner::randomDesign() {
  timer_ = 0;
  sound("randomize");
  manager().startWindowAnimation(id(), kTickMs);
}

// OnTaskAnimate (0x33872). The lower bounds 1 and 2 for layers 2 and 3 are the original's.
bool RecipeDesigner::onAnimate() {
  timer_ += kTickMs;
  if (timer_ >= kEndMs) return false;
  std::string lua;
  auto roll = [&](int layer, int lo) {
    const int size = layerSize(layer);
    if (size == 0) return;
    setLayer(layer, ctx_.rng.range(lo, size - 1));
    lua += "SetRecipeTint(" + std::to_string(layer + 1) + ", RandRange(1,100));";
  };
  if (timer_ <= 1000) roll(0, 0);
  if (timer_ <= 1500) roll(1, 1);
  if (timer_ <= 1800) roll(2, 2);
  roll(3, 0);
  // The script may destroy this window, so nothing is touched after the call.
  if (!lua.empty()) {
    if (ui::ScriptSink* sink = manager().scriptSink()) sink->runString(lua);
  }
  return true;
}

std::string RecipeDesigner::detail() const {
  std::string s;
  for (int i = 0; i < kLayers; ++i) s += " layer" + std::to_string(i + 1) + "=" + std::to_string(layerIndex(i)) + "/" + std::to_string(layerSize(i));
  return s;
}

}  // namespace choco::game
