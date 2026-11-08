#include <doctest/doctest.h>

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "ScriptHarness.h"
#include "app/Args.h"
#include "games/choc3/ImportCommand.h"
#include "games/choc3/PlayerSave.h"
#include "games/choc3/PrefsImport.h"
#include "sdk/Users.h"
#include "platform/UserDir.h"
#include "script/ScriptHost.h"

using namespace choco::game;
namespace fs = std::filesystem;

namespace {

std::string slurp(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

struct TempDir {
  explicit TempDir(const char* tag) {
    path = fs::temp_directory_path() / (std::string("choco_import_") + tag);
    fs::remove_all(path);
    fs::create_directories(path);
  }
  ~TempDir() { fs::remove_all(path); }
  std::string str() const { return path.string(); }
  fs::path path;
};

std::string hex(const std::string& s) {
  static const char* h = "0123456789abcdef";
  std::string r;
  for (char c : s) {
    r += h[(static_cast<unsigned char>(c) >> 4) & 15];
    r += h[static_cast<unsigned char>(c) & 15];
  }
  return r;
}

std::string bytes(const char* hexStr) {
  std::string r;
  for (const char* p = hexStr; *p; p += 2) {
    auto v = [](char c) { return c <= '9' ? c - '0' : (c | 32) - 'a' + 10; };
    r += static_cast<char>(v(p[0]) * 16 + v(p[1]));
  }
  return r;
}

std::string sha1(const std::string& msg) {
  auto rol = [](std::uint32_t x, int n) { return (x << n) | (x >> (32 - n)); };
  std::uint32_t h[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};
  std::string m = msg;
  m += static_cast<char>(0x80);
  while (m.size() % 64 != 56) m += '\0';
  const std::uint64_t bits = static_cast<std::uint64_t>(msg.size()) * 8;
  for (int i = 7; i >= 0; --i) m += static_cast<char>((bits >> (8 * i)) & 0xFF);
  for (std::size_t off = 0; off < m.size(); off += 64) {
    std::uint32_t w[80];
    for (int i = 0; i < 16; ++i)
      w[i] = (static_cast<std::uint32_t>(static_cast<unsigned char>(m[off + 4 * i])) << 24) |
             (static_cast<std::uint32_t>(static_cast<unsigned char>(m[off + 4 * i + 1])) << 16) |
             (static_cast<std::uint32_t>(static_cast<unsigned char>(m[off + 4 * i + 2])) << 8) |
             static_cast<std::uint32_t>(static_cast<unsigned char>(m[off + 4 * i + 3]));
    for (int i = 16; i < 80; ++i) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    for (int i = 0; i < 80; ++i) {
      std::uint32_t f, k;
      if (i < 20) f = (b & c) | (~b & d), k = 0x5A827999u;
      else if (i < 40) f = b ^ c ^ d, k = 0x6ED9EBA1u;
      else if (i < 60) f = (b & c) | (b & d) | (c & d), k = 0x8F1BBCDCu;
      else f = b ^ c ^ d, k = 0xCA62C1D6u;
      const std::uint32_t t = rol(a, 5) + f + e + k + w[i];
      e = d;
      d = c;
      c = rol(b, 30);
      b = a;
      a = t;
    }
    h[0] += a, h[1] += b, h[2] += c, h[3] += d, h[4] += e;
  }
  std::string r;
  for (std::uint32_t x : h)
    for (int i = 3; i >= 0; --i) r += static_cast<char>((x >> (8 * i)) & 0xFF);
  return hex(r);
}

std::string blockHex(const Blowfish& bf, const char* plain) {
  const std::string p = bytes(plain);
  auto be = [&](int o) {
    return (static_cast<std::uint32_t>(static_cast<unsigned char>(p[o])) << 24) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(p[o + 1])) << 16) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(p[o + 2])) << 8) |
           static_cast<std::uint32_t>(static_cast<unsigned char>(p[o + 3]));
  };
  std::uint32_t l = be(0), r = be(4);
  bf.encryptBlock(l, r);
  std::string out;
  for (std::uint32_t x : {l, r})
    for (int i = 3; i >= 0; --i) out += static_cast<char>((x >> (8 * i)) & 0xFF);
  return hex(out);
}

