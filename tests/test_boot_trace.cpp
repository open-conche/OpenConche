// Boot trace: what a booted script host registers, per game. Goldens live in tests/golden; this guards the structural
// refactor of the game modules (docs/design/engine-and-games.md, section 6).
#include <doctest/doctest.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "games/Registry.h"
#include "ScriptHarness.h"

using namespace choco;
namespace fs = std::filesystem;

namespace {

struct BootEnv : test::ScriptEnv {
  explicit BootEnv(const char* g) : ScriptEnv(std::string(CHOCO_TEST_ASSETS) + "/host/poll"), host(ctx) {
    ctx.module = games::findModule(g);
    ctx.module->attach(ctx);
    booted = host.boot();
  }
  script::ScriptHost host;
  bool booted = false;
};

// The boot trace: natives in registration order, window types in order, then every global sorted as
// name TAB type TAB upvalue count TAB the second upvalue's string (a native's name; empty otherwise).
std::string traceOf(BootEnv& e) {
  std::ostringstream out;
  out << "[natives]\n";
  for (const auto& n : e.ctx.nativeNames) out << n << '\n';
  out << "[window types]\n";
  for (const auto& t : e.wm.windowTypes()) out << t << '\n';
  out << "[globals]\n";
  lua_State* L = e.lua.L();
  std::vector<std::string> rows;
  lua_pushvalue(L, LUA_GLOBALSINDEX);
  lua_pushnil(L);
  while (lua_next(L, -2) != 0) {
    if (lua_type(L, -2) == LUA_TSTRING) {
      std::string name = lua_tostring(L, -2);
      int ups = 0;
      std::string upName;
      if (lua_isfunction(L, -1)) {
        const int fn = lua_gettop(L);
        while (lua_getupvalue(L, fn, ups + 1) != nullptr) {
          if (ups == 1 && lua_isstring(L, -1) && lua_iscfunction(L, fn)) upName = lua_tostring(L, -1);
          lua_pop(L, 1);
          ++ups;
        }
      }
      rows.push_back(name + '\t' + lua_typename(L, lua_type(L, -1)) + '\t' + std::to_string(ups) + '\t' + upName);
    }
    lua_pop(L, 1);
  }
  lua_pop(L, 1);
  std::sort(rows.begin(), rows.end());
  for (const auto& r : rows) out << r << '\n';
  return out.str();
}

std::string readAll(const fs::path& p) {
  std::ifstream f(p, std::ios::binary);
  std::ostringstream s;
  s << f.rdbuf();
  return s.str();
}

std::vector<std::string> lines(const std::string& s) {
  std::vector<std::string> v;
  std::istringstream in(s);
  for (std::string l; std::getline(in, l);) {
    if (!l.empty() && l.back() == '\r') l.pop_back();  // a golden checked out with CRLF (git autocrlf on Windows)
    v.push_back(l);
  }
  return v;
}

// Compares with tests/golden/boot_<id>.txt; on a mismatch writes <test_out>/boot_<id>.actual.txt and fails.
void checkGolden(const char* id, const std::string& actual) {
  const fs::path golden = fs::path(CHOCO_GOLDEN_DIR) / (std::string("boot_") + id + ".txt");
  if (const char* up = std::getenv("CHOCO_UPDATE_GOLDEN"); up && *up && std::string(up) != "0") {
    fs::create_directories(golden.parent_path());
    std::ofstream(golden, std::ios::binary) << actual;
    return;
  }
  REQUIRE_MESSAGE(fs::exists(golden), "missing golden " << golden.string() << " (CHOCO_UPDATE_GOLDEN=1 creates it)");
  const std::string want = readAll(golden);
  if (lines(want) == lines(actual)) return;  // line-wise: the golden may be CRLF on a Windows checkout
  const fs::path out = fs::path(CHOCO_TEST_OUT) / (std::string("boot_") + id + ".actual.txt");
  fs::create_directories(out.parent_path());
  std::ofstream(out, std::ios::binary) << actual;
  const auto a = lines(want), b = lines(actual);
  std::ostringstream msg;
  msg << "boot trace for " << id << " differs from the golden (actual written to " << out.string() << ")";
  int shown = 0;
  for (size_t i = 0; i < std::max(a.size(), b.size()) && shown < 5; ++i) {
    const std::string x = i < a.size() ? a[i] : "<end>", y = i < b.size() ? b[i] : "<end>";
    if (x != y) {
      msg << "\n  line " << i + 1 << ": golden '" << x << "' actual '" << y << "'";
      ++shown;
    }
  }
  FAIL(msg.str());
}

}  // namespace

// The poll tree is enough for all three games: boot reads no game assets beyond ui/mainloop.lua, so no staging gate.
TEST_CASE("boot trace: Chocolatier 3") {
  BootEnv e("choc3");
  REQUIRE(e.booted);
  checkGolden("choc3", traceOf(e));
}

#ifdef CHOCO_GAME_CHOC2
TEST_CASE("boot trace: Chocolatier 2") {
  BootEnv e("choc2");
  REQUIRE(e.booted);
  checkGolden("choc2", traceOf(e));
}
#endif

#ifdef CHOCO_GAME_CHOC1
TEST_CASE("boot trace: Chocolatier 1") {
  BootEnv e("choc1");
  REQUIRE(e.booted);
  checkGolden("choc1", traceOf(e));
}
#endif
