#include <doctest/doctest.h>

#include <algorithm>
#include <cstring>
#include <limits>
#include <optional>
#include <stdexcept>
#include <tuple>

#include "ScriptHarness.h"
#include "script/Loaders.h"

using namespace choco::script;
using choco::test::ScriptEnv;

namespace {

std::string gSeen;

void tInt(ScriptContext&, int v) { gSeen = "int:" + std::to_string(v); }
void tDouble(ScriptContext&, double v) { gSeen = "double:" + std::to_string(v); }
void tU32(ScriptContext&, std::uint32_t v) { gSeen = "u32:" + std::to_string(v); }
void tBool(ScriptContext&, bool v) { gSeen = v ? "bool:true" : "bool:false"; }
void tStr(ScriptContext&, std::string v) { gSeen = "str:" + v; }
void tOpt(ScriptContext&, std::optional<int> v) { gSeen = v ? "opt:" + std::to_string(*v) : "opt:none"; }
void tTable(ScriptContext&, TableArg t) {
  gSeen = t.valid() ? "table:" + std::to_string(luaL_getn(t.L, t.index)) : "table:invalid";
}
LuaRef gKept;
void tRef(ScriptContext&, LuaRef r) {
  gSeen = r.valid() ? "ref:valid" : "ref:empty";
  gKept = std::move(r);
}
void tColor(ScriptContext&, ColorArg c) {
  gSeen = "color:" + std::to_string(c.r) + "," + std::to_string(c.g) + "," + std::to_string(c.b) + "," + std::to_string(c.a);
}
void tMissing(ScriptContext&, int a, std::string b, std::optional<double> c) {
  gSeen = std::to_string(a) + "|" + b + "|" + (c ? "c" : "nocopt");
}
std::tuple<int, std::string, bool> tTuple(ScriptContext&) { return {7, "seven", true}; }
std::optional<double> tOptRet(ScriptContext&, bool give) {
  if (give) return 2.5;
  return std::nullopt;
}
void tThrow(ScriptContext&) { throw std::runtime_error("kaput"); }
std::string tConstRef(ScriptContext&, const std::string& s) { return s + "!"; }

struct BindEnv : ScriptEnv {
  BindEnv() : ScriptEnv(CHOCO_TEST_ASSETS) {
    lua_State* L = lua.L();
    bind<tInt>(L, ctx, "TInt");
    bind<tDouble>(L, ctx, "TDouble");
    bind<tU32>(L, ctx, "TU32");
    bind<tBool>(L, ctx, "TBool");
    bind<tStr>(L, ctx, "TStr");
    bind<tOpt>(L, ctx, "TOpt");
    bind<tTable>(L, ctx, "TTable");
    bind<tRef>(L, ctx, "TRef");
    bind<tColor>(L, ctx, "TColor");
    bind<tMissing>(L, ctx, "TMissing");
    bind<tTuple>(L, ctx, "TTuple");
    bind<tOptRet>(L, ctx, "TOptRet");
    bind<tThrow>(L, ctx, "TThrow");
    bind<tConstRef>(L, ctx, "TConstRef");
    run("function nvals(...) return arg.n end");
  }
  ~BindEnv() { gKept.reset(); }
  std::string call(const std::string& code) {
    gSeen.clear();
    REQUIRE_MESSAGE(run(code, "@test/bind.lua") == 0, lastError);
    return gSeen;
  }
  int warnings() const { return log.count("[lua-arg]"); }
};

}  // namespace

TEST_CASE("bind: numbers coerce leniently") {
  BindEnv e;
  CHECK(e.call("TInt(3.9)") == "int:3");
  CHECK(e.call("TInt(-3.9)") == "int:-3");
  CHECK(e.call("TInt('12')") == "int:12");
  CHECK(e.call("TInt(1e30)") == "int:2147483647");
  CHECK(e.call("TInt(-1e30)") == "int:-2147483648");
  CHECK(e.warnings() == 0);
  CHECK(e.call("TDouble('2.5')") == "double:2.500000");
  CHECK(e.call("TU32(-5)") == "u32:0");
  CHECK(e.call("TU32(1e12)") == "u32:4294967295");
  CHECK(e.call("TU32(4000000000)") == "u32:4000000000");
  CHECK(e.warnings() == 0);

  CHECK(e.call("TInt('abc')") == "int:0");
  CHECK(e.call("TInt(nil)") == "int:0");
  CHECK(e.call("TInt({})") == "int:0");
  CHECK(e.call("TInt()") == "int:0");
  CHECK(e.call("TDouble(true)") == "double:0.000000");
  CHECK(e.call("TU32('x')") == "u32:0");
}