std::string assetPath(const char* name) { return std::string(CHOCO_TEST_ASSETS) + "/prefs/" + name; }

using Section = std::map<std::string, std::string>;

// The workbench/re/prefs_dump.py synthetic vector, section by section.
std::vector<Section> syntheticSections(const std::string& save, const char* curuser) {
  Section glob{{"curuser", curuser}, {"fullscreen", "1"}};
  Section ada{{"name", "Ada"},         {"savegame", save},       {"soundlevel", "750"},
              {"musiclevel", "650"},   {"ambientlevel", "500"},  {"soundoff", "0"}};
  Section bob{{"name", "Bob"},         {"soundlevel", "400"},    {"musiclevel", "650"},
              {"ambientlevel", "500"}, {"soundoff", "1"},        {"_anonusername", "bobby"}};
  return {glob, ada, bob};
}

constexpr const char* kSyntheticSave = "return {name=\"Ada\",money=1234,time=17,}";

}  // namespace

TEST_CASE("blowfish: standard ECB test vectors") {
  const std::uint8_t zero[8] = {0};
  CHECK(blockHex(Blowfish(zero, 8), "0000000000000000") == "4ef997456198dd78");
  const std::uint8_t ones[8] = {255, 255, 255, 255, 255, 255, 255, 255};
  CHECK(blockHex(Blowfish(ones, 8), "ffffffffffffffff") == "51866fd5b85ecb8a");
  const std::string k1 = bytes("0123456789abcdef");
  CHECK(blockHex(Blowfish(reinterpret_cast<const std::uint8_t*>(k1.data()), 8), "1111111111111111") == "61f9c3802281b096");
  const std::string k2 = bytes("fedcba9876543210");
  CHECK(blockHex(Blowfish(reinterpret_cast<const std::uint8_t*>(k2.data()), 8), "0123456789abcdef") == "0aceab0fc6a0a28d");
}

TEST_CASE("blowfish: pi tables and decrypt inverts encrypt") {
  const auto& pi = blowfishPiWords();
  REQUIRE(pi.size() == 18 + 4 * 256);
  CHECK(pi[0] == 0x243F6A88u);
  CHECK(pi[17] == 0x8979FB1Bu);        // last P word
  CHECK(pi[18] == 0xD1310BA6u);        // S0[0]
  CHECK(pi.back() == 0x3AC372E6u);     // S3[255]
  const std::uint8_t key[5] = {1, 2, 3, 4, 5};
  Blowfish bf(key, 5);
  std::uint32_t l = 0xDEADBEEFu, r = 0x01234567u;
  bf.encryptBlock(l, r);
  CHECK((l != 0xDEADBEEFu || r != 0x01234567u));
  bf.decryptBlock(l, r);
  CHECK(l == 0xDEADBEEFu);
  CHECK(r == 0x01234567u);
}

TEST_CASE("prefs cipher: key bytes, padding rules and the first IKEY chunk of the synthetic vector") {
  const std::vector<std::uint8_t> key = prefsKeyBytes();
  CHECK(hex(std::string(key.begin(), key.end())) == "1f7da4c2675b080000");
  PrefsCipher c;
  for (std::size_t n : {0u, 1u, 7u, 8u, 9u, 15u, 16u, 17u}) {
    std::string pt;
    for (std::size_t i = 0; i < n; ++i) pt += static_cast<char>(i + 1);
    const std::string ct = c.encrypt(pt);
    CHECK(ct.size() == (n & ~std::size_t{7}) + 8);
    auto back = c.decrypt(ct);
    REQUIRE(back.has_value());
    CHECK(*back == pt);
  }
  // The leading bytes workbench/re/prefs_dump.py writes for "chocolatier-decadence-design\0".
  const std::string ct = c.encrypt(std::string("chocolatier-decadence-design\0", 29));
  CHECK(ct.size() == 32);
  CHECK_FALSE(c.decrypt("").has_value());
}

TEST_CASE("prefs vector: the writer reproduces the Python vector byte for byte (sha1 1c113bac...)") {
  const std::string built = buildPrefsDat("chocolatier-decadence-design", syntheticSections(kSyntheticSave, "1"));
  CHECK(built.size() == 542);
  CHECK(sha1(built) == "1c113baca62b1f2f0ad9d1747ead191ae0ee45bf");
  const std::string file = slurp(assetPath("synthetic.dat"));
  CHECK(sha1(file) == "1c113baca62b1f2f0ad9d1747ead191ae0ee45bf");
  CHECK(built == file);
  CHECK(hex(file.substr(0, 16)) == "494b4559" "20000000" "dd20e7c70766fe82");  // IKEY, length 32, first cipher bytes
}

