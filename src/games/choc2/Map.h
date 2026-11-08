#pragma once

#include <memory>
#include <string>
#include <vector>

#include "games/common/MapWindow.h"
#include "gfx/Texture.h"
#include "ui/Window.h"

namespace choco::game::choc2 {

// Map (Chocolatier 2, docs/re/choc2-port-and-ledger.md 5.1, docs/design/choc2-port-and-ledger.md 2.6). A container: its one child
// is the `map` Bitmap (image/map) with the `colormask` Bitmap and the port icon and label groups, all built by ui/map.lua with
// Rollovers, Sprites and Texts, so the markers, popups and labels need no native drawing here.
//
// What the window reads itself are the three style keys the Lua puts in front of it with AppendStyle: `ports` (LPort._ByName:
// every port's name, mapx and mapy, parsed with the same game::parsePorts that Chocolatier 3's MapWindow uses), `routes` (the
// current port's routes by destination) and `current` (its name). They give the marker list the dump and the tests see; nothing
// is drawn from them.
//
// INFERRED (a recorded decision): `image/map` (map.jpg, the flat region map) is the base picture because the Lua names it.
// `colormask` shows image/map_color's pixels through the visited ports' ports/map_mask_<port> alpha (docs/re/choc2-map-binary.md
// 1.1, composeColorMask). Like the original's class statics, the list of visited ports and the picture are made once per
// ResetColorMap, when the next Map is created; the picture is kept as pixels and uploaded for each Map.
//
// Keys: Escape runs DoModal('ui/pausedialog.lua') and every key is consumed (TMap::OnKeyDown). Losing the app focus while a trip
// runs opens the same dialog; both edges start and stop the 40 ms animation (World::setForeground calls onForeground).
class Map : public ui::Window {
 public:
  explicit Map(ui::WindowManager& wm) : Window(wm) {}
  std::string_view typeName() const override { return "Map"; }
  void init(const ui::Style& style) override;
  void postChildrenInit(const ui::Style& style) override;
  bool onKeyDown(const ui::KeyEvent& ev) override;
  // The 40 ms task keeps the port icons' sheets redrawing. It also takes the focus back when a modal above has closed:
  // WindowManager::popTop gives the focus to the modal itself, not its default focus, so Escape would stop working.
  bool onAnimate() override;
  std::string detail() const override;

  // ResetColorMap(): forgets the visited list and the picture; the next Map made rebuilds them.
  static void resetColorMap();
  // The app lost (false) or regained (true) the foreground (message 1001 / 1000, an INFERENCE for their meaning).
  void onForeground(bool fg) override;
  const std::vector<std::string>& visitedPorts() const { return visited_; }
  bool hasColorMask() const { return colorMask_ != nullptr; }

  const std::vector<PortInfo>& markers() const { return ports_; }
  const std::string& current() const { return current_; }
  int routeCount() const { return routes_; }
  // The marker of a port, or null.
  const PortInfo* marker(const std::string& name) const;

 private:
  std::vector<PortInfo> ports_;
  std::vector<std::string> visited_;  // ports with `available` and `visited` true, in table order
  std::shared_ptr<gfx::Texture> colorMask_;
  std::string current_;
  int routes_ = 0;
};

}  // namespace choco::game::choc2
