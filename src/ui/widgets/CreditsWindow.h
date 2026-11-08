#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "gfx/text/TextLayout.h"
#include "ui/Window.h"

namespace choco::ui {

// The three text columns of TCredits::Init (docs/re/sweep-findings.md section 3): left, right and the
// centre column that carries lines without a '|'. Each ends every line with '\n'.
struct CreditsColumns {
  std::string left, right, centre;
};

// Parses a credits file. `headerColor` is the 8-hex "rrggbbaa" string that '[' opens as <font color='...'> (']' closes).
// Every line loses its last character, then is split at the first '|' (left/right, the centre column gets a blank line)
// or is a full-width line (blank in left and right). Lines are trimmed of spaces and tabs. floor(windowHeight/fontSize)
// blank lines follow so the text scrolls out of view.
//
// The original drops the last character of each line unconditionally, which is the '\n' its line reader leaves in
// place. That reader (`_pf_gets`) stops at '\r' or '\n', so the Mac build shows a CRLF file double-spaced (the Windows
// vanilla credits.txt has CRLF endings; Reforged's has LF). What the Windows build does is unverified. Here a CR in front
// of the '\n' is removed on purpose, so CRLF files come out single-spaced. A last line with no '\n' loses a real
// character, as in the original.
CreditsColumns parseCredits(std::string_view file, const std::string& headerColor, int windowHeight, int fontSize);

// "rrggbbaa" of a colour with each channel (int)(c * 255).
std::string colorHex(const gfx::ColorF& c);

// The original scrolls by H = floor(column text height / 2) (docs/re/sweep-findings.md section 3), so the
// last ~14 (vanilla) or ~20 (Reforged) lines never come into view. That is treated as an original bug: with this set,
// H is the whole text height and the column is fully scrolled out by the end of `time`. Set it to false to revert.
constexpr bool kCreditsScrollFullHeight = true;

// The scroll in pixels at `elapsedMs` after the first draw: nothing during `intropause`, then ((elapsed - intropause)
// mod time) * scrollHeight / time; the intro pause applies only before the first cycle. 0 when time is 0.
double creditsScrollPx(double elapsedMs, int intropause, int time, int scrollHeight);

// What the window takes from its style. Chocolatier 3 passes every one of them.
struct CreditsParams {
  int time = 0, intropause = 0, columnGap = 0, columnWidth = 0, fontSize = 16;
  std::string file, face = "__DEFAULT";
  gfx::ColorF fontColor{1, 1, 1, 1}, headerColor{1, 1, 1, 1};
};

// CreditsWindow (TCredits): the scrolling credits. No input handlers: clicks fall through.
class CreditsWindow : public Window {
 public:
  explicit CreditsWindow(WindowManager& wm) : Window(wm) {}
  std::string_view typeName() const override { return "CreditsWindow"; }
  void init(const Style& style) override;
  void draw(DrawContext& ctx) const override;
  std::string detail() const override;

  // For tests: the scroll in pixels at `elapsedMs` since the first draw.
  double scrollAt(double elapsedMs) const { return creditsScrollPx(elapsedMs, intropause_, time_, scrollHeight_); }
  int scrollHeight() const { return scrollHeight_; }
  const CreditsColumns& columns() const { return cols_; }

 protected:
  // The Chocolatier 3 keys: file, font, fontsize, fontcolor, headercolor, columnwidth, columngap, time, intropause.
  virtual CreditsParams readParams(const Style& style) const;
  // The credits file's text; null when it cannot be read. Chocolatier 1 maps its `assets/` path and appends the version line.
  virtual std::optional<std::string> loadText(const CreditsParams& p) const;
  int scrollTimeMs() const { return time_; }

 private:
  struct Column {
    std::unique_ptr<gfx::text::TextLayout> layout;
    int left = 0;
  };
  int time_ = 0, intropause_ = 0, columnGap_ = 0, columnWidth_ = 0, fontSize_ = 16;
  int scrollHeight_ = 0;
  CreditsColumns cols_;
  Column colA_, colB_, colC_;
  bool loaded_ = false;
  mutable double startMs_ = -1;  // latched on the first draw
};

// Registers CreditsWindow with the manager.
void registerCreditsWindow(WindowManager& wm);

}  // namespace choco::ui
