#include "games/common/Crates.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>

#include "audio/Audio.h"
#include "script/AudioBridge.h"
#include "script/Formats.h"
#include "script/LuaApi.h"
#include "ui/WindowManager.h"

namespace choco::game {

namespace {

CratesGame* gActive = nullptr;

constexpr std::array<double, CratesGame::kCount> kFactors = {0.435, 0.435, 0.435, 0.4,  0.35, 0.35, 0.35,
                                                              0.35,  0.25,  0.25,  0.25, 0.25, 0.24, 0.24,
                                                              0.24,  0.15,  0.1,   0.05, 0.0,  0.0,  0.0};

// The Chocolatier 2 guess: Chocolatier 3's classes scaled to 25 crates (4 x 0.435, 0.4, 5 x 0.35, 5 x 0.25, 3 x 0.24, 0.15,
// 0.1, 0.05, 4 x 0), sum 6.16.
constexpr std::array<double, 25> kBonusFactors = {0.435, 0.435, 0.435, 0.435, 0.4,  0.35, 0.35, 0.35, 0.35,
                                                  0.35,  0.25,  0.25,  0.25,  0.25, 0.25, 0.24, 0.24, 0.24,
                                                  0.15,  0.1,   0.05,  0.0,   0.0,  0.0,  0.0};

}  // namespace

const CratesSpec& CratesSpec::chocolatier3() {
  static const CratesSpec s = [] {
    CratesSpec c;
    c.factors.assign(kFactors.begin(), kFactors.end());
    return c;
  }();
  return s;
}

const CratesSpec& CratesSpec::bonus() {
  static const CratesSpec s = [] {
    CratesSpec c;
    c.type = "BonusGame";
    c.count = 25;
    c.scale = 0.5f;  // INFERRED: large_crate*.png is 100x94, 2x the Lua's 50x47 select_N buttons
    c.factors.assign(kBonusFactors.begin(), kBonusFactors.end());
    c.closed = "image/large_crate";
    c.open = "image/large_crate_open";
    c.mask = "image/large_crate_front";
    c.soundDir = "sfx/";
    return c;
  }();
  return s;
}

CratesGame::CratesGame(ui::WindowManager& wm, script::ScriptContext& ctx, const CratesSpec& spec)
    : Window(wm),
      ctx_(ctx),
      spec_(&spec),
      crates_(static_cast<std::size_t>(spec.count)),
      text_(static_cast<std::size_t>(spec.count)) {
  assert(spec.factors.size() == static_cast<std::size_t>(spec.count));
  gActive = this;
}

CratesGame::~CratesGame() {
  if (gActive == this) gActive = nullptr;
}

CratesGame* CratesGame::active() { return gActive; }

std::shared_ptr<gfx::Texture> CratesGame::load(const std::string& name) const {
  gfx::TextureCache* cache = manager().textures();
  if (!cache || cache->resolve(name).path.empty()) return nullptr;
  return cache->load(name);
}

void CratesGame::init(const ui::Style& style) {
  Window::init(style);
  closed_ = load(spec_->closed);
  open_ = load(spec_->open);
  mask_ = load(spec_->mask);
}

void CratesGame::postChildrenInit(const ui::Style& style) {
  Window::postChildrenInit(style);
  for (int i = 1; i <= spec_->count; ++i) {
    Crate& c = crates_[static_cast<std::size_t>(i - 1)];
    c = Crate{};
    const Window* b = find("select_" + std::to_string(i));
    if (!b) continue;  // a missing child leaves a zero rectangle
    int x = 0, y = 0;  // the button's position in this window's coordinates
    for (const Window* p = b; p && p != this; p = p->parent()) x += p->rect().x, y += p->rect().y;
    const int w = b->rect().w, h = b->rect().h;
    c.rect = {x - 20, y, w + 40, h};
    c.x = static_cast<float>(x) + static_cast<float>(w) / 2;
    c.y = static_cast<float>(y) + static_cast<float>(h) / 2;
  }
  manager().startWindowAnimation(id(), kTickMs);
}

// ---------------------------------------------------------------- the rules

const std::array<double, CratesGame::kCount>& CratesGame::prizeFactors() { return kFactors; }

std::vector<unsigned> CratesGame::baseValues(unsigned bet, const CratesSpec& spec) {
  std::vector<unsigned> v(spec.factors.size());
  for (std::size_t k = 0; k < v.size(); ++k) {
    unsigned x = static_cast<unsigned>(static_cast<double>(bet) * spec.factors[k] + 0.5);
    x = x / 100 * 100;
    if (x < 100 && spec.factors[k] > 0) x = 100;
    v[k] = x;
  }
  return v;
}

std::array<unsigned, CratesGame::kCount> CratesGame::baseValues(unsigned bet) {
  std::array<unsigned, kCount> v{};
  const auto base = baseValues(bet, CratesSpec::chocolatier3());
  std::copy(base.begin(), base.end(), v.begin());
  return v;
}

void CratesGame::shuffle(std::vector<unsigned>& v, const std::function<int()>& pick) {
  for (std::size_t k = 0; k < v.size(); ++k) {
    const std::size_t j = static_cast<std::size_t>(pick());
    if (j < v.size()) std::swap(v[k], v[j]);
  }
}

void CratesGame::shuffle(std::array<unsigned, kCount>& v, const std::function<int()>& pick) {
  std::vector<unsigned> t(v.begin(), v.end());
  shuffle(t, pick);
  std::copy(t.begin(), t.end(), v.begin());
}

float CratesGame::valueScale(double t) {
  if (t >= kFadeMs) return 1.0f;
  if (t <= kGrowMs) return static_cast<float>(t / (2 * kGrowMs) + 1.0);
  return static_cast<float>((t - kGrowMs) / (-2 * kGrowMs) + 1.5);
}

float CratesGame::whammyScale(double t, bool blank) {
  if (blank) return 0.25f;
  double u = std::fmod(t, 1000.0);
  if (u > 500) u = 1000 - u;
  return static_cast<float>(u / 1000 + 0.5);
}

unsigned CratesGame::prepare(unsigned bet) {
  auto values = baseValues(bet, *spec_);
  shuffle(values, [this] { return ctx_.rng.range(0, spec_->count - 1); });  // rand() % count
  unsigned sum = 0;
  for (std::size_t k = 0; k < values.size(); ++k) {
    Crate& c = crates_[k];
    c.value = values[k];
    c.bomb = values[k] == 0;  // derived from the value only
    sum += values[k];
  }
  prepared_ = true;
  return sum;
}

void CratesGame::sound(const char* key) const {
  if (spec_->soundDir[0] == '\0')
    script::soundEvent(ctx_, key);
  else if (ctx_.audio)
    ctx_.audio->playFile(std::string(spec_->soundDir) + key + ".ogg");
}

std::string CratesGame::dollars(unsigned v) const {
  return script::dollars(ctx_.lua.L(), static_cast<double>(v));
}

unsigned CratesGame::select(int index, bool final) {
  if (index < 1 || index > spec_->count) return 0;
  Crate& c = crates_[static_cast<std::size_t>(index - 1)];
  if (c.state != State::Waiting) return c.value;
  sound(spec_->soundClick);
  c.state = State::Opening;
  c.time = 0;
  c.final = final;
  if (c.value == 0) {
    if (!whammy_) whammy_ = load(spec_->whammy);
    if (final) c.state = State::Blank;
    sound(spec_->soundNoMoney);
  } else if (!final) {
    // StartSparkle: the burst is a particle system, not drawn yet. The origin and the colours are not used yet.
    if (ctx_.stubs.hit("StartSparkle")) ctx_.log.write("[unimplemented] crate sparkle particles");
    sound(spec_->soundMoney);
  }
  text_[static_cast<std::size_t>(index - 1)] = Text{};
  return c.value;
}

bool CratesGame::onAnimate() {
  for (Crate& c : crates_)
    if (c.state == State::Opening) c.time += kTickMs;
  return true;  // never ends by itself
}

// ---------------------------------------------------------------- drawing

void CratesGame::draw(ui::DrawContext& ctx) const {
  gfx::Renderer& r = ctx.renderer;
  const float ox = ctx.origin.x, oy = ctx.origin.y;
  auto put = [&](const gfx::Texture* tex, float cx, float cy, float alpha, float scale) {
    if (!tex || alpha <= 0) return;
    const float w = static_cast<float>(tex->width()) * scale, h = static_cast<float>(tex->height()) * scale;
    r.drawTexture(*tex, {}, {ox + cx - w / 2, oy + cy - h / 2, w, h}, {1, 1, 1, alpha * ctx.alpha}, gfx::Blend::Normal);
  };

  for (std::size_t k = 0; k < crates_.size(); ++k) {
    const Crate& c = crates_[k];
    if (!prepared_ || c.rect.empty()) continue;
    if (c.state == State::Waiting) {
      put(closed_.get(), c.x, c.y, 1.0f, spec_->scale);
      continue;
    }
    const double t = c.time;
    const float alpha = static_cast<float>(1.0 - t / kFadeMs);
    const float scale = static_cast<float>((t / 1600 + 1) * spec_->scale);
    const bool blank = c.state == State::Blank;
    if (alpha > 0 && !blank) put(open_.get(), c.x, c.y, alpha, scale);
    if (c.bomb) {
      put(whammy_.get(), c.x, c.y, 1.0f, whammyScale(t, blank));
    } else if (c.value > 0) {
      // The value text, at 16 px (opened by the player, black) or 14 px (revealed, red-brown), grown by the scale.
      const float s = valueScale(t);
      const int basePx = c.final ? 14 : 16;
      const int size = std::max(1, static_cast<int>(std::lround(static_cast<float>(basePx) * s)));
      Text& tx = text_[k];
      gfx::text::TextSystem* ts = manager().textSystem();
      if (ts && (!tx.layout || tx.size != size)) {
        gfx::text::TextStyle st;
        st.font = ts->fonts.get("fonts/fertigo.mvec");
        st.fonts = &ts->fonts;
        st.size = static_cast<float>(size);
        st.color = c.final ? gfx::ColorF{0.8f, 0.2f, 0.0f, 1.0f} : gfx::ColorF{0, 0, 0, 1};
        st.flags = gfx::text::kHAlignCenter + gfx::text::kVAlignCenter;
        tx.layout = std::make_unique<gfx::text::TextLayout>(
            gfx::text::TextLayout::build(dollars(c.value), st, c.rect.w, c.rect.h));
        tx.size = size;
      }
      // The anchoring of the scaled text inside the rectangle is not decoded: it is centred, and the font size scales.
      if (ts && tx.layout)
        tx.layout->draw(r, ts->atlas, {ox + static_cast<float>(c.rect.x), oy + static_cast<float>(c.rect.y)}, ctx.alpha);
    }
    if (alpha > 0 && !blank) put(mask_.get(), c.x, c.y, alpha, scale);
  }
}

std::string CratesGame::detail() const {
  static const char* names[] = {"waiting", "opening", "open", "blank"};
  std::string s = prepared_ ? " prepared" : "";
  for (int i = 1; i <= spec_->count; ++i) {
    const Crate& c = crate(i);
    s += " c" + std::to_string(i) + "=" + names[static_cast<int>(c.state)];
    if (c.state != State::Waiting) s += "/" + std::to_string(c.value) + "/" + std::to_string(static_cast<int>(c.time));
  }
  return s;
}

}  // namespace choco::game
