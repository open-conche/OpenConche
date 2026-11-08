#include "script/LuaState.h"

#include <cstdio>
#include <cstdlib>

#include "script/LuaApi.h"
#include "script/LuaCompat.h"

namespace choco::script {

LuaState::LuaState() {
  L_ = lua_open();
  luaopen_base(L_);  // includes coroutine and _LOADED in 5.0
  luaopen_string(L_);
  installGameStringMeta(L_);
  luaopen_table(L_);
  lua_settop(L_, 0);
  lua_settop(L_, 0);
}

LuaState::~LuaState() {
  if (live_ != 0) {
    std::fprintf(stderr, "LuaState destroyed with %d live LuaRef(s)\n", live_);
    std::fflush(stderr);
    std::abort();
  }
  if (L_) lua_close(L_);
}

LuaRef::LuaRef(LuaState& state, lua_State* from, int index) {
  lua_pushvalue(from, index);
  ref_ = luaL_ref(from, LUA_REGISTRYINDEX);  // pops the value
  state_ = &state;
  ++state.live_;
}

LuaRef& LuaRef::operator=(LuaRef&& o) noexcept {
  if (this != &o) {
    reset();
    state_ = o.state_;
    ref_ = o.ref_;
    o.state_ = nullptr;
    o.ref_ = kNone;
  }
  return *this;
}

void LuaRef::push(lua_State* L) const {
  if (!state_) lua_pushnil(L);
  else lua_rawgeti(L, LUA_REGISTRYINDEX, ref_);
}

void LuaRef::reset() {
  if (!state_) return;
  luaL_unref(state_->L_, LUA_REGISTRYINDEX, ref_);
  --state_->live_;
  state_ = nullptr;
  ref_ = kNone;
}

}  // namespace choco::script
