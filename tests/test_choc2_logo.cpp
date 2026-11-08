// Chocolatier 2's logo editor (docs/re/choc2-logo-and-trade.md part A): LogoSpec, the CPU renderer, the LogoEditor
// window with its natives, and the .LOGO port sign.
#include <doctest/doctest.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include "platform/Png.h"

#include "games/Registry.h"
#include "ScriptHarness.h"
#include "games/common/PortWindow.h"
#include "games/choc2/LogoEditor.h"
#include "games/choc2/LogoRenderer.h"
#include "games/choc2/LogoSpec.h"
#include "games/choc2/Port.h"
#include "games/choc2/PortScene.h"
#include "gfx/SheetCache.h"
#include "gfx/TextureCache.h"
#include "script/LuaStyle.h"
#include "script/ScriptHost.h"
#include "ui/widgets/InputWidgets.h"
#include "ui/widgets/Widgets.h"

using namespace choco;
using game::choc2::LogoEditor;
using game::choc2::LogoSpec;
namespace fs = std::filesystem;

namespace {

std::string u8path(const fs::path& p) {
  const auto s = p.u8string();
  return std::string(s.begin(), s.end());
}

void writePng(const fs::path& p, int w, int h, unsigned char r, unsigned char g, unsigned char b, unsigned char a = 255) {
  fs::create_directories(p.parent_path());
  std::vector<unsigned char> px(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4);
  for (std::size_t i = 0; i < px.size(); i += 4) {
    px[i] = r, px[i + 1] = g, px[i + 2] = b, px[i + 3] = a;
  }
  REQUIRE(choco::platform::writePng(p.string(), w, h, px.data()));
}

// Solid-colour backgrounds and graphics: bg1 red, bg4 blue, logo1 green, logo10 yellow, an item picture and a game script.
std::string logoAssets() {
  const fs::path dir = fs::temp_directory_path() / "choco_test_choc2_logo";
  fs::remove_all(dir);
  for (int i = 1; i <= 8; ++i) writePng(dir / ("logo/bg" + std::to_string(i) + ".png"), 350, 200, 90, 90, 90);
  for (int i = 1; i <= 12; ++i) writePng(dir / ("logo/logo" + std::to_string(i) + ".png"), 200, 200, 90, 90, 90);
  writePng(dir / "logo/bg1.png", 350, 200, 255, 0, 0);
  writePng(dir / "logo/bg2.png", 350, 200, 0, 0, 255);
  writePng(dir / "logo/bg4.png", 350, 200, 0, 0, 255);
  writePng(dir / "logo/logo1.png", 200, 200, 0, 255, 0);
  writePng(dir / "logo/logo10.png", 200, 200, 255, 255, 0);
  writePng(dir / "item/tea_big.png", 64, 64, 255, 0, 255);
  writePng(dir / "knob.png", 12, 12, 200, 200, 200);
  fs::create_directories(dir / "ui");
  std::ofstream(dir / "ui/mainloop.lua") << "function Main() while true do WaitForMessage() end end\nreturn Main\n";
  return u8path(dir);
}

// The pixel at (x, y) as {r,g,b,a}.
std::vector<int> px(const gfx::ImageData& img, int x, int y) {
  const std::size_t i = (static_cast<std::size_t>(y) * static_cast<std::size_t>(img.width) + static_cast<std::size_t>(x)) * 4;
  return {img.rgba[i], img.rgba[i + 1], img.rgba[i + 2], img.rgba[i + 3]};
}

void setString(test::ScriptEnv& e, const char* global, const std::string& value) {
  lua_pushlstring(e.lua.L(), value.data(), value.size());
  lua_setglobal(e.lua.L(), global);
}

// A LogoSpec read from a Lua expression that evaluates to the table.
LogoSpec specFromLua(test::ScriptEnv& e, const std::string& expr) {
  lua_State* L = e.lua.L();
  const int top = lua_gettop(L);
  const std::string code = "return " + expr;
  REQUIRE(luaL_loadbuffer(L, code.data(), code.size(), "=spec") == 0);
  REQUIRE(lua_pcall(L, 0, 1, 0) == 0);
  LogoSpec out;
  {
    const script::LuaStyle view = script::LuaStyle::plainView(L, lua_gettop(L), &e.lua);
    out = LogoSpec::fromStyle(view);
  }
  lua_settop(L, top);
  return out;
}

struct LogoEnv : test::ScriptEnv {
  LogoEnv() : ScriptEnv(logoAssets()), textures(fs, nullptr), sheets(fs, textures), host(ctx) {
    ctx.module = games::findModule("choc2");
    ctx.module->attach(ctx);
    wm.setTextures(&textures);
    wm.setSheets(&sheets);
    booted = host.boot();
  }
  LogoEditor* editor() { return LogoEditor::current(wm); }
  ui::SliderWindow* slider(const char* name) { return dynamic_cast<ui::SliderWindow*>(wm.topModal()->find(name)); }
  // The editor's string through the Lua natives, parsed back.
  LogoSpec spec() {
    REQUIRE(run("s_ = GetLogoString(); t_ = loadstring(s_)()") == 0);
    return specFromLua(*this, "t_");
  }
  // An editor as logoedit_basic.lua builds it: the table, the preview position, the allowed items and the named sliders.
  void open(const std::string& logo = "{t='Acme'}", const std::string& items = "{'tea_big'}") {
    std::string sliders;
    for (const char* n : {"background", "graphic", "font", "textsize", "textalpha", "tColorH", "tColorS", "tColorV", "textx", "texty",
                          "logoscale", "logoalpha", "logorotate", "logox", "logoy"})
      sliders += std::string("Slider{name='") + n + "', x=0, y=0, w=100, sliderimage='knob', value=0}, ";
    REQUIRE(run("PushModal('dlg'); LogoEditor{x=0, y=0, w=700, h=365, logo=" + logo + ", logox=5, logoy=85, itemsAllowed=" + items +
                ", Window{x=0, y=0, w=300, h=200, " + sliders + "}, Button{name='outline', x=0, y=0, w=10, h=10, type=kToggle, command=function() ToggleOutline() end},"
                " Button{name='italic', x=0, y=0, w=10, h=10, type=kToggle, command=function() ToggleItalic() end},"
                " Button{name='bold', x=0, y=0, w=10, h=10, type=kToggle, command=function() ToggleBold() end}}()") == 0);
    REQUIRE(lastError.empty());
  }
  gfx::TextureCache textures;
  gfx::SheetCache sheets;
  script::ScriptHost host;
  bool booted = false;
};

}  // namespace

