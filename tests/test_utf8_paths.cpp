#include <doctest/doctest.h>

#include <filesystem>

#include "assets/AssetFS.h"
#include "platform/PathUtil.h"

namespace fs = std::filesystem;
using choco::platform::pathFromUtf8;
using choco::platform::pathToUtf8;

namespace {
// "Chocolaté/ünïcode" spelled as UTF-8 bytes, so the source file's encoding does not matter.
const std::string kUtf8Dir = "Chocolat\xC3\xA9/\xC3\xBCn\xC3\xAF" "code";
}  // namespace

TEST_CASE("UTF-8 paths survive pathFromUtf8 / pathToUtf8 and the user-file writer") {
  CHECK(pathToUtf8(pathFromUtf8(kUtf8Dir)) == kUtf8Dir);

  // AssetFS builds <userdir>/<rel> from UTF-8 strings: the file must land in the non-ASCII folder.
  const fs::path base = fs::temp_directory_path() / "choco_utf8_args_test";
  fs::remove_all(base);
  const std::string root = pathToUtf8(base) + "/" + kUtf8Dir;
  {
    choco::assets::AssetFS afs(CHOCO_TEST_ASSETS "/ui", root);
    REQUIRE(afs.ok());
    CHECK(afs.writeFile("user:saves/x.txt", choco::assets::Bytes{std::byte{'o'}, std::byte{'k'}}));
  }
  CHECK(fs::exists(pathFromUtf8(root) / pathFromUtf8("saves/x.txt")));
  fs::remove_all(base);
}
