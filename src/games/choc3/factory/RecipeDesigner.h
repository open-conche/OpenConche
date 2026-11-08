#pragma once

#include <array>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "games/common/Textures.h"
#include "script/ScriptContext.h"
#include "ui/Window.h"

namespace choco::game {

// RecipeDesigner (docs/design/factories.md 3.6, docs/re/coffee-factory-and-recipes.md 5, ledger-and-game-natives.md
// 10.3): the layer chooser behind the kitchen's design mode. It holds four lists of `custom/<category>/layerN_NN`
// pictures and the current index of each, and shows the current ones in the child windows `option_layer<n>`,
// `option_highlight<n>`, `display_layer<n>` and `display_highlight<n>`. It never tints; the Lua global SetRecipeTint does.
class RecipeDesigner : public ui::Window {
 public:
  static constexpr int kLayers = 4;
  static constexpr int kTickMs = 100;
  static constexpr int kEndMs = 2000;

  RecipeDesigner(ui::WindowManager& wm, script::ScriptContext& ctx);
  ~RecipeDesigner() override;

  // The newest live designer: the target of the four natives. Null when none exists.
  static RecipeDesigner* active();

  std::string_view typeName() const override { return "RecipeDesigner"; }

  // Lists `custom/<category>/` (sorted), skipping `_highlight` and non-.png files, and files each name by `layer1..4`.
  // Layers 2 and 4 get a trailing "" (none), layer 3 too unless the category is `beverage`. The indices and the
  // child windows are not touched, as in the original (`entry` gives "" for an index past a shorter new list).
  void setRecipeType(std::string_view category);
  // SetLayer: stores (index + size) % size for the layer 0..3, updates the four child windows, and returns the index
  // applied. An empty list or a bad layer changes nothing and gives 0.
  int setLayer(int layer, int index);
  // GetLayerImage: the current entry without the leading "custom/" ("bar/layer1_01"), "" for none.
  std::string layerImage(int layer) const;
  // RandomDesign: timer 0, sound "randomize", and a 100 ms animation (no catch-up).
  void randomDesign();
  bool onAnimate() override;

  // For tests and the dump.
  int layerSize(int layer) const;
  int layerIndex(int layer) const;
  std::string entry(int layer, int index) const;  // the full name, e.g. "custom/bar/layer1_01"; "" for none
  int timer() const { return timer_; }
  std::string detail() const override;

 private:
  void sound(std::string_view name) const;
  void showLayer(int layer);

  script::ScriptContext& ctx_;
  Textures tex_;
  std::array<std::vector<std::string>, kLayers> lists_;
  std::array<int, kLayers> index_{};
  int timer_ = 0;
  std::vector<std::shared_ptr<gfx::Texture>> keep_;  // the asset map: the textures stay loaded while the window lives
};

}  // namespace choco::game