// ---------------------------------------------------------------- LogoSpec

TEST_CASE("LogoSpec: the defaults are the new-game table of sim.lua:125-129") {
  const LogoSpec s = LogoSpec::defaults("Tester");
  CHECK(s.b == "bg4");
  CHECK(s.l == "logo/logo10");
  CHECK(s.lx == 500);
  CHECK(s.ly == 500);
  CHECK(s.la == 1000);
  CHECK(s.ls == 1000);
  CHECK(s.lf == 0);
  CHECK(s.lr == 1000);
  CHECK(s.t == "Tester");
  CHECK(s.to == 5);
  CHECK(s.ts == 40);
  CHECK(s.ta == 1000);
  CHECK(s.tch == 500);
  CHECK(s.tcs == 500);
  CHECK(s.tcv == 500);
  CHECK(s.tx == 500);
  CHECK(s.ty == 500);
  CHECK(s.tfo == 1);
  CHECK(s.tfi == 0);
  CHECK(s.tfb == 0);
}

TEST_CASE("LogoSpec: the emitted string loadstrings into an equal table, whatever the text") {
  test::ScriptEnv e(CHOCO_TEST_ASSETS);
  LogoSpec s = LogoSpec::defaults("He said \"hi\"\\ and\nleft\t1");
  s.b = "bg7";
  s.l = "item/tea_big";
  s.lx = 0, s.ly = 1000, s.la = 321, s.ls = 55, s.lf = 8, s.lr = 250;
  s.to = LogoSpec::kRight, s.ts = 77, s.ta = 900, s.tch = 999, s.tcs = 1, s.tcv = 1000, s.tx = 17, s.ty = 983;
  s.tfo = 0, s.tfi = 1, s.tfb = 1;
  INFO(e.lastError);
  setString(e, "chunk_", s.toLuaString());
  REQUIRE(e.run("local f = loadstring(chunk_); if type(f) ~= 'function' then error('not a chunk') end; t_ = f()") == 0);
  CHECK(specFromLua(e, "t_") == s);
  // The text came back byte for byte.
  REQUIRE(e.run("tt_ = t_.t; nk_ = 0; for k in pairs(t_) do nk_ = nk_ + 1 end") == 0);
  CHECK(e.getString("tt_") == s.t);
  CHECK(e.getNumber("nk_") == 20);  // b l t and the 17 numbers
  // A control character and a quote at the end survive too, and numbers stay integers.
  LogoSpec c = LogoSpec::defaults(std::string("a\x01") + "7\"");
  setString(e, "chunk_", c.toLuaString());
  REQUIRE(e.run("t_ = loadstring(chunk_)()") == 0);
  CHECK(specFromLua(e, "t_") == c);
  CHECK(s.toLuaString().find('.') == std::string::npos);
}

