// Compiles every .lua file under a directory with the engine's (patched) Lua 5.0.2, so a lexer or parser gap shows up
// at once. Usage: lua_compile_all <dir>. Exit 1 if any file fails; prints each failure.
#include <cstdio>
#include <string>
#include <string_view>

#include "assets/AssetFS.h"
#include "script/Loaders.h"
#include "script/LuaState.h"

int main(int argc, char** argv) {
  if (argc != 2) {
    std::fprintf(stderr, "usage: lua_compile_all <dir>\n");
    return 2;
  }
  choco::assets::AssetFS afs(argv[1], "");
  if (!afs.ok()) {
    std::fprintf(stderr, "%s\n", afs.error().c_str());
    return 2;
  }
  choco::script::LuaState state;
  int total = 0, failed = 0;
  for (const std::string& rel : afs.listBySuffix(".lua")) {  // loose files and the assets.pfp archive, if any
    auto bytes = afs.readFile(rel);
    ++total;
    lua_State* L = state.L();
    if (!bytes || choco::script::loadBuffer(L, std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()), rel.c_str()) != 0) {
      ++failed;
      std::printf("FAIL %s\n", bytes ? lua_tostring(L, -1) : rel.c_str());
    }
    lua_settop(L, 0);
  }
  std::printf("%d files, %d failed\n", total, failed);
  return failed == 0 && total > 0 ? 0 : 1;
}