TEST_CASE("sha1 helper sanity") { CHECK(sha1("abc") == "a9993e364706816aba3e25717850c26c9cd0d89d"); }

TEST_CASE("prefs parser: sections, keys and values of the synthetic vector") {
  const PrefsParseResult r = parsePrefsDat(slurp(assetPath("synthetic.dat")));
  REQUIRE(r.ok);
  CHECK(r.prefs.complete);
  CHECK(r.prefs.identity == "chocolatier-decadence-design");
  REQUIRE(r.prefs.sections.size() == 3);
  CHECK(r.prefs.sections[0].at("curuser") == "1");
  CHECK(r.prefs.sections[0].at("fullscreen") == "1");
  CHECK(r.prefs.sections[1].at("name") == "Ada");
  CHECK(r.prefs.sections[1].at("savegame") == kSyntheticSave);
  CHECK(r.prefs.sections[2].at("name") == "Bob");
  CHECK(r.prefs.sections[2].at("_anonusername") == "bobby");
  CHECK(r.prefs.sections[2].at("soundoff") == "1");
  CHECK(r.prefs.chunks == 2 + 3 + 2 + 6 + 6);
}

TEST_CASE("prefs parser: truncation keeps what was read, with a reason") {
  const std::string full = slurp(assetPath("synthetic.dat"));
  // Cut in the middle of a late chunk: Ada is complete, Bob is partly there.
  const PrefsParseResult cut = parsePrefsDat(full.substr(0, full.size() - 5));
  REQUIRE(cut.ok);
  CHECK_FALSE(cut.prefs.complete);
  CHECK(cut.prefs.stopReason.find("truncated") != std::string::npos);
  CHECK(cut.prefs.sections[1].at("name") == "Ada");
  // Cut inside a chunk header.
  const PrefsParseResult hdr = parsePrefsDat(full.substr(0, 32 + 8 + 3));
  REQUIRE(hdr.ok);
  CHECK_FALSE(hdr.prefs.complete);
  // Every possible prefix either fails cleanly or parses a usable prefix; none crashes or throws.
  for (std::size_t n = 0; n <= full.size(); n += 7) {
    const PrefsParseResult p = parsePrefsDat(full.substr(0, n));
    if (!p.ok) CHECK_FALSE(p.error.empty());
  }
  // Header only, no payload.
  const PrefsParseResult none = parsePrefsDat(full.substr(0, 8));
  CHECK_FALSE(none.ok);
  CHECK(none.error.find("truncated") != std::string::npos);
}

TEST_CASE("prefs parser: garbage, empty and old XML files fail cleanly") {
  CHECK(parsePrefsDat("").error.find("empty") != std::string::npos);
  CHECK(parsePrefsDat("<><prefs/>").error.find("XML") != std::string::npos);
  std::string junk;
  for (int i = 0; i < 300; ++i) junk += static_cast<char>((i * 37 + 11) & 255);
  CHECK_FALSE(parsePrefsDat(junk).ok);
  CHECK_FALSE(parsePrefsDat(junk).error.empty());
  // A plausible header with a nonsense length, and an IKEY whose payload does not decrypt.
  std::string bad = std::string("IKEY") + std::string("\xff\xff\xff\xff", 4) + "abcdefgh";
  CHECK_FALSE(parsePrefsDat(bad).ok);
  std::string notdecrypt = std::string("IKEY") + std::string("\x08\0\0\0", 4) + std::string(8, 'x');
  const auto nd = parsePrefsDat(notdecrypt);
  CHECK_FALSE(nd.ok);
  CHECK(nd.error.find("decrypt") != std::string::npos);
  // A file whose first chunk is not IKEY.
  std::string notFirst = std::string("KVPR") + std::string("\x08\0\0\0", 4) + std::string(8, 'x');
  CHECK_FALSE(parsePrefsDat(notFirst).ok);
  // Random bytes after a valid prefix never throw.
  std::string mixed = slurp(assetPath("synthetic.dat"));
  mixed.resize(200);
  for (std::size_t i = 100; i < 200; ++i) mixed[i] = static_cast<char>(i * 91);
  parsePrefsDat(mixed);
}

