#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>

#include "assets/AssetFS.h"
#include "assets/Content.h"
#include "games/choc3/ContentLoaders.h"
#include "assets/Xml.h"
#include "platform/UserDir.h"

using namespace choco::assets;
namespace fs = std::filesystem;

namespace {

fs::path makeTemp(const char* name) {
  fs::path p = fs::temp_directory_path() / (std::string("choco_test_") + name);
  fs::remove_all(p);
  fs::create_directories(p);
  return p;
}

void put(const fs::path& p, const std::string& text) {
  fs::create_directories(p.parent_path());
  std::ofstream(p, std::ios::binary) << text;
}

std::string get(const KeyValues& kv, const std::string& key) {
  for (auto& [k, v] : kv)
    if (k == key) return v;
  return "<none>";
}

std::string u8(const fs::path& p) {
  const auto s = p.u8string();
  return std::string(s.begin(), s.end());
}

bool haveReal() { return fs::is_directory(CHOCO_REFORGED_ASSETS); }

}  // namespace

TEST_CASE("Xml whitespace collapsing and entities") {
  CHECK(collapseWhitespace("  a \n\t b  c ") == "a b c");
  Xml x;
  REQUIRE(x.parse(std::string_view("<?xml version=\"1.0\"?><r><t>  Tom &amp;\n   Jerry\t</t><e/></r>")));
  CHECK(childText(x.root(), "t") == "Tom & Jerry");
  bool found = true;
  CHECK(childText(x.root(), "missing", &found).empty());
  CHECK_FALSE(found);
  Xml bad;
  CHECK_FALSE(bad.parse(std::string_view("<r><a></r>")));
  CHECK_FALSE(bad.error().empty());
}

TEST_CASE("AssetFS on a synthetic tree") {
  fs::path root = makeTemp("assets"), user = makeTemp("user_parent") / "sub" / "user";
  put(root / "UI" / "Main.LUA", "x");
  put(root / "items" / "apple.png", "png");
  put(root / "items" / "apple.jpg", "jpg");
  put(root / "chars" / "bob.png", "png");
  put(root / "chars" / "bob.xml", "<sequence/>");
  put(root / "snd" / "boom.ogg", "ogg");
  put(root / "snd" / "boom.lua", "lua");
  put(root / ".svn" / "entries", "skip");
  AssetFS a(u8(root), u8(user));
  REQUIRE(a.ok());
  CHECK_FALSE(fs::exists(user));  // nothing is created at mount
  CHECK(a.fileCount() == 7);
  CHECK(a.resolve("ui/main.lua") == "UI/Main.LUA");
  CHECK(a.resolve("UI\\MAIN.lua") == "UI/Main.LUA");
  CHECK(a.resolve("./ui//main.lua") == "UI/Main.LUA");
  CHECK_FALSE(a.exists("assets/ui/main.lua"));  // only "::assets/..." retries inside the index
  CHECK_FALSE(a.exists("../x"));
  CHECK_FALSE(a.exists(".svn/entries"));
  CHECK(a.readText("ui/main.lua") == "x");
  CHECK_FALSE(a.readFile("nope").has_value());

  CHECK(a.probe("chars/bob") == "chars/bob.xml");  // .xml beats .png
  CHECK(a.probe("snd/boom") == "snd/boom.ogg");     // .ogg before .lua
  CHECK(a.probe("items/apple", kTextureExtensions) == "items/apple.png");
  CHECK(a.probe("items/apple", kAssetExtensions) == "items/apple.jpg");  // .jpg before .png in the asset order
  CHECK(a.probe("chars/bob.png") == "chars/bob.png");                    // exact name wins
  CHECK_FALSE(a.probe("chars/none").has_value());
  CHECK(std::string(*(kTextureExtensions.begin() + 2)) == ".jp2");
  CHECK(a.listBySuffix("BOOM.ogg") == std::vector<std::string>{"snd/boom.ogg"});

  // "::" is a real path relative to the cwd, never the index; "::assets/x" falls back to the assets root.
  const fs::path oldCwd = fs::current_path();
  fs::path work = makeTemp("cwd");
  put(work / "Local.txt", "cwd");
  put(work / "assets" / "Shadow.txt", "shadow-cwd");
  put(root / "Shadow.txt", "shadow-root");
  put(root / "Only.txt", "root-only");
  AssetFS a2(u8(root), u8(user));
  fs::current_path(work);
  CHECK(a2.readText("::Local.txt") == "cwd");
  CHECK_FALSE(a2.exists("::ui/main.lua"));  // in the index, but "::" never consults it
  CHECK(a2.readText("::assets/Shadow.txt") == "shadow-cwd");  // cwd wins
  CHECK(a2.readText("::assets/only.txt") == "root-only");     // fallback
  CHECK_FALSE(a2.exists("::assets/nothing.txt"));
  fs::current_path(oldCwd);

  // user: goes to disk on every call
  CHECK_FALSE(a.exists("user:Prefs.dat"));
  CHECK_FALSE(a.writeFile("prefs.dat", {}));  // not a user: path
  CHECK_FALSE(a.writeText("user:../evil", "x"));
  CHECK_FALSE(fs::exists(user));
  REQUIRE(a.writeText("user:Saves/Prefs.dat", "p1"));
  CHECK(fs::is_directory(user / "saves"));  // parents created on write; names lowercased as the original does
  CHECK(a.resolve("USER:saves/prefs.dat") == "user:saves/prefs.dat");
  CHECK(a.readText("user:SAVES\\prefs.dat") == "p1");
  CHECK_FALSE(a.exists("saves/prefs.dat"));
  CHECK(a.writeText("user:saves/prefs.dat", "p2"));
  CHECK(a.readText("user:saves/prefs.dat") == "p2");
  put(user / "external.txt", "ext");  // added behind our back: seen without re-indexing
  CHECK(a.readText("user:external.txt") == "ext");
  fs::remove(user / "external.txt");
  CHECK_FALSE(a.exists("user:external.txt"));

  AssetFS none(u8(root), "");
  CHECK_FALSE(none.writeText("user:x", "y"));
}

