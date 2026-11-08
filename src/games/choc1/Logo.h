#pragma once

// Chocolatier 1's Logo window (TLogo, exe vtable 004e8504; Init 00415a90, the redraw 004155f0 and the window Draw
// 00415410; docs/re/choc1-logo-and-hiscore.md): the company-sign composite the shipped Lua builds through
// ShowPlayerLogo / PlayerLogo (ui/logoinfo.lua) from `gSim.logo` and `gSim.companyName`, refreshed by recreating the
// window (ui/logo.lua behind the DynamicWindow named "logo" and the port's PlayerLogo).
//
// The exe renders once, into an offscreen canvas of round(350*s+0.5) x round(200*s+0.5), s = window width / 350:
// the background stretched over it, the graphic centred at ((graphicW/2+gx)*s, (graphicH/2+gy)*s) and drawn at its
// natural size times s, then the text in a (trunc(textW*s+0.5) x trunc(textH*s+0.5)) box whose top-left is
// (trunc(textX*s+0.5), trunc(textY*s+0.5)) - Init OVERWRITES the raw keys with these scaled values (0x415f84..):
// a drop copy in dropColor at +1,+1 plus another at +2,+2 when s > 0.5, then the text in fontColor. The window blits
// the canvas over its rect. Everything reads facts from the exe; see the doc.
//
// Not Chocolatier 2's LogoEditor: that one edits a different key-based spec (HSV text colour, free-space placement,
// rotation, sliders by name, docs/re/choc2-logo-and-trade.md 1.6), so nothing is shared with games/choc2. Only the
// pixel-compositing idiom of LogoRenderer is followed.

#include <cmath>
#include <string>

#include "gfx/Image.h"
#include "gfx/Texture.h"
#include "gfx/text/TextLayout.h"
#include "script/ScriptContext.h"
#include "ui/Window.h"

namespace choco::game::choc1 {

class LogoWindow : public ui::Window {
 public:
  explicit LogoWindow(ui::WindowManager& wm);

  std::string_view typeName() const override { return "Logo"; }
  void init(const ui::Style& style) override;
  void draw(ui::DrawContext& ctx) const override;
  std::string detail() const override;

  // The canvas size (Init 00415a90): s = windowW / 350; the surface is (350*s + 0.5) and (200*s + 0.5), truncated
  // (the ROUND+fistp pattern of the disassembly), so e.g. a 262-wide window gets a 262x150 canvas.
  static int canvasW(int windowW) { return static_cast<int>(windowW / 350.0 * 350.0 + 0.5); }
  static int canvasH(int windowW) { return static_cast<int>(windowW / 350.0 * 200.0 + 0.5); }
  static float scaleOf(int windowW) { return static_cast<float>(windowW) / 350.0f; }
  static int scaled(int value, float s) { return static_cast<int>(value * s + 0.5f); }

  // For tests: the composite drawn into `canvas` (bg, graphic, drop passes, main text), the exe's FUN_004155f0.
  // `assets` supplies the decoded pictures and the font cache; a missing one is skipped as TLogo skips a failed
  // image load.
  struct Keys {
    std::string background = "logo/bg1", graphic = "logo/logo1", text, font;
    int flags = 5, textX = 10, textY = 10, textW = 330, textH = 180, fontSize = 30;
    float gx = 0, gy = 0;
    gfx::ColorF fontColor{1, 1, 1, 1}, dropColor{0, 0, 0, 1};
  };
  struct Assets {
    const gfx::ImageData *background = nullptr, *graphic = nullptr;
    gfx::text::FontCache* fonts = nullptr;  // the text system's font cache; null skips the text
  };
  static void render(const Keys& k, const Assets& assets, gfx::ImageData& canvas);
  const Keys& keys() const { return keys_; }

 private:
  Keys keys_;
  gfx::ImageData backgroundImage_, graphicImage_;  // decoded once, per window
  std::shared_ptr<gfx::Texture> texture_;
};

}  // namespace choco::game::choc1
