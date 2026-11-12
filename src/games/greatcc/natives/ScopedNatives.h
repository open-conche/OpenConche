#pragma once

// Natives that exist only while their owner window lives (docs/re/greatcc-boot-and-natives.md 2.1, docs/design/greatcc.md
// 3.2): the window's constructor binds them, its destructor sets each global back to nil. A window holds one as a member.
// It always works on the main Lua state (never a coroutine's thread, which may be collected before the window dies); that
// state outlives the window tree (script-host design 2), so the destructor may always touch it. When a newer owner has
// bound the same name since (a window swapped for its successor before the old one is destroyed), the older owner's
// death leaves the newer binding alone.

#include <string>
#include <vector>

#include "script/Bind.h"
#include "ui/Message.h"

namespace choco::games::greatcc {

class ScopedNatives {
 public:
  explicit ScopedNatives(script::ScriptContext& ctx);
  ~ScopedNatives();
  ScopedNatives(const ScopedNatives&) = delete;
  ScopedNatives& operator=(const ScopedNatives&) = delete;

  // Binds a typed native (script/Bind.h) as the global `name` until this object dies.
  template <auto Fn>
  void bind(const char* name) {
    bindRaw(name, &script::detail::trampoline<Fn>);
  }
  // The same for a raw lua_CFunction.
  void bindRaw(const char* name, lua_CFunction fn);
  // A raw native that acts on its owner window: the closure carries the owner's WindowId as a third upvalue, which the
  // function reads with ownerOf(L) and resolves through the window manager on each call (design 3.2: nothing keeps a
  // pointer to a window).
  void bindOwned(const char* name, lua_CFunction fn, ui::WindowId owner);
  // The owner id of the running owned native, or 0.
  static ui::WindowId ownerOf(lua_State* L);

 private:
  lua_State* L_;
  script::ScriptContext& ctx_;
  std::vector<std::string> names_;
};

}  // namespace choco::games::greatcc
