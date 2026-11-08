#pragma once

#include <utility>

struct lua_State;

namespace choco::script {

class LuaRef;

// Owns the one Lua state. Counts live LuaRefs and aborts at destruction if any survive (an ownership mistake must fail
// loudly, also in release builds).
class LuaState {
 public:
  LuaState();  // base, string and table libraries only
  ~LuaState();
  LuaState(const LuaState&) = delete;
  LuaState& operator=(const LuaState&) = delete;

  lua_State* L() const { return L_; }
  int liveRefs() const { return live_; }

 private:
  friend class LuaRef;
  lua_State* L_ = nullptr;
  int live_ = 0;
};

// Move-only registry reference. Works from any thread of the state (the registry is shared).
class LuaRef {
 public:
  LuaRef() = default;
  // References the value at `index` of `from` (a main-state or coroutine stack).
  LuaRef(LuaState& state, lua_State* from, int index);
  LuaRef(LuaRef&& o) noexcept : state_(o.state_), ref_(o.ref_) { o.state_ = nullptr; o.ref_ = kNone; }
  LuaRef& operator=(LuaRef&& o) noexcept;
  LuaRef(const LuaRef&) = delete;
  LuaRef& operator=(const LuaRef&) = delete;
  ~LuaRef() { reset(); }

  bool valid() const { return state_ != nullptr; }
  explicit operator bool() const { return valid(); }
  void push(lua_State* L) const;  // pushes the value (nil for an empty ref)
  void reset();

 private:
  static constexpr int kNone = -1;
  LuaState* state_ = nullptr;
  int ref_ = kNone;
};

}  // namespace choco::script