TEST_CASE("prefs mapper: Ada and Bob into a UserStore, and Ada's save through LoadGameString") {
  TempDir d("map");
  const PrefsParseResult r = parsePrefsDat(slurp(assetPath("synthetic.dat")));
  REQUIRE(r.ok);
  UserStore store(d.str());
  const ImportReport rep = importPrefs(r.prefs, store);
  CHECK(rep.usersFound == 2);
  CHECK(rep.usersImported == 2);
  CHECK(rep.savesImported == 1);
  CHECK(rep.savesRefused == 0);
  CHECK(rep.savesEmpty == 1);
  CHECK(rep.prefsMapped == 4 + 4 + 1 + 1);  // Ada's four, Bob's four plus _anonusername, global fullscreen
  CHECK(rep.ignored.empty());
  CHECK(store.count() == 2);
  CHECK(store.name(0) == "Ada");
  CHECK(store.name(1) == "Bob");
  CHECK(store.current() == 1);  // curuser=1 in the synthetic vector
  CHECK(store.currentName() == "Bob");
  CHECK(store.pref("soundlevel") == "400");
  CHECK(store.pref("soundoff") == "1");
  CHECK(store.pref("_anonusername") == "bobby");
  CHECK(store.globalPref("fullscreen") == "1");
  CHECK(store.loadSave() == "");
  store.setCurrent(0);
  CHECK(store.loadSave() == kSyntheticSave);
  CHECK(store.pref("soundlevel") == "750");
  // A reload from disk sees the same.
  UserStore again(d.str());
  CHECK(again.count() == 2);
  CHECK(again.current() == 0);  // the store was left on Ada
  again.setCurrent(0);
  CHECK(again.loadSave() == kSyntheticSave);
  // And the scripts read it back through LoadGameString.
  struct Env {
    explicit Env(const std::string& dir) : fs(CHOCO_TEST_ASSETS, dir), ctx(lua, fs, wm, clock, rng, config, stubs, log) {
      log.setEcho(false);
      log.setKeep(true);
      choco::script::registerUserNatives(lua.L(), ctx);
    }
    choco::script::LuaState lua;
    choco::assets::AssetFS fs;
    choco::ui::WindowManager wm;
    choco::script::Clock clock;
    choco::script::Rng rng{1};
    choco::script::Config config;
    choco::script::StubLog stubs;
    choco::script::Log log;
    choco::script::ScriptContext ctx;
  } env(d.str());
  lua_State* L = env.lua.L();
  const char* code = "SetCurrentUser(0) n = GetNumUsers() who = GetCurrentUserName() s = LoadGameString()";
  REQUIRE(luaL_loadbuffer(L, code, std::strlen(code), "=t") == 0);
  REQUIRE(lua_pcall(L, 0, 0, 0) == 0);
  lua_getglobal(L, "n");
  CHECK(lua_tonumber(L, -1) == 2);
  lua_getglobal(L, "who");
  CHECK(std::string(lua_tostring(L, -1)) == "Ada");
  lua_getglobal(L, "s");
  CHECK(std::string(lua_tostring(L, -1)) == kSyntheticSave);
  lua_settop(L, 0);
}