TEST_CASE("bind: warning text and warn-once per native, argument and type") {
  BindEnv e;
  e.call("local x = 1\nTStr({})");
  REQUIRE(e.log.lines().size() == 1);
  CHECK(e.log.lines()[0] == "[lua-arg] TStr arg 1: expected string, got table (test/bind.lua:2)");
  e.call("TStr({})\nTStr({})");
  CHECK(e.warnings() == 1);  // same native, argument and type
  e.call("TStr(true)");
  CHECK(e.warnings() == 2);  // another Lua type
  e.call("TInt('a')");
  e.call("TInt('a')");
  e.call("TInt('a')");
  CHECK(e.warnings() == 3);
  e.call("TMissing(1, 'x', nil)");
  e.call("TMissing('a', {}, 'b')");  // two different arguments
  CHECK(e.warnings() == 6);  // arg 1 string, arg 2 table, arg 3 string are three more
  CHECK(e.log.lines().back().find("(test/bind.lua:1)") != std::string::npos);
}

TEST_CASE("bind: booleans") {
  BindEnv e;
  CHECK(e.call("TBool(true)") == "bool:true");
  CHECK(e.call("TBool(false)") == "bool:false");
  CHECK(e.call("TBool(2)") == "bool:true");
  CHECK(e.call("TBool(0)") == "bool:false");
  CHECK(e.call("TBool(nil)") == "bool:false");
  CHECK(e.call("TBool()") == "bool:false");
  CHECK(e.warnings() == 0);
  CHECK(e.call("TBool('yes')") == "bool:false");
  CHECK(e.warnings() == 1);
}

TEST_CASE("bind: strings") {
  BindEnv e;
  CHECK(e.call("TStr('hi')") == "str:hi");
  CHECK(e.call("TStr(12)") == "str:12");
  CHECK(e.call("TStr(nil)") == "str:");
  CHECK(e.call("TStr()") == "str:");
  CHECK(e.warnings() == 0);  // nil is silent
  CHECK(e.call("TStr(print)") == "str:");
  CHECK(e.warnings() == 1);
  CHECK(e.call("TStr('a\\0b')") == std::string("str:a\0b", 7));  // embedded zeros survive
  CHECK(e.call("TConstRef('x')") == "");
}

TEST_CASE("bind: optional, table, ref and colour arguments") {
  BindEnv e;
  CHECK(e.call("TOpt(nil)") == "opt:none");
  CHECK(e.call("TOpt()") == "opt:none");
  CHECK(e.call("TOpt(5)") == "opt:5");
  CHECK(e.warnings() == 0);
  CHECK(e.call("TOpt('x')") == "opt:0");
  CHECK(e.warnings() == 1);

  CHECK(e.call("TTable({1,2,3})") == "table:3");
  CHECK(e.call("TTable(5)") == "table:invalid");
  CHECK(e.warnings() == 2);

  CHECK(e.call("TRef(function() end)") == "ref:valid");
  CHECK(e.lua.liveRefs() == 1);
  CHECK(e.call("TRef({})") == "ref:valid");  // replaces the kept one
  CHECK(e.lua.liveRefs() == 1);
  CHECK(e.call("TRef(5)") == "ref:empty");
  CHECK(e.lua.liveRefs() == 0);
  CHECK(e.warnings() == 3);

  CHECK(e.call("TColor{1, 0.5, 0}") == "color:1.000000,0.500000,0.000000,1.000000");
  CHECK(e.call("TColor{0.1, 0.2, 0.3, 0.4}") == "color:0.100000,0.200000,0.300000,0.400000");
  CHECK(e.call("TColor(7)") == "color:1.000000,1.000000,1.000000,1.000000");  // white
  CHECK(e.warnings() == 4);
  gKept.reset();
}

TEST_CASE("bind: missing trailing arguments") {
  BindEnv e;
  CHECK(e.call("TMissing(1, 'x')") == "1|x|nocopt");
  CHECK(e.call("TMissing(2)") == "2||nocopt");
  CHECK(e.call("TMissing(3, 'y', 4)") == "3|y|c");
  CHECK(e.warnings() == 0);
}

