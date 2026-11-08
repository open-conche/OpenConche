#include <doctest/doctest.h>
#include "platform/Png.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "assets/AssetFS.h"
#include "gfx/Quad.h"
#include "gfx/SheetCache.h"
#include "gfx/SpriteSheet.h"
#include "gfx/TextureCache.h"

using namespace choco;
using namespace choco::gfx;
namespace fs = std::filesystem;

namespace {

fs::path tempDir(const char* name) {
  fs::path p = fs::temp_directory_path() / (std::string("choco_sheet_") + name);
  fs::remove_all(p);
  fs::create_directories(p);
  return p;
}

void writeJpg(const fs::path& p, int w, int h) {
  fs::create_directories(p.parent_path());
  // A committed solid-grey fixture at the exact size: nothing in-tree writes JPEG.
  const fs::path fixture = fs::path(CHOCO_TEST_ASSETS) / "gfx" / "image" /
                           ("grey_" + std::to_string(w) + "x" + std::to_string(h) + ".jpg");
  REQUIRE(fs::is_regular_file(fixture));
  REQUIRE(fs::copy_file(fixture, p));
}

// A mask whose blue channel is x*16 (the red and green channels are noise that must not matter).
void writeMask(const fs::path& p, int w, int h) {
  fs::create_directories(p.parent_path());
  std::vector<unsigned char> px(static_cast<std::size_t>(w) * h * 4);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      unsigned char* q = &px[(static_cast<std::size_t>(y) * w + x) * 4];
      q[0] = 255;
      q[1] = 77;
      q[2] = static_cast<unsigned char>(x * 16);
      q[3] = 255;
    }
  REQUIRE(choco::platform::writePng(p.string(), w, h, px.data()));
}

// Remembers the pixels of every upload, so alpha can be checked.
struct CapturingUploader : TextureUploader {
  std::vector<std::vector<std::uint8_t>> uploads;
  std::vector<void*> handles;
  void* upload(const std::uint8_t* rgba, int w, int h) override {
    uploads.emplace_back(rgba, rgba + static_cast<std::size_t>(w) * h * 4);
    handles.push_back(new int(static_cast<int>(uploads.size())));
    return handles.back();
  }
  void release(void* handle) override { delete static_cast<int*>(handle); }
};

}  // namespace

TEST_CASE("Mask merge: a JPG takes its alpha from the blue channel of a sibling .mask.png") {
  auto dir = tempDir("sibling");
  writeJpg(dir / "image/pic.jpg", 16, 4);
  writeMask(dir / "image/pic.mask.png", 16, 4);
  assets::AssetFS afs(dir.string(), "");
  CapturingUploader up;
  TextureCache cache(afs, &up);
  auto t = cache.load("image/pic.jpg");
  REQUIRE(t);
  CHECK(t->maskPath() == "image/pic.mask.png");
  REQUIRE(up.uploads.size() == 1);
  for (int x = 0; x < 16; ++x) CHECK(up.uploads[0][static_cast<std::size_t>(x) * 4 + 3] == x * 16);
  // Another request for the same JPG shares the entry; the same JPG with an explicit mask is a different one.
  CHECK(cache.load("image/pic.jpg") == t);
  CHECK(cache.load("image/pic") == t);
  CHECK(cache.size() == 1);

  // reuploadAll merges the mask again.
  cache.reuploadAll();
  REQUIRE(up.uploads.size() == 2);
  for (int x = 0; x < 16; ++x) CHECK(up.uploads[1][static_cast<std::size_t>(x) * 4 + 3] == x * 16);
}

TEST_CASE("Mask merge: an explicit mask, a PNG without a mask, mismatched sizes and a missing mask") {
  auto dir = tempDir("explicit");
  writeJpg(dir / "image/a.jpg", 16, 4);
  writeMask(dir / "image/other.mask.png", 16, 4);
  writeMask(dir / "image/small.png", 8, 4);
  assets::AssetFS afs(dir.string(), "");
  CapturingUploader up;
  TextureCache cache(afs, &up);

  auto plain = cache.load("image/a.jpg");
  REQUIRE(plain);
  CHECK(plain->maskPath().empty());
  CHECK(up.uploads[0][3] == 255);  // opaque without a mask

  auto masked = cache.load("image/a.jpg", "image/other.mask.png");
  REQUIRE(masked);
  CHECK(masked != plain);
  CHECK(masked->maskPath() == "image/other.mask.png");
  CHECK(up.uploads[1][5 * 4 + 3] == 80);

  auto wrongSize = cache.load("image/a.jpg", "image/small.png");
  REQUIRE(wrongSize);
  CHECK(up.uploads.back()[7 * 4 + 3] == 255);  // ignored, opaque

  auto missing = cache.load("image/a.jpg", "image/nope.png");
  REQUIRE(missing);
  CHECK(missing->maskPath().empty());
  CHECK(missing == plain);  // no usable mask: the same entry as the plain image
}

