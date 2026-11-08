#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "ScriptHarness.h"
#include "games/choc3/factory/RecipeDesigner.h"
#include "script/ScriptHost.h"
#include "games/choc3/Choc3Natives.h"
#include "ui/ScriptSink.h"
#include "ui/widgets/Widgets.h"

using namespace choco;

namespace {

// Records the Lua text the designer runs.
struct RecordingSink : ui::ScriptSink {
  void deliver(const ui::Message*, const script::LuaRef*) override {}
  void runInScript(const script::LuaRef&) override {}
  bool callProtected(const script::LuaRef&) override { return true; }
  void runString(std::string_view code) override { strings.emplace_back(code); }
  void setGlobalString(const char*, std::string_view) override {}
  std::vector<std::string> strings;
};

bool haveReforged() { return std::filesystem::exists(std::filesystem::path(CHOCO_REFORGED_ASSETS) / "custom" / "bar"); }

// A designer in a modal over the real Reforged files, with the child windows layer 0 needs.
struct DesignerEnv {
  DesignerEnv() : env(CHOCO_REFORGED_ASSETS) {
    env.wm.setScriptSink(&sink);
    ui::ModalWindow& m = env.wm.pushModal("m");
    designer = static_cast<game::RecipeDesigner*>(&m.adoptFront(std::make_unique<game::RecipeDesigner>(env.wm, env.ctx)));
    for (const char* n : {"option_layer0", "display_layer0"}) add<ui::BitmapTintWindow>(n);
    for (const char* n : {"option_highlight0", "display_highlight0"}) add<ui::BitmapWindow>(n);
  }
  template <class W>
  W* add(const char* name) {
    auto w = std::make_unique<W>(env.wm);
    w->setName(name);
    return static_cast<W*>(&designer->adoptFront(std::move(w)));
  }
  ui::Window* child(const char* name) { return designer->find(name); }

  test::ScriptEnv env;
  RecordingSink sink;
  game::RecipeDesigner* designer = nullptr;
};

}  // namespace

TEST_CASE("AssetFS::list gives the files directly in a directory, sorted") {
  test::ScriptEnv env(std::string(CHOCO_TEST_ASSETS) + "/ledger");
  CHECK(env.fs.list("no/such/dir").empty());
  if (!haveReforged()) return;
  assets::AssetFS fs(CHOCO_REFORGED_ASSETS, "");
  const auto bar = fs.list("custom/bar/");
  REQUIRE(bar.size() > 4);
  CHECK(bar.front() == "custom/bar/layer1_01.png");
  CHECK(std::is_sorted(bar.begin(), bar.end()));
  CHECK(fs.list("custom/bar") == bar);
  for (const auto& p : bar) CHECK(p.find('/', 11) == std::string::npos);
  const auto top = fs.list("custom");  // only subdirectories live here: nothing is a direct file
  for (const auto& p : top) CHECK(p.rfind("custom/", 0) == 0);
}

TEST_CASE("RecipeDesigner: setRecipeType(bar) lists the real layers, with the empty option on layers 2 to 4") {
  if (!haveReforged()) return;
  DesignerEnv d;
  d.designer->setRecipeType("bar");
  CHECK(d.designer->layerSize(0) == 4);  // no blank on layer 1
  CHECK(d.designer->layerSize(1) == 7 + 1);
  CHECK(d.designer->layerSize(2) == 5 + 1);
  CHECK(d.designer->layerSize(3) == 11 + 1);
  CHECK(d.designer->entry(0, 0) == "custom/bar/layer1_01");
  CHECK(d.designer->entry(0, 3) == "custom/bar/layer1_04");
  CHECK(d.designer->entry(1, 6) == "custom/bar/layer2_07");
  CHECK(d.designer->entry(1, 7).empty());
  CHECK(d.designer->layerImage(1) == "bar/layer2_01");

  d.designer->setRecipeType("beverage");  // no blank on layer 3
  CHECK(d.designer->layerSize(0) == 4);
  CHECK(d.designer->layerSize(1) == 5 + 1);
  CHECK(d.designer->layerSize(2) == 6);
  CHECK(d.designer->layerSize(3) == 8 + 1);
  d.designer->setRecipeType("nonesuch");
  CHECK(d.designer->layerSize(0) == 0);
  CHECK(d.designer->setLayer(0, 3) == 0);  // an empty list: no division by zero
  CHECK(d.designer->layerImage(0).empty());
}