TEST_CASE("LogoSpec: any key order is read, absent keys keep the defaults, values are clamped") {
  test::ScriptEnv e(CHOCO_TEST_ASSETS);
  const LogoSpec a = specFromLua(e, "{ty=100, t='X', tx=900, b='bg2', l='logo3', ts=500, ta=-5, tfb=3, lf=99}");
  CHECK(a.t == "X");
  CHECK(a.tx == 900);
  CHECK(a.ty == 100);
  CHECK(a.b == "bg2");
  CHECK(a.l == "logo/logo3");  // the loader's fix for a graphic without a directory
  CHECK(a.ts == 100);
  CHECK(a.ta == 0);
  CHECK(a.tfb == 1);
  CHECK(a.lf == 8);
  CHECK(a.lx == 500);  // absent
  CHECK(a.tfo == 1);   // absent: the Lua forces it on
  // Floats from an older save round.
  CHECK(specFromLua(e, "{lx=499.6}").lx == 500);
  // The same table in another order is an equal spec.
  CHECK(specFromLua(e, "{t='N', tx=10, ty=20}") == specFromLua(e, "{ty=20, tx=10, t='N'}"));
}

TEST_CASE("LogoSpec: slider mapping and the graphic list") {
  using namespace game::choc2;
  CHECK(indexForSlider(0, 8) == 0);
  CHECK(indexForSlider(1, 8) == 7);
  for (int n : {8, 12, 13, 9})
    for (int i = 0; i < n; ++i) CHECK(indexForSlider(sliderForIndex(i, n), n) == i);
  CHECK(sliderForIndex(3, 1) == 0);
  const auto list = logoGraphicList({"tea_big", "cocoa_big"});
  REQUIRE(list.size() == 14);
  CHECK(list[0] == "logo/logo1");
  CHECK(list[11] == "logo/logo12");
  CHECK(list[12] == "item/tea_big");
  CHECK(logoFontNames().size() == 9);
  CHECK(logoFontNames()[4] == "fertigo");
}

// ---------------------------------------------------------------- the renderer's maths

TEST_CASE("Logo colour: HSV thousandths to RGB and back, and the editor's eight swatches") {
  using namespace game::choc2;
  CHECK(hsvToRgb(0, 1000, 1000) == Rgb8{255, 0, 0});
  CHECK(hsvToRgb(1000, 1000, 1000) == Rgb8{255, 0, 0});  // a full turn is red again
  CHECK(hsvToRgb(333, 1000, 1000).g == 255);
  CHECK(hsvToRgb(500, 1000, 1000) == Rgb8{0, 255, 255});
  CHECK(hsvToRgb(666, 1000, 1000).b == 255);
  CHECK(hsvToRgb(0, 0, 500) == Rgb8{128, 128, 128});  // the default (500, 500, 500) is a muted cyan
  CHECK(hsvToRgb(500, 500, 500) == Rgb8{64, 128, 128});
  CHECK(hsvToRgb(250, 0, 0) == Rgb8{0, 0, 0});
  const int swatches[8][3] = {{255, 255, 255}, {209, 27, 27}, {245, 121, 12}, {23, 148, 16},
                              {33, 94, 217},   {150, 29, 229}, {242, 128, 227}, {0, 0, 0}};
  for (const auto& s : swatches) {
    int h, sat, v;
    rgbToHsv(s[0], s[1], s[2], h, sat, v);
    const Rgb8 back = hsvToRgb(h, sat, v);
    CHECK(std::abs(back.r - s[0]) <= 1);  // thousandths lose a little
    CHECK(std::abs(back.g - s[1]) <= 1);
    CHECK(std::abs(back.b - s[2]) <= 1);
  }
  int h, s, v;
  rgbToHsv(0, 0, 255, h, s, v);
  CHECK(h == 667);
  CHECK(s == 1000);
  CHECK(v == 1000);
}

TEST_CASE("Logo graphic placement: the three layout slots, scale, rotation and alpha") {
  using namespace game::choc2;
  LogoSpec s;
  s.lx = 0;
  auto g = graphicPlacement(s, 200, 200);
  CHECK(g.x == 0);
  CHECK(g.y == 0.5f * (200 - 200) + 0);  // ly 500 of no free height
  s.lx = 500;
  CHECK(graphicPlacement(s, 200, 200).x == 75);
  s.lx = 1000;
  CHECK(graphicPlacement(s, 200, 200).x == 150);  // Chocolatier 1's fixed slots 0, 75, 150
  s.ls = 500;
  g = graphicPlacement(s, 200, 200);
  CHECK(g.w == 100);
  CHECK(g.x == 250);  // right edge at the canvas edge
  CHECK(g.y == 50);   // ly 500 over 200 - 100
  s.lr = 250;
  s.la = 400;
  g = graphicPlacement(s, 200, 200);
  CHECK(g.rotationDeg == doctest::Approx(90));
  CHECK(g.alpha == doctest::Approx(0.4f));
  s.lr = 1000;
  CHECK(graphicPlacement(s, 200, 200).rotationDeg == doctest::Approx(360));
  // An item picture keeps its own size.
  s = LogoSpec{};
  s.lx = 1000;
  g = graphicPlacement(s, 64, 64);
  CHECK(g.w == 64);
  CHECK(g.x == 286);
}

