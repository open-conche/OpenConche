#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "assets/AssetFS.h"
#include "assets/AssetSource.h"

using namespace choco::assets;
namespace fs = std::filesystem;

namespace {

void le32(std::string& s, std::uint32_t v) {
  for (int i = 0; i < 4; ++i) s += static_cast<char>((v >> (8 * i)) & 0xFF);
}

// An Ogg payload: the BOS page magic plus a tag (a string literal cannot carry the NUL).
std::string ogg(const std::string& tag) { return std::string("OggS\0\2", 6) + tag; }

std::string str(const Bytes& b) { return std::string(reinterpret_cast<const char*>(b.data()), b.size()); }

// Builds a CAD from (authoring path, content) pairs plus a symbol table, entries sorted by key like the real file.
std::string makeCad(const std::vector<std::pair<std::string, std::string>>& files, bool omitFromSymbols = false) {
  std::string xml = "\r\n<PropertySet>\r\n";
  for (std::size_t i = 0; i < files.size(); ++i)
    if (!(omitFromSymbols && i == 0))
      xml += "\t<Property>\r\n\t\t<Key>" + files[i].first + "</Key>\r\n\t\t<Value>" + files[i].first + "</Value>\r\n\t</Property>\r\n";
  xml += "</PropertySet>\r\n";
  const std::string symPath = "Resources\\symbols.xml";
  struct E {
    std::uint32_t key;
    const std::string* data;
  };
  std::vector<std::pair<std::string, std::string>> all = files;
  std::vector<E> es;
  for (auto& f : all) es.push_back({CadSource::hashPath(f.first), &f.second});
  const std::uint32_t symKey = 0xddfefdc8u;  // the symbol table is keyed outside the path hashes
  es.push_back({symKey, &xml});
  std::sort(es.begin(), es.end(), [](const E& a, const E& b) { return a.key < b.key; });
  std::string out(8, '\0');
  le32(out, symKey);
  le32(out, static_cast<std::uint32_t>(es.size()));
  out.append(16, '\0');
  std::uint32_t off = 0x20 + 16 * static_cast<std::uint32_t>(es.size());
  for (auto& e : es) {
    le32(out, e.key);
    le32(out, 0);
    le32(out, static_cast<std::uint32_t>(e.data->size()));
    le32(out, off);
    off += static_cast<std::uint32_t>(e.data->size());
  }
  for (auto& e : es) out += *e.data;
  (void)symPath;
  return out;
}

fs::path makeTemp(const char* name) {
  fs::path p = fs::temp_directory_path() / (std::string("choco_test_cad_") + name);
  fs::remove_all(p);
  fs::create_directories(p);
  return p;
}

void writeFile(const fs::path& p, const std::string& s) {
  fs::create_directories(p.parent_path());
  std::ofstream f(p, std::ios::binary);
  REQUIRE(f.is_open());
  f.write(s.data(), static_cast<std::streamsize>(s.size()));
}

fs::path realCad() { return fs::path(CHOCO_VANILLA_ASSETS) / "cadi" / "CADI_Choc3_Adam  FINAL.CAD"; }

}  // namespace

TEST_CASE("CadSource: SuperFastHash matches the DLL's hash on known strings") {
  CHECK(CadSource::hashPath("") == 0);
  CHECK(CadSource::hashPath("a") == 0x115ea782u);
  CHECK(CadSource::hashPath("ab") == 0x516b8b44u);
  CHECK(CadSource::hashPath("abc") == 0xd2be198au);
  CHECK(CadSource::hashPath("abcd") == 0xdad8b8dbu);
  CHECK(CadSource::hashPath("abc\xe9") == 0xa461e067u);
}

