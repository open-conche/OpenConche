#include <doctest/doctest.h>

#include <cmath>
#include <vector>

#include "sdk/Ticker.h"
#include "gfx/Curve.h"
#include "ui/WindowManager.h"

using namespace choco;
using gfx::Curve;
using gfx::PointF;

namespace {

float dist(PointF a, PointF b) { return std::hypot(a.x - b.x, a.y - b.y); }

PointF bezier(PointF p0, PointF p1, PointF p2, PointF p3, float t) {
  const float u = 1 - t;
  return {u * u * u * p0.x + 3 * u * u * t * p1.x + 3 * u * t * t * p2.x + t * t * t * p3.x,
          u * u * u * p0.y + 3 * u * u * t * p1.y + 3 * u * t * t * p2.y + t * t * t * p3.y};
}

}  // namespace

TEST_CASE("Curve: a doubled-end-point straight line is one piece of the chord's length") {
  const Curve c = Curve::fromPoints({{10, 20}, {10, 20}, {110, 20}, {110, 20}});
  CHECK(c.length() == doctest::Approx(100.0f));
  PointF tangent{7, 7};
  const PointF mid = c.at(0.5f, &tangent);
  CHECK(mid.x == doctest::Approx(60.0f));
  CHECK(mid.y == doctest::Approx(20.0f));
  CHECK(tangent.x == doctest::Approx(1.0f));
  CHECK(tangent.y == doctest::Approx(0.0f));
  // Uniform in arc length.
  CHECK(c.at(0.25f).x == doctest::Approx(35.0f));
  CHECK(c.at(0.9f).x == doctest::Approx(100.0f));
}

TEST_CASE("Curve: a curved Bezier's length is within 0.5% of a dense sampling") {
  const PointF p0{0, 0}, p1{0, 300}, p2{400, 300}, p3{400, 0};
  const Curve c = Curve::fromPoints({p0, p1, p2, p3});
  float dense = 0;
  PointF prev = p0;
  for (int i = 1; i <= 20000; ++i) {
    const PointF p = bezier(p0, p1, p2, p3, static_cast<float>(i) / 20000);
    dense += dist(prev, p);
    prev = p;
  }
  CHECK(c.length() == doctest::Approx(dense).epsilon(0.005));
  CHECK(c.length() >= dense);  // pieces are measured along the control polygon, which is never shorter than the arc
  // The sampled point stays near the true curve.
  PointF mid = c.at(0.5f);
  CHECK(mid.x == doctest::Approx(200.0f).epsilon(0.01));
  CHECK(mid.y == doctest::Approx(225.0f).epsilon(0.02));
}

TEST_CASE("Curve: end points, the tangent and the 3n+1 rule") {
  PointF tangent{5, 6};
  const Curve one = Curve::fromPoints({{0, 0}, {0, 0}, {30, 40}, {30, 40}});
  CHECK(one.at(0.0f, &tangent) == PointF{0, 0});
  CHECK(tangent == PointF{5, 6});  // untouched at t <= 0
  CHECK(one.at(-3.0f, &tangent) == PointF{0, 0});
  CHECK(one.at(1.0f, &tangent) == PointF{30, 40});
  CHECK(one.at(2.0f, &tangent) == PointF{30, 40});
  CHECK(tangent == PointF{5, 6});  // and at t >= 1
  one.at(0.5f, &tangent);
  CHECK(tangent.x == doctest::Approx(0.6f));
  CHECK(tangent.y == doctest::Approx(0.8f));

  // Seven points: two segments, the end is index 6. Ten points: index 9.
  const Curve seven = Curve::fromPoints({{0, 0}, {0, 0}, {10, 0}, {10, 0}, {10, 0}, {10, 10}, {10, 10}});
  CHECK(seven.at(1.0f) == PointF{10, 10});
  CHECK(seven.length() == doctest::Approx(20.0f));
  CHECK(seven.at(0.75f).y == doctest::Approx(5.0f));
  std::vector<PointF> ten(10);
  for (int i = 0; i < 10; ++i) ten[static_cast<std::size_t>(i)] = {static_cast<float>(i), 0};
  CHECK(Curve::fromPoints(ten).at(1.0f).x == 9.0f);
  // An extra trailing point is not part of a full segment: the end is still index 6.
  std::vector<PointF> eight(8);
  for (int i = 0; i < 8; ++i) eight[static_cast<std::size_t>(i)] = {static_cast<float>(i), 0};
  CHECK(Curve::fromPoints(eight).at(1.0f).x == 6.0f);
}

TEST_CASE("Curve: too few points and the empty curve") {
  CHECK(Curve().length() == 0.0f);
  CHECK(Curve().at(0.5f) == PointF{});
  Curve c;
  c.addControlPoint(4, 5);
  c.addControlPoint(6, 7);
  CHECK(c.length() == 0.0f);
  CHECK(c.at(0.5f) == PointF{4, 5});
  CHECK(c.at(1.0f) == PointF{4, 5});
  c.addControlPoint(8, 9);
  c.addControlPoint(10, 11);  // now a segment: the cache must not be stale
  CHECK(c.length() > 0.0f);
  CHECK(c.at(1.0f) == PointF{10, 11});
}