TEST_CASE("bind: return values") {
  BindEnv e;
  CHECK(e.call("a, b, c, d = TTuple()") == "");
  CHECK(e.getNumber("a") == 7);
  CHECK(e.getString("b") == "seven");
  CHECK(e.getString("c") == "true");
  CHECK(e.isNil("d"));
  e.call("x = TOptRet(true); y = TOptRet(false); n = nvals(TOptRet(false))");
  CHECK(e.getNumber("x") == 2.5);
  CHECK(e.isNil("y"));
  CHECK(e.getNumber("n") == 1);  // an empty optional is one nil, not zero values
  e.call("n = nvals(TInt(1))");
  CHECK(e.getNumber("n") == 0);  // void returns nothing
  e.call("s = TConstRef('ab')");
  CHECK(e.getString("s") == "ab!");
}

TEST_CASE("bind: C++ exceptions are caught at the boundary") {
  BindEnv e;
  e.call("n = nvals(TThrow()); after = 'reached'");
  CHECK(e.getNumber("n") == 0);
  CHECK(e.getString("after") == "reached");
  CHECK(e.log.contains("[native-error] TThrow: kaput"));
}

TEST_CASE("bind: registered names are tracked") {
  BindEnv e;
  CHECK(std::find(e.ctx.nativeNames.begin(), e.ctx.nativeNames.end(), "TInt") != e.ctx.nativeNames.end());
}

namespace {

// Runs `code` in the given game state; returns the error text, or "" when it ran clean.
std::string runLua(choco::script::LuaState& lua, const char* code) {
  lua_State* L = lua.L();
  lua_settop(L, 0);
  if (luaL_loadbuffer(L, code, std::strlen(code), "t") != 0 || lua_pcall(L, 0, 0, 0) != 0) {
    std::string msg = lua_tostring(L, -1);
    lua_settop(L, 0);
    return msg;
  }
  return "";
}

}  // namespace

// The game's per-type default metatables (docs/re/embedded-lua.md section 7): luaopen_string makes the `string` table
// the default metatable of strings; every other non-table type has an empty table; nil has none.
TEST_CASE("default metatables: strings index into the string library") {
  choco::script::LuaState lua;
  CHECK(runLua(lua,
               "local s = 'tea'\n"
               "assert(s.name == nil)\n"
               "assert(s:len() == 3 and s:sub(2, 3) == 'ea')\n"
               "assert(('x').len == string.len)\n"
               "assert(('x').foo == nil)\n"
               "assert(getmetatable('x') == string)\n"
               "local t = { category = 'fruit' }\n"
               "assert(t.category.name == nil)\n") == "");
  // Storing into a string stores into the library itself.
  CHECK(runLua(lua, "('x').foo = 1\nassert(string.foo == 1 and ('y').foo == 1)\nstring.foo = nil") == "");
}

TEST_CASE("default metatables: other types have an empty table, only nil raises") {
  choco::script::LuaState lua;
  CHECK(runLua(lua,
               "assert((3).x == nil)\n"
               "assert((true).x == nil)\n"
               "assert(print.x == nil)\n"
               "local m = getmetatable(3)\n"
               "assert(type(m) == 'table' and next(m) == nil and m ~= string)\n"
               "assert(getmetatable(print) ~= nil and getmetatable(true) ~= nil)\n"
               "assert(getmetatable(nil) == nil)\n"
               "assert(getmetatable({}) == nil)  -- known difference: the game returns its shared defmt[5] table (embedded-lua.md §7)\n") == "");
  CHECK(runLua(lua, "local n = nil; return n.x").find("attempt to index") != std::string::npos);
  CHECK(runLua(lua, "local n = nil; n.x = 1").find("attempt to index") != std::string::npos);
}

TEST_CASE("default metatables: a string's handler is the string table, so a __concat on the other operand is not used") {
  choco::script::LuaState lua;
  CHECK(runLua(lua, "local t = setmetatable({}, { __concat = function() return 'ok' end })\nlocal r = t .. 'a'") == "");
  CHECK(runLua(lua, "local t = setmetatable({}, { __concat = function() return 'ok' end })\nlocal r = 'a' .. t") != "");
  CHECK(runLua(lua, "local r = 'a' .. 'b' .. 1") == "");
}

TEST_CASE("default metatables: survive garbage collection") {
  choco::script::LuaState lua;
  CHECK(runLua(lua, "collectgarbage()\nassert(('abc'):len() == 3 and getmetatable(3) ~= nil)") == "");
}
