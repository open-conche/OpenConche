#pragma once

// VTutorial and VTutorialStep (docs/re/greatcc-meta-flow.md 1.1, 1.3, 1.4). A tutorial is a sprite built from one entry of
// locations/tutorials.lua: a list of steps (each a nine-slice speech panel with text, arrows and an optional Continue button, plus
// the sprites it exempts from the dim, makes clickable and waits on) and the state that decides whether it shows. It lives in the
// manager's map and, while it runs, in a TutorialWindow's sprite manager; the dim and the lockdown act on the level's.

#include <climits>
#include <map>
#include <memory>
#include <set>
#include <string>

#include "games/greatcc/scene/Effects.h"
#include "games/greatcc/scene/TextSprite.h"
#include "games/greatcc/scene/TiledObject.h"
#include "games/greatcc/scene/VButton.h"
#include "ui/Message.h"
#include "ui/Window.h"

namespace choco::games::greatcc {

class TutorialManager;
class TutorialSpider;

// eStepTrigger (locations/defines.lua): what a named sprite does for a step.
enum StepTrigger { kNoTrigger = 0, kClick = 1, kResume = 2, kArrival = 3, kIgnore = 4 };

class TutorialStep : public TiledObject {
 public:
  TutorialStep(script::ScriptContext& ctx, int number);
  static std::shared_ptr<TutorialStep> create(script::ScriptContext& ctx, int number) {
    return std::make_shared<TutorialStep>(ctx, number);
  }
  const char* kind() const override { return "VTutorialStep"; }

  int number() const { return number_; }
  // The keys of the step (the lookup depth 2 reaches the tutorial around it), the arrays of sprite names, the Continue button, the
  // clickAny rule, then the panel (VTiledObject keys); a step with no panel size has none. A step that has a panel makes its text
  // sprite from the same table and puts every arrow at a position relative to the panel.
  void loadFromTable(const StyleChain& style) override;
  // Init: not begun, no effects, hidden, then the panel's own init.
  void init() override;
  // Begin: stamps the begin time once; a panel is shown with the fade-in; the level's sprites are soft-paused when `pause` and
  // `tutorialPause` are both set.
  void begin(const std::shared_ptr<SpriteEffect>& fadeIn, bool tutorialPause);
  // End: a panel runs the fade-out in place of the fade-in (a null one just leaves it as it is); the soft pause ends.
  void end(const std::shared_ptr<SpriteEffect>& fadeOut);
  // Advance: the request time is stamped once.
  void advance();
  // The step has run for `duration` (always when it has no begin time or a negative duration) and the request has waited `delay`
  // (always when it was never made or the delay is negative).
  bool durationComplete() const;
  bool delayComplete() const;
  // Hides the panel when its fade-out has finished.
  void update(bool softPaused) override;

  // The kind of trigger of a name (-1 when the step has none for it).
  int trigger(const std::string& name) const;
  const std::map<std::string, int>& triggers() const { return triggers_; }
  // The names looked up in the level (action objects and trigger objects), the ones that are exempt from the dim (highlighted,
  // action and trigger objects) and the ones that are clickable (action objects and the trigger objects that did not say
  // action = false).
  const std::set<std::string>& lookupNames() const { return lookup_; }
  const std::set<std::string>& exemptNames() const { return exempt_; }
  const std::set<std::string>& actionNames() const { return action_; }
  bool pause() const { return pause_; }
  bool dim() const { return dim_; }
  bool skip() const { return skip_; }
  bool clickAny() const { return clickAny_; }
  int duration() const { return duration_; }
  int delay() const { return delay_; }
  bool hasPanel() const { return !noPanel_; }
  TextSprite* text() const { return text_.get(); }

 private:
  // The clock the step reads: its manager's, and before it has one the top modal's (the sprite constructor's clock).
  double nowMs() const;

