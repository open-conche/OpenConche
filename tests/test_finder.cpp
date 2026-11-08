// The game finder (docs/design/game-finder.md): the variant table, the assets-folder resolution from the folder
// the user picked, the validation of a resolved folder against a variant, and the finder.txt preferences.
#include <doctest/doctest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "app/Args.h"
#include "app/Finder.h"
#include "platform/PathUtil.h"
#include "platform/UserDir.h"

using namespace choco;
using namespace choco::app;
namespace fs = std::filesystem;

namespace {

// A throwaway tree under the test output folder.
std::string tempRoot(const std::string& name) {
  static const std::string base = std::string(CHOCO_TEST_OUT) + "/finder-unit";
  std::error_code ec;
  fs::create_directories(base, ec);
  const fs::path dir = fs::path(base) / name;
  fs::remove_all(dir, ec);
  fs::create_directories(dir, ec);
  return platform::pathToUtf8(dir.lexically_normal());  // normalised like the finder's candidates (one separator kind)
}

std::string mkdirs(const std::string& root, const std::initializer_list<const char*>& sub) {
  fs::path p = platform::pathFromUtf8(root);
  for (const char* s : sub) p /= s;
  std::error_code ec;
  fs::create_directories(p, ec);
  return platform::pathToUtf8(p.lexically_normal());
}

bool existsDir(const std::string& path) {
  std::error_code ec;
  return fs::is_directory(platform::pathFromUtf8(path), ec);
}

// Surrounds the real tree's path: the finder mounts what the user points it at, read only.
const char* vanillaAssets() {
  static const std::string p = std::string(CHOCO_VANILLA_ASSETS);
  return existsDir(p) ? p.c_str() : nullptr;
}
const char* reforgedAssets() {
  static const std::string p = std::string(CHOCO_REFORGED_ASSETS);
  return existsDir(p) ? p.c_str() : nullptr;
}

// Points the platform's per-user data root at a scratch folder: HOME where it is the root, APPDATA on Windows.
struct SavedHome {
#if defined(_WIN32)
  static constexpr const char* kVar = "APPDATA";
#else
  static constexpr const char* kVar = "HOME";
#endif
  std::string old;
  static void set(const std::string& v) {
#if defined(_WIN32)
    _putenv_s(kVar, v.c_str());
#else
    setenv(kVar, v.c_str(), 1);
#endif
  }
  explicit SavedHome(const std::string& setter) {
    if (const char* h = std::getenv(kVar)) old = h;
    set(setter);
  }
  ~SavedHome() { set(old); }
};

}  // namespace

