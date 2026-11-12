#pragma once

// VMachine, VMachineButton (docs/re/greatcc-machines-and-chocolates.md 4): the base of the machines of a station. A machine is a
// sprite with selection buttons; a click on a button stores the choice and sends the avatar to the station; her arrival there
// starts the machine's work (ProcessChocolate -> Activate) or collects its output. A machine's work is a chain of waits
// (VSuspendable): each OnResume is a step switch on `state`, the animations come from the `.anm` scripts of its children.
// Every machine type is built: the mixer, ingredient, press, topping, infusion, sauce, drink and delivery machines.

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "games/greatcc/scene/VButton.h"
#include "games/greatcc/shop/ShopHost.h"
#include "games/greatcc/shop/VChocolate.h"
#include "games/greatcc/shop/VSuspendable.h"

namespace choco::games::greatcc {

class VMachine;
class VPlayer;
class VStation;

// One of a machine's choices. A leaf of the machine's tree at sprite layer 10; its click picks the option, its hover tells the
// machine's glow, and a long hover swaps the icon for the colour stripe the button may carry.
class VMachineButton : public VButton {
 public:
  VMachineButton(script::ScriptContext& ctx, std::weak_ptr<VMachine> machine);
  // The button's own glow (0.05, passed on to its children) is added on creation.
  static std::shared_ptr<VMachineButton> create(script::ScriptContext& ctx, std::weak_ptr<VMachine> machine);
  const char* kind() const override { return "VMachineButton"; }

  // The children go; the button art is the `button` asset's default state; an owned quality upgrade tints it (0, 1, 1).
  void setUpgraded(bool upgraded);
  bool upgraded() const { return upgraded_; }
  // The `icon` (a leaf showing the asset `icon`, state from the table's `icon`; tinted when it is `dark`) and, when the table has
  // a `stripe`, the stripe leaf, which is then what shows (the icon is hidden until the pointer rests on the button).
  void init() override;
  void onMouseEnter() override;
  void onMouseLeave() override;
  void onMouseHover(double ms) override;
  void onClick() override;
  void update(bool softPaused) override;
  // The icon and stripe swap (a 300 ms cross-fade); `hovered` true shows the icon.
  void swapVisual(bool hovered);

  VObject* iconSprite() const { return icon_.get(); }
  VObject* stripeSprite() const { return stripe_.get(); }

 private:
  std::weak_ptr<VMachine> machine_;
  std::shared_ptr<VObject> icon_, stripe_;
  std::shared_ptr<AlphaEffect> iconFx_, stripeFx_;
  bool upgraded_ = false;
  bool swapped_ = false;
};

class VMachine : public VSuspendable {
 public:
  explicit VMachine(script::ScriptContext& ctx);
  const char* kind() const override { return "VMachine"; }

  // VMachine::Create for the station type: the mixer (2), ingredient (3), press (4), topping (5), sauce (6), infusion (7), drink (8) and
  // delivery (10) machines; any other type returns null.
  static std::shared_ptr<VMachine> create(script::ScriptContext& ctx, int stationType);
  void setHost(ShopHost* host) { host_ = host; }

  // The chocolate held (a machine never holds one at load) goes, the buttons are made from `selections` (a lower-cased key each, an
  // existing one is reused), the machine's own glow (0.15, passed on, taken off the buttons) is added, then the VObject keys.
  void loadFromTable(const StyleChain& style) override;
  // Once: VObject::Init, the `base` asset, Reset, every frame delay set to `frameDelay` ms.
  void init() override;
  // A held chocolate that has decayed is dropped silently; then VSuspendable.
  void update(bool softPaused) override;
  // The click (the sprite's own, or the one ChooseOption raises) goes to the station by the machine's name.
  void onClick() override;