TEST_CASE("CadSource: synthetic container serves named Oggs lazily and skips the rest") {
  const fs::path dir = makeTemp("synthetic");
  const std::string pa = "Resources\\a\\b\\Click.ogg", pb = "Resources\\c\\Click.ogg";
  writeFile(dir / "x.cad", makeCad({{pa, ogg("-click")},
                                    {pb, ogg("-second")},  // same basename: the earlier directory entry wins
                                    {"Resources\\project.xml", "<Project/>"},
                                    {"Resources\\d\\unnamed.ogg", ogg("-nameless")}}));
  std::string err;
  auto src = CadSource::open(dir / "x.cad", err);
  REQUIRE_MESSAGE(src, err);
  CHECK(src->entryCount() == 5);
  CHECK(src->oggCount() == 3);
  CHECK(src->unnamedOggCount() == 0);
  CHECK(src->entries().size() == 3);
  std::vector<std::string> names;
  src->list(names);
  std::sort(names.begin(), names.end());
  CHECK(names == std::vector<std::string>{"cadi/audio/Click.ogg", "cadi/audio/unnamed.ogg", "cadi/cad/Resources/a/b/Click.ogg",
                                          "cadi/cad/Resources/c/Click.ogg", "cadi/cad/Resources/d/unnamed.ogg"});
  // Directory order is by key, so the copy served by basename is the one with the lower key.
  const bool aFirst = CadSource::hashPath(pa) < CadSource::hashPath(pb);
  auto click = src->read("cadi/audio/Click.ogg");
  REQUIRE(click);
  CHECK(str(*click) == ogg(aFirst ? "-click" : "-second"));
  CHECK(str(*src->read("cadi/cad/Resources/a/b/Click.ogg")) == ogg("-click"));
  CHECK(str(*src->read("cadi/cad/Resources/c/Click.ogg")) == ogg("-second"));
  CHECK_FALSE(src->read("cadi/audio/project.xml"));
  fs::remove_all(dir);
}

TEST_CASE("CadSource: the later twin of a differing duplicate is also served under Reforged's alias") {
  const fs::path dir = makeTemp("alias");
  const std::string pa = "Resources\\m\\footsteps.ogg", pb = "Resources\\n\\footsteps.ogg";
  writeFile(dir / "x.cad", makeCad({{pa, ogg("-one")}, {pb, ogg("-two")}, {"Resources\\x\\bird1.ogg", ogg("-b")},
                                    {"Resources\\y\\bird1.ogg", ogg("-b")}}));
  std::string err;
  auto src = CadSource::open(dir / "x.cad", err);
  REQUIRE_MESSAGE(src, err);
  const bool aFirst = CadSource::hashPath(pa) < CadSource::hashPath(pb);
  CHECK(str(*src->read("cadi/audio/footsteps.ogg")) == ogg(aFirst ? "-one" : "-two"));
  auto alias = src->read("cadi/audio/footsteps_2.ogg");
  REQUIRE(alias);
  CHECK(str(*alias) == ogg(aFirst ? "-two" : "-one"));
  CHECK_FALSE(src->read("cadi/audio/Ce_MapMELODICLAYER (2).ogg"));  // no such twin here
  std::vector<std::string> names;
  src->list(names);
  CHECK(std::count(names.begin(), names.end(), "cadi/audio/bird1_2.ogg") == 0);  // identical twins get no alias
  fs::remove_all(dir);
}

TEST_CASE("CadSource: a missing or empty symbol table is an error") {
  const fs::path dir = makeTemp("nosym");
  std::string err;
  std::string noSym = makeCad({{"Resources\\a.ogg", ogg("-a")}});
  noSym[8] = static_cast<char>(noSym[8] ^ 0x55);  // header names a key no entry has
  writeFile(dir / "nosym.cad", noSym);
  CHECK_FALSE(CadSource::open(dir / "nosym.cad", err));
  CHECK(err.find("symbol") != std::string::npos);
  writeFile(dir / "empty.cad", makeCad({{"Resources\\a.ogg", ogg("-a")}}, true));  // symbol table names nothing
  err.clear();
  CHECK_FALSE(CadSource::open(dir / "empty.cad", err));
  CHECK(err.find("names none") != std::string::npos);
  fs::remove_all(dir);
}