TEST_SUITE("finder") {

TEST_CASE("the variant table: ids resolve, unknown ids do not") {
  CHECK(findFinderVariant("choc1"));
  CHECK(findFinderVariant("choc2"));
  const FinderVariant* v3 = findFinderVariant("choc3");
  const FinderVariant* vr = findFinderVariant("choc3-reforged");
  REQUIRE(v3);
  REQUIRE(vr);
  CHECK(v3->moduleId == std::string("choc3"));
  CHECK(vr->moduleId == std::string("choc3"));
  CHECK(v3->reforged == false);
  CHECK(vr->reforged == true);
  CHECK(findFinderVariant("dd2") == nullptr);
  CHECK(findFinderVariant("") == nullptr);
  CHECK(kFinderVariantCount == 4);
}

TEST_CASE("resolveAssetCandidates: the install layouts, best first") {
  const std::string root = tempRoot("layouts");
  SUBCASE("a Windows/Linux install root: assets/ first") {
    const std::string assets = mkdirs(root, {"assets"});
    const auto c = resolveAssetCandidates(root);
    REQUIRE(c.size() >= 1);
    CHECK(c.front() == assets);
    CHECK(resolveAssetCandidates(assets).back() == assets);  // an assets folder picked directly
  }
  SUBCASE("a Mac .app bundle picked directly") {
    const std::string assets = mkdirs(root, {"Contents", "Resources", "assets"});
    const auto c = resolveAssetCandidates(root);
    REQUIRE(c.size() >= 1);
    CHECK(c.front() == assets);
  }
  SUBCASE("the folder that holds the install, one level down") {
    const std::string deep = mkdirs(root, {"hold", "App.app", "Contents", "Resources", "assets"});
    const std::string win = mkdirs(root, {"hold", "win", "assets"});
    const auto c = resolveAssetCandidates(mkdirs(root, {"hold"}));
    REQUIRE(c.size() == 3);  // both installs and the folder itself (its own "assets" has already come first)
    bool sawDeep = false, sawWin = false, sawSelf = false;
    for (const auto& cand : c) {
      sawDeep = sawDeep || cand == deep;
      sawWin = sawWin || cand == win;
      sawSelf = sawSelf || cand == mkdirs(root, {"hold"});
    }
    CHECK(sawDeep);
    CHECK(sawWin);
    CHECK(sawSelf);
  }
  SUBCASE("a Reforged checkout root") {
    const std::string assets = mkdirs(root, {"Chocolatier-Decadence-Reforged", "assets"});
    bool saw = false;
    for (const auto& cand : resolveAssetCandidates(root)) saw = saw || cand == assets;
    CHECK(saw);
  }
  SUBCASE("empty for a missing folder, deduped otherwise") {
    CHECK(resolveAssetCandidates("/no/such/folder/here").empty());
    CHECK(resolveAssetCandidates("").empty());
    const std::string assets = mkdirs(root, {"dedup", "assets"});
    (void)mkdirs(root, {"dedup", "Contents", "Resources", "assets"});
    const auto c = resolveAssetCandidates(mkdirs(root, {"dedup", "Contents", "Resources", "assets"}));
    CHECK(c.size() >= 1);
    (void)assets;
  }
}

TEST_CASE("validateAssetsForVariant recognises a folder from a probe, loose or inside assets.pfp") {
  const fs::path base = fs::temp_directory_path() / "choco_finder_probe";
  fs::remove_all(base);
  const auto put = [](const fs::path& p, const std::string& text) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << text;
  };
  const auto* v3 = findFinderVariant("choc3");
  const auto* vr = findFinderVariant("choc3-reforged");
  const auto* v1 = findFinderVariant("choc1");
  // Loose, Reforged: the marker and the main loop sit in subfolders, the settings at the root.
  put(base / "loose" / "Settings.xml", "<settings><game>chocolatier3</game></settings>");
  put(base / "loose" / "UI" / "mainloop.lua", "");
  put(base / "loose" / "community" / "account.lua", "");
  put(base / "loose" / "deep" / "a" / "b" / "c.png", "x");
  CHECK(validateAssetsForVariant(*vr, (base / "loose").string()).empty());
  CHECK(validateAssetsForVariant(*v3, (base / "loose").string()).find("Reforged") != std::string::npos);
  CHECK(validateAssetsForVariant(*v1, (base / "loose").string()).find("not Chocolatier") != std::string::npos);
  // Vanilla: everything inside an archive (PFPK, count, then name length, name, offset, size).
  std::string settings = "<settings><game>chocolatier3</game></settings>";
  const std::string names[2] = {"settings.xml", "ui/mainloop.lua"};
  const std::string bodies[2] = {settings, "x"};
  std::string pfp = "PFPK";
  auto le32 = [&](std::uint32_t v) { for (int i = 0; i < 4; ++i) pfp += static_cast<char>((v >> (8 * i)) & 0xFF); };
  le32(2);
  std::size_t dirSize = 8;
  for (const auto& n : names) dirSize += 1 + n.size() + 8;
  std::uint32_t off = static_cast<std::uint32_t>(dirSize);
  for (int i = 0; i < 2; ++i) {
    pfp += static_cast<char>(names[i].size());
    pfp += names[i];
    le32(off);
    le32(static_cast<std::uint32_t>(bodies[i].size()));
    off += static_cast<std::uint32_t>(bodies[i].size());
  }
  pfp += bodies[0] + bodies[1];
  put(base / "vanilla" / "assets.pfp", pfp);
  CHECK(validateAssetsForVariant(*v3, (base / "vanilla").string()).empty());
  CHECK(validateAssetsForVariant(*vr, (base / "vanilla").string()).find("original") != std::string::npos);
  // A truncated archive is an error, not a crash.
  put(base / "bad" / "assets.pfp", pfp.substr(0, 20));
  CHECK_FALSE(validateAssetsForVariant(*v3, (base / "bad").string()).empty());
  fs::remove_all(base);
}