TEST_CASE("Logo text: the block sits by the free-space rule and the lines justify within it") {
  using namespace game::choc2;
  gfx::text::FontCache fonts(nullptr);  // the built-in font
  LogoSpec s;
  s.ts = 20;
  s.tfo = 0;
  s.t = "AAAA";
  s.tx = 0, s.ty = 0;
  TextBlock b = layoutLogoText(s, &fonts);
  CHECK(b.bounds.x == 0);
  CHECK(b.bounds.y == 0);
  CHECK(b.bounds.w > 0);
  s.tx = 1000, s.ty = 1000;
  b = layoutLogoText(s, &fonts);
  CHECK(b.bounds.x + b.bounds.w == LogoSpec::kCanvasW);
  CHECK(b.bounds.y + b.bounds.h == LogoSpec::kCanvasH);
  s.tx = 500, s.ty = 500;
  b = layoutLogoText(s, &fonts);
  CHECK(std::abs((b.bounds.x + b.bounds.w / 2) - LogoSpec::kCanvasW / 2) <= 1);

  // A two-line paragraph: its short last line starts at the left, the middle or the right of the widest line.
  s.ts = 30;
  s.t = "WWWW WWWW WWWW WWWW WWWW WWWW WWWW WWWW WWWW WWWW";
  s.tx = 0;
  auto firstGlyphX = [&](int to, int line) {
    s.to = to;
    const TextBlock blk = layoutLogoText(s, &fonts);
    REQUIRE(blk.layout.lineCount() >= 2);
    for (const auto& g : blk.layout.glyphPositions())
      if (g.line == line) return g.x;
    return -1.0f;
  };
  auto lastLine = [&] {
    s.to = LogoSpec::kLeft;
    return layoutLogoText(s, &fonts).layout.lineCount() - 1;
  };
  const int last = lastLine();
  const float left = firstGlyphX(LogoSpec::kLeft, last), center = firstGlyphX(LogoSpec::kCenter, last),
              right = firstGlyphX(LogoSpec::kRight, last);
  CHECK(left < center);
  CHECK(center < right);
  CHECK(firstGlyphX(LogoSpec::kLeft, 0) == doctest::Approx(left));  // the first line is full, so it starts where the left one does
}

TEST_CASE("Logo renderer: background, then graphic in its slot with alpha, then text with a shadow") {
  using namespace game::choc2;
  gfx::ImageData red, green;
  red.width = 350, red.height = 200;
  red.rgba.assign(350 * 200 * 4, 0);
  green.width = 200, green.height = 200;
  green.rgba.assign(200 * 200 * 4, 0);
  for (std::size_t i = 0; i < red.rgba.size(); i += 4) red.rgba[i] = 255, red.rgba[i + 3] = 255;
  for (std::size_t i = 0; i < green.rgba.size(); i += 4) green.rgba[i + 1] = 255, green.rgba[i + 3] = 255;
  gfx::text::FontCache fonts(nullptr);
  LogoAssets assets;
  assets.image = [&](const std::string& n) -> const gfx::ImageData* { return n == "logo/bg1" ? &red : n == "logo/logo1" ? &green : nullptr; };
  assets.fonts = &fonts;

  LogoSpec s;
  s.b = "bg1";
  s.l = "logo/logo1";
  s.t = "";
  s.lx = 0;
  gfx::ImageData img = renderLogo(s, assets);
  REQUIRE(img.width == 350);
  REQUIRE(img.height == 200);
  CHECK(px(img, 10, 10) == std::vector<int>{0, 255, 0, 255});    // graphic in the left slot
  CHECK(px(img, 300, 10) == std::vector<int>{255, 0, 0, 255});   // red beyond it
  s.lx = 1000;
  img = renderLogo(s, assets);
  CHECK(px(img, 10, 10) == std::vector<int>{255, 0, 0, 255});
  CHECK(px(img, 340, 100) == std::vector<int>{0, 255, 0, 255});
  CHECK(px(img, 150, 100) == std::vector<int>{0, 255, 0, 255});  // the slot starts at 150
  CHECK(px(img, 149, 100) == std::vector<int>{255, 0, 0, 255});
  s.la = 500;  // half transparent over red
  img = renderLogo(s, assets);
  const auto mix = px(img, 340, 100);
  CHECK(std::abs(mix[0] - 128) <= 1);
  CHECK(std::abs(mix[1] - 128) <= 1);
  CHECK(mix[3] == 255);
  // A missing graphic leaves the background.
  s.l = "logo/missing";
  s.la = 1000;
  CHECK(px(renderLogo(s, assets), 340, 100) == std::vector<int>{255, 0, 0, 255});
  // A quarter turn of a wide picture covers a tall rectangle about its centre.
  gfx::ImageData bar;
  bar.width = 100, bar.height = 20;
  bar.rgba.assign(100 * 20 * 4, 255);
  assets.image = [&](const std::string& n) -> const gfx::ImageData* { return n == "logo/bg1" ? &red : n == "logo/bar" ? &bar : nullptr; };
  s.l = "logo/bar";
  s.lx = 500, s.ly = 500;
  s.lr = 1000;
  img = renderLogo(s, assets);
  CHECK(px(img, 175 + 40, 100)[1] == 255);  // flat: wide
  CHECK(px(img, 175, 100 + 30)[1] == 0);
  s.lr = 250;
  img = renderLogo(s, assets);
  CHECK(px(img, 175 + 40, 100)[1] == 0);    // turned: tall
  CHECK(px(img, 175, 100 + 30)[1] == 255);

  // The text: white fill, a black shadow one pixel down and right, and the text alpha fades both.
  s.l = "logo/missing";
  s.t = "H";
  s.ts = 40;
  s.tfo = 0;
  s.tch = 0, s.tcs = 0, s.tcv = 1000;
  img = renderLogo(s, assets);
  int whites = 0, shadows = 0, uncovered = 0;
  for (int y = 0; y < 199; ++y)
    for (int x = 0; x < 349; ++x) {
      const auto p = px(img, x, y);
      if (p[1] > 200 && p[2] > 200) {
        ++whites;
        const auto d = px(img, x + 1, y + 1);
        if (d[1] > 200 && d[2] > 200) continue;  // still fill
        if (d[0] < 255) ++shadows;               // red darkened by the shadow
        else ++uncovered;
      }
    }
  CHECK(whites > 20);
  CHECK(shadows > 0);
  CHECK(uncovered == 0);  // every fill pixel has the shadow (or more fill) at +1,+1
  s.ta = 0;
  img = renderLogo(s, assets);
  for (int y = 0; y < 200; ++y)
    for (int x = 0; x < 350; ++x) REQUIRE(px(img, x, y) == std::vector<int>{255, 0, 0, 255});
}

