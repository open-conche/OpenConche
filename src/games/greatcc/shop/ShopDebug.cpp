// ShopWindow's developer tools (docs/re/greatcc-debug-tools.md 1-3): the Ctrl+C console, its ten commands, the Debug button's zone reload
// and the zones_debug drawing.

#include <algorithm>
#include <climits>
#include <cmath>

#include "games/greatcc/GreatCCWorld.h"
#include "games/greatcc/debug/DebugCommand.h"
#include "games/greatcc/shop/ShopWindow.h"
#include "games/greatcc/shop/VShopHud.h"
#include "games/greatcc/ui/RTimer.h"
#include "gfx/Renderer.h"
#include "script/RunFile.h"
#include "ui/WindowManager.h"

namespace choco::games::greatcc {

namespace {

ui::ModalWindow* modalOf(ui::Window& w) {
  for (ui::Window* p = w.parent(); p; p = p->parent())
    if (auto* m = dynamic_cast<ui::ModalWindow*>(p)) return m;
  return nullptr;
}

// ZoneController::Draw: a 10 x 10 square on each enabled point and a line on each connection between enabled points; red, green for
// the points of the current path and the connections between consecutive path points; all at alpha 0.75.
void drawZones(gfx::Renderer& r, gfx::PointF origin, const ZoneGraph& graph) {
  const gfx::ColorF normal{1, 0, 0, 0.75f}, highlight{0, 1, 0, 0.75f};
  const auto& path = graph.path();
  auto onPath = [&](const ZonePoint* p) { return std::find(path.begin(), path.end(), p) != path.end(); };
  auto consecutive = [&](const ZonePoint* a, const ZonePoint* b) {
    for (auto it = path.begin(); it != path.end(); ++it) {
      auto next = std::next(it);
      if (next == path.end()) break;
      if ((*it == a && *next == b) || (*it == b && *next == a)) return true;
    }
    return false;
  };
  static const int kIdx[6] = {0, 1, 2, 0, 2, 3};
  for (const auto& zone : graph.zones())
    for (const auto& p : zone->points) {
      if (p->disabled) continue;
      for (const ZoneConnection& c : p->connections) {
        if (!c.target || c.target->disabled) continue;
        const gfx::PointF a{origin.x + static_cast<float>(p->x), origin.y + static_cast<float>(p->y)};
        const gfx::PointF b{origin.x + static_cast<float>(c.target->x), origin.y + static_cast<float>(c.target->y)};
        const float dx = b.x - a.x, dy = b.y - a.y, len = std::sqrt(dx * dx + dy * dy);
        if (len <= 0) continue;
        const float nx = -dy / len * 0.5f, ny = dx / len * 0.5f;  // a one pixel wide line
        const gfx::ColorF col = consecutive(p.get(), c.target) ? highlight : normal;
        const gfx::Vertex v[4] = {{{a.x + nx, a.y + ny}, col, {}}, {{b.x + nx, b.y + ny}, col, {}}, {{b.x - nx, b.y - ny}, col, {}},
                                  {{a.x - nx, a.y - ny}, col, {}}};
        r.drawGeometry(nullptr, v, kIdx, gfx::Blend::Normal);
      }
    }
  for (const auto& zone : graph.zones())
    for (const auto& p : zone->points) {
      if (p->disabled) continue;
      r.fillRect({origin.x + static_cast<float>(p->x) - 5, origin.y + static_cast<float>(p->y) - 5, 10, 10},
                 onPath(p.get()) ? highlight : normal, gfx::Blend::Normal);
    }
}

}  // namespace

void ShopWindow::draw(ui::DrawContext& ctx) const {
  VSpriteWindow::draw(ctx);
  if (zonesDebug_ && zones_) drawZones(ctx.renderer, ctx.origin, *zones_);
}

bool ShopWindow::onKeyDown(const ui::KeyEvent& ev) {
  if (ev.ctrl && ev.name.size() == 1 && (ev.name[0] == 'c' || ev.name[0] == 'C')) {
    openConsole();
    return true;
  }
  return VSpriteWindow::onKeyDown(ev);
}

void ShopWindow::openConsole() {
  ui::ModalWindow* modal = modalOf(*this);
  if (!modal) return;
  ui::Window& temp = modal->adoptFront(std::make_unique<ui::Window>(manager()));
  script::runFileInto(ctx_, ctx_.lua.L(), temp, "scripts/console.lua");
  if (ui::Window* console = temp.find("console_window")) {
    if (std::unique_ptr<ui::Window> owned = console->parent()->orphan(*console)) modal->adoptFront(std::move(owned));
  } else {
    ctx_.log.write("[greatcc] scripts/console.lua made no console_window");
  }
  manager().destroyWindow(temp);
}

void ShopWindow::closeConsole() {
  ui::ModalWindow* modal = modalOf(*this);
  if (!modal) return;
  ui::Window* console = modal->find("console_window");
  if (!console) return;
  manager().destroyWindow(*console);
  // The focused text field goes with it: the modal hands the key focus back to its default focus (this window), so Ctrl+C works again.
  manager().setFocus(modal->id());
}

void ShopWindow::doCommand(const std::string& line) {
  if (line.empty()) return;
  const DebugCommand cmd = parseDebugCommand(line);
  const std::vector<std::string>& a = cmd.args;
  GreatCCWorld* world = worldOf(ctx_);
  auto sprite = [&](std::size_t arg) { return a.size() > arg ? sprites().findSpriteByName(a[arg]) : nullptr; };

  if (cmd.word == "closeconsole") {
    closeConsole();
  } else if (cmd.word == "choctint") {
    if (a.empty() || !world) return;
    const int type = chocolateLayerTypeFromString(a[0]);
    gfx::ColorF c{1, 1, 1, 1};
    if (a.size() > 1) c.r = debugToFloat(a[1]);
    if (a.size() > 2) c.g = debugToFloat(a[2]);
    if (a.size() > 3) c.b = debugToFloat(a[3]);
    world->setChocolateTint(type, c);
  } else if (cmd.word == "setvalue") {
    if (a.size() >= 2 && world) world->values().setValue(a[0], debugToFloat(a[1]), false);
  } else if (cmd.word == "setpos") {
    if (a.size() < 3) return;
    if (VSprite* s = sprite(0)) {
      s->setPosition({debugToFloat(a[1]), debugToFloat(a[2])});
      s->attachToAnchor("");
    }
  } else if (cmd.word == "setlayer") {
    if (a.size() < 2) return;
    if (VSprite* s = sprite(0)) {
      if (s->parent()) s->setLayer(debugToInt(a[1]));
      else s->setDepth(debugToInt(a[1]));  // a top-level sprite sorts by depth
    }
  } else if (cmd.word == "setvisible") {
    if (a.size() < 2) return;
    if (VSprite* s = sprite(0)) s->setVisible(debugToInt(a[1]) != 0);
  } else if (cmd.word == "getupgrade") {
    if (a.size() >= 2 && world) world->upgrades().enableUpgrade(a[0], a[1], true, nullptr);
  } else if (cmd.word == "setdelay") {
    if (a.size() < 2) return;
    if (VSprite* s = sprite(0)) s->setDelay(debugToFloat(a[1]), true);
  } else if (cmd.word == "setscale") {
    if (a.size() < 3) return;
    if (VSprite* s = sprite(0)) s->setScale(debugToFloat(a[1]), debugToFloat(a[2]));
  } else if (cmd.word == "addminutes") {
    if (a.empty() || !hud_ || !hud_->timer()) return;
    LevelTimer& t = hud_->timer()->timer();
    t.elapsedMs = debugAddMinutes(t.elapsedMs, debugToInt(a[0]));
  }
}

void ShopWindow::doDebug() {
  if (!zones_) return;
  const auto xml = ctx_.fs.readText("zones/zones.xml");
  if (!xml || !zones_->load(*xml)) ctx_.log.write("[greatcc] DoDebug: zones/zones.xml did not load; the zone graph is unchanged");
}

}  // namespace choco::games::greatcc