TEST_CASE("addGameFromFolder registers once, names the game, and saves") {
  const fs::path base = fs::temp_directory_path() / "choco_finder_add";
  fs::remove_all(base);
  fs::create_directories(base / "home");
  const SavedHome restore((base / "home").string());
  const auto put = [](const fs::path& p, const std::string& text) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << text;
  };
  put(base / "game" / "assets" / "settings.xml", "<settings><game>chocolatier3</game></settings>");
  put(base / "game" / "assets" / "ui" / "mainloop.lua", "");
  GameLibrary library;
  const AddGameResult first = addGameFromFolder(library, (base / "game").string());
  CHECK(first.status == AddGameStatus::Added);
  CHECK(first.id == "choc3");
  CHECK(library.entries.count("choc3") == 1);
  CHECK(library.last.empty());  // adding never picks what launches
  const AddGameResult again = addGameFromFolder(library, (base / "game").string());
  CHECK(again.status == AddGameStatus::AlreadyListed);
  GameLibrary reloaded;
  std::string error;
  CHECK(loadGameLibrary(reloaded, error));
  CHECK(reloaded.entries.count("choc3") == 1);
  fs::create_directories(base / "empty");
  CHECK(addGameFromFolder(library, (base / "empty").string()).status == AddGameStatus::NotFound);
  fs::remove_all(base);
}

TEST_CASE("validateAssetsForVariant against the real trees") {
  const char* vanilla = vanillaAssets();
  const char* reforged = reforgedAssets();
  const FinderVariant* const v3 = findFinderVariant("choc3");
  const FinderVariant* const vr = findFinderVariant("choc3-reforged");
  const FinderVariant* const v1 = findFinderVariant("choc1");
  if (!vanilla || !reforged) {
    WARN_MESSAGE(false, "the Chocolatier 3 trees are not in this checkout, skipping");
    return;
  }
  CHECK(validateAssetsForVariant(*v3, vanilla).empty());
  CHECK(validateAssetsForVariant(*vr, reforged).empty());
  // The community marker is the split: vanilla under the Reforged variant and back.
  CHECK(validateAssetsForVariant(*vr, vanilla).find("Reforged") != std::string::npos);
  CHECK(validateAssetsForVariant(*v3, reforged).find("Reforged") != std::string::npos);
  // A different game in the folder is refused with the game it actually holds.
  CHECK(validateAssetsForVariant(*v1, vanilla).find("not Chocolatier") != std::string::npos);
  // Not a folder at all.
  CHECK(!validateAssetsForVariant(*v3, "/no/such/folder").empty());
  CHECK(!validateAssetsForVariant(*v3, "").empty());
}