TEST_CASE("prefs mapper: the real-save vector, ignored prefs, refusals and replacement by name") {
  TempDir d("map2");
  const std::string save = slurp(assetPath("ada_save.txt"));
  REQUIRE(save.size() > 5000);
  const PrefsParseResult r = parsePrefsDat(slurp(assetPath("ada_bob.dat")));
  REQUIRE(r.ok);
  REQUIRE(r.prefs.sections.size() == 3);
  CHECK(r.prefs.sections[1].at("savegame") == save);
  UserStore store(d.str());
  const ImportReport rep = importPrefs(r.prefs, store);
  CHECK(rep.savesImported == 1);
  CHECK(store.current() == 0);
  CHECK(store.currentName() == "Ada");
  CHECK(store.loadSave() == save);

  // A hand-made file: an uninitialised-player save is refused, unknown prefs are ignored and listed, the pref machine id is dropped.
  const std::string bare = "return {knownRecipes={},integrity={version=1,},}";
  REQUIRE(isUninitialisedPlayerSave(bare));
  std::vector<Section> secs = {
      {{"curuser", "0"}, {"fullscreen", "0"}, {"_pflifetime", "xyz"}, {"mystery", "1"}},
      {{"name", "Cy"}, {"savegame", bare}, {"soundlevel", "abc"}, {"extra", "q"}},
      {{"name", "Ada"}, {"savegame", "return {money=5,time=2,}"}},
  };
  const PrefsParseResult r2 = parsePrefsDat(buildPrefsDat("some-game", secs));
  REQUIRE(r2.ok);
  CHECK(r2.prefs.identity == "some-game");
  const ImportReport rep2 = importPrefs(r2.prefs, store);  // Ada already exists: replaced, Cy appended
  CHECK(rep2.usersImported == 2);
  CHECK(rep2.usersReplaced == 1);
  CHECK(rep2.savesRefused == 1);
  CHECK(rep2.savesImported == 1);
  CHECK(rep2.ignored.size() == 3);  // _pflifetime, mystery, extra
  CHECK_FALSE(rep2.warnings.empty());
  CHECK(store.count() == 3);  // Ada, Bob, Cy
  CHECK(store.name(0) == "Ada");
  CHECK(store.name(2) == "Cy");
  store.setCurrent(0);
  CHECK(store.loadSave() == "return {money=5,time=2,}");
  store.setCurrent(2);
  CHECK(store.loadSave() == "");              // refused: nothing written
  CHECK(store.pref("soundlevel") == "750");   // not a number: left at the default
  CHECK(store.globalPref("fullscreen") == "0");
  CHECK(rep2.currentUser == 2);  // curuser 0 is Cy, appended after Ada and Bob
}

TEST_CASE("prefs mapper: non-UTF-8 and missing names, out-of-range curuser") {
  TempDir d("names");
  std::vector<Section> secs = {{{"curuser", "9"}}, {{"name", "Ren\xe9"}}, {{"name", ""}}, {{"savegame", "return {money=1,time=1,}"}}};
  const PrefsParseResult r = parsePrefsDat(buildPrefsDat("g", secs));
  REQUIRE(r.ok);
  UserStore store(d.str());
  const ImportReport rep = importPrefs(r.prefs, store);
  CHECK(store.count() == 3);
  CHECK(store.name(0) == "Ren\xc3\xa9");
  CHECK(store.name(1) == "Player 2");
  CHECK(store.name(2) == "Player 3");
  CHECK(store.current() == -1);
  CHECK(rep.warnings.size() >= 3);
  CHECK(formatReport(rep).find("users found:      3") != std::string::npos);
}

TEST_CASE("import command: backup folder names, copies, and flag checks") {
  using namespace std::chrono;
  const auto t = sys_days{year{2021} / 6 / 3} + hours{14} + minutes{7};
  CHECK(choco::app::backupDirName("/x/Chocolatier3", t) == "/x/Chocolatier3-backup-20210603-1407");
  CHECK(choco::app::backupDirName("/x/Chocolatier3/", t) == "/x/Chocolatier3-backup-20210603-1407");

  TempDir d("backup");
  const std::string dir = (d.path / "user").string();
  std::string err;
  CHECK(choco::app::backUpUserDir(dir, t, err).empty());  // missing: nothing to back up
  CHECK(err.empty());
  fs::create_directories(d.path / "user" / "profiles" / "A");
  { std::ofstream(d.path / "user" / "users.txt") << "x"; }
  { std::ofstream(d.path / "user" / "profiles" / "A" / "save.txt") << "y"; }
  const std::string b1 = choco::app::backUpUserDir(dir, t, err);
  CHECK(err.empty());
  CHECK(slurp(fs::path(b1) / "profiles" / "A" / "save.txt") == "y");
  const std::string b2 = choco::app::backUpUserDir(dir, t, err);  // same minute: a new name, the first is kept
  CHECK(b2 != b1);
  CHECK(b2.size() > b1.size());
  CHECK(fs::exists(b1));

  using choco::app::parseArgs;
  CHECK_FALSE(parseArgs({"--import-prefs", "p.dat"}).ok());  // default user dir refused
  CHECK(parseArgs({"--import-prefs", "p.dat"}).error.find("--import-into-default") != std::string::npos);
  CHECK(parseArgs({"--import-prefs", "p.dat", "--userdir", dir}).ok());
  CHECK(parseArgs({"--import-prefs", "p.dat", "--userdir", choco::platform::defaultUserDir("Chocolatier3"), "--import-into-default"}).ok());
  CHECK(parseArgs({"--import-prefs", "p.dat", "--import-into-default"}).ok());
  CHECK_FALSE(parseArgs({"--import-into-default"}).ok());
  CHECK_FALSE(parseArgs({"--import-prefs", "p.dat", "--userdir", choco::platform::defaultUserDir("Chocolatier3")}).ok());
}