TEST_CASE("Logo renderer: bold, italic and outline change the pixels") {
  using namespace game::choc2;
  gfx::ImageData red;
  red.width = 350, red.height = 200;
  red.rgba.assign(350 * 200 * 4, 0);
  for (std::size_t i = 0; i < red.rgba.size(); i += 4) red.rgba[i] = 255, red.rgba[i + 3] = 255;
  gfx::text::FontCache fonts(nullptr);
  LogoAssets assets;
  assets.image = [&](const std::string& n) -> const gfx::ImageData* { return n == "logo/bg1" ? &red : nullptr; };
  assets.fonts = &fonts;
  LogoSpec s;
  s.b = "bg1";
  s.t = "Hi";
  s.ts = 40;
  s.tfo = 0;
  const auto plain = renderLogo(s, assets).rgba;
  s.tfb = 1;
  const auto bold = renderLogo(s, assets).rgba;
  s.tfb = 0, s.tfi = 1;
  const auto italic = renderLogo(s, assets).rgba;
  s.tfi = 0, s.tfo = 1;
  const auto outline = renderLogo(s, assets).rgba;
  CHECK(plain != bold);
  CHECK(plain != italic);
  CHECK(plain != outline);
  CHECK(renderLogo(s, assets).rgba == outline);  // deterministic
}

// ---------------------------------------------------------------- the texture made in memory

namespace {
struct CountingUploader : gfx::TextureUploader {
  int uploads = 0, updates = 0, releases = 0;
  std::vector<std::uint8_t> last;
  void* upload(const std::uint8_t* rgba, int w, int h) override {
    ++uploads;
    last.assign(rgba, rgba + w * h * 4);
    return new int(uploads);
  }
  bool update(void*, int, int, int w, int h, const std::uint8_t* rgba, int) override {
    ++updates;
    last.assign(rgba, rgba + w * h * 4);
    return true;
  }
  void release(void* h) override {
    ++releases;
    delete static_cast<int*>(h);
  }
};
}  // namespace

TEST_CASE("TextureCache::makeFromPixels: updates in place, re-uploads after a device reset, trims when unused") {
  const fs::path dir = fs::temp_directory_path() / "choco_test_choc2_logo_tex";
  fs::remove_all(dir);
  fs::create_directories(dir);
  assets::AssetFS afs(u8path(dir), "");
  CountingUploader up;
  {
    gfx::TextureCache cache(afs, &up);
    gfx::ImageData a;
    a.width = 4, a.height = 2;
    a.rgba.assign(4 * 2 * 4, 7);
    auto t = cache.makeFromPixels("$logo", a);
    REQUIRE(t);
    CHECK(t->width() == 4);
    CHECK(up.uploads == 1);
    a.rgba.assign(4 * 2 * 4, 9);
    auto again = cache.makeFromPixels("$logo", a);
    CHECK(again == t);  // the same Texture object, patched
    CHECK(up.updates == 1);
    CHECK(up.uploads == 1);
    // A device reset sends the latest pixels again.
    up.last.clear();
    cache.reuploadAll();
    CHECK(up.uploads == 2);
    REQUIRE(up.last.size() == 32);
    CHECK(up.last[0] == 9);
    // A different size replaces the image.
    gfx::ImageData big;
    big.width = 8, big.height = 8;
    big.rgba.assign(8 * 8 * 4, 1);
    CHECK(cache.makeFromPixels("$logo", big) == t);
    CHECK(t->width() == 8);
    t.reset();
    again.reset();
    cache.trim();
    CHECK(cache.size() == 0);
  }
  CHECK(up.releases == up.uploads);  // nothing leaked
}