  int number_;
  int duration_ = -1, delay_ = -1;
  bool pause_ = true, dim_ = true, skip_ = true, clickAny_ = false, noPanel_ = false;
  double begun_ = 0, advanced_ = 0;
  std::set<std::string> lookup_, exempt_, action_;
  std::map<std::string, int> triggers_;
  std::shared_ptr<TextSprite> text_;
  std::shared_ptr<SpriteEffect> fadeIn_, fadeOut_;
};

class Tutorial : public VObject {
 public:
  // `slot` is the persistence slot the manager passes (always 0).
  Tutorial(script::ScriptContext& ctx, std::string name, int slot);
  ~Tutorial() override;
  static std::shared_ptr<Tutorial> create(script::ScriptContext& ctx, std::string name, int slot) {
    return std::make_shared<Tutorial>(ctx, std::move(name), slot);
  }
  const char* kind() const override { return "VTutorial"; }

  const std::string& tutorialName() const { return tutorialName_; }
  // The persistence slot the manager was given (0 for every tutorial; the original's key into one shared block).
  int slot() const { return slot_; }
  // minlevel (any depth), maxlevel, window, repeatable and tryonce (this table only), pause (any depth), modes (any depth), the steps
  // (each loaded, initialised and added as a child), the skip button; the VObject keys first.
  void loadFromTable(const StyleChain& style) override;
  // VTutorial::Init (it does not run VObject's): back to step 0, finished, every step initialised, the spider made or reset.
  void init() override;

  // ---- state: 0 never run, 1 started or skipped by the option, 3 finished; it only goes up ----
  int state() const { return state_; }
  // SetState: nothing when `s` is not above the current state; saving writes the state to the player's progress.
  void setState(int s, bool save);
  bool shouldShow() const { return state_ == 0 || (state_ == 1 && !tryonce_) || repeatable_; }
  bool inRange(int level) const { return level >= minLevel_ && level <= maxLevel_; }
  bool inMode(int mode) const { return modes_.count(mode) != 0; }
  void loadCurrentUser();
  void updateCurrentUser();

  // ---- running ----
  // Activate(true): init, not done, the spider starts, the first step comes up, state 1 saved. Activate(false) only moves to the
  // step past the last one's number and advances (never called).
  bool activate(bool fresh);
  // IsComplete: the last step has ended, or the tutorial was skipped or reset.
  bool isComplete() const { return complete_; }
  // The flag the window reads to close at once (set by init, cleared by activate).
  bool finished() const { return finished_; }
  void advanceStep();
  std::shared_ptr<TutorialStep> currentStep() const;
  std::shared_ptr<TutorialStep> step(int number) const;
  int stepNumber() const { return step_; }
  int numSteps() const { return static_cast<int>(steps_.size()); }
  // The kind of trigger the current step has for a sprite name (0 when there is no current step; -1 when it names none).
  int trigger(const std::string& name) const;
  // Type 3 messages: the skip button's name turns the option off and advances (which collapses the tutorial); the name of a click
  // trigger advances. True when the message was taken.
  bool handleMessage(const ui::Message& m);
  // May a click at `screenPt` reach the game? True when the tutorial names no window; else the deepest window of that name's
  // subtree under the point is looked at through the kIgnore windows, and the click is allowed when it or an ancestor (up to the
  // container) has any trigger.
  bool handleMouse(ui::Window* container, gfx::PointI screenPt);
  // The retry of a deferred advance, then the sprite update.
  void update(bool softPaused) override;

  const std::string& windowName() const { return window_; }
  bool pause() const { return pause_; }
  bool repeatable() const { return repeatable_; }
  bool tryonce() const { return tryonce_; }
  int minLevel() const { return minLevel_; }
  int maxLevel() const { return maxLevel_; }
  VButton* skipButton() const { return skip_.get(); }
  TutorialSpider* spider() const { return spider_.get(); }

 private:
  // The manager of the world (it owns the tutorials and may be gone at shutdown): null then.
  TutorialManager* tutorials() const;

  std::string tutorialName_;
  int slot_;
  int minLevel_ = 0, maxLevel_ = -1;
  std::string window_;
  bool pause_ = true, repeatable_ = false, tryonce_ = false;
  std::set<int> modes_;
  std::map<int, std::shared_ptr<TutorialStep>> steps_;
  int state_ = 0;
  int step_ = 0;
  bool complete_ = true, finished_ = true, deferred_ = false;
  std::shared_ptr<VButton> skip_;
  std::shared_ptr<SpriteEffect> fade_;
  std::shared_ptr<TutorialSpider> spider_;
};

}  // namespace choco::games::greatcc
