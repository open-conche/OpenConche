#pragma once

#include "script/LuaApi.h"
#include <functional>
#include <memory>

#include "ui/Style.h"

namespace choco::script {

// ui::Style over a Lua table (window-system design section 1, "Style lookup"; TWindowStyle::_GetTag): the table itself
// (gettable, so metatables apply), then the global gCurrentDefaultStyle read at call time and its `parent` chain. The
// table's own `parent` is not followed. The view is valid only while the table's stack slot is.
class LuaStyle : public ui::Style {
 public:
  // `state` is needed only for function(): the LuaRef is made through it. Without it function() finds nothing.
  LuaStyle(lua_State* L, int tableIndex, LuaState* state = nullptr);
  // A view of the table alone: no gCurrentDefaultStyle chain (a sprite table handed to a native is not a window style).
  static LuaStyle plainView(lua_State* L, int tableIndex, LuaState* state = nullptr) { return LuaStyle(L, tableIndex, state, true, false); }
  std::optional<double> number(const char* key) const override;
  std::optional<std::string> string(const char* key) const override;
  std::optional<bool> boolean(const char* key) const override;
  std::optional<std::vector<double>> numbers(const char* key, int count, double def) const override;
  std::optional<std::vector<std::string>> strings(const char* key, int count) const override;
  std::optional<LuaRef> function(const char* key) const override;
  std::optional<std::string> tableString(const char* key, int index) const override;
  std::optional<std::vector<double>> tableNumbers(const char* key, int index, int count, double def) const override;

  ~LuaStyle() override;
  // A registry reference to the table this view looks at, for a window that must read its own style again after its creation
  // call has returned (an empty reference when the view has no LuaState).
  LuaRef tableRef() const;
  // Nested read-only views (ui::Style). Sub-tables are read with plain gettable and no default-style chain. A sub-view
  // owns the stack slot of its table and drops it when destroyed, so sub-views must go in reverse order of creation.
  std::unique_ptr<const ui::Style> table(const char* key) const override;
  int size() const override;
  std::unique_ptr<const ui::Style> element(int i) const override;
  std::optional<double> numberAt(int i) const override;
  std::optional<std::string> stringAt(int i) const override;
  void forEachTable(const std::function<void(const ui::Style&)>& fn) const override;
  void forEachNumber(const std::function<void(const std::string&, double)>& fn) const override;
  void forEachEntry(const EntryFn& fn) const override;

 private:
  LuaStyle(lua_State* L, int tableIndex, LuaState* state, bool plain, bool owns);
  // Pushes the found value and returns true, or pushes nothing.
  bool lookup(const char* key) const;
  bool lookupIndexed(const char* key, int index) const;
  lua_State* L_;
  int table_;  // absolute index
  LuaState* state_;
  bool plain_ = false;  // a sub-table: no default-style chain
  bool owns_ = false;   // the table's stack slot (and anything above) is dropped on destruction
};

}  // namespace choco::script
