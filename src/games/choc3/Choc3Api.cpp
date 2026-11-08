#include "games/choc3/Choc3Api.h"

#include <algorithm>
#include <iterator>

namespace choco::script {

namespace {

const StubRow kRows[] = {
#define STUB(name, ret, args) {name, ret, args},
#define WINDOW_TYPE(name)
#define API_NAME(name)
#include "games/choc3/generated/Stubs_choc3.inc"
#undef STUB
#undef WINDOW_TYPE
#undef API_NAME
};

const char* const kTypes[] = {
#define STUB(name, ret, args)
#define WINDOW_TYPE(name) name,
#define API_NAME(name)
#include "games/choc3/generated/Stubs_choc3.inc"
#undef STUB
#undef WINDOW_TYPE
#undef API_NAME
};

const char* const kApi[] = {
#define STUB(name, ret, args)
#define WINDOW_TYPE(name)
#define API_NAME(name) name,
#include "games/choc3/generated/Stubs_choc3.inc"
#undef STUB
#undef WINDOW_TYPE
#undef API_NAME
};

}  // namespace

const StubRow* stubRows(std::size_t& count) {
  count = sizeof(kRows) / sizeof(kRows[0]);
  return kRows;
}

const char* const* stubWindowTypes(std::size_t& count) {
  count = sizeof(kTypes) / sizeof(kTypes[0]);
  return kTypes;
}

bool isApiName(std::string_view name) {
  return std::any_of(std::begin(kApi), std::end(kApi), [&](const char* n) { return name == n; });
}

void registerStubs(lua_State* L, ScriptContext& ctx) { registerStubRows(L, ctx, kRows, sizeof(kRows) / sizeof(kRows[0])); }

}  // namespace choco::script