  // OnResume: Reset, finished. A machine with steps overrides it.
  bool onResume() override;
  // State 0, Unlock, the wait flag off (without resuming), VSuspendable::Reset; the `anim_` children hide and stop, the `static_`
  // ones show, every child becomes a leaf; a choice made while busy is then applied (ProcessChocolate) when the machine holds
  // nothing it has not collected.
  void reset() override;
  // Applies the choice to what the machine holds. The base does nothing.
  virtual bool processChocolate() { return false; }
  // ChooseOption: unless locked the choice is stored; a click event goes to the machine either way.
  void chooseOption(int id);
  // Activate: the `static_` children hide, the steam booster sets the frame delay (x0.9), the station is told (it is suspended
  // until Unlock), state 1, locked, processed, the choice used.
  virtual void activate();
  // Unlock (not virtual): when locked, unlock and let the station take arrivals again.
  void unlock();
  // OnArrival: hands a held output to the avatar when unlocked and a hand is free.
  virtual void onArrival(VPlayer& player);
  // AttachChocolate: only when empty or the same chocolate; it joins as a child at that sprite layer, anchor `chocolate`.
  bool attachChocolate(const std::shared_ptr<VChocolate>& choc, int spriteLayer, bool playSound);
  // DetachChocolate: the held chocolate leaves (null when none).
  std::shared_ptr<VChocolate> detachChocolate(bool playSound);
  // SetStation: not a leaf, clickable.
  void setStation(const std::shared_ptr<VStation>& station);

  int option() const { return option_; }
  bool enabled() const { return enabled_; }
  bool locked() const { return locked_; }
  bool processed() const { return processed_; }
  int state() const { return state_; }
  int selectionCount() const { return selectionCount_; }
  float frameDelay() const { return frameDelay_; }
  const std::shared_ptr<VChocolate>& chocolate() const { return chocolate_; }
  const std::map<std::string, std::shared_ptr<VMachineButton>>& buttons() const { return buttons_; }
  std::shared_ptr<VStation> station() const { return station_.lock(); }
  ShopHost* host() const { return host_; }
  // VMachine::GetWhichHand(player, layer): the hand (2 or 1) whose chocolate takes `layer`, or -1. With two that do, the order a
  // counter customer is waiting for decides (ShopWindow::FindOrderWithLayer with this machine's choice); a hand the order does not
  // lead to either way gives -1.
  int getWhichHand(VPlayer& player, int layer) const;

 protected:
  bool stationUpgrade(const std::string& name, const std::string& category = "station") const {
    return host_ && host_->upgradeActive(name, category);
  }
  void playSfx(const char* file) const;
  // The arrival of the ingredient and topping machines (their OnArrival is one function): when unlocked, the choice is applied to what
  // the machine holds (done); with no choice a held chocolate is handed over when a hand is free, or an empty machine takes the
  // chocolate `getWhichHand(layer)` names; with a choice that could not be applied the machine and that hand swap their chocolates
  // and the choice is applied again, or, when no hand fits, the choice is dropped. An empty machine with no fitting hand calls
  // detachObject(-1), which takes nothing and turns the preferred hand.
  void feedArrival(VPlayer& player, int layer);
  // The child by its lower-cased key (null when the table has none).
  VObject* child(const char* key) const { return childObject(key); }
  void showChild(const char* key, bool visible) const;
  // Plays `fn` on the child when it exists (a missing child is skipped).
  void playChild(const char* key, const std::string& fn, bool loop = false) const;
  // The first half of a machine's Init: `enabled` off, then for each choice i = 1..selectionCount the upgrade `<family><i>` decides
  // whether `button<i>` (and the child `<tankPrefix><i>`, when given) shows; the button's quality mark is `<qualityFamily><i>`
  // (none: off); a shown button is initialised again and the machine is enabled.
  void initSelections(const char* family, const char* qualityFamily, const char* tankPrefix);
  // The second half: VMachine::Init when enabled (or in endless mode, when every machine is built), then shown only when enabled.
  void finishInit();

  ShopHost* host_ = nullptr;
  int option_ = -1;
  bool enabled_ = false;
  std::shared_ptr<VChocolate> chocolate_;
  bool processed_ = false;
  int selectionCount_ = 0;
  std::string forward_, reverse_;
  int state_ = 0;
  bool locked_ = false;
  float frameDelay_ = 30.0f;
  bool initialised_ = false;
  std::weak_ptr<VStation> station_;
  std::map<std::string, std::shared_ptr<VMachineButton>> buttons_;
};

}  // namespace choco::games::greatcc
