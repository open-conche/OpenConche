#pragma once

// VValueManager / VValue (docs/re/greatcc-boot-and-natives.md 4.2): the named numbers of values.xml. A value has a base, an
// optional clamp, a `saved` flag and modifiers; GetValue applies the modifiers (last first) as percentages of the base and
// clamps. Saved values live in Progress as `value.<name>`, as exact doubles.

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "games/greatcc/core/Progress.h"

namespace choco::games::greatcc {

class ValueManager {
 public:
  // The callbacks VUpgradeManager::IsActive(name, category) and VPlotManager::IsPlotItemActive(name) answer; both default to
  // "not active" until those managers exist.
  using UpgradeActive = std::function<bool(const std::string& upgrade, const std::string& category)>;
  using PlotActive = std::function<bool(const std::string& item)>;

  explicit ValueManager(Progress& progress) : progress_(progress) {}

  // Keeps `xml` (values.xml) as the schema and builds every value from it. False when it does not parse (the table is then
  // empty).
  bool load(std::string_view xml);
  // Clears the table and builds it again from the schema, reading the saved values of the current user (the new user's, after
  // a switch). Runtime-only values go back to their schema base.
  void reload();

  void setUpgradeActive(UpgradeActive f) { upgradeActive_ = std::move(f); }
  void setPlotActive(PlotActive f) { plotActive_ = std::move(f); }

  bool exists(const std::string& name) const { return values_.count(name) != 0; }
  size_t size() const { return values_.size(); }

  // GetManagedReal / GetManagedInt / GetManagedUint: a missing value is created on the spot (unsaved, base = def, no limits).
  double getReal(const std::string& name, double def);
  int getInt(const std::string& name, int def);  // floor of the value taken as a float
  std::uint32_t getUint(const std::string& name, std::uint32_t def);

  // SetManagedValue: clamped to [min, max], stored, written to Progress when `save` and the value is saved and there is a
  // current user. A missing value is created with base `v` (no clamp).
  void setValue(const std::string& name, double v, bool save);
  // AddToValue / SubtractFromValue: unclamped; a missing value starts from `def`.
  void addToValue(const std::string& name, double delta, double def, bool save);
  void subtractFromValue(const std::string& name, double delta, double def, bool save) { addToValue(name, -delta, def, save); }
  // GetProduct(name, x, clamp): the value (1 for one that does not exist) times `x` in single precision, held to [min, max] when `clamp`.
  float getProduct(const std::string& name, float x, bool clamp);
  // GetSum(name, delta, clamp): the value plus `delta` in single precision, held to [min, max] when `clamp`.
  float getSum(const std::string& name, float delta, bool clamp);
  // The clamp; FLT_MIN (the smallest positive float, as the original) and FLT_MAX for a name that does not exist.
  double minOf(const std::string& name) const;
  double maxOf(const std::string& name) const;
  // SaveValues: writes every saved value.
  void saveAll();

 private:
  struct Modifier {
    int type = 3;  // 0 upgrade, 1 plot, 2 byval, 3 always
    double amount = 0;
    std::string name, category;
  };
  struct Value {
    double base = 0, initial = 0;
    double min, max;
    bool saved = false;
    std::vector<Modifier> modifiers;
  };

  double evaluate(const std::string& name);
  Value& ensure(const std::string& name, double def);
  void store(const std::string& name, const Value& v);

  Progress& progress_;
  std::string schema_;
  std::map<std::string, Value> values_;
  UpgradeActive upgradeActive_;
  PlotActive plotActive_;
  int depth_ = 0;  // nesting of byval reads, so a value that names itself ends
};

}  // namespace choco::games::greatcc