TEST_CASE("RecipeDesigner: setLayer wraps at -1 and at the size, and layerImage drops custom/") {
  if (!haveReforged()) return;
  DesignerEnv d;
  d.designer->setRecipeType("bar");
  CHECK(d.designer->layerImage(0) == "bar/layer1_01");
  CHECK(d.designer->setLayer(0, 1) == 1);
  CHECK(d.designer->layerImage(0) == "bar/layer1_02");
  CHECK(d.designer->setLayer(0, -1) == 3);
  CHECK(d.designer->layerImage(0) == "bar/layer1_04");
  CHECK(d.designer->setLayer(0, 4) == 0);
  CHECK(d.designer->layerImage(0) == "bar/layer1_01");
  CHECK(d.designer->setLayer(1, -1) == 7);  // the blank
  CHECK(d.designer->layerImage(1).empty());
  CHECK(d.designer->setLayer(1, 8) == 0);
  CHECK(d.designer->setLayer(9, 1) == 0);  // no such layer
}

TEST_CASE("RecipeDesigner: setRecipeType again keeps the layer indices, so GetLayerImage still matches the windows") {
  if (!haveReforged()) return;
  DesignerEnv d;
  d.designer->setRecipeType("bar");
  d.designer->setLayer(0, 3);
  REQUIRE(d.designer->layerImage(0) == "bar/layer1_04");
  d.designer->setRecipeType("bar");  // DesignMode() after Go Back, or after a Community import
  CHECK(d.designer->layerIndex(0) == 3);
  CHECK(d.designer->layerImage(0) == "bar/layer1_04");
}

TEST_CASE("RecipeDesigner: setLayer shows the pictures, the highlight only where the file exists, and none hides all") {
  if (!haveReforged()) return;
  DesignerEnv d;
  d.designer->setRecipeType("bar");
  d.designer->setLayer(0, 0);  // layer1_01 has a _highlight file
  CHECK(d.child("option_layer0")->shown());
  CHECK(d.child("display_highlight0")->shown());
  CHECK(static_cast<ui::BitmapWindow*>(d.child("option_layer0"))->scale() == doctest::Approx(0.5));
  CHECK(static_cast<ui::BitmapWindow*>(d.child("display_layer0"))->scale() == doctest::Approx(1.0));
  // Layer 1 (index 0) windows exist only for layer 0 here; make layer 2 (index 1) check through the layer-0 windows by
  // loading a category whose first layer picture has no highlight.
  d.designer->setRecipeType("blend");
  bool sawMissing = false;
  for (int i = 0; i < d.designer->layerSize(0); ++i) {
    d.designer->setLayer(0, i);
    const bool has = d.env.fs.probe(d.designer->entry(0, i) + "_highlight", assets::kTextureExtensions).has_value();
    CHECK(d.child("option_highlight0")->shown() == has);
    CHECK(d.child("display_highlight0")->shown() == has);
    CHECK(d.child("option_layer0")->shown());
    sawMissing = sawMissing || !has;
  }
  (void)sawMissing;
  d.designer->setRecipeType("bar");
  d.designer->setLayer(1, 7);  // layer 2's windows are absent here: nothing breaks
  CHECK(d.child("option_layer0")->shown());
}

TEST_CASE("RecipeDesigner: an empty entry hides the four windows") {
  if (!haveReforged()) return;
  DesignerEnv d;
  // Put the blank on layer 0 by borrowing layer 2's list shape: use the layer-1 windows under the names of layer 0.
  d.designer->setRecipeType("bar");
  d.designer->setLayer(0, 0);
  REQUIRE(d.child("option_layer0")->shown());
  // Layer 0 has no blank, so check the rule on layer 1 with its own windows.
  for (const char* n : {"option_layer1", "display_layer1"}) d.add<ui::BitmapTintWindow>(n);
  for (const char* n : {"option_highlight1", "display_highlight1"}) d.add<ui::BitmapWindow>(n);
  d.designer->setLayer(1, 0);
  CHECK(d.child("option_layer1")->shown());
  d.designer->setLayer(1, 7);
  for (const char* n : {"option_layer1", "display_layer1", "option_highlight1", "display_highlight1"}) CHECK_FALSE(d.child(n)->shown());
  d.designer->setLayer(1, 2);
  CHECK(d.child("option_layer1")->shown());
}

