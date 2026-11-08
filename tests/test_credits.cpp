#include <doctest/doctest.h>

#include "gfx/text/TextLayout.h"
#include "ui/WindowManager.h"
#include "ui/widgets/CreditsWindow.h"

using namespace choco;

TEST_CASE("credits: a '|' line splits into the two columns, other lines are centred full width") {
  const auto c = ui::parseCredits("Left | Right\nTitle\n", "000000ff", 0, 16);
  CHECK(c.left == "Left\n\n");
  CHECK(c.right == "Right\n\n");
  CHECK(c.centre == "\nTitle\n");
  // Only the first '|' splits.
  const auto d = ui::parseCredits("a|b|c\n", "000000ff", 0, 16);
  CHECK(d.left == "a\n");
  CHECK(d.right == "b|c\n");
}

TEST_CASE("credits: [ and ] become the header colour tags, other markup passes through") {
  const auto c = ui::parseCredits("[Art] | <b>x</b>\n", "aabbccdd", 0, 16);
  CHECK(c.left == "<font color='aabbccdd'>Art</font>\n");
  CHECK(c.right == "<b>x</b>\n");
  CHECK(ui::colorHex({0, 0.5f, 1, 1}) == "007fffff");
}

TEST_CASE("credits: lines are trimmed of spaces and tabs, the last character goes, a CR before the LF is dropped on purpose (single spacing)") {
  const auto c = ui::parseCredits(" \t Name \t \r\n\r\nlast", "000000ff", 0, 16);
  CHECK(c.centre == "Name\n\nlas\n");  // "last" has no terminator, so a real character is dropped
  CHECK(c.left == "\n\n\n");
}

TEST_CASE("credits: floor(height / fontsize) blank lines are appended to every column") {
  const auto c = ui::parseCredits("x\n", "000000ff", 347, 16);  // 21 blank lines
  CHECK(c.centre == "x\n" + std::string(21, '\n'));
  CHECK(c.left == std::string(1 + 21, '\n'));
  CHECK(c.right == c.left);
  CHECK(ui::parseCredits("x\n", "000000ff", 347, 0).centre == "x\n");  // no font size: no padding
}

TEST_CASE("credits: scroll offset waits out the intro pause and then wraps every `time`") {
  CHECK(ui::creditsScrollPx(0, 3000, 20000, 400) == 0);
  CHECK(ui::creditsScrollPx(2999, 3000, 20000, 400) == 0);
  CHECK(ui::creditsScrollPx(13000, 3000, 20000, 400) == doctest::Approx(200));  // half way through the first cycle
  CHECK(ui::creditsScrollPx(3000 + 20000, 3000, 20000, 400) == 0);              // wrapped: no second intro pause
  CHECK(ui::creditsScrollPx(3000 + 20000 + 5000, 3000, 20000, 400) == doctest::Approx(100));
  CHECK(ui::creditsScrollPx(5000, 0, 0, 400) == 0);  // time 0 does not divide
}

namespace {
struct Uploader : gfx::TextureUploader {
  void* upload(const std::uint8_t*, int, int) override { return new int(0); }
  void release(void* h) override { delete static_cast<int*>(h); }
};
}  // namespace

// Note: the repo's credits files differ in line endings: Reforged assets/credits.txt and choco_assets/credits.txt are LF
// only, the Windows vanilla credits.txt is CRLF.
TEST_CASE("credits: the window loads its file, computes H once, and a missing file logs and stays empty") {
  Uploader up;
  gfx::text::TextSystem ts(nullptr, &up);
  ui::WindowManager wm;
  wm.setTextSystem(&ts);
  std::vector<std::string> logged;
  wm.setLogger([&](const std::string& l) { logged.push_back(l); });
  ui::MapStyle s;
  s.set("x", 21.0).set("y", 54.0).set("w", 458.0).set("h", 347.0).set("fontsize", 16.0).set("columngap", 10.0)
      .set("columnwidth", 224.0).set("time", 20000.0).set("intropause", 3000.0).set("file", std::string("credits.txt"));

  {
    auto w = wm.createWindow("CreditsWindow");
    w->init(s);
    CHECK(w->typeName() == "CreditsWindow");
    REQUIRE(logged.size() == 1);
    CHECK(logged[0].find("[credits] cannot open 'credits.txt'") == 0);
    CHECK(static_cast<ui::CreditsWindow&>(*w).scrollHeight() == 0);
  }
  wm.setTextFileReader([](const std::string& n) -> std::optional<std::string> {
    if (n != "credits.txt") return std::nullopt;
    return std::string("Title\nA | B\n");
  });
  auto w = wm.createWindow("CreditsWindow");
  w->init(s);
  auto& cw = static_cast<ui::CreditsWindow&>(*w);
  CHECK(cw.columns().centre == "Title\n\n" + std::string(21, '\n'));
  // H is the whole text height (the original's half is behind kCreditsScrollFullHeight): two blank lines of column A plus
  // the 21 padding lines, each one font size high.
  CHECK(ui::kCreditsScrollFullHeight);
  CHECK(cw.scrollHeight() == (2 + 21) * 16);
  CHECK(cw.scrollAt(3000 + 20000) == 0);
  CHECK(cw.scrollAt(3000 + 10000) == doctest::Approx(cw.scrollHeight() / 2.0));
  // No input handlers: nothing is consumed.
  CHECK_FALSE(w->onMouseDown(1, 1));
  CHECK_FALSE(w->onChar(ui::KeyEvent{"Escape"}));
}