TEST_CASE("Ticker: due at next, re-armed from now, one tick per call, paused never fires") {
  sdk::Ticker t;
  CHECK_FALSE(t.due(0));  // never started
  t.start(100, 50);
  CHECK_FALSE(t.due(149));
  CHECK(t.due(150));
  CHECK_FALSE(t.due(150));  // one tick per call, and re-armed for 200
  CHECK_FALSE(t.due(199));
  // A late frame ticks once and the next is 50 after *now*: no catch-up.
  CHECK(t.due(500));
  CHECK_FALSE(t.due(501));
  CHECK_FALSE(t.due(549));
  CHECK(t.due(550));
  t.pause();
  CHECK_FALSE(t.due(10000));
  CHECK_FALSE(t.running());
  t.start(10000, 30);
  CHECK(t.due(10030));
  // The epsilon absorbs frame * 1000/60 rounding.
  sdk::Ticker f;
  f.start(0, 50);
  CHECK(f.due(3 * (1000.0 / 60.0)));
}

namespace {

struct Counter : ui::Window {
  explicit Counter(ui::WindowManager& wm, int stopAfter = -1) : Window(wm), stopAfter(stopAfter) {}
  bool onAnimate() override {
    ++ticks;
    return stopAfter < 0 || ticks < stopAfter;
  }
  int ticks = 0;
  int stopAfter;
};

}  // namespace

TEST_CASE("Modal clock pauses under a pushed modal and the gap is discarded on pop") {
  ui::ModalClock c;
  CHECK(c.read(100) == 100);
  c.pause(100);
  CHECK(c.read(5000) == 100);
  CHECK(c.paused());
  c.start(1100);
  CHECK(c.read(1200) == 200);
  c.start(1300);  // starting a running clock changes nothing
  CHECK(c.read(1300) == 300);
}

TEST_CASE("Window animations: due on the modal clock, frozen while a dialog covers them, end on false or a dead window") {
  ui::WindowManager wm;
  wm.pushModal("a");
  auto up = std::make_unique<Counter>(wm);
  Counter* w = up.get();
  const ui::WindowId id = w->id();
  wm.topModal()->adoptFront(std::move(up));

  wm.setNowMs(1000);
  wm.startWindowAnimation(id, 30);
  CHECK(wm.windowAnimationCount() == 1);
  wm.tickWindowAnimations(1029);
  CHECK(w->ticks == 0);
  wm.tickWindowAnimations(1030);
  CHECK(w->ticks == 1);
  wm.tickWindowAnimations(1031);  // re-armed from now: at most one tick per call and no catch-up
  CHECK(w->ticks == 1);
  wm.tickWindowAnimations(2000);
  CHECK(w->ticks == 2);

  // A dialog covers the modal: its clock stops, so nothing fires for the whole gap.
  wm.pushModal("dialog");
  wm.tickWindowAnimations(2010);
  wm.tickWindowAnimations(5000);
  CHECK(w->ticks == 2);
  wm.setNowMs(5000);
  wm.popModal("dialog");
  wm.tickWindowAnimations(5010);
  // The modal's clock read 2000 when the dialog opened and the next tick was due at 2030 on that clock, so 30 ms more of
  // its own time must pass after the pop: platform 5030.
  CHECK(w->ticks == 2);
  wm.tickWindowAnimations(5030);
  CHECK(w->ticks == 3);
  wm.clearDestroyed();

  // onAnimate returning false ends the entry.
  auto stopper = std::make_unique<Counter>(wm, 2);
  Counter* s = stopper.get();
  wm.topModal()->adoptFront(std::move(stopper));
  wm.startWindowAnimation(s->id(), 10);
  wm.tickWindowAnimations(5040);
  CHECK(s->ticks == 1);
  wm.tickWindowAnimations(5050);
  CHECK(s->ticks == 2);
  CHECK(wm.windowAnimationCount() == 1);  // the first window's animation is still there
  wm.tickWindowAnimations(5100);
  CHECK(s->ticks == 2);

  // A destroyed window drops its entry; starting for an unknown id does nothing.
  wm.destroyWindow(*w);
  wm.tickWindowAnimations(6000);
  CHECK(wm.windowAnimationCount() == 0);
  wm.startWindowAnimation(9999, 10);
  CHECK(wm.windowAnimationCount() == 0);
  wm.clearDestroyed();
}

TEST_CASE("Window animations: restarting resets the interval, even from inside onAnimate") {
  ui::WindowManager wm;
  wm.pushModal("a");
  struct Restarter : ui::Window {
    explicit Restarter(ui::WindowManager& m) : Window(m) {}
    bool onAnimate() override {
      ++ticks;
      manager().startWindowAnimation(id(), 100);
      return true;
    }
    int ticks = 0;
  };
  auto up = std::make_unique<Restarter>(wm);
  Restarter* r = up.get();
  wm.topModal()->adoptFront(std::move(up));
  wm.setNowMs(0);
  wm.startWindowAnimation(r->id(), 10);
  wm.tickWindowAnimations(10);
  CHECK(r->ticks == 1);
  wm.tickWindowAnimations(60);
  CHECK(r->ticks == 1);  // now 100 ms apart
  wm.tickWindowAnimations(110);
  CHECK(r->ticks == 2);
}