TEST_CASE("bootFromLibrary: the switcher's click-to-boot and the re-pick fallbacks") {
  const char* vanilla = vanillaAssets();
  if (!vanilla) {
    WARN_MESSAGE(false, "the Chocolatier 3 trees are not in this checkout, skipping");
    return;
  }
  const FinderVariant* const v3 = findFinderVariant("choc3");
  const FinderVariant* const vr = findFinderVariant("choc3-reforged");
  REQUIRE(v3);
  REQUIRE(vr);
  // A valid entry boots: the switcher's one-click play.
  GameLibrary library;
  library.entries[v3->id] = {std::string("/no/such/install-anymore"), vanilla};  // dir moved, assets path holds
  library.entries[vr->id] = {"/no/such", "/no/such/assets"};
  // The options a person passed on the command line survive the boot: only the game changes (the Play click
  // once replaced the whole struct).
  Options o;
  o.scale = 2;
  o.fullscreen = true;
  o.userDir = "/scratch/profile";
  o.userDirExplicit = true;
  o.logFile = "run.log";
  o.configOverrides = {{"cheatmode", "1"}};
  std::string why;
  CHECK(bootFromLibrary(*v3, library, o, why));
  CHECK(o.assetsDir == vanilla);
  CHECK(o.game == "choc3");
  CHECK(why.empty());
  CHECK(o.scale == 2);
  CHECK(o.fullscreen);
  CHECK(o.userDir == "/scratch/profile");
  CHECK(o.userDirExplicit);
  CHECK(o.logFile == "run.log");
  CHECK(o.configOverrides.size() == 1);
  // No entry: the folder dialog path is needed.
  CHECK_FALSE(bootFromLibrary(*findFinderVariant("choc1"), library, o, why));
  CHECK(why.find("no library entry") != std::string::npos);
  // A stale entry (neither dir nor assets playable): re-pick, not a silent boot.
  CHECK_FALSE(bootFromLibrary(*vr, library, o, why));
  CHECK(why.find("no longer points") != std::string::npos);
}

TEST_CASE("the game library: save, load, every game kept, and a moved install") {
  const std::string home = tempRoot("library-home");
  const std::string vanilla = vanillaAssets() ? vanillaAssets() : "";
  const std::string reforged = reforgedAssets() ? reforgedAssets() : "";
  const SavedHome restore(home);
  GameLibrary library;
  std::string error;
  CHECK(loadGameLibrary(library, error));  // nothing written yet: empty, no error
  CHECK(library.last.empty());
  CHECK(library.entries.empty());
  library.last = "choc3";
  library.entries["choc3"] = {vanilla, vanilla};
  library.entries["choc3-reforged"] = {reforged, reforged};  // both games stay: it is a library, not a last pick
  CHECK(saveGameLibrary(library, error));
  CHECK(error.empty());
  GameLibrary loaded;
  CHECK(loadGameLibrary(loaded, error));
  CHECK(loaded.last == "choc3");
  CHECK(loaded.entries.count("choc3") == 1);
  CHECK(loaded.entries.count("choc3-reforged") == 1);
  CHECK(loaded.entries.at("choc3").assets == vanilla);
  CHECK(loaded.entries.at("choc3-reforged").dir == reforged);
#if defined(_WIN32) || defined(__APPLE__)
  CHECK(loaded.path.find("OpenConche") != std::string::npos);
#else
  CHECK(loaded.path.find("openconche") != std::string::npos);  // the XDG leaf is lowercased (platform/UserDir.cpp)
#endif
  CHECK(loaded.path.find("games.cfg") != std::string::npos);
  // The saved choice booted from the library still validates (the real trees are present in this checkout).
  if (!vanilla.empty()) {
    Options o;
    CHECK(savedFinderChoice(o));
    CHECK(o.assetsDir == vanilla);
    CHECK(o.game == "choc3");
  }
  // A stale file: `last` naming an unknown variant is a load error, not a boot.
  {
    std::ofstream out(platform::pathFromUtf8(loaded.path), std::ios::trunc);
    out << "last = dd2\n";
  }
  GameLibrary broken;
  CHECK_FALSE(loadGameLibrary(broken, error));
  Options none;
  CHECK_FALSE(savedFinderChoice(none));
}

