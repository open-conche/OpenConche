#pragma once

#include <set>
#include <string>

#include <optional>

#include "games/common/PortDef.h"
#include "games/choc2/FireworkSprite.h"
#include "games/choc2/LogoRenderer.h"
#include "games/common/PortWindow.h"

namespace choco::game::choc2 {

// Port (Chocolatier 2, docs/re/choc2-port-and-ledger.md 1, docs/design/choc2-port-and-ledger.md 2.1). The scene does not come
// from a style key: each layout closure of ui/portview.lua calls bsgAddSprite(t) while this window is being built, and the
// calls are turned into a PortDef (PortScene.h) that PortWindow builds from in postChildrenInit. The background is a sibling
// Bitmap, so this window has none. Keys read: xOffset, yOffset (0) on top of PortWindow's.
//
// FACT (docs/re/choc2-port-binary.md 1.5, 1.6): a 40 ms task; the `buildinglabels` child window shows until the first tick at or
// after 3900 ms and is then hidden at once, with no fade; until then every building with a `command` pulses in sync. StopPulsing()
// (Lua's NameRollover calls it) hides the labels and ends the pulse. INFERRED (a recorded decision): the scene is not faded in
// and the Chocolatier 3 port music does not run (Chocolatier 2's music is SetMusicState).
class Port : public PortWindow {
 public:
  static constexpr double kTickMs = 40;

  Port(ui::WindowManager& wm, script::Rng& rng, script::Log& log, World* world)
      : PortWindow(wm, rng, log, world), portLog_(log), rng_(rng) {
    fadeIn_ = false;
    portMusic_ = false;
    choc2Pulse_ = true;
  }
  std::string_view typeName() const override { return "Port"; }
  void init(const ui::Style& style) override;
  void postChildrenInit(const ui::Style& style) override;
  // The 40 ms scene task: the intro pulse and the label hold.
  bool onAnimate() override;

  // bsgAddSprite: adds one sprite or building to the scene being collected. Before postChildrenInit only.
  void addSprite(const ui::Style& table);
  // StartFireworks (docs/re/choc2-port-binary.md 7): creates the five TFireworks, drawn between the sprites around
  // this call's layout position (the recorded sprite-list size; the next sprites land after them). Before
  // postChildrenInit only, like bsgAddSprite.
  void startFireworks();

  // The Port under construction: the first Port from the creation stack's top up to the root; null if none.
  static Port* underConstruction(ui::WindowManager& wm);
  // The Port the scripts talk to: the one under construction, else the first one in the modal chain, top modal first.
  static Port* current(ui::WindowManager& wm);

 protected:
  PortDef readScene(const ui::Style& style) override;

 private:
  script::Log& portLog_;
  script::Rng& rng_;  // the fireworks roll from the context's stream, as every port sprite does
  // The five fireworks StartFireworks created during the layout, attached to the scene's root at postChildrenInit.
  std::vector<std::unique_ptr<FireworkSprite>> pendingFireworks_;
  std::vector<FireworkSprite*> fireworks_;  // the live ones, for the 40 ms update
  int fireworksLayer_ = -1;                // the layout slot they sit in (-1: no StartFireworks ran)
  std::set<std::string> noted_;
  std::optional<LogoImages> logoImages_;  // one per port build, so a missing picture is logged once
  bool collected_ = false;  // readScene has run: later sprites have nowhere to go
  PortDef pending_;
  int order_ = 0;
  float xOffset_ = 0, yOffset_ = 0;
};

}  // namespace choco::game::choc2
