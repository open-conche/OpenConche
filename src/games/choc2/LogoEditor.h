#pragma once

#include <memory>
#include <string>
#include <vector>

#include "games/choc2/LogoRenderer.h"
#include "games/choc2/LogoSpec.h"
#include "script/ScriptContext.h"
#include "ui/Window.h"
#include "ui/widgets/InputWidgets.h"

namespace choco::game::choc2 {

// LogoEditor{x y w h logo logox logoy itemsAllowed} (Chocolatier 2, docs/re/choc2-logo-and-trade.md 1.2). The window holds a
// working copy of the `logo` table (a LogoSpec) and draws its 350x200 preview at (logox, logoy). The controls are the Lua's own
// child windows, found by name: this window answers their messages (a recorded design decision):
//
//   kButtonPress from       outline italic bold              -> the toggle button is put in step with the flag (see onMessage)
//   kSliderValChanged from  background graphic font          -> index into the 8 backgrounds, the graphic list, the 9 fonts
//                           textsize                         -> ts = 10 + round(v * 90)
//                           textalpha tColorH tColorS tColorV textx texty
//                           logoscale logoalpha logorotate logox logoy -> ta tch tcs tcv tx ty ls la lr lx ly = round(v * 1000)
//
// and the natives (SetLogoText, SetTextPosition, ...) edit the same copy of the table. GetLogoString hands it back as a Lua
// chunk; the Lua stores it in gSim.logo, so closing without Done leaves gSim.logo as it was. On open the sliders are set to the
// table (quietly), which the Lua cannot do: it starts the background, graphic and font sliders at 0.
//
// Keys read: logo, logox (0), logoy (0), itemsAllowed (an array of texture names).
class LogoEditor : public ui::Window {
 public:
  explicit LogoEditor(ui::WindowManager& wm) : Window(wm), images_(wm.textures()) {}
  std::string_view typeName() const override { return "LogoEditor"; }
  void init(const ui::Style& style) override;
  void postChildrenInit(const ui::Style& style) override;
  void draw(ui::DrawContext& ctx) const override;
  bool onMessage(const ui::Message& m) override;
  std::string detail() const override;

  // The LogoEditor of the top-most modal that has one; null if none is open.
  static LogoEditor* current(ui::WindowManager& wm);

  const LogoSpec& spec() const { return spec_; }
  const std::vector<std::string>& graphics() const { return graphics_; }
  int previewX() const { return logoX_; }
  int previewY() const { return logoY_; }
  // The preview texture, rendered now if the table changed since it was last drawn.
  const std::shared_ptr<gfx::Texture>& preview() const;

  // ---- the natives' effects (each re-renders the preview and moves the matching sliders) ----
  void setText(const std::string& text);
  std::string luaString() const { return spec_.toLuaString(); }
  // 0..1 arguments, stored as thousandths.
  void setTextPosition(double x, double y);
  void setGraphicPosition(double x, double y);
  // 0..255 RGB, stored as HSV thousandths. The alpha is ignored (the Lua always passes 255).
  void setTextColor(int r, int g, int b);
  void toggleOutline();
  void toggleItalic();
  void toggleBold();
  void setJustify(int flags);
  // A random background, graphic (of the 12 stock ones) and font from `rng`, in that order.
  void randomize(script::Rng& rng);

 private:
  void refresh();       // mark the preview stale
  void render() const;  // render it if stale
  void syncSliders();   // every slider to the table, quietly
  void setSlider(const char* name, float v);
  void syncButton(const char* name, bool on);
  void applySlider(const std::string& name, float v);
  int graphicIndex() const;
  int backgroundIndex() const;

  LogoSpec spec_;
  std::vector<std::string> graphics_;
  mutable LogoImages images_;
  mutable std::shared_ptr<gfx::Texture> preview_;
  mutable bool dirty_ = false;
  int logoX_ = 0, logoY_ = 0;
  bool built_ = false;
};

}  // namespace choco::game::choc2