TEST_CASE("AssetFS bad root and layering") {
  AssetFS bad("/definitely/not/here", "");
  CHECK_FALSE(bad.ok());
  CHECK(bad.error().find("/definitely/not/here") != std::string::npos);

  struct Mem : AssetSource {
    void list(std::vector<std::string>& out) const override { out = {"Z/One.txt", "z/two.txt"}; }
    std::optional<Bytes> read(const std::string& p) const override {
      Bytes b;
      for (char c : p) b.push_back(static_cast<std::byte>(c));
      return b;
    }
  };
  fs::path root = makeTemp("layer");
  put(root / "z" / "one.txt", "disk");
  AssetFS a(u8(root), "");
  a.addSource(std::make_unique<Mem>());
  CHECK(a.readText("z/one.txt") == "disk");  // earlier source wins
  CHECK(a.readText("z/two.txt") == "z/two.txt");
}

TEST_CASE("StringTable BSG and SDK lookup on synthetic rows") {
  StringTable t;
  t.loadRows({{"hello", "Hi %1%, you owe %2%!"},
              {"game", "Choc"},
              {"quit", "Quit %game%? 100%% sure"},
              {"nest", "[%quit%]"},
              {"missing_ref", "x %nope% y"},
              {"pct", "Switch to %s?"},
              {"loop", "%loop%"},
              {"two", "%game%%game%!"}});
  // BSG: no arguments, every %name% is a key (including 1)
  CHECK(t.lookup("hello") == "Hi -->1<--, you owe -->2<--!");
  CHECK(t.lookup("game") == "Choc");
  CHECK(t.lookup("quit") == "Quit Choc? 100% sure");
  CHECK(t.lookup("nest") == "[Quit Choc? 100% sure]");
  CHECK(t.lookup("missing_ref") == "x -->nope<-- y");
  CHECK(t.lookup("pct") == "Switch to %s?");
  CHECK(t.lookup("nokey") == "#####");
  CHECK(t.lookup("#literal %1%") == "literal %1%");
  CHECK(t.lookup("two") == "Choc%game%!");  // the char after a replacement (here a '%') is skipped, as in the original
  CHECK_NOTHROW(t.lookup("loop"));
  // SDK: arguments
  CHECK(t.sdkLookup("hello", {"Bob", "$5"}) == "Hi Bob, you owe $5!");
  CHECK(t.sdkLookup("hello", {"%game%", "%%"}) == "Hi Choc, you owe %!");
  CHECK(t.sdkLookup("hello") == "Hi , you owe !");
  CHECK(t.sdkLookup("hello", {"%nokey%"}) == "#####");
  CHECK(t.sdkLookup("hello", {"%open"}) == "#####");
  CHECK(t.sdkLookup("quit") == "Quit Choc? 100% sure");
  CHECK(t.sdkLookup("nest") == "[Quit %game%? 100%% sure]");  // %name% inserts the raw value
  CHECK(t.sdkLookup("missing_ref") == "#####");
  CHECK(t.sdkLookup("pct") == "#####");  // lone '%' with text after it
  CHECK(t.sdkLookup("nokey") == "#####");
  CHECK(t.sdkLookup("#lit") == "lit");
}

