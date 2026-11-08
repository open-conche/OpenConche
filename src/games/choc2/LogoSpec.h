#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "ui/Style.h"

namespace choco::game::choc2 {

// The Chocolatier 2 company logo: the table `gSim.logo` (docs/re/choc2-logo-and-trade.md 1.3). A pure value: no window, no
// Lua state, no assets. Numbers are thousandths (0..1000) except `ts`, the text size in pixels (10..100).
//
// INFERRED (a recorded decision): `lf` is the font index (the Lua gives it 0 and a `font` slider that starts at 0); `to` is
// the text flags (Left 4, Center 5, Right 6: kHAlign + kVAlignCenter); `lr` is the turns of rotation, 1000 being a full turn
// (no rotation, the default).
struct LogoSpec {
  static constexpr int kCanvasW = 350, kCanvasH = 200;
  static constexpr int kTextMin = 10, kTextMax = 100;
  static constexpr int kLeft = 4, kCenter = 5, kRight = 6;

  std::string b = "bg4";          // background: logo/<b>
  std::string l = "logo/logo10";  // graphic, with its directory
  int lx = 500, ly = 500;         // graphic position, p in 0..1000 over the free space
  int la = 1000, ls = 1000;       // graphic alpha and scale
  int lf = 0;                     // font index (INFERRED)
  int lr = 1000;                  // graphic rotation
  std::string t;                  // text
  int to = kCenter;               // text flags
  int ts = 40;                    // text size
  int ta = 1000;                  // text alpha
  int tch = 500, tcs = 500, tcv = 500;  // text colour, HSV
  int tx = 500, ty = 500;         // text position
  int tfo = 1, tfi = 0, tfb = 0;  // outline, italic, bold

  bool operator==(const LogoSpec&) const = default;

  // Reads a logo table; absent keys keep the new-game defaults (sim/sim.lua:125-129), numbers are rounded and clamped to their
  // range, and a graphic without a directory gets `logo/` (the loader's old-save fix, sim.lua:304-306). `name` is the text
  // when the table has none.
  static LogoSpec fromStyle(const ui::Style& table, std::string_view name = {});
  // The new-game table for a player called `name`.
  static LogoSpec defaults(std::string_view name) {
    LogoSpec s;
    s.t = std::string(name);
    return s;
  }

  // A Lua chunk that returns this table: `return {b="bg4",...}`. Integers as integers, strings escaped.
  std::string toLuaString() const;

  // Clamps every number to its range.
  void clamp();
};

// The nine fonts of fonts/*.mvec in directory order (INFERRED: the `font` slider spans them in this order).
const std::vector<std::string>& logoFontNames();
std::string logoBackgroundPath(const std::string& b);  // "logo/" + b unless b already has a directory

constexpr int kLogoBackgrounds = 8;
constexpr int kLogoStockGraphics = 12;

// The graphic list the `graphic` slider spans: logo/logo1..12, then item/<name> for each allowed item (`<name>_big`).
std::vector<std::string> logoGraphicList(const std::vector<std::string>& itemsAllowed);

// A 0..1 slider value for index `i` of `n` and back: i / (n - 1), and round(v * (n - 1)) (INFERRED: the original's mapping is
// not readable). n <= 1 gives 0.
float sliderForIndex(int i, int n);
int indexForSlider(float v, int n);

// Colour: thousandths of hue, saturation, value <-> 8-bit RGB. Hue 1000 is the same as 0 (a full turn).
struct Rgb8 {
  int r = 0, g = 0, b = 0;
  bool operator==(const Rgb8&) const = default;
};
Rgb8 hsvToRgb(int h, int s, int v);
void rgbToHsv(int r, int g, int b, int& h, int& s, int& v);

}  // namespace choco::game::choc2
