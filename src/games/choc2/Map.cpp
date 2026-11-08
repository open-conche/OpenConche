#include "games/choc2/Map.h"

#include "games/choc2/ColorMask.h"
#include "gfx/TextureCache.h"
#include "ui/Input.h"
#include "ui/ScriptSink.h"
#include "ui/WindowManager.h"
#include "ui/widgets/Widgets.h"
#include "games/choc2/MapTravel.h"

namespace choco::game::choc2 {

namespace {

// TMap's class statics (pColorMap and the visited list): the picture is made once after a reset. Kept as pixels, not as a
// texture, so nothing outlives the texture cache that made it; each Map uploads its own copy.
struct ColorMapState {
  gfx::ImageData pixels;
  bool built = false;
};
ColorMapState& colorMapState() {
  static ColorMapState s;
  return s;
}

}  // namespace

void Map::resetColorMap() { colorMapState() = {}; }

void Map::init(const ui::Style& style) {
  Window::init(style);
  current_ = style.stringOr("current");
  if (auto ports = style.table("ports")) {
    ports_ = parsePorts(ports.get());
    ports->forEachTable([&](const ui::Style& p) {
      const std::string name = p.stringOr("name");
      if (!name.empty() && p.booleanOr("available", false) && p.booleanOr("visited", false)) visited_.push_back(name);
    });
  }
  if (auto routes = style.table("routes")) routes->forEachTable([&](const ui::Style&) { ++routes_; });
  // The map is its modal's default focus, so keys reach onKeyDown (TMap::Init).
  for (ui::Window* p = parent(); p; p = p->parent()) {
    if (p->isModal()) {
      static_cast<ui::ModalWindow*>(p)->defaultFocus = id();
      break;
    }
  }
}

void Map::postChildrenInit(const ui::Style& style) {
  Window::postChildrenInit(style);
  gfx::TextureCache* cache = manager().textures();
  ColorMapState& st = colorMapState();
  if (cache && !st.built && !visited_.empty()) {
    // UpdateColorMap: image/map_color's pixels with the alpha of the visited ports' masks (a port without a mask is skipped).
    gfx::ImageData colour;
    std::vector<gfx::ImageData> masks;
    if (cache->loadImage("image/map_color", colour)) {
      masks.resize(visited_.size());
      std::vector<const gfx::ImageData*> used;
      for (std::size_t i = 0; i < visited_.size(); ++i)
        if (cache->loadImage("ports/map_mask_" + visited_[i], masks[i])) used.push_back(&masks[i]);
      composeColorMask(colour, used);
      st.pixels = std::move(colour);
      st.built = true;
    }
  }
  if (cache && st.built) colorMask_ = cache->makeFromPixels("$choc2-colormask", st.pixels);
  if (colorMask_)
    if (auto* mask = dynamic_cast<ui::BitmapWindow*>(find("colormask"))) mask->setTexture(colorMask_);
  manager().startWindowAnimation(id(), 40);  // restarted by onForeground(true)
  manager().setFocus(id());                  // keys reach onKeyDown
}

bool Map::onKeyDown(const ui::KeyEvent& ev) {
  if (ui::keyChar(ev) == 27)
    if (ui::ScriptSink* sink = manager().scriptSink()) sink->runString("DoModal('ui/pausedialog.lua')");
  return true;  // every key is consumed
}

void Map::onForeground(bool fg) {
  if (fg) {
    manager().startWindowAnimation(id(), 40);
    return;
  }
  manager().stopWindowAnimation(id());
  // INFERENCE (docs/re/choc2-map-binary.md 1.1, message 1001): a trip pauses on focus loss. Only when this map is the top
  // modal, so an open dialog is not stacked with a second one.
  MapTravel* travel = MapTravel::current(manager());
  const ui::Window* top = manager().topModal();
  bool onTop = false;
  for (const ui::Window* w = this; w; w = w->parent())
    if (w == top) onTop = true;
  if (travel && travel->stillTravelling() && onTop)
    if (ui::ScriptSink* sink = manager().scriptSink()) sink->runString("DoModal('ui/pausedialog.lua')");
}

bool Map::onAnimate() {
  // In the top modal with the focus on the modal itself (a dialog above just closed): take it back.
  const ui::Window* top = manager().topModal();
  if (top && manager().focus() == top->id() && manager().focus() != id()) {
    for (const ui::Window* w = this; w; w = w->parent())
      if (w == top) {
        manager().setFocus(id());
        break;
      }
  }
  return true;
}

const PortInfo* Map::marker(const std::string& name) const {
  for (const PortInfo& p : ports_)
    if (p.name == name) return &p;
  return nullptr;
}

std::string Map::detail() const {
  return " ports=" + std::to_string(ports_.size()) + " routes=" + std::to_string(routes_) + " current=" + current_ +
         " visited=" + std::to_string(visited_.size()) + (colorMask_ ? " colormask" : "");
}

}  // namespace choco::game::choc2