TEST_CASE("SpriteSheet::parse reads frames, registration points, the script and the generated default") {
  const char* xml = R"(<?xml version="1.0"?>
<!DOCTYPE sequence SYSTEM "sidewalkdtd">
<sequence name="x" source=".\a*.png">
  <img regx="1.0" regy="2.0" name="a.png" mask="a.mask.png" w="0" h="0"/>
  <framelist>
    <frame regx="5" regy="6" name="f1" w="10" h="11" x="1" y="2"/>
    <frame name="f2" w="12" h="13" x="3" y="4"/>
    <frame regx="9" name="f3" w="1" h="1" x="0" y="0" xform="1 0 0 1 0 0"><anchor name="n" x="1" y="2"/></frame>
  </framelist>
</sequence>)";
  SheetData d;
  std::string err;
  REQUIRE(SpriteSheet::parse(xml, d, err));
  CHECK(d.image == "a.png");
  CHECK(d.mask == "a.mask.png");
  REQUIRE(d.frames.size() == 3);
  CHECK(d.frames[0].src == RectI{1, 2, 10, 11});
  CHECK(d.frames[0].reg == PointI{5, 6});
  CHECK(d.frames[1].reg == PointI{5, 6});  // copied from the previous frame
  CHECK(d.frames[2].reg == PointI{9, 6});
  CHECK(d.script == "function DoAnim()\n    delay(60);\n    frames{1,2,3}\nend\n");
  CHECK(d.warnings.size() == 2);

  SheetData s;
  REQUIRE(SpriteSheet::parse(R"(<sequence><img name="a.png"/><framelist><frame x="0" y="0" w="1" h="1"/></framelist>
      <script luascript="function DoAnim() end"/></sequence>)", s, err));
  CHECK(s.script == "function DoAnim() end");
  REQUIRE(SpriteSheet::parse(R"(<sequence><img name="a.png"/><framelist><frame x="0" y="0" w="1" h="1"/></framelist>
      <script>function DoAnim() frames{1} end</script></sequence>)", s, err));
  CHECK(s.script == "function DoAnim() frames{1} end");
  CHECK(s.frames[0].reg == PointI{0, 0});

  CHECK_FALSE(SpriteSheet::parse("<foo/>", d, err));
  CHECK(err.find("sequence") != std::string::npos);
  CHECK_FALSE(SpriteSheet::parse("<sequence><framelist/></sequence>", d, err));
  CHECK(err.find("img") != std::string::npos);
  CHECK_FALSE(SpriteSheet::parse("<sequence><img name=\"a\"/></sequence>", d, err));
  CHECK(err.find("framelist") != std::string::npos);
  CHECK_FALSE(SpriteSheet::parse("<sequence><img name=\"a\"/><framelist/></sequence>", d, err));
  CHECK(err.find("frames") != std::string::npos);
  CHECK_FALSE(SpriteSheet::parse("<sequence", d, err));
}

TEST_CASE("SheetCache: probing, overrides, sharing and trimming") {
  auto dir = tempDir("cache");
  writeJpg(dir / "image/s.jpg", 16, 4);
  writeMask(dir / "image/s.mask.png", 16, 4);
  writeMask(dir / "image/alt.png", 16, 4);
  std::ofstream(dir / "image/s.xml") << R"(<sequence><img name="s.jpg" mask="s.mask.png"/><framelist>
      <frame regx="1" regy="2" x="0" y="0" w="8" h="4"/><frame regx="3" regy="4" x="8" y="0" w="8" h="4"/></framelist></sequence>)";
  std::ofstream(dir / "image/old.anm") << "x";
  std::ofstream(dir / "image/bad.xml") << "<sequence><img name=\"missing.png\"/><framelist><frame x=\"0\" y=\"0\" w=\"1\" h=\"1\"/></framelist></sequence>";
  assets::AssetFS afs(dir.string(), "");
  CapturingUploader up;
  TextureCache textures(afs, &up);
  SheetCache sheets(afs, textures);

  auto a = sheets.get("image/s");
  REQUIRE(a);
  CHECK(a->frames.size() == 2);
  CHECK(a->image->maskPath() == "image/s.mask.png");
  CHECK(sheets.get("image/s.xml") == a);
  CHECK(sheets.get("IMAGE/S") == a);
  auto b = sheets.get("image/s", "alt.png");  // image override next to the XML
  REQUIRE(b);
  CHECK(b != a);
  CHECK(b->image->path() == "image/alt.png");
  auto c = sheets.get("image/s", "", "alt.png");
  REQUIRE(c);
  CHECK(c->image->maskPath() == "image/alt.png");
  CHECK_FALSE(sheets.get("image/old"));
  CHECK_FALSE(sheets.get("image/bad"));
  CHECK_FALSE(sheets.get("image/missing"));
  CHECK_FALSE(sheets.get("image/s.jpg"));  // an image, not a sheet

  // Trimming: sheets nobody holds go first, then their textures.
  CHECK(sheets.size() == 3);
  a.reset();
  b.reset();
  c.reset();
  sheets.trim();
  CHECK(sheets.size() == 0);
  const std::size_t before = textures.size();
  textures.trim();
  CHECK(textures.size() < before);
  CHECK(textures.size() == 0);
}

