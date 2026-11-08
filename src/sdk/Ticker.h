#pragma once

namespace choco::sdk {

// A fixed-interval task as the original's TAnimTask runs it: when it is due it fires once and re-arms from `now`, so a
// late frame is not made up for. At most one tick per due() call. Pure arithmetic, so ui and game windows both use it.
class Ticker {
 public:
  void start(double now, double ms) {
    ms_ = ms;
    next_ = now + ms;
  }
  void pause() { next_ = -1; }
  bool running() const { return next_ >= 0; }
  double interval() const { return ms_; }
  // The epsilon absorbs the rounding of `frame * 1000 / 60` clocks.
  bool due(double now) {
    if (next_ < 0 || now + 1e-6 < next_) return false;
    next_ = now + ms_;
    return true;
  }

 private:
  double next_ = -1;
  double ms_ = 0;
};

// The original's TAnimTask with catch-up (docs/re/chocolate-factory.md 2.1): the next due time advances by exactly `ms`
// per step, not from `now`, so a late frame runs several steps and the step count depends on the clock only. More than a
// second behind, it resyncs to `now` instead of running them all. Call due() in a loop until it returns false.
class FixedStep {
 public:
  void start(double now, double ms) {
    ms_ = ms;
    next_ = now + ms;
  }
  void stop() { next_ = -1; }
  bool running() const { return next_ >= 0; }
  double interval() const { return ms_; }
  bool due(double now) {
    if (next_ < 0) return false;
    if (now - next_ > 1000) next_ = now;
    if (now + 1e-6 < next_) return false;
    next_ += ms_;
    return true;
  }

 private:
  double next_ = -1;
  double ms_ = 0;
};

}  // namespace choco::sdk