TEST_CASE("CadSource: an Ogg missing from the symbol table is counted, not served") {
  const fs::path dir = makeTemp("unnamed");
  writeFile(dir / "x.cad", makeCad({{"Resources\\gone.ogg", ogg("-x")}, {"Resources\\kept.ogg", ogg("-y")}}, true));
  std::string err;
  auto src = CadSource::open(dir / "x.cad", err);
  REQUIRE_MESSAGE(src, err);
  CHECK(src->oggCount() == 2);
  CHECK(src->unnamedOggCount() == 1);
  std::vector<std::string> names;
  src->list(names);
  std::sort(names.begin(), names.end());
  CHECK(names == std::vector<std::string>{"cadi/audio/kept.ogg", "cadi/cad/Resources/kept.ogg"});
  fs::remove_all(dir);
}

TEST_CASE("CadSource rejects corrupt containers") {
  const fs::path dir = makeTemp("corrupt");
  const std::string good = makeCad({{"Resources\\a.ogg", ogg("-a")}});
  std::string err;

  writeFile(dir / "tiny.cad", "short");
  CHECK_FALSE(CadSource::open(dir / "tiny.cad", err));
  CHECK_FALSE(err.empty());

  std::string hugeCount = good;  // count field at 0x0c
  hugeCount[0x0c] = '\xff';
  hugeCount[0x0d] = '\xff';
  writeFile(dir / "count.cad", hugeCount);
  err.clear();
  CHECK_FALSE(CadSource::open(dir / "count.cad", err));
  CHECK(err.find("does not fit") != std::string::npos);

  std::string zeroCount = good;
  zeroCount[0x0c] = 0;
  writeFile(dir / "zero.cad", zeroCount);
  CHECK_FALSE(CadSource::open(dir / "zero.cad", err));

  writeFile(dir / "trunc.cad", good.substr(0, good.size() - 8));  // last entry runs past the end
  err.clear();
  CHECK_FALSE(CadSource::open(dir / "trunc.cad", err));
  CHECK(err.find("outside the file") != std::string::npos);

  std::string highKey = good;  // upper half of the first key
  highKey[0x24] = 1;
  writeFile(dir / "key.cad", highKey);
  err.clear();
  CHECK_FALSE(CadSource::open(dir / "key.cad", err));
  CHECK(err.find("32 bits") != std::string::npos);

  CHECK_FALSE(CadSource::open(dir / "missing.cad", err));
  fs::remove_all(dir);
}

TEST_CASE("AssetFS mounts cadi/*.CAD as cadi/audio only when that directory is absent; loose files win") {
  const fs::path dir = makeTemp("assetfs");
  writeFile(dir / "cadi" / "Pack.CAD", makeCad({{"Resources\\x\\Boom.ogg", ogg("-boom")}, {"Resources\\x\\Zap.ogg", ogg("-zap")}}));
  {
    AssetFS a(dir.string(), "");
    CHECK(a.error().empty());
    auto b = a.readFile("CADI/Audio/boom.OGG");
    REQUIRE(b);
    CHECK(str(*b) == ogg("-boom"));
  }
  writeFile(dir / "cadi" / "ui_click.ogg", "loose");
  writeFile(dir / "cadi" / "Pack2.CAD", makeCad({{"Resources\\ui_click.ogg", ogg("-packed")}}));
  {
    AssetFS a(dir.string(), "");
    auto b = a.readFile("cadi/ui_click.ogg");
    REQUIRE(b);
    CHECK(str(*b) == "loose");
    CHECK(a.readFile("cadi/audio/ui_click.ogg"));
  }
  fs::create_directories(dir / "cadi" / "audio");
  writeFile(dir / "cadi" / "audio" / "other.ogg", "x");
  {
    AssetFS a(dir.string(), "");
    CHECK_FALSE(a.readFile("cadi/audio/boom.ogg"));
    CHECK(a.readFile("cadi/audio/other.ogg"));
  }
  fs::remove_all(dir);
}