TEST_CASE("Excel rows reader") {
  Xml x;
  REQUIRE(x.parse(std::string_view(
      "<Workbook><Worksheet><Table>"
      "<Row><Cell><Data>a</Data></Cell><Cell><Data>  one\n two </Data></Cell></Row>"
      "<Row><Cell/><Cell><Data>ignored</Data></Cell></Row>"
      "<Row><Cell><Data>b</Data></Cell></Row>"
      "</Table></Worksheet><Worksheet><Table><Row><Cell><Data>c</Data></Cell></Row></Table></Worksheet></Workbook>")));
  auto rows = readExcelRows(x);
  REQUIRE(rows.size() == 2);
  CHECK(rows[0] == std::make_pair(std::string("a"), std::string("one two")));
  CHECK(rows[1] == std::make_pair(std::string("b"), std::string()));
}

TEST_CASE("Content file lists on a synthetic tree") {
  fs::path root = makeTemp("lists");
  put(root / "ports" / "ports.xml", "<root><port><name>Alpha</name></port><port><name>beta</name></port><port><name>gone</name></port></root>");
  put(root / "ports" / "alpha" / "alpha.lua", "");
  put(root / "ports" / "beta.lua", "");
  put(root / "quests" / "quests.xml", "<root><file>q1.lua</file><file>q2</file></root>");
  put(root / "quests" / "q1.lua", "");
  AssetFS a(u8(root), "");
  REQUIRE(a.ok());
  Warnings w;
  CHECK(loadPortFileList(a, &w) == std::vector<std::string>{"ports/alpha/alpha.lua", "ports/beta.lua"});
  CHECK(w.size() == 1);
  CHECK(loadQuestFileList(a) == std::vector<std::string>{"quests/q1.lua"});
}

TEST_CASE("UserDir default is per platform") {
  const std::string d = choco::platform::defaultUserDir("Chocolatier3");
  if (d.empty()) return;  // no HOME in this environment
#if defined(__APPLE__)
  CHECK(d.find("/Library/Application Support/Chocolatier3") != std::string::npos);
#elif defined(_WIN32)
  CHECK(d.find("\\Chocolatier3") != std::string::npos);
#else
  CHECK(d.find("chocolatier3") != std::string::npos);
#endif
}

TEST_CASE("Reforged data: lookup and probing") {
  if (!haveReal()) {
    MESSAGE("SKIPPED: " CHOCO_REFORGED_ASSETS " not present");
    return;
  }
  fs::path user = makeTemp("real_user");
  AssetFS a(CHOCO_REFORGED_ASSETS, u8(user));
  REQUIRE(a.ok());
  CHECK(a.fileCount() > 2000);
  CHECK(a.exists("ITEMS/Ingredients.XML"));
  CHECK(a.exists("::assets/items\\ingredients.xml"));  // not in the cwd, so the assets index
  CHECK(a.resolve("ITEMS/INGREDIENTS.XML") == "items/ingredients.xml");
  CHECK(a.probe("items/allspice") == "items/allspice.png");
  CHECK(a.probe("ports/ports") == "ports/ports.xml");
  CHECK(a.probe("quests/tutorial") == "quests/tutorial.lua");
  CHECK_FALSE(a.probe("items/zzz_nothing").has_value());
}

