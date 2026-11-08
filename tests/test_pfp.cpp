#include <doctest/doctest.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <string>

extern "C" {
#include "lauxlib.h"
#include "lua.h"
#include "lobject.h"
#include "lstate.h"
}

#include "assets/AssetFS.h"
#include "assets/AssetSource.h"
#include "assets/Xml.h"
#include "script/LuaState.h"

using namespace choco::assets;
namespace fs = std::filesystem;

namespace {

void le32(std::string& s, std::uint32_t v) {
  for (int i = 0; i < 4; ++i) s += static_cast<char>((v >> (8 * i)) & 0xFF);
}

// Builds an archive from (name, content) pairs, data laid out after the directory.
std::string makeArchive(const std::vector<std::pair<std::string, std::string>>& files) {
  std::string dir = "PFPK";
  le32(dir, static_cast<std::uint32_t>(files.size()));
  std::size_t dirSize = dir.size();
  for (auto& f : files) dirSize += 9 + f.first.size();
  std::string data;
  for (auto& f : files) {
    dir += static_cast<char>(f.first.size());
    dir += f.first;
    le32(dir, static_cast<std::uint32_t>(dirSize + data.size()));
    le32(dir, static_cast<std::uint32_t>(f.second.size()));
    data += f.second;
  }
  return dir + data;
}

Bytes toBytes(const std::string& s) {
  Bytes b(s.size());
  for (std::size_t i = 0; i < s.size(); ++i) b[i] = static_cast<std::byte>(s[i]);
  return b;
}

std::string str(const Bytes& b) { return std::string(reinterpret_cast<const char*>(b.data()), b.size()); }

void writeFile(const fs::path& p, const std::string& s) {
  fs::create_directories(p.parent_path());
  std::ofstream f(p, std::ios::binary);
  REQUIRE(f.is_open());
  f.write(s.data(), static_cast<std::streamsize>(s.size()));
}

fs::path realPfp() { return fs::path(CHOCO_VANILLA_ASSETS) / "assets.pfp"; }
bool haveRealPfp() { return fs::is_regular_file(realPfp()); }

fs::path makeTemp(const char* name) {
  fs::path p = fs::temp_directory_path() / (std::string("choco_test_pfp_") + name);
  fs::remove_all(p);
  fs::create_directories(p);
  return p;
}

// Compares the parts of two prototypes that do not depend on debug info, recursively. Returns a description of the first
// difference or "".
std::string diffProto(const Proto* a, const Proto* b, const std::string& path) {
  if (a->numparams != b->numparams) return path + ": numparams";
  if (a->is_vararg != b->is_vararg) return path + ": is_vararg";
  if (a->nups != b->nups) return path + ": nups";
  if (a->sizep != b->sizep) return path + ": sizep";
  if (a->sizek != b->sizek) return path + ": sizek";
  if (a->sizecode != b->sizecode) return path + ": sizecode";
  for (int i = 0; i < a->sizecode; ++i)
    if (a->code[i] != b->code[i]) return path + ": code";
  for (int i = 0; i < a->sizep; ++i)
    if (auto d = diffProto(a->p[i], b->p[i], path + "/" + std::to_string(i)); !d.empty()) return d;
  return "";
}

// A minimal chunk in the game's dialect (32-bit widths): one function whose only instruction word is `word`.
std::string tinyChunk(std::uint32_t word) {
  std::string c = "\x1b" "Lua";
  c += "\x50\x01\x04\x04\x04\x06\x08\x09\x09\x08";
  const double test = 3.14159265358979e7;
  c.append(reinterpret_cast<const char*>(&test), sizeof(test));  // little-endian hosts only
  le32(c, 3);
  c += std::string("=t\0", 3);
  le32(c, 0);                                     // linedefined
  c += std::string("\0\0\0\2", 4);                // nups, numparams, is_vararg, maxstacksize
  for (int i = 0; i < 5; ++i) le32(c, 0);         // lines, locals, upvalues, constants, protos
  le32(c, 1);
  le32(c, word);
  return c;
}

}  // namespace