// ---------------------------------------------------------------- the editor and its natives

TEST_CASE("LogoEditor: opens on the table, syncs the sliders and draws the preview texture") {
  LogoEnv e;
  REQUIRE(e.booted);
  e.open("{b='bg2', l='item/tea_big', t='Acme', tx=250, ts=55, ta=500, tch=100, ls=300, lr=750, lf=4, tfo=1}");
  LogoEditor* ed = e.editor();
  REQUIRE(ed);
  CHECK_FALSE(e.log.contains("[unimplemented-type] LogoEditor"));
  CHECK(ed->spec().b == "bg2");
  CHECK(ed->spec().t == "Acme");
  CHECK(ed->graphics().size() == 13);
  CHECK(ed->previewX() == 5);
  CHECK(ed->previewY() == 85);
  REQUIRE(ed->preview());
  CHECK(ed->preview()->width() == 350);
  CHECK(ed->preview()->height() == 200);
  // The Lua started every slider at 0; the editor moves them to the table.
  CHECK(e.slider("background")->value() == doctest::Approx(1.0f / 7.0f));
  CHECK(e.slider("graphic")->value() == doctest::Approx(12.0f / 12.0f));  // the 13th of 13
  CHECK(e.slider("font")->value() == doctest::Approx(4.0f / 8.0f));
  CHECK(e.slider("textsize")->value() == doctest::Approx(0.5f));
  CHECK(e.slider("textalpha")->value() == doctest::Approx(0.5f));
  CHECK(e.slider("tColorH")->value() == doctest::Approx(0.1f));
  CHECK(e.slider("textx")->value() == doctest::Approx(0.25f));
  CHECK(e.slider("logoscale")->value() == doctest::Approx(0.3f));
  CHECK(e.slider("logorotate")->value() == doctest::Approx(0.75f));
  // The spec comes back out unchanged.
  CHECK(e.spec() == ed->spec());
}

TEST_CASE("LogoEditor: slider messages edit the table") {
  LogoEnv e;
  REQUIRE(e.booted);
  e.open();
  LogoEditor* ed = e.editor();
  REQUIRE(ed);
  e.slider("background")->setValue(1.0f, false);
  CHECK(ed->spec().b == "bg8");
  e.slider("graphic")->setValue(0.0f, false);
  CHECK(ed->spec().l == "logo/logo1");
  e.slider("graphic")->setValue(1.0f, false);
  CHECK(ed->spec().l == "item/tea_big");
  e.slider("font")->setValue(1.0f, false);
  CHECK(ed->spec().lf == 8);
  e.slider("textsize")->setValue(0.0f, false);
  CHECK(ed->spec().ts == 10);
  e.slider("textsize")->setValue(1.0f, false);
  CHECK(ed->spec().ts == 100);
  e.slider("textalpha")->setValue(0.25f, false);
  CHECK(ed->spec().ta == 250);
  e.slider("tColorH")->setValue(0.123f, false);
  e.slider("tColorS")->setValue(0.5f, false);
  e.slider("tColorV")->setValue(0.9f, false);
  CHECK(ed->spec().tch == 123);
  CHECK(ed->spec().tcs == 500);
  CHECK(ed->spec().tcv == 900);
  e.slider("textx")->setValue(0.1f, false);
  e.slider("texty")->setValue(0.2f, false);
  CHECK(ed->spec().tx == 100);
  CHECK(ed->spec().ty == 200);
  e.slider("logoscale")->setValue(0.4f, false);
  e.slider("logoalpha")->setValue(0.6f, false);
  e.slider("logorotate")->setValue(0.8f, false);
  e.slider("logox")->setValue(0.3f, false);
  e.slider("logoy")->setValue(0.7f, false);
  CHECK(ed->spec().ls == 400);
  CHECK(ed->spec().la == 600);
  CHECK(ed->spec().lr == 800);
  CHECK(ed->spec().lx == 300);
  CHECK(ed->spec().ly == 700);
  // A message from a slider the editor does not know is left alone.
  const LogoSpec before = ed->spec();
  ui::Message other{ui::kSliderValChanged, "somethingelse", ed->id()};
  CHECK_FALSE(ed->onMessage(other));
  CHECK(ed->spec() == before);
}

