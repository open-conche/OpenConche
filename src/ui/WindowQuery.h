#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "gfx/Types.h"

namespace choco::ui {

// The only thing the test driver needs to know about the windows (script-host design section 8).
class WindowQuery {
 public:
  virtual ~WindowQuery() = default;
  // True when a window of that name is found (top modal down the modal chain, pre-order, a modal matches by its script
  // path) and it and all its ancestors are shown.
  virtual bool isShown(std::string_view name) const = 0;
  // True when `name` matches a modal whose script logged a Lua error since it was pushed.
  // The label of the named window (top modal down the chain) when it has one, for expect-label.
  virtual bool labelOf(std::string_view name, std::string& out) const {
    (void)name, (void)out;
    return false;
  }
  // Visible, not removed sprites in shown windows whose debug name starts with `prefix` (expect-sprite).
  virtual int countSprites(std::string_view prefix) const {
    (void)prefix;
    return 0;
  }
  // The window tree (with sprite trees) as text, for the harness `dump` command.
  virtual std::string dumpWindows() const { return {}; }
  // The screen point the harness `autoshoot` should click now (the first shown window that answers), or none.
  virtual std::optional<gfx::PointF> autoAim() const { return std::nullopt; }
  virtual bool hasScriptError(std::string_view name) const { (void)name; return false; }
};

}  // namespace choco::ui