TEST_CASE("the chunk loader accepts the game's opcode dialect and rejects opcodes outside it") {
  choco::script::LuaState state;
  lua_State* L = state.L();
  auto load = [&](std::uint32_t op) {
    const std::string c = tinyChunk(op);
    const int st = luaL_loadbuffer(L, c.data(), c.size(), "tiny");
    std::string msg = st ? lua_tostring(L, -1) : "";
    lua_settop(L, 0);
    return std::make_pair(st, msg);
  };
  {
    const auto [st, msg] = load(31);  // RETURN in the game's numbering
    CHECK_MESSAGE(st == 0, msg);
  }
  for (std::uint32_t op : {17u, 40u}) {
    const auto [st, msg] = load(op);
    CHECK_MESSAGE(st != 0, "opcode " << op);
    CHECK_MESSAGE(msg.find("bad opcode") != std::string::npos, "opcode " << op << ": " << msg);
  }
}

TEST_CASE("PfpSource parses a synthetic archive") {
  const std::string arc = makeArchive({{"a/B.txt", "hello"}, {"empty.bin", ""}, {"Z.lua", "return 1"}});
  std::string err;
  auto src = PfpSource::fromBytes(toBytes(arc), err);
  REQUIRE_MESSAGE(src, err);
  CHECK(src->fileCount() == 3);
  std::vector<std::string> names;
  src->list(names);
  CHECK(names == std::vector<std::string>{"a/B.txt", "empty.bin", "Z.lua"});
  CHECK(str(*src->read("a/B.txt")) == "hello");
  CHECK(src->read("empty.bin")->empty());
  CHECK_FALSE(src->read("missing").has_value());
}

TEST_CASE("PfpSource rejects bad archives") {
  std::string err;
  CHECK_FALSE(PfpSource::fromBytes(toBytes(std::string("PFPX\1\0\0\0", 8)), err));
  CHECK_FALSE(PfpSource::fromBytes(toBytes("PF"), err));
  const std::string arc = makeArchive({{"a.txt", "hello"}});
  CHECK_FALSE(PfpSource::fromBytes(toBytes(arc.substr(0, 12)), err));  // truncated directory
  CHECK(err.find("truncated") != std::string::npos);
  CHECK_FALSE(PfpSource::fromBytes(toBytes(arc.substr(0, arc.size() - 1)), err));  // data cut short
  CHECK(err.find("outside") != std::string::npos);
  std::string bad = arc;  // a count larger than the directory
  bad[4] = 9;
  CHECK_FALSE(PfpSource::fromBytes(toBytes(bad), err));
}

TEST_CASE("AssetFS picks up assets.pfp, case-insensitively, and loose files override it") {
  const fs::path dir = makeTemp("fs");
  writeFile(dir / "assets.pfp",
            makeArchive({{"Ui/Menu.XML", "from archive"}, {"data/only.txt", "only in archive"}}));
  writeFile(dir / "ui" / "menu.xml", "loose");
  AssetFS afs(dir.string(), "");
  REQUIRE_MESSAGE(afs.ok(), afs.error());
  CHECK(afs.readText("UI/MENU.xml") == "loose");  // loose wins
  CHECK(afs.readText("data\\ONLY.txt") == "only in archive");
  CHECK(*afs.resolve("DATA/only.txt") == "data/only.txt");
  CHECK(afs.list("data").size() == 1);
  fs::remove_all(dir);
}

TEST_CASE("A corrupt assets.pfp is reported by AssetFS") {
  const fs::path dir = makeTemp("corrupt");
  writeFile(dir / "assets.pfp", "garbage!");
  AssetFS afs(dir.string(), "");
  CHECK_FALSE(afs.ok());
  CHECK(afs.error().find("assets.pfp") != std::string::npos);
  fs::remove_all(dir);
}