TEST_CASE("LogoEditor natives: each edits the working copy and GetLogoString returns it") {
  LogoEnv e;
  REQUIRE(e.booted);
  e.open("{t='Old'}");
  REQUIRE(e.run("SetLogoText('New \"Name\"')") == 0);
  CHECK(e.spec().t == "New \"Name\"");
  REQUIRE(e.run("SetTextPosition(0, 0.5); SetGraphicPosition(1, 0.25)") == 0);
  LogoSpec s = e.spec();
  CHECK(s.tx == 0);
  CHECK(s.ty == 500);
  CHECK(s.lx == 1000);
  CHECK(s.ly == 250);
  CHECK(e.slider("textx")->value() == doctest::Approx(0.0f));  // the sliders follow
  CHECK(e.slider("logox")->value() == doctest::Approx(1.0f));
  REQUIRE(e.run("SetTextColor(209, 27, 27, 255)") == 0);
  s = e.spec();
  CHECK(s.tch == 0);
  CHECK(s.tcs == 871);
  CHECK(s.tcv == 820);
  REQUIRE(e.run("SetTextColor(255, 255, 255, 255)") == 0);
  s = e.spec();
  CHECK(s.tcs == 0);
  CHECK(s.tcv == 1000);
  // Toggles flip the flag and the button.
  auto on = [&](const char* name) { return dynamic_cast<ui::ButtonWindow*>(e.wm.topModal()->find(name))->on(); };
  REQUIRE(e.run("ToggleOutline()") == 0);
  CHECK(e.spec().tfo == 0);
  CHECK_FALSE(on("outline"));
  REQUIRE(e.run("ToggleOutline(); ToggleItalic(); ToggleBold()") == 0);
  s = e.spec();
  CHECK(s.tfo == 1);
  CHECK(s.tfi == 1);
  CHECK(s.tfb == 1);
  CHECK(on("outline"));
  CHECK(on("italic"));
  CHECK(on("bold"));
  REQUIRE(e.run("ToggleItalic()") == 0);
  CHECK(e.spec().tfi == 0);
  // A real press: the button flips itself after the command, and the editor leaves it in step with the flag.
  auto press = [&](const char* name) {
    dynamic_cast<ui::ButtonWindow*>(e.wm.topModal()->find(name))->press();
    e.wm.dispatchMessages();
  };
  press("bold");
  CHECK(e.spec().tfb == 0);
  CHECK_FALSE(on("bold"));
  press("bold");
  CHECK(e.spec().tfb == 1);
  CHECK(on("bold"));
  press("outline");
  CHECK(e.spec().tfo == 0);
  CHECK_FALSE(on("outline"));
  // Justification.
  REQUIRE(e.run("SetLeftJustify()") == 0);
  CHECK(e.spec().to == 4);
  REQUIRE(e.run("SetRightJustify()") == 0);
  CHECK(e.spec().to == 6);
  REQUIRE(e.run("SetCenterJustify()") == 0);
  CHECK(e.spec().to == 5);
  // The preview was redrawn along the way.
  CHECK(e.editor()->preview());
}

TEST_CASE("RandomizeLogo: background, stock graphic and font come from the game RNG, the rest is untouched") {
  LogoEnv e;
  REQUIRE(e.booted);
  e.open("{t='Keep', tx=123, ts=33}");
  std::set<std::string> bgs, graphics;
  std::set<int> fonts;
  for (int i = 0; i < 80; ++i) {
    REQUIRE(e.run("RandomizeLogo()") == 0);
    const LogoSpec s = e.spec();
    bgs.insert(s.b);
    graphics.insert(s.l);
    fonts.insert(s.lf);
    CHECK(s.t == "Keep");
    CHECK(s.tx == 123);
    CHECK(s.ts == 33);
    CHECK(s.l.rfind("logo/logo", 0) == 0);  // never an item picture
  }
  CHECK(bgs.size() == 8);
  CHECK(graphics.size() == 12);
  CHECK(fonts.size() == 9);
  // The sliders follow, and the same seed gives the same picks.
  const LogoSpec last = e.editor()->spec();
  CHECK(e.slider("font")->value() == doctest::Approx(static_cast<float>(last.lf) / 8.0f));
  LogoEnv f;
  REQUIRE(f.booted);
  f.open("{t='Keep'}");
  LogoEnv g;
  REQUIRE(g.booted);
  g.open("{t='Keep'}");
  REQUIRE(f.run("RandomizeLogo(); RandomizeLogo()") == 0);
  REQUIRE(g.run("RandomizeLogo(); RandomizeLogo()") == 0);
  CHECK(f.editor()->spec() == g.editor()->spec());
}