TEST_CASE("AssetFS carries on without a corrupt CAD (audio is optional)") {
  const fs::path dir = makeTemp("assetfs_bad");
  writeFile(dir / "cadi" / "bad.cad", "not a cad file at all, but longer than a header....");
  writeFile(dir / "main.lua", "x");
  AssetFS a(dir.string(), "");
  CHECK(a.error().empty());
  CHECK(a.readFile("main.lua"));
  CHECK_FALSE(a.readFile("cadi/audio/anything.ogg"));
  fs::remove_all(dir);
}

TEST_CASE("vanilla CAD: directory facts, and every served Ogg equals Reforged's file of the same name") {
  if (!fs::is_regular_file(realCad())) return;  // not available in this checkout
  std::string err;
  auto src = CadSource::open(realCad(), err);
  REQUIRE_MESSAGE(src, err);
  CHECK(src->entryCount() == 177);
  CHECK(src->oggCount() == 175);
  CHECK(src->unnamedOggCount() == 0);
  CHECK(src->entries().size() == 175);

  std::vector<std::string> all, names;
  src->list(all);
  const std::string audio = "cadi/audio/", cad = "cadi/cad/";
  std::size_t cadNames = 0;
  for (auto& n : all) {
    if (n.compare(0, audio.size(), audio) == 0) names.push_back(n);
    else if (n.compare(0, cad.size(), cad) == 0) ++cadNames;
  }
  CHECK(cadNames == 175);        // every Ogg by its unique authoring path
  CHECK(names.size() == 164);    // 162 distinct basenames plus the two Reforged aliases
  for (auto& e : src->entries()) {
    std::string q = e.path;
    std::replace(q.begin(), q.end(), '\\', '/');
    auto b = src->read(cad + q);
    REQUIRE_MESSAGE(b, q);
    CHECK(b->size() == e.size);
  }

  const fs::path refDir = fs::path(CHOCO_REFORGED_ASSETS) / "cadi" / "audio";
  if (!fs::is_directory(refDir)) {
    MESSAGE("SKIPPED byte comparison: " CHOCO_REFORGED_ASSETS " not present");
    return;
  }
  std::map<std::string, fs::path> ref;  // lowercase file name -> path
  for (auto& e : fs::directory_iterator(refDir)) {
    std::string n = e.path().filename().string();
    std::transform(n.begin(), n.end(), n.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    ref[n] = e.path();
  }
  std::vector<std::string> mismatches;
  std::set<std::string> served;
  int equal = 0, byBasename = 0;
  for (const std::string& n : names) {
    const std::string file = AssetFS::normalize(n).substr(audio.size());
    served.insert(file);
    const bool alias = file == "ce_mapmelodiclayer (2).ogg" || file == "footsteps_2.ogg";
    auto it = ref.find(file);
    auto data = src->read(n);
    REQUIRE(data);
    if (it == ref.end()) {
      mismatches.push_back(n + " (no such Reforged file)");
      continue;
    }
    auto disk = readWholeFile(it->second);
    REQUIRE(disk);
    if (*disk == *data) {
      ++equal;
      if (!alias) ++byBasename;
    } else {
      mismatches.push_back(n + " (bytes differ)");
    }
  }
  for (auto& m : mismatches) MESSAGE("mismatch: " << m);
  CHECK(byBasename == 162);  // by name; by entry 170 of 175 equal a Reforged file (the rest are same-name twins)
  CHECK(equal == 164);       // including the two aliases
  CHECK(mismatches.empty());
  CHECK(served.size() == ref.size());  // the served names are exactly Reforged's files

  AssetFS a(CHOCO_VANILLA_ASSETS, "");
  CHECK(a.error().empty());
  auto viaFs = a.readFile("cadi/audio/bank.ogg");
  REQUIRE(viaFs);
  CHECK(viaFs->size() == 49461);
  auto alt = a.readFile("cadi/audio/Ce_MapMELODICLAYER (2).ogg");
  REQUIRE(alt);
  CHECK(alt->size() == 190267);
}