TEST_CASE("vanilla assets.pfp: header facts") {
  if (!haveRealPfp()) return;  // not available in this checkout
  std::string err;
  auto src = PfpSource::open(realPfp(), err);
  REQUIRE_MESSAGE(src, err);
  CHECK(src->fileCount() == 1509);
  std::vector<std::string> names;
  src->list(names);
  int lua = 0;
  for (auto& n : names) {
    CHECK(n.find('\\') == std::string::npos);
    CHECK(n == AssetFS::normalize(n));  // already lowercase and '/'-separated
    if (n.size() > 4 && n.compare(n.size() - 4, 4, ".lua") == 0) ++lua;
  }
  CHECK(lua == 122);
}

TEST_CASE("vanilla assets.pfp: every Lua chunk loads, and matches the loose source") {
  if (!haveRealPfp()) return;
  AssetFS afs(CHOCO_VANILLA_ASSETS, "");
  REQUIRE_MESSAGE(afs.ok(), afs.error());
  const auto luaFiles = afs.listBySuffix(".lua");
  CHECK(luaFiles.size() == 122);
  choco::script::LuaState state;
  lua_State* L = state.L();
  int compared = 0, identical = 0;
  std::string firstDiff;
  for (const std::string& rel : luaFiles) {
    auto bytes = afs.readFile(rel);
    REQUIRE(bytes);
    REQUIRE_MESSAGE(bytes->size() > 4, rel);
    CHECK_MESSAGE(static_cast<unsigned char>((*bytes)[0]) == 0x1B, rel << " is not bytecode");
    const int st = luaL_loadbuffer(L, reinterpret_cast<const char*>(bytes->data()), bytes->size(), rel.c_str());
    INFO(rel << ": " << (st ? lua_tostring(L, -1) : ""));
    REQUIRE(st == 0);
    const Proto* vanilla = clvalue(L->top - 1)->l.p;
    // Cross-check against the loose Mac source, compiled by our own Lua.
    const fs::path loose = fs::path(CHOCO_MAC_ASSETS) / rel;
    if (fs::is_regular_file(loose) && luaL_loadfile(L, loose.string().c_str()) == 0) {
      const Proto* ours = clvalue(L->top - 1)->l.p;
      ++compared;
      const std::string d = diffProto(vanilla, ours, rel);
      if (d.empty()) ++identical;
      else {
        if (firstDiff.empty()) firstDiff = d;
        // The two sources are known to differ: building.lua function 25 lacks the Mac source's Player:AutoSave() call,
        // and player.lua's main differs by 3 instructions and 1 proto.
        const bool known = rel == "sim/building.lua" || rel == "sim/player.lua";
        CHECK_MESSAGE(known, "unexpected difference: " << d);
      }
    }
    lua_settop(L, 0);
  }
  MESSAGE("compared " << compared << ", identical " << identical << ", first difference: " << firstDiff);
  // Windows v1.0 against the Mac source: every chunk is identical (code, params, nups, counts) except the two named above.
  CHECK(compared >= 100);
  CHECK(identical >= compared - 2);
}

TEST_CASE("Xml reads the ZXML wrapper (stored zlib block) from bytes and from text") {
  const std::string xml = "<sequence><img name=\"a\"/></sequence>";
  std::string z = "ZXML";
  auto put32 = [&](uint32_t v) {
    for (int i = 0; i < 4; ++i) z += char((v >> (8 * i)) & 0xff);
  };
  const uint32_t n = uint32_t(xml.size());
  put32(2 + 5 + n + 4);
  put32(n);
  z += std::string("\x78\x01", 2);
  z += char(1);  // final stored block
  z += char(n & 0xff);
  z += char(n >> 8);
  z += char(~n & 0xff);
  z += char((~n >> 8) & 0xff);
  z += xml;
  z += std::string(4, '\0');  // adler32 (not checked)
  Xml a;
  REQUIRE(a.parse(std::string_view(z)));
  CHECK(std::string(a.root().name()) == "sequence");
  Bytes b(z.size());
  std::memcpy(b.data(), z.data(), z.size());
  Xml c;
  REQUIRE(c.parse(b));
  CHECK(std::string(c.root().child("img").attribute("name").value()) == "a");
  z[8] = char(z[8] + 1);  // raw length disagrees
  Xml d;
  CHECK_FALSE(d.parse(std::string_view(z)));
}
