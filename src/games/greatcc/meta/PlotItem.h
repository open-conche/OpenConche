#pragma once

// VPlotItem, VPlotStep and VPlotListener (docs/re/greatcc-meta-flow.md 2): one diary page. An item is a sprite built from one
// `story_*` table of a location's plot file; its `nextStep` table is its first (and, in the shipped plots, only) step, a sprite with
// the page's text and portraits and an OK button. The item runs its steps in order: a step fades in when it starts, is done when its
// button is clicked and fades out; an item is complete when its steps are used up. The listener on the step's button tells the plot
// manager which item a click belongs to.

#include <memory>
#include <string>

#include "games/greatcc/scene/Effects.h"
#include "games/greatcc/scene/VButton.h"
#include "games/greatcc/scene/VObject.h"

namespace choco::games::greatcc {

class PlotItem;

// VPlotListener: a click on the button of a step goes to the plot manager under the item's name, as a response (the button's id) when
// the sprite is a button, else as an advance.
class PlotListener : public SpriteListener {
 public:
  explicit PlotListener(script::ScriptContext& ctx) : ctx_(ctx) {}
  void setPlotItem(std::string name) { item_ = std::move(name); }
  const std::string& plotItem() const { return item_; }
  void onClick(VSprite& sprite) override;

 private:
  script::ScriptContext& ctx_;
  std::string item_;
};

class PlotStep : public VObject {
 public:
  explicit PlotStep(script::ScriptContext& ctx) : VObject(ctx, "", "") {}
  static std::shared_ptr<PlotStep> create(script::ScriptContext& ctx) { return std::make_shared<PlotStep>(ctx); }
  const char* kind() const override { return "VPlotStep"; }

  // `speaker`; the `responses` table is not built (no shipped plot has one: it is logged once); with no responses the `default_button`
  // style makes the OK button, a child of the step (found in this table or any around it); then the sprite keys.
  void loadFromTable(const StyleChain& style) override;
  // Init: the object's, the button's, and the step is neither begun nor advanced.
  void init() override;
  // Reset: neither begun nor advanced (the textures of the original's Reset are not unloaded).
  void reset();
  // Advance: the first call begins the step (a 500 ms fade in) and answers false; once begun, a step with no responses (or with a
  // chosen one) is advanced by the next call: it fades out over 500 ms from the alpha it has and the answer is true.
  bool advance();
  // IsComplete: advanced, and the fade out is over.
  bool isComplete() const { return advanced_ && !(fade_ && fade_->inProgress()); }
  // ChooseResponse: the responses are not built, so nothing is chosen and the step after it is none; the value is 0.
  int chooseResponse(const std::string& id, std::shared_ptr<PlotStep>& next);
  // The listener goes on the button.
  void setListener(const std::shared_ptr<SpriteListener>& listener);

  const std::string& speaker() const { return speaker_; }
  VButton* button() const { return button_.get(); }
  bool begun() const { return begun_; }
  bool advanced() const { return advanced_; }

 private:
  void begin();
  void end();

  std::string speaker_;
  std::shared_ptr<VButton> button_;
  std::shared_ptr<SpriteEffect> fade_;
  bool begun_ = false, advanced_ = false;
};

class PlotItem : public VObject {
 public:
  // `slot` is the item's place in the plot table (the original's persistence slot, which nothing writes).
  PlotItem(script::ScriptContext& ctx, std::string name, int slot, bool quest);
  static std::shared_ptr<PlotItem> create(script::ScriptContext& ctx, std::string name, int slot, bool quest = false) {
    return std::make_shared<PlotItem>(ctx, std::move(name), slot, quest);
  }
  const char* kind() const override { return quest_ ? "VQuestItem" : "VPlotItem"; }

  // `type`, the step of the `nextStep` table (loaded under this table), then the sprite keys (the page's backdrop is the item's
  // `assets`).
  void loadFromTable(const StyleChain& style) override;
  // Init: the object's, the first step's, value 0 (the completed flag stays).
  void init() override;
  // Reset: the first step is neither begun nor advanced.
  void reset();
  void setListener(const std::shared_ptr<PlotListener>& l) { listener_ = l; }

  // Advance: nothing once complete; else the current step is advanced (the first call starts the first step), and when that
  // finishes it the item moves on. True when the item is complete.
  bool advance();
  // ChooseResponse(id): the current step is advanced, and the item moves on when it is; an item with no step to run is complete.
  bool chooseResponse(const std::string& id);
  // IsComplete: every step has been advanced (set at the click of the last one, while its fade out still runs).
  bool isComplete() const { return complete_; }
  // Update: the sprite's, then a step that has faded out leaves the item, and the next one is brought in.
  void update(bool softPaused) override;

  const std::string& plotType() const { return type_; }
  int slot() const { return slot_; }
  int value() const { return value_; }
  PlotStep* currentStep() const { return cur_.get(); }
  PlotStep* firstStep() const { return first_.get(); }

 private:
  bool advanceStep();

  int slot_;
  bool quest_;
  std::string type_;
  std::shared_ptr<PlotStep> first_, prev_, cur_, next_;
  std::shared_ptr<PlotListener> listener_;
  int value_ = 0;
  bool complete_ = false;
};

}  // namespace choco::games::greatcc
