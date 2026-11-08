#pragma once

#include <cstddef>
#include <string_view>

#include "script/Bind.h"

namespace choco::script {

// Safe value a stub returns, parsed from the `returns` column of the API tables.
enum class Ret { None, Nil, String, Number, Bool, Table };

struct StubRow {
  const char* name;
  Ret ret;
  const char* args;  // the documented argument list, for the log reader
};

// Registers a row table as stub globals (the rows must outlive the state). Each stub logs "[unimplemented] Name" once and counts
// its calls in ctx.stubs.
void registerStubRows(lua_State* L, ScriptContext& ctx, const StubRow* rows, std::size_t count);

// A generated ALIAS row: `name` is bound as Chocolatier 3's native `target`.
struct AliasRow {
  const char* name;
  const char* target;
};

// Binds each alias as the same C++ function under the new name (so argument warnings name the function the script called)
// when `target` is a real native (in ctx.nativeNames); otherwise `name` stays a logged stub (Ret::Nil), so the gap is
// visible. Returns the number of rows bound for real.
std::size_t registerAliasRows(lua_State* L, ScriptContext& ctx, const AliasRow* rows, std::size_t count);

}  // namespace choco::script
