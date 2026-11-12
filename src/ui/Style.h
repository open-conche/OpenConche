#pragma once

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "script/LuaState.h"

namespace choco::ui {

// What a window reads while it is created: one key at a time, with the lookup chain (own table, then the default style
// and its parents) hidden behind the interface. The script side implements it over a Lua table (script/LuaStyle.h);
// ui itself never sees Lua. A view is valid only for the duration of the native call that made it.
class Style {
 public:
  virtual ~Style() = default;
  // A number, or a string that converts to one. Nullopt when the key is absent.
  virtual std::optional<double> number(const char* key) const = 0;
  // A string or a number rendered as text. Nullopt when the key is absent.
  virtual std::optional<std::string> string(const char* key) const = 0;

  // A bool by the PopBool rules: a boolean, or a number (non-zero is true). Nullopt when the key is absent; other
  // types read as false.
  virtual std::optional<bool> boolean(const char* key) const = 0;
  // A table of numbers read at [1]..[count]; a missing or non-numeric entry becomes `def`. Nullopt when the key is
  // absent or not a table.
  virtual std::optional<std::vector<double>> numbers(const char* key, int count, double def) const = 0;

  // A table of strings read at [1]..[count]; a missing or non-string entry becomes "". Nullopt when the key is absent or
  // not a table. (The `graphics` list of a Button.)
  virtual std::optional<std::vector<std::string>> strings(const char* key, int count) const {
    (void)key, (void)count;
    return std::nullopt;
  }
  // A Lua function kept as a registry reference (`command`). Nullopt when the key is absent or not a function.
  virtual std::optional<script::LuaRef> function(const char* key) const {
    (void)key;
    return std::nullopt;
  }
  // Entries of a table-valued key, for `font = { face, size, Color(...) }`: the string at [index] (1-based), and the
  // numbers of the table at [index] read at [1]..[count]. Nullopt when absent or of the wrong type. Styles that have
  // no such tables keep these defaults.
  virtual std::optional<std::string> tableString(const char*, int) const { return std::nullopt; }
  virtual std::optional<std::vector<double>> tableNumbers(const char*, int, int, double) const { return std::nullopt; }

  // ---- nested read-only views (game data: ports, routes, Player) ----
  // The default-style chain applies only at the top level; a sub-table is read with plain gettable (metatables still
  // apply). A sub-view is valid only while its parent is, and sub-views must be destroyed in the reverse order they were
  // made (they share a stack). Styles with no nested data keep these defaults.
  // The table at `key`, or null if absent or not a table.
  virtual std::unique_ptr<const Style> table(const char* key) const {
    (void)key;
    return nullptr;
  }
  // The array length of this table (luaL_getn); 0 for none.
  virtual int size() const { return 0; }
  // Element [i] (1-based) if it is a table, else null.
  virtual std::unique_ptr<const Style> element(int i) const {
    (void)i;
    return nullptr;
  }
  virtual std::optional<double> numberAt(int i) const {
    (void)i;
    return std::nullopt;
  }
  virtual std::optional<std::string> stringAt(int i) const {
    (void)i;
    return std::nullopt;
  }
  // Calls `fn` with a view of every table-valued entry, in pairs order (the array part first, then the hash).
  virtual void forEachTable(const std::function<void(const Style&)>& fn) const { (void)fn; }
  // Calls `fn` with the name and value of every entry that has a string key and a number value (a map such as a recipe
  // `{sugar = 1}`), in no particular order.
  virtual void forEachNumber(const std::function<void(const std::string&, double)>& fn) const { (void)fn; }
  // Calls `fn` for every entry that has a string key (the array part is not visited): `table` is a view of the value when it
  // is a table (else null), `text` the value as text when it is a string or a number. Used for tables whose keys name
  // things (`children`, `assets`) and for the loose attributes of a game object.
  using EntryFn = std::function<void(const std::string& key, const Style* table, const std::optional<std::string>& text)>;
  virtual void forEachEntry(const EntryFn& fn) const { (void)fn; }

  bool booleanOr(const char* key, bool def) const { return boolean(key).value_or(def); }
  double numberOr(const char* key, double def) const { return number(key).value_or(def); }
  std::string stringOr(const char* key, const std::string& def = {}) const { return string(key).value_or(def); }
};

// A fixed key/value style, for tests and for windows made from C++.
class MapStyle : public Style {
 public:
  using Value = std::variant<double, std::string, std::vector<double>, std::vector<std::string>, std::shared_ptr<MapStyle>>;
  using Element = std::variant<double, std::string, std::shared_ptr<MapStyle>>;
  MapStyle() = default;
  MapStyle(std::map<std::string, Value> values) : values_(std::move(values)) {}
  MapStyle& set(const std::string& key, Value v) {
    values_[key] = std::move(v);
    return *this;
  }
  std::optional<double> number(const char* key) const override;
  std::optional<std::string> string(const char* key) const override;
  std::optional<bool> boolean(const char* key) const override;
  std::optional<std::vector<double>> numbers(const char* key, int count, double def) const override;
  std::optional<std::vector<std::string>> strings(const char* key, int count) const override;
  // Entries are keyed "<key>.<index>": set("font.1", "fonts/a.mvec") and set("font.3", std::vector<double>{...}).
  std::optional<std::string> tableString(const char* key, int index) const override;
  std::optional<std::vector<double>> tableNumbers(const char* key, int index, int count, double def) const override;

  // Nested data (a view is a copy of the nested table; its own children are shared): set(key, shared_ptr<MapStyle>) makes a sub-table, append() adds to the array part ([1], [2], ...).
  MapStyle& append(Element e) {
    array_.push_back(std::move(e));
    return *this;
  }
  std::unique_ptr<const Style> table(const char* key) const override;
  int size() const override { return static_cast<int>(array_.size()); }
  std::unique_ptr<const Style> element(int i) const override;
  std::optional<double> numberAt(int i) const override;
  std::optional<std::string> stringAt(int i) const override;
  void forEachTable(const std::function<void(const Style&)>& fn) const override;
  void forEachNumber(const std::function<void(const std::string&, double)>& fn) const override;
  void forEachEntry(const EntryFn& fn) const override;

 private:
  std::map<std::string, Value> values_;
  std::vector<Element> array_;
};

}  // namespace choco::ui
