#pragma once

#include <utility>

#include "app/Args.h"

namespace choco::app {

class App {
 public:
  explicit App(Options options) : options_(std::move(options)) {}
  // Returns the process exit code: 0 ok, 1 runtime failure, 2 usage or script error.
  int run();
  // After run() returned: true when the in-game switcher (Ctrl+Shift+G) chose another game, with `next` then the
  // Options to boot it with (call main's loop again); false ends the process.
  bool takeReboot(Options& next);

 private:
  Options options_;
  bool reboot_ = false;
};

}  // namespace choco::app
