#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "gfx/text/TextLayout.h"
#include "ui/Window.h"

namespace choco::game::choc2 {

// Credits (Chocolatier 2), after TCredits in the Chocolatier 2 Mac binary (docs/re/choc2-map-binary.md 3.3): pages, not a
// scroll. The window shows one page of credits.txt at a time: it fades in over `fadeUp` (300 ms), holds `showTime` (4000 ms),
// fades out over `fadeDown` (500 ms), then builds the next page; after the last page it wraps to the first. The clock counts a
// 50 ms window-animation tick. Chocolatier 3's scrolling ui::CreditsWindow is a different class and is not involved.
//
// A page is the lines up to a line starting with `---`. `+text` is a header (size 40), `*text` a title (size 30), anything
// else a name (size 20); an empty line is a blank row of the name size. `a | b` splits a row into equal-width cells (width /
// cells, integer division), each with its own prefix; the row is as high as its tallest cell. Rows stack from the top and the
// whole stack is centred vertically in the window. Every cell is a centred box (flags kHAlignCenter + kVAlignCenter) whose
// font size is its row's size; all colours default to black.
//
// FACT (binary): the keys and defaults, the sizes, the timing, the layout. INFERENCE: the `file` default `credits.txt` and
// the flag value 5 for centring (a recorded decision).
class Credits : public ui::Window {
 public:
  static constexpr double kTickMs = 50;
  static constexpr int kSizeName = 20, kSizeTitle = 30, kSizeHeader = 40;

  enum class Kind { Name, Title, Header };
  struct Cell {
    std::string text;
    Kind kind = Kind::Name;
  };
  using Row = std::vector<Cell>;
  using Page = std::vector<Row>;

  explicit Credits(ui::WindowManager& wm) : Window(wm) {}
  std::string_view typeName() const override { return "Credits"; }
  void init(const ui::Style& style) override;
  bool onAnimate() override;
  void draw(ui::DrawContext& ctx) const override;
  std::string detail() const override;

  // Splits the file into pages (CRLF tolerated, empty lines kept, a trailing newline does not make an empty last page).
  static std::vector<Page> parsePages(std::string_view file);
  // The page alpha at `clockMs` into a page's life: 0 at the start, up to 1 over fadeUp, 1 until fadeUp + showTime, then back
  // down over fadeDown.
  static float pageAlpha(double clockMs, double fadeUp, double showTime, double fadeDown);
  int sizeOf(Kind k) const { return sizes_[static_cast<int>(k)]; }

  // For tests.
  std::size_t pageCount() const { return pages_.size(); }
  std::size_t pageIndex() const { return page_; }
  float alphaNow() const { return alpha_; }
  double fadeUpMs() const { return fadeUp_; }
  double showTimeMs() const { return showTime_; }
  double fadeDownMs() const { return fadeDown_; }

 private:
  struct Box {
    std::unique_ptr<gfx::text::TextLayout> layout;
    int x = 0, y = 0;
  };
  void buildPage();
  std::vector<Page> pages_;
  std::vector<Box> boxes_;
  std::size_t page_ = 0;
  double clockMs_ = 0, fadeUp_ = 300, showTime_ = 4000, fadeDown_ = 500;
  float alpha_ = 0;
  std::string face_ = "__DEFAULT";
  gfx::ColorF colorName_{0, 0, 0, 1}, colorHeader_{0, 0, 0, 1}, colorTitle_{0, 0, 0, 1};
  int sizes_[3] = {kSizeName, kSizeTitle, kSizeHeader};  // by Kind
};

}  // namespace choco::game::choc2