TEST_CASE("the game library: parser corners and the finder.txt migration") {
  const std::string home = tempRoot("library-corners");
  const SavedHome restore(home);
  GameLibrary probe;
  std::string error;
  REQUIRE(loadGameLibrary(probe, error));  // nothing yet; learn where the file lives
  const fs::path cfg = platform::pathFromUtf8(probe.path);
  std::error_code ec;
  fs::create_directories(cfg.parent_path(), ec);
  const std::string utf8Dir = "/Spiele/Schokoladen\xC3\xA4r/Chocolatier 2";  // a UTF-8 path round-trips
  SUBCASE("CRLF, comments, no '=', duplicate keys, spaces around '='") {
    {
      std::ofstream out(cfg, std::ios::binary | std::ios::trunc);
      out << "# a comment line, no equals sign\r\n"
          << "last = choc2\r\n"
          << "choc2.dir = /first\r\n"
          << "choc2.dir = " << utf8Dir << "\r\n"  // the later line wins
          << "choc2.assets=" << utf8Dir << "/assets\r\n"
          << "this line has no equals and is skipped\r\n"
          << "unknown.key = ignored\r\n"
          << "\r\n";
    }
    GameLibrary lib;
    CHECK(loadGameLibrary(lib, error));
    CHECK(error.empty());
    CHECK(lib.last == "choc2");
    REQUIRE(lib.entries.count("choc2") == 1);
    CHECK(lib.entries.at("choc2").dir == utf8Dir);
    CHECK(lib.entries.at("choc2").assets == utf8Dir + "/assets");
    CHECK(lib.entries.size() == 1);  // unknown.key made no entry
    // Save and load again: the UTF-8 path and the single entry survive a round trip.
    CHECK(saveGameLibrary(lib, error));
    GameLibrary again;
    CHECK(loadGameLibrary(again, error));
    CHECK(again.entries.at("choc2").dir == utf8Dir);
    CHECK(again.last == "choc2");
  }
  SUBCASE("`last` naming a known variant with no entry is a load error") {
    {
      std::ofstream out(cfg, std::ios::binary | std::ios::trunc);
      out << "last = choc1\nchoc2.dir = /x\nchoc2.assets = /x/assets\n";
    }
    GameLibrary lib;
    CHECK_FALSE(loadGameLibrary(lib, error));
    CHECK(error.find("has no entry") != std::string::npos);
    Options o;
    CHECK_FALSE(savedFinderChoice(o));
    CHECK(o.assetsDir.empty());
  }
  SUBCASE("a half entry (dir without assets) is not bootable, and is not an error") {
    {
      std::ofstream out(cfg, std::ios::binary | std::ios::trunc);
      out << "last = choc2\nchoc2.dir = /x\n";
    }
    GameLibrary lib;
    CHECK(loadGameLibrary(lib, error));
    Options o;
    std::string why;
    CHECK_FALSE(bootFromLibrary(*findFinderVariant("choc2"), lib, o, why));
    CHECK(why == "no library entry");
  }
  SUBCASE("the finder.txt migration: no games.cfg, the old file read, no nameless entry") {
    fs::remove(cfg, ec);
    const std::string oldDir = platform::defaultUserDir("ChocolatierEngine");
    fs::create_directories(platform::pathFromUtf8(oldDir), ec);
    {
      std::ofstream out(platform::pathFromUtf8(oldDir + "/finder.txt"), std::ios::binary | std::ios::trunc);
      out << "dir = /stray-before-variant\n"  // once made an entries[""] slot
          << "assets = /stray/assets\n"
          << "variant = choc1\n"
          << "dir = /games/c1\n"
          << "assets = /games/c1/assets\n";
    }
    GameLibrary lib;
    CHECK(loadGameLibrary(lib, error));
    CHECK(lib.last == "choc1");
    CHECK(lib.entries.size() == 1);
    CHECK(lib.entries.count("") == 0);
    REQUIRE(lib.entries.count("choc1") == 1);
    CHECK(lib.entries.at("choc1").dir == "/games/c1");
    CHECK(lib.entries.at("choc1").assets == "/games/c1/assets");
  }
}

}  // TEST_SUITE