TEST_CASE("Reforged data: content") {
  if (!haveReal()) {
    MESSAGE("SKIPPED: " CHOCO_REFORGED_ASSETS " not present");
    return;
  }
  AssetFS a(CHOCO_REFORGED_ASSETS, "");
  REQUIRE(a.ok());
  Warnings w;

  auto ing = loadIngredients(a, &w);
  REQUIRE(ing.size() == 100);
  CHECK(get(ing[0], "name") == "allspice");
  CHECK(get(ing[0], "code") == "ALL");
  CHECK(ing[0].front().first == "name");  // element order preserved
  CHECK(get(ing[0], "season_end") == "52");
  CHECK(get(ing[0], "locked") == "true");
  CHECK(get(ing[2], "name") == "amaretto");
  CHECK(get(ing[2], "alcohol") == "true");
  CHECK(get(ing[2], "recipe_family") == "flavor");
  CHECK(get(ing[3], "price_high_notinseason") == "699");

  auto prod = loadProducts(a, &w);
  REQUIRE(prod.size() == 72);
  CHECK(get(prod[0], "code") == "B01");
  CHECK(get(prod[0], "category") == "bar");
  CHECK(prod[0].size() == 4);
  CHECK(get(prod[1], "milk") == "1");

  auto ports = loadPortFileList(a, &w);
  // ports.xml lists 23 names, but bombay, istanbul and riodejaneiro have no script in the data and are skipped.
  CHECK(ports.size() == 20);
  CHECK(w.size() == 3);
  w.clear();
  for (auto& p : ports) CHECK_MESSAGE(a.exists(p), p);
  CHECK(ports.front() == "ports/baghdad/baghdad.lua");
  auto quests = loadQuestFileList(a, &w);
  REQUIRE(quests.size() == 5);
  for (auto& p : quests) CHECK_MESSAGE(a.exists(p), p);
  CHECK(quests.front() == "quests/tutorial.lua");

  auto settings = loadSettings(a, &w);
  CHECK(get(settings, "game") == "chocolatier3");
  CHECK(get(settings, "locale") == "en");
  CHECK(get(settings, "cheatmode") == "0");
  CHECK(get(settings, "gameversion") == "dl,pf,1.1");

  StringTable t;
  REQUIRE(t.loadFile(a, "strings.xml", StringTable::Layer::Both, &w));
  CHECK(t.loadFile(a, "catalogue_strings.xml", StringTable::Layer::Bsg, &w));
  CHECK_FALSE(t.loadFile(a, "no_such_strings.xml"));
  CHECK(t.size() > 1000);
  CHECK(t.lookup("windowtitle") == "Chocolatier: Decadence by Design");
  CHECK(t.lookup("welcome") == "Welcome back, -->1<--!");  // BSG layer: no arguments, "1" is a missing key
  CHECK(t.sdkLookup("welcome", {"Ann"}) == "Welcome back, Ann!");
  CHECK(t.sdkLookup("welcome") == "Welcome back, !");
  CHECK(t.lookup("quit_game_placeholder") == "#####");
  CHECK(t.lookup("no_such_key_xyz") == "#####");
  CHECK(t.lookup("#raw") == "raw");
  CHECK(t.lookup("ok") == "Ok");
  CHECK(w.empty());
  for (auto& m : w) MESSAGE(m);

  // Rows with unpaired '%' (msg_buy_ingredient: "Bought <b>%%1% sacks of %2%</b>% for %3% each.")
  CHECK(t.sdkLookup("msg_buy_ingredient", {"3", "cacao", "$9"}) == "#####");  // " sacks of " is not an entry
  const std::string bsg = t.lookup("msg_buy_ingredient");
  MESSAGE("BSG msg_buy_ingredient: " << bsg);
  CHECK(bsg == "Bought <b>%1--> sacks of <--2--></b><-- for -->3<-- each.");  // quirks of the original, see Content.h
  CHECK(t.lookup("gVersionString") == "Chocolatier: Decadence by Design Reforged - v.2");
  CHECK(t.lookup("windowtitle") == t.sdkLookup("windowtitle"));
  // Every row terminates and BSG never yields an empty string for a non-empty value.
  Xml x;
  REQUIRE(x.parse(*a.readFile("strings.xml")));
  int unpaired = 0;
  for (auto& [k, v] : readExcelRows(x)) {
    const std::string r = t.lookup(k);
    if (!v.empty()) CHECK_MESSAGE(!r.empty(), k);
    if (t.sdkLookup(k, {"a", "b", "c", "d", "e"}) == "#####") ++unpaired;
  }
  MESSAGE("rows the SDK layer rejects: " << unpaired);
  CHECK(unpaired > 0);
}

TEST_CASE("Reforged data: settings need the exact ::assets path") {
  if (!haveReal()) {
    MESSAGE("SKIPPED: " CHOCO_REFORGED_ASSETS " not present");
    return;
  }
  AssetFS a(CHOCO_REFORGED_ASSETS, "");
  Warnings w;
  CHECK(loadSettings(a, &w).size() == 7);
  CHECK(w.empty());
}

TEST_CASE("Reforged data: loading everything Main touches is fast") {
  if (!haveReal()) {
    MESSAGE("SKIPPED: " CHOCO_REFORGED_ASSETS " not present");
    return;
  }
  const auto t0 = std::chrono::steady_clock::now();
  AssetFS a(CHOCO_REFORGED_ASSETS, "");
  REQUIRE(a.ok());
  auto ing = loadIngredients(a);
  auto prod = loadProducts(a);
  auto ports = loadPortFileList(a);
  auto quests = loadQuestFileList(a);
  auto settings = loadSettings(a);
  StringTable t;
  for (const char* f : {"strings.xml", "dialogue_strings.xml", "catalogue_strings.xml", "kitchen_strings.xml"}) t.loadFile(a, f);
  for (auto& q : quests) {
    std::string s = q;
    s.replace(s.size() - 4, 4, "_strings.xml");
    t.loadFile(a, s);
  }
  const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  MESSAGE("index + all Main data: " << ms << " ms");
  CHECK(!ing.empty());
  CHECK(ms < 1000.0);
}
