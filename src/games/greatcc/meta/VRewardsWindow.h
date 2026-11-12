#pragma once

// VRewardsWindow and VReward (docs/re/greatcc-meta-flow.md 6): the "Sweet Rewards" screen of scripts/rewardswindow.lua. Every
// `reward_*` entry of scripts/rewards.lua is a VReward, one chocolate on the table, greyed out until its achievement is earned; the
// pointer over one shows a box with the achievement's title and description. A lid (`box_top`) covers them until the screen has faded
// in, when the script calls RewardWindowReady() and the lid slides away.

#include <map>
#include <memory>
#include <string>

#include "games/greatcc/natives/ScopedNatives.h"
#include "games/greatcc/scene/TextSprite.h"
#include "games/greatcc/scene/TiledObject.h"
#include "games/greatcc/scene/VSpriteWindow.h"

namespace choco::games::greatcc {

class VReward : public VObject {
 public:
  explicit VReward(script::ScriptContext& ctx) : VObject(ctx, "", "") { setVisible(false); }
  static std::shared_ptr<VReward> create(script::ScriptContext& ctx) { return std::make_shared<VReward>(ctx); }
  const char* kind() const override { return "VReward"; }

  // The object's keys; `category` (the achievement's category; its name is the sprite's `name`); the tooltip box from the `box` table
  // (any table around), hidden, at (boxX, boxY), added to the manager the reward is in; and, for an achievement the table knows, the
  // title (headerStyle) and the description (contentStyle) as text sprites inside it, the description one header-size line down.
  void loadFromTable(const StyleChain& style) override;
  // Init: the object's, shown; an achievement not yet earned is drawn at alpha 0.2 tinted (0.2, 0.2, 0.2).
  void init() override;
  // Enter: the box is shown, fading in over 300 ms from its alpha (0 when it was hidden). Leave: it fades out over 100 ms and is
  // hidden by Update when the fade is over.
  void onMouseEnter() override;
  void onMouseLeave() override;
  void update(bool softPaused) override;

  const std::string& category() const { return category_; }
  TiledObject* box() const { return box_.get(); }
  bool boxVisible() const { return box_ && box_->visible(); }

 private:
  std::string category_;
  std::shared_ptr<TiledObject> box_;
  std::shared_ptr<SpriteEffect> effect_;
  std::shared_ptr<TextSprite> header_, content_;
};

class VRewardsWindow : public VSpriteWindow {
 public:
  VRewardsWindow(ui::WindowManager& wm, script::ScriptContext& ctx);
  std::string_view typeName() const override { return "VRewardsWindow"; }
  // The `box_top` lid, then a VReward per `reward_*` entry of the `rewards` table (loaded and initialised).
  void postChildrenInit(const ui::Style& style) override;
  std::string detail() const override;

  // Ready (the script's RewardWindowReady): the lid moves to (600, -600) in 400 ms after a second's wait.
  void ready();
  const std::map<std::string, std::shared_ptr<VReward>>& rewards() const { return rewards_; }
  const std::shared_ptr<VObject>& lid() const { return lid_; }

 private:
  ScopedNatives natives_;
  std::map<std::string, std::shared_ptr<VReward>> rewards_;
  std::shared_ptr<VObject> lid_;
};

}  // namespace choco::games::greatcc