TEST_CASE("import flags: never write next to the source or into a PlayFirst folder") {
  using choco::app::parseArgs;
  TempDir d("guard");
  const std::string src = (d.path / "copy" / "prefs.dat").string();
  const auto r1 = parseArgs({"--import-prefs", src, "--userdir", (d.path / "copy").string()});
  CHECK_FALSE(r1.ok());
  CHECK(r1.error.find("folder of the file") != std::string::npos);
  CHECK_FALSE(parseArgs({"--import-prefs", src, "--userdir", (d.path / "copy" / "sub" / "u").string()}).ok());
  CHECK(parseArgs({"--import-prefs", src, "--userdir", (d.path / "other").string()}).ok());
  const auto r2 = parseArgs({"--import-prefs", src, "--userdir", (d.path / "AppData" / "PlayFirst" / "x").string()});
  CHECK_FALSE(r2.ok());
  CHECK(r2.error.find("PlayFirst") != std::string::npos);
  CHECK_FALSE(parseArgs({"--import-prefs", src, "--userdir", (d.path / "playfirst").string()}).ok());
  CHECK(choco::app::pathInside("/a/b/c", "/a/b"));
  CHECK_FALSE(choco::app::pathInside("/a/bc", "/a/b"));
}

#ifdef __APPLE__
TEST_CASE("user-dir guard is case-insensitive even when the folder does not exist (fake HOME)") {
  TempDir d("home");
  const char* old = std::getenv("HOME");
  const std::string saved = old ? old : "";
  setenv("HOME", d.str().c_str(), 1);
  const std::string wrong = d.str() + "/library/application support/chocolatier3";
  CHECK(choco::app::samePath(wrong, choco::platform::defaultUserDir("Chocolatier3")));
  CHECK_FALSE(choco::app::parseArgs({"--headless", "--userdir", wrong}).ok());
  CHECK_FALSE(choco::app::parseArgs({"--import-prefs", "x.dat", "--userdir", wrong}).ok());
  if (old) setenv("HOME", saved.c_str(), 1);
  else unsetenv("HOME");
}
#endif

TEST_CASE("prefs mapper: Windows-1252 names and the per-user report lines") {
  TempDir d("cp1252");
  UserStore store(d.str());
  store.create("Old");
  store.saveSave("return {money=1,time=1,}");
  std::vector<Section> secs = {{{"curuser", "0"}},
                               {{"name", "Old"}},
                               {{"name", "\x93Q\x94 \x80"}},
                               {{"name", "Zed"}, {"savegame", "return {money=9,time=9,}"}}};
  const PrefsParseResult r = parsePrefsDat(buildPrefsDat("elsewhere", secs));
  REQUIRE(r.ok);
  const ImportReport rep = importPrefs(r.prefs, store);
  CHECK(store.name(1) == "\xE2\x80\x9CQ\xE2\x80\x9D \xE2\x82\xAC");
  const std::string text = formatReport(rep);
  CHECK(text.find("Old: replaced the existing user; no save in the file; the existing engine save is kept") != std::string::npos);
  CHECK(text.find("Zed: new user; save imported") != std::string::npos);
  CHECK(text.find("identities in the file: 'elsewhere'") != std::string::npos);
  store.setCurrent(0);
  CHECK(store.loadSave() == "return {money=1,time=1,}");
  REQUIRE(rep.users.size() == 3);
  CHECK(rep.users[0].hadOldSave);
  CHECK(rep.users[2].saveWritten);
}