TEST_CASE("Logo natives with no editor open log once and GetLogoString returns nil") {
  LogoEnv e;
  REQUIRE(e.booted);
  REQUIRE(e.run("PushModal('x'); r_ = GetLogoString(); SetLogoText('a'); SetLogoText('b'); ToggleBold()") == 0);
  CHECK(e.isNil("r_"));
  CHECK(e.log.count("[native-warning] SetLogoText") == 1);
  CHECK(e.log.count("[native-warning] ToggleBold") == 1);
}

// ---------------------------------------------------------------- the sign

TEST_CASE("fromAddSprite: .LOGO is a building carrying the player's logo and its scale") {
  test::ScriptEnv e(CHOCO_TEST_ASSETS);
  lua_State* L = e.lua.L();
  const std::string code = "return {x=195, y=234, image='.LOGO', logo={t='Acme', b='bg2'}, scale=0.35, contents='PlayerLogoRollover()', command=function() end}";
  REQUIRE(luaL_loadbuffer(L, code.data(), code.size(), "=t") == 0);
  REQUIRE(lua_pcall(L, 0, 1, 0) == 0);
  game::choc2::AddedSprite a;
  {
    const script::LuaStyle view = script::LuaStyle::plainView(L, lua_gettop(L), &e.lua);
    a = game::choc2::fromAddSprite(view, 5, [](const std::string&) { return 0; });
  }
  lua_settop(L, 0);
  REQUIRE(a.kind == game::choc2::AddedSprite::Kind::Building);
  CHECK(a.isLogo);
  CHECK(a.logo.t == "Acme");
  CHECK(a.logo.b == "bg2");
  CHECK(a.building.scale == doctest::Approx(0.35f));
  CHECK(a.building.image == ".LOGO");
  CHECK(a.building.x == 195);
  CHECK(a.building.layer == 5);
  // Without a logo table there is no sign.
  REQUIRE(luaL_loadbuffer(L, "return {x=1, y=2, image='.LOGO', command=function() end}", 56, "=t") == 0);
  REQUIRE(lua_pcall(L, 0, 1, 0) == 0);
  {
    const script::LuaStyle view = script::LuaStyle::plainView(L, lua_gettop(L), &e.lua);
    a = game::choc2::fromAddSprite(view, 0, [](const std::string&) { return 0; });
  }
  CHECK(a.kind == game::choc2::AddedSprite::Kind::Skipped);
  CHECK_FALSE(a.notes.empty());
}

TEST_CASE("Choc2 Port: a .LOGO building draws the logo texture scaled about its centre, and a click runs its command") {
  LogoEnv e;
  REQUIRE(e.booted);
  REQUIRE(e.run(
      "gClicks = 0; PushModal('t');"
      "Port{ x=0, y=0, w=800, h=600, xOffset=0, yOffset=0,"
      "  Group({"
      "    function() bsgAddSprite{image='.LOGO', logo={t='Acme', b='bg1', l='logo/logo1', tx=0, ty=0}, x=400, y=300, scale=0.5,"
      "      contents=\"Window{name='pophold', w=40, h=18}()\", command=function() gClicks = gClicks + 1 end} end,"
      "  }) }()") == 0);
  REQUIRE(e.lastError.empty());
  game::choc2::Port* port = nullptr;
  for (const auto& c : e.wm.topModal()->children())
    if (auto* p = dynamic_cast<game::choc2::Port*>(c.get())) port = p;
  REQUIRE(port);
  REQUIRE(port->buildings().size() == 1);
  const auto& b = port->buildings()[0];
  CHECK(b.name == ".LOGO");
  REQUIRE(b.sprite);
  REQUIRE(b.sprite->texture);
  CHECK(b.sprite->texture->width() == 350);
  CHECK(b.sprite->texture->height() == 200);
  CHECK(b.sprite->scale == doctest::Approx(0.5f));
  // The rectangle is the canvas times scale, centred on x,y: 175 x 100 about (400, 300).
  const gfx::RectF r = b.sprite->rect();
  CHECK(r.x == doctest::Approx(312.5f));
  CHECK(r.y == doctest::Approx(250.0f));
  CHECK(r.w == doctest::Approx(175.0f));
  CHECK(r.h == doctest::Approx(100.0f));
  CHECK(port->findBuilding({400, 300}) == 0);
  CHECK(port->findBuilding({300, 300}) == -1);
  // Hover and click go through the usual building routes.
  auto pointer = [&](ui::InputEvent::Type type, float x, float y) {
    ui::InputEvent ev;
    ev.type = type;
    ev.x = x;
    ev.y = y;
    e.wm.handleInput(ev);
  };
  using T = ui::InputEvent::Type;
  pointer(T::MouseMove, 400, 300);
  pointer(T::MouseDown, 400, 300);
  pointer(T::MouseUp, 400, 300);
  e.host.tick(10);
  e.host.tick(20);
  CHECK(e.getNumber("gClicks") == 1);
}
