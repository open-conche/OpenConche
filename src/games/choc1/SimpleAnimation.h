#pragma once

// Chocolatier 1's SimpleAnimation window (TSimpleAnimation, exe vtable
// 004e6484; docs/re/choc1-natives.md section 5): a looping frame sequence from
// a sprite-sheet XML (`anim`), or one still (`image`), drawn scaled around the
// point (animx, animy) of the window. The main menu uses
// image/buttonhighlight.xml twice (fps 12; and scale 0.85 at fps 5).
//
// Keys: image anim scale(1) animx(0) animy(0) xtile(8000) ytile(6000) flip path
// fps(10) random (docs/re/choc1-port-view.md 2). Drawn: image, anim, scale,
// animx, animy, fps, path, random, xtile, ytile. `path` is {time=ms(1000),
// loop=bool(false), {x,y}, {x,y}, ...}: a polyline walked at constant speed,
// its position an offset added to (animx, animy). `random` backdates the start
// by rand() % (path time, or 20000). The frame is (elapsed / (1000 / fps)) %
// frameCount with an integer-ms period. The sprite repeats every `xtile` while
// x < frameW/2 + 800 and every `ytile` while y < frameH/2 + 600, unclipped.
// `flip` (default TRUE, only the clouds set false) mirrors the sprite while the
// path heading is outside [0, pi), i.e. while it moves right (art faces left);
// no path: never. Mirrored about the registration point. A still (`image`) is
// drawn centred on the point, not from its corner.

#include <memory>
#include <string>
#include <vector>

#include "gfx/SpriteSheet.h"
#include "gfx/Texture.h"
#include "script/ScriptContext.h"
#include "ui/Window.h"

namespace choco::game::choc1 {

// LinearPath (exe 004195a0): the position at an elapsed time along a polyline
// at constant speed.
class PolylinePath {
 public:
  struct Point {
    double x = 0, y = 0;
  };
  PolylinePath() = default;
  PolylinePath(int timeMs, bool loop, std::vector<Point> points);
  // Loop: t modulo time; then t >= time is the last point. Empty path: (0, 0).
  Point at(long long elapsedMs) const;
  // The heading the exe derives for the segment under the time (the last
  // segment once stopped at the end): acos(nx) of the normalised direction,
  // negated when it moves right (nx > 0). 0 for fewer than two points or a
  // zero-length segment.
  double heading(long long elapsedMs) const;
  int timeMs() const { return time_; }
  bool loop() const { return loop_; }
  const std::vector<Point>& points() const { return points_; }

 private:
  int time_ = 1000;
  bool loop_ = false;
  std::vector<Point> points_;
  double total_ = 0;
};

class SimpleAnimation : public ui::Window {
 public:
  SimpleAnimation(ui::WindowManager& wm, script::ScriptContext& ctx) : Window(wm), ctx_(ctx) {}
  std::string_view typeName() const override { return "SimpleAnimation"; }
  void init(const ui::Style& style) override;
  // The sprite is not clipped to the window's rectangle (the exe has no clip:
  // the paths run far outside it), only to the ancestors'.
  bool drawsOutside() const override { return true; }
  void drawOutside(ui::DrawContext& ctx) const override;
  std::string detail() const override;

  // The frame shown `elapsedMs` after the start: (elapsed / (1000 / fps)) %
  // frameCount, the period in whole ms. The mirror rule of the draw: flip on, a
  // path, and the heading angle outside [0, pi). Where a still is drawn for the
  // point (px, py): centred on it, the scale about the centre.
  static gfx::RectF stillRect(float px, float py, int w, int h, float scale);
  static bool mirrored(bool flip, bool hasPath, double heading);
  static int frameAt(double elapsedMs, int fps, int frameCount);
  // `random`: the start moves back by rand() % T; T is the path time, or 20000
  // without a path (r is the raw random value).
  static long long randomBackdate(int pathTimeMs, bool hasPath, unsigned r) { return r % static_cast<unsigned>(hasPath ? pathTimeMs : 20000); }
  // Draw positions along one axis: start, start + tile, ... while < start-limit
  // (the first copy is always drawn).
  static std::vector<float> tileOffsets(float start, float tile, float limit);
  const PolylinePath* path() const { return hasPath_ ? &path_ : nullptr; }
  long long randomOffsetMs() const { return randomMs_; }

 private:
  script::ScriptContext& ctx_;
  std::shared_ptr<const gfx::SpriteSheet> sheet_;
  std::shared_ptr<gfx::Texture> still_;
  std::string source_;
  double scale_ = 1;
  float animX_ = 0, animY_ = 0;
  int fps_ = 10;
  bool flip_ = true;
  float xtile_ = 8000, ytile_ = 6000;
  PolylinePath path_;
  bool hasPath_ = false;
  long long randomMs_ = 0;     // `random`: how far into its cycle the animation starts
  mutable float lastTop_ = 0;  // a still: the top edge of its first tile (window
                               // coordinates), for the dump
  mutable bool lastMirror_ = false;
  mutable float lastX_ = 0, lastY_ = 0;  // the last drawn position (animx/animy
                                         // plus the path offset), for the dump
  mutable bool started_ = false;
  mutable double startMs_ = 0;  // the window's clock at the first draw, less randomMs_
};

}  // namespace choco::game::choc1