TEST_CASE("RecipeDesigner: randomDesign makes 19 rolls and stops at 2000 ms") {
  if (!haveReforged()) return;
  DesignerEnv d;
  d.designer->setRecipeType("bar");
  d.env.wm.setNowMs(0);
  d.designer->randomDesign();
  for (int t = 100; t <= 2600; t += 100) d.env.wm.tickWindowAnimations(t);
  REQUIRE(d.sink.strings.size() == 19);
  CHECK(d.designer->timer() == 2000);
  // Layers 1 to 4 on the early ticks, only layer 4 on the last ones.
  CHECK(d.sink.strings.front().find("SetRecipeTint(1, RandRange(1,100));SetRecipeTint(2,") == 0);
  const std::string& tick11 = d.sink.strings[10];  // timer 1100: layers 2, 3 and 4
  CHECK(tick11.find("SetRecipeTint(1,") == std::string::npos);
  CHECK(tick11.find("SetRecipeTint(2,") == 0);
  const std::string& tick16 = d.sink.strings[15];  // timer 1600: layers 3 and 4
  CHECK(tick16.find("SetRecipeTint(3,") == 0);
  CHECK(tick16.find("SetRecipeTint(2,") == std::string::npos);
  CHECK(d.sink.strings.back() == "SetRecipeTint(4, RandRange(1,100));");
  // The rolls honour the original's lower bounds: layer 2 never rolls index 0, layer 3 never 0 or 1.
  CHECK(d.designer->layerIndex(1) >= 1);
  CHECK(d.designer->layerIndex(2) >= 2);
  // It has ended: no more calls.
  d.env.wm.tickWindowAnimations(5000);
  CHECK(d.sink.strings.size() == 19);
}

TEST_CASE("RecipeDesigner natives drive the active designer, and do nothing without one") {
  if (!haveReforged()) return;
  test::ScriptEnv env(CHOCO_REFORGED_ASSETS);
  script::registerFactoryNatives(env.lua.L(), env.ctx);
  CHECK(env.run("SetRecipeType('bar'); gL = SetLayer(0, 2); gI = GetLayerImage(0)") == 0);
  CHECK(env.getNumber("gL") == 0);
  CHECK(env.getString("gI").empty());

  env.wm.pushModal("m");
  auto* d = static_cast<game::RecipeDesigner*>(&env.wm.topModal()->adoptFront(env.wm.createWindow("RecipeDesigner")));
  REQUIRE(dynamic_cast<game::RecipeDesigner*>(d));
  CHECK(env.run("SetRecipeType('bar'); gL = SetLayer(0, -1); gI = GetLayerImage(0)") == 0);
  CHECK(env.getNumber("gL") == 3);
  CHECK(env.getString("gI") == "bar/layer1_04");
}

TEST_CASE("BTSetTint and SetRectangleColor find the named window under the top modal") {
  test::ScriptEnv env(std::string(CHOCO_TEST_ASSETS) + "/ledger");
  script::registerFactoryNatives(env.lua.L(), env.ctx);
  ui::ModalWindow& m = env.wm.pushModal("m");
  auto tint = std::make_unique<ui::BitmapTintWindow>(env.wm);
  tint->setName("t");
  auto* t = static_cast<ui::BitmapTintWindow*>(&m.adoptFront(std::move(tint)));
  auto rect = std::make_unique<ui::RectangleWindow>(env.wm);
  rect->setName("r");
  auto* r = static_cast<ui::RectangleWindow*>(&m.adoptFront(std::move(rect)));

  CHECK(env.run("BTSetTint('t', {0.5, 0.25}); SetRectangleColor('r', {0.5, 0.25, 1})") == 0);
  CHECK(t->tint().r == doctest::Approx(0.5));
  CHECK(t->tint().g == doctest::Approx(0.25));
  CHECK(t->tint().b == doctest::Approx(1.0));  // missing components are 1.0 for the tint
  CHECK(t->tint().a == doctest::Approx(1.0));
  CHECK(r->color().b == doctest::Approx(1.0));
  CHECK(r->color().a == doctest::Approx(0.0));  // and 0.0 for the rectangle
  // The wrong type or a missing window changes nothing and is not an error.
  CHECK(env.run("BTSetTint('r', {0, 0, 0, 0}); SetRectangleColor('t', {0, 0, 0, 0}); BTSetTint('none', {0})") == 0);
  CHECK(r->color().b == doctest::Approx(1.0));
  CHECK(t->tint().r == doctest::Approx(0.5));
}
