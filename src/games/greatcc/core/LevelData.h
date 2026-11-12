#pragma once

// A level file (docs/re/greatcc-shop-simulation.md 3.1; VLevel::LoadLevel, CSLevel::LoadCustomNode, VCustomerData::ParseNode,
// VOrderInfo::ParseNode): the length, the location, the scripted customers with their orders, the zeppelin orders, the lockdown
// window and the decay settings. Pure data parsed from `levels/levelNN.xml`; no rendering and no Lua.

#include <cfloat>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace choco::games::greatcc {

// eChocolateLayer (locations/defines.lua) and the shapes a level file names.
enum ChocolateShape { kShapeBar = 0, kShapeTruffle = 1, kShapeNovelty = 2, kShapeInfusion = 3, kShapeUnspecified = -2 };
enum ChocolateLayerType { kLayerEmpty = -1, kLayerBucket = 0, kLayerIngredient = 1, kLayerChocolate = 2, kLayerInfusion = 3, kLayerTopping = 4 };
enum OrderType { kOrderInvalid = -1, kOrderChocolate = 0, kOrderSauce = 1, kOrderDrink = 2 };

// VOrderInfo. `layer[i]` is -2 (not requested), -1 (include, random option) or the pinned option, for i in 1..4 (only 1, 3 and
// 4 are ever written by a file).
struct OrderInfo {
  int type = kOrderInvalid;
  int shape = kShapeUnspecified;
  int layer[5] = {-2, -2, -2, -2, -2};
  int mix = -1;           // 0 dark, 1 milk, 2 white; -1 random
  int layerBudget = -1;   // 1..3 for a chocolate order (read from the attributes), -1 until then

  // VOrderInfo::ParseNode on one <order> element's data. `fixBugs`: F2 reads the typo shape `kTruffler` as `kTruffle`; `fixed`
  // is set when that changed the outcome.
  struct Attrs {
    std::string type, shape, mix, layers;
    std::vector<std::pair<std::string, std::string>> layerNodes;  // (type, option) of each <layer> child, in order
  };
  static OrderInfo parse(const Attrs& a, bool fixBugs, bool* fixed);
};

// VChocolate::GetTypeFromString (case-insensitive; with or without the k prefix): -1 for an unknown word.
int chocolateShapeFromString(const std::string& s);
// VChocolateLayer::GetTypeFromString: -1 for an unknown word.
int chocolateLayerFromString(const std::string& s);
// VOrderInfo::GetOrderTypeFromString.
int orderTypeFromString(const std::string& s);

// VCustomerData.
struct CustomerData {
  std::string type;     // the key in the posse or the location's locals
  std::string name;     // the sprite name ("" = the type's own)
  std::string station;  // "" = any free counter
  float start = FLT_MAX;  // percent of the level time
  float end = FLT_MAX;    // parsed, never read by the shop
  float decay = -1, tip = -1;
  std::vector<OrderInfo> orders;
  // VCustomerData::InRange: start <= percent.
  bool inRange(float percent) const { return start <= percent; }
};

struct LevelData {
  int number = 0;
  std::string name;  // the <name> content as written (the original looks it up in the string table)
  std::string location;
  int minutes = 0, seconds = 0;
  float goal = 0, mastery = 0;  // the file's own, which no shipped level uses (levels/goals.xml has the real ones)
  std::vector<std::string> plots;
  std::vector<std::pair<std::string, std::string>> upgrades;
  bool needPrelevel = false;  // the constructor sets it for every level above 3
  int maxLayers = 3;
  float lockdownMin = -1, lockdownMax = -1;
  int chocolateDecay = 0;     // ms, 0 = never
  float customerDecay = 0;
  std::vector<std::shared_ptr<CustomerData>> customers;
  std::vector<OrderInfo> zeppelin;

  int durationMs() const { return minutes * 60000 + seconds * 1000; }
  bool needPlot() const { return !plots.empty(); }
  // CSLevel::InLockDown: min <= percent <= max, inclusive, against the raw floats.
  bool inLockDown(float percent) const { return !(percent < lockdownMin || lockdownMax < percent); }

  struct Result {
    bool parsed = false;
    std::string error;
    bool fixedF2 = false;  // the F2 reading changed an order
    bool fixedF1 = false;  // F1 closed an open <order> and the file then parsed
    bool partial = false;  // the file did not parse and the tree built up to the error was used (`error` says why)
  };
  // CSLevel(n) then LoadLevel: defaults first, then the file. A file that does not parse leaves the defaults (and says so).
  static Result load(int number, std::string_view xml, bool fixBugs, LevelData& out);
};

}  // namespace choco::games::greatcc
