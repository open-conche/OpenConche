#pragma once

#include <string>
#include <utility>

#include "script/ScriptContext.h"
#include "ui/Window.h"

namespace choco::game {

// DynamicWindow (TDynamicWindow, docs/re/choc1-natives.md section 4; shared by Chocolatier 1 and 2): a window whose content is a
// Lua file. Building it (and every rebuild) deletes its children and runs the `contents` file with the window itself as the one
// being built. Keys read: `contents` (a path) and `init` (default true; false skips the first build, and the owner rebuilds it
// when it needs it: the ledger's tabs). The window's size comes from x y w h; after a build it only grows to its children (FitToChildren never shrinks), for a window
// made without w and h.
//
// `ontext` (Chocolatier 1's TDynamicWindow listener on message type 7, kTextEditChanged; docs/re/choc2-logo-and-trade.md 2.6): the
// window runs again on every text change of any TextEdit, for as long as it lives. With a `script` key (Chocolatier 2's buy/sell
// receipt updater: a Lua string) it runs that string; otherwise it rebuilds `contents`. The first build is not a text change:
// `script` is not run at creation (the Lua calls its callback once itself). A change that arrives while the window's own script
// or build is still running is ignored and counted. A script that edits text posts a message, which is handled after the script
// ends, so it is a further run (bounded by the script) and never a nested one.
class DynamicWindow : public ui::Window {
 public:
  // `logTag` prefixes this window's log lines ("choc2", "choc1").
  DynamicWindow(ui::WindowManager& wm, script::ScriptContext& ctx, std::string logTag)
      : Window(wm), ctx_(ctx), tag_(std::move(logTag)) {}
  std::string_view typeName() const override { return "DynamicWindow"; }
  void init(const ui::Style& style) override;
  void postChildrenInit(const ui::Style& style) override;
  std::string detail() const override;

  // Deletes the children and runs the contents file into this window. A rebuild asked for while this window's own script is
  // still running is ignored and logged (re-entering would delete the windows the script is filling); returns false then,
  // and when there is no contents file.
  bool rebuild();
  // Deletes the children without rebuilding (the ledger empties the tab bodies it hides).
  void clear();

  // One text change: runs `script` (or rebuilds). Called by the message listener; public for tests.
  void onTextChanged();

  const std::string& contents() const { return contents_; }
  const std::string& script() const { return script_; }
  bool hasOnText() const { return onText_; }
  int textRuns() const { return textRuns_; }
  int textIgnored() const { return textIgnored_; }
  bool building() const { return building_; }
  int builds() const { return builds_; }

  // UpdateDynamicContent(name) in Chocolatier 2, UpdateDynamicWindow(name) in Chocolatier 1: rebuilds every DynamicWindow called `name` in the whole modal chain, in tree order. The
  // matches are collected first and found again by id, so a rebuild that deletes another match (a nested one) is safe.
  // Returns how many were rebuilt.
  static int updateNamed(ui::WindowManager& wm, const std::string& name);

 private:
  script::ScriptContext& ctx_;
  std::string tag_;
  std::string contents_;
  bool init_ = true;
  std::string script_;
  bool onText_ = false;
  bool scriptRunning_ = false;
  bool building_ = false;
  int builds_ = 0;
  int textRuns_ = 0;
  int textIgnored_ = 0;
};

}  // namespace choco::game
