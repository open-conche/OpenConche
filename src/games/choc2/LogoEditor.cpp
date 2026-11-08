#include "games/choc2/LogoEditor.h"

#include <algorithm>
#include <cmath>
#include <iterator>

#include "gfx/TextureCache.h"
#include "ui/DrawContext.h"
#include "ui/WindowManager.h"
#include "ui/widgets/Widgets.h"

namespace choco::game::choc2 {

namespace {

int thousandths(double p) { return static_cast<int>(std::lround(std::clamp(p, 0.0, 1.0) * 1000.0)); }

LogoEditor* findEditor(ui::Window& w) {
  if (auto* e = dynamic_cast<LogoEditor*>(&w)) return e;
  for (const auto& c : w.children())
    if (auto* e = findEditor(*c)) return e;
  return nullptr;
}

}  // namespace

LogoEditor* LogoEditor::current(ui::WindowManager& wm) {
  const auto& modals = wm.modals();
  for (auto it = modals.rbegin(); it != modals.rend(); ++it)
    if (LogoEditor* e = findEditor(**it)) return e;
  return nullptr;
}

void LogoEditor::init(const ui::Style& style) {
  Window::init(style);
  logoX_ = static_cast<int>(style.numberOr("logox", 0));
  logoY_ = static_cast<int>(style.numberOr("logoy", 0));
  if (auto logo = style.table("logo")) spec_ = LogoSpec::fromStyle(*logo);
  std::vector<std::string> items;
  if (auto allowed = style.table("itemsAllowed"))
    for (int i = 1, n = allowed->size(); i <= n; ++i)
      if (auto name = allowed->stringAt(i)) items.push_back(*name);
  graphics_ = logoGraphicList(items);
}

void LogoEditor::postChildrenInit(const ui::Style& style) {
  Window::postChildrenInit(style);
  built_ = true;
  syncSliders();
  refresh();
}

int LogoEditor::backgroundIndex() const {
  const std::string path = logoBackgroundPath(spec_.b);
  for (int i = 1; i <= kLogoBackgrounds; ++i)
    if (path == "logo/bg" + std::to_string(i)) return i - 1;
  return 0;
}

int LogoEditor::graphicIndex() const {
  const auto it = std::find(graphics_.begin(), graphics_.end(), spec_.l);
  return it == graphics_.end() ? 0 : static_cast<int>(it - graphics_.begin());
}

void LogoEditor::setSlider(const char* name, float v) {
  if (auto* s = dynamic_cast<ui::SliderWindow*>(find(name))) s->setValue(v, true);
}

void LogoEditor::syncButton(const char* name, bool on) {
  if (auto* b = dynamic_cast<ui::ButtonWindow*>(find(name))) b->setOn(on);
}

void LogoEditor::syncSliders() {
  if (!built_) return;
  setSlider("background", sliderForIndex(backgroundIndex(), kLogoBackgrounds));
  setSlider("graphic", sliderForIndex(graphicIndex(), static_cast<int>(graphics_.size())));
  setSlider("font", sliderForIndex(spec_.lf, static_cast<int>(logoFontNames().size())));
  setSlider("textsize", static_cast<float>(spec_.ts - LogoSpec::kTextMin) / (LogoSpec::kTextMax - LogoSpec::kTextMin));
  const std::pair<const char*, int> thousand[] = {{"textalpha", spec_.ta}, {"tColorH", spec_.tch}, {"tColorS", spec_.tcs},
                                                   {"tColorV", spec_.tcv},  {"textx", spec_.tx},     {"texty", spec_.ty},
                                                   {"logoscale", spec_.ls}, {"logoalpha", spec_.la}, {"logorotate", spec_.lr},
                                                   {"logox", spec_.lx},     {"logoy", spec_.ly}};
  for (const auto& [name, value] : thousand) setSlider(name, static_cast<float>(value) / 1000.0f);
}

// Natives and slider messages only mark the preview stale; it is rendered once, when it is next drawn or asked for.
void LogoEditor::refresh() { dirty_ = built_; }

void LogoEditor::render() const {
  if (!dirty_) return;
  dirty_ = false;
  gfx::TextureCache* textures = manager().textures();
  if (!textures) return;
  preview_ = textures->makeFromPixels("$logo-preview-" + std::to_string(id()), renderLogo(spec_, images_.assets(logoFonts())));
}

const std::shared_ptr<gfx::Texture>& LogoEditor::preview() const {
  render();
  return preview_;
}

void LogoEditor::applySlider(const std::string& name, float v) {
  const int k = thousandths(v);
  if (name == "background") {
    spec_.b = "bg" + std::to_string(indexForSlider(v, kLogoBackgrounds) + 1);
  } else if (name == "graphic") {
    spec_.l = graphics_[static_cast<std::size_t>(indexForSlider(v, static_cast<int>(graphics_.size())))];
  } else if (name == "font") {
    spec_.lf = indexForSlider(v, static_cast<int>(logoFontNames().size()));
  } else if (name == "textsize") {
    spec_.ts = LogoSpec::kTextMin + static_cast<int>(std::lround(std::clamp(v, 0.0f, 1.0f) * (LogoSpec::kTextMax - LogoSpec::kTextMin)));
  } else if (name == "textalpha") spec_.ta = k;
  else if (name == "tColorH") spec_.tch = k;
  else if (name == "tColorS") spec_.tcs = k;
  else if (name == "tColorV") spec_.tcv = k;
  else if (name == "textx") spec_.tx = k;
  else if (name == "texty") spec_.ty = k;
  else if (name == "logoscale") spec_.ls = k;
  else if (name == "logoalpha") spec_.la = k;
  else if (name == "logorotate") spec_.lr = k;
  else if (name == "logox") spec_.lx = k;
  else if (name == "logoy") spec_.ly = k;
}

bool LogoEditor::onMessage(const ui::Message& m) {
  if (m.type == ui::kButtonPress) {
    // A toggle button with a `command` is flipped by the button itself right after the command ran (ButtonWindow::press), and
    // the command's native has already put the button in step with the flag: so put it right again once the press is over.
    if (m.name == "outline") syncButton("outline", spec_.tfo > 0);
    else if (m.name == "italic") syncButton("italic", spec_.tfi > 0);
    else if (m.name == "bold") syncButton("bold", spec_.tfb > 0);
    return false;
  }
  if (m.type != ui::kSliderValChanged) return false;
  static const char* const kNames[] = {"background", "graphic", "font",     "textsize",  "textalpha", "tColorH",
                                       "tColorS",    "tColorV", "textx",    "texty",     "logoscale", "logoalpha",
                                       "logorotate", "logox",   "logoy"};
  if (std::find(std::begin(kNames), std::end(kNames), m.name) == std::end(kNames)) return false;
  const auto* s = dynamic_cast<const ui::SliderWindow*>(find(m.name));
  if (!s) return false;
  applySlider(m.name, s->value());
  refresh();
  return true;
}

void LogoEditor::setText(const std::string& text) {
  spec_.t = text;
  refresh();
}

void LogoEditor::setTextPosition(double x, double y) {
  spec_.tx = thousandths(x);
  spec_.ty = thousandths(y);
  setSlider("textx", static_cast<float>(spec_.tx) / 1000.0f);
  setSlider("texty", static_cast<float>(spec_.ty) / 1000.0f);
  refresh();
}

void LogoEditor::setGraphicPosition(double x, double y) {
  spec_.lx = thousandths(x);
  spec_.ly = thousandths(y);
  setSlider("logox", static_cast<float>(spec_.lx) / 1000.0f);
  setSlider("logoy", static_cast<float>(spec_.ly) / 1000.0f);
  refresh();
}

void LogoEditor::setTextColor(int r, int g, int b) {
  rgbToHsv(r, g, b, spec_.tch, spec_.tcs, spec_.tcv);
  setSlider("tColorH", static_cast<float>(spec_.tch) / 1000.0f);
  setSlider("tColorS", static_cast<float>(spec_.tcs) / 1000.0f);
  setSlider("tColorV", static_cast<float>(spec_.tcv) / 1000.0f);
  refresh();
}

void LogoEditor::toggleOutline() {
  spec_.tfo = spec_.tfo > 0 ? 0 : 1;
  syncButton("outline", spec_.tfo > 0);
  refresh();
}

void LogoEditor::toggleItalic() {
  spec_.tfi = spec_.tfi > 0 ? 0 : 1;
  syncButton("italic", spec_.tfi > 0);
  refresh();
}

void LogoEditor::toggleBold() {
  spec_.tfb = spec_.tfb > 0 ? 0 : 1;
  syncButton("bold", spec_.tfb > 0);
  refresh();
}

void LogoEditor::setJustify(int flags) {
  spec_.to = flags;
  refresh();
}

void LogoEditor::randomize(script::Rng& rng) {
  spec_.b = "bg" + std::to_string(rng.range(1, kLogoBackgrounds));
  spec_.l = "logo/logo" + std::to_string(rng.range(1, kLogoStockGraphics));
  spec_.lf = rng.range(0, static_cast<int>(logoFontNames().size()) - 1);
  syncSliders();
  refresh();
}

void LogoEditor::draw(ui::DrawContext& ctx) const {
  render();
  if (!preview_) return;
  const gfx::RectF dst{ctx.origin.x + static_cast<float>(logoX_), ctx.origin.y + static_cast<float>(logoY_),
                       static_cast<float>(LogoSpec::kCanvasW), static_cast<float>(LogoSpec::kCanvasH)};
  ctx.renderer.drawTexture(*preview_, {}, dst, {1, 1, 1, ctx.alpha}, gfx::Blend::Normal);
}

std::string LogoEditor::detail() const {
  return " preview=" + std::to_string(logoX_) + "," + std::to_string(logoY_) + " b=" + spec_.b + " l=" + spec_.l +
         " lf=" + std::to_string(spec_.lf) + " lx=" + std::to_string(spec_.lx) + " tx=" + std::to_string(spec_.tx) +
         " to=" + std::to_string(spec_.to) + " tfo=" + std::to_string(spec_.tfo) + " tfb=" + std::to_string(spec_.tfb) + " t=\"" +
         spec_.t + "\"" + (preview() ? "" : " nopreview");
}

}  // namespace choco::game::choc2