namespace {
bool haveReforged() { return fs::is_directory(CHOCO_REFORGED_ASSETS); }
}  // namespace

TEST_CASE("Real data: travel_air has 16 frames, its registration points and a merged alpha") {
  if (!haveReforged()) {
    MESSAGE("SKIPPED: " CHOCO_REFORGED_ASSETS " not present");
    return;
  }
  assets::AssetFS afs(CHOCO_REFORGED_ASSETS, "");
  CapturingUploader up;
  TextureCache textures(afs, &up);
  SheetCache sheets(afs, textures);
  auto s = sheets.get("image/travel_air.xml");
  REQUIRE(s);
  REQUIRE(s->frames.size() == 16);
  CHECK(s->frames[0].reg == PointI{43, 34});
  CHECK(s->frames[0].src == RectI{0, 0, 87, 68});
  CHECK(s->frames[1].reg == PointI{39, 33});
  CHECK(s->frames[8].reg == PointI{44, 40});
  CHECK(s->frames[8].src == RectI{161, 73, 89, 74});
  CHECK(s->frames[15].reg == PointI{44, 33});
  CHECK(s->image->path() == "image/travel_air.jpg");
  CHECK(s->image->maskPath() == "image/travel_air.mask.png");

  // The alpha equals the mask's blue channel, pixel for pixel, and is not all opaque.
  auto maskBytes = afs.readFile("image/travel_air.mask.png");
  REQUIRE(maskBytes);
  ImageData mask;
  REQUIRE(decodeImage(*maskBytes, mask));
  REQUIRE(up.uploads.size() == 1);
  const auto& px = up.uploads[0];
  REQUIRE(px.size() == mask.rgba.size());
  bool translucent = false;
  for (std::size_t i = 0; i < px.size() / 4; ++i) {
    REQUIRE(px[i * 4 + 3] == mask.rgba[i * 4 + 2]);
    if (px[i * 4 + 3] != 255) translucent = true;
  }
  CHECK(translucent);
  CHECK(s->script == SpriteSheet::defaultScript(16));
}

TEST_CASE("Real data: every character sheet draws inside its image (no texture wrap)") {
  if (!haveReforged()) {
    MESSAGE("SKIPPED: " CHOCO_REFORGED_ASSETS " not present");
    return;
  }
  assets::AssetFS afs(CHOCO_REFORGED_ASSETS, "");
  TextureCache textures(afs, nullptr);
  SheetCache sheets(afs, textures);
  int checked = 0, overflowing = 0;
  for (const auto& e : fs::directory_iterator(fs::path(CHOCO_REFORGED_ASSETS) / "characters")) {
    if (e.path().extension() != ".xml") continue;
    auto s = sheets.get("characters/" + e.path().stem().string());
    if (!s) continue;
    ++checked;
    for (const auto& f : s->frames) {
      const RectF src{float(f.src.x), float(f.src.y), float(f.src.w), float(f.src.h)};
      if (f.src.x + f.src.w > s->image->width() || f.src.y + f.src.h > s->image->height()) ++overflowing;
      auto q = buildTexturedQuad(src, {0, 0, src.w, src.h}, s->image->width(), s->image->height(), {}, 0, {}, false, false);
      for (const auto& v : q) {
        CHECK(v.uv.x >= 0.0f);
        CHECK(v.uv.x <= 1.0f);
        CHECK(v.uv.y >= 0.0f);
        CHECK(v.uv.y <= 1.0f);
      }
    }
  }
  CHECK(checked > 50);
  MESSAGE(checked << " sheets, " << overflowing << " frames overflow their image");
  // zur_marketkeep's body is 264 rows in a 219-row image
  auto z = sheets.get("characters/zur_marketkeep");
  REQUIRE(z);
  CHECK(z->frames[0].src.h > z->image->height());
}

TEST_CASE("Real data: characters/main_alex.xml gives four frames") {
  if (!haveReforged()) {
    MESSAGE("SKIPPED: " CHOCO_REFORGED_ASSETS " not present");
    return;
  }
  assets::AssetFS afs(CHOCO_REFORGED_ASSETS, "");
  TextureCache textures(afs, nullptr);
  SheetCache sheets(afs, textures);
  auto s = sheets.get("characters/main_alex");
  REQUIRE(s);
  REQUIRE(s->frames.size() == 4);
  CHECK(s->frames[0].reg == PointI{70, 105});
  CHECK(s->frames[0].src == RectI{0, 0, 160, 240});
  CHECK(s->frames[1].reg == PointI{17, 75});
  CHECK(s->frames[3].src == RectI{0, 231, 51, 61});
  CHECK(s->image->path() == "characters/main_alex.png");
}
