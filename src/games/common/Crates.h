#pragma once

#include <array>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "gfx/Texture.h"
#include "gfx/Types.h"
#include "gfx/text/TextLayout.h"
#include "script/ScriptContext.h"
#include "ui/Window.h"

namespace choco::game {

// The crates bonus game's window (TBonusGame, Lua type `CratesGame`; docs/re/game-windows-questions.md section 6 and
// docs/re/ledger-and-game-natives.md 10.1). It reads no style keys. Its children `select_1` .. `select_21` (the Lua's
// invisible buttons) give the 21 crate slots: each crate is centred on its button's centre, and the text rectangle is
// the button's rectangle widened by 20 px on each side. A 50 ms window animation (on the owning modal's clock, so it
// stops while a dialog covers the modal) adds 50 ms to the time of every crate that is opening.
//
// PrepareGame/SelectPrize are the natives' targets: prepare() computes and shuffles the spec's values (21 for Chocolatier 3), select() opens one.
// The sparkle particle burst is not drawn (a follow-up); the sound events are logged stubs.
//
// What differs between Chocolatier 3's CratesGame and Chocolatier 2's BonusGame is a CratesSpec: the Lua type name, the
// number of crates, the prize factor table, the art and the sound route. `chocolatier3()` is today's
// values and is the default, so Chocolatier 3 is unchanged (docs/design/choc2-minigames.md 2.6).
struct CratesSpec {
  const char* type = "CratesGame";
  int count = 21;
  float scale = 0.4f;  // the closed and open crate art, drawn at this scale (Chocolatier 2: 0.5, inferred)
  std::vector<double> factors;  // slot order before the shuffle, count entries
  const char* closed = "image/box_closed";
  const char* open = "image/box_open";
  const char* mask = "image/box_open_mask";
  const char* whammy = "image/whammy_devil";
  // Empty: SoundEvent(<key>) as Chocolatier 3 does. Otherwise the sounds are files, <soundDir><name>.ogg, played as
  // Chocolatier 2's PlaySound does.
  const char* soundDir = "";
  const char* soundClick = "crate_click";
  const char* soundMoney = "crate_with_money";
  const char* soundNoMoney = "crate_no_money";

  static const CratesSpec& chocolatier3();
  // Chocolatier 2's BonusGame: 25 crates. The factors are a GUESS: Chocolatier 3's table scaled by 25/21 per
  // value class, with four whammies (the original's table is in the packed Chocotwo.exe).
  static const CratesSpec& bonus();
};

class CratesGame : public ui::Window {
 public:
  static constexpr int kCount = 21;  // Chocolatier 3's; the window's own count is spec().count
  static constexpr double kTickMs = 50;
  static constexpr double kGrowMs = 400;  // GROW_TIME
  static constexpr double kFadeMs = 800;  // the open box fades out over this long
  enum class State { Waiting = 0, Opening = 1, Open = 2, Blank = 3 };  // Open is never assigned (as in the original)

  CratesGame(ui::WindowManager& wm, script::ScriptContext& ctx, const CratesSpec& spec = CratesSpec::chocolatier3());
  ~CratesGame() override;
  std::string_view typeName() const override { return spec_->type; }
  const CratesSpec& spec() const { return *spec_; }
  void init(const ui::Style& style) override;
  void postChildrenInit(const ui::Style& style) override;
  void draw(ui::DrawContext& ctx) const override;
  bool onAnimate() override;
  std::string detail() const override;

  // PrepareGame(bet): writes the shuffled values of the 21 crates and returns their sum, nothing else (as the original PrepareGame:
  // states and times are reset only when the window is built, in postChildrenInit).
  unsigned prepare(unsigned bet);
  // SelectPrize(index, final): index is 1-based. Opens a waiting crate; returns the value either way (0 when out of range).
  unsigned select(int index, bool final);

  // The window that PrepareGame and SelectPrize act on: the live one (the original registered the globals in its
  // constructor and cleared them in its destructor). Null when none exists.
  static CratesGame* active();

  // ---- the pure rules, public for tests ----
  // The factor table in slot order before the shuffle; sums to 5.125 and has three zeros.
  static const std::array<double, kCount>& prizeFactors();
  // The 21 rounded values in slot order, before the shuffle: (uint)(bet*f + 0.5), rounded down to hundreds, at least 100
  // when the factor is above 0.
  static std::array<unsigned, kCount> baseValues(unsigned bet);
  // The same for any spec.
  static std::vector<unsigned> baseValues(unsigned bet, const CratesSpec& spec);
  // The single-pass shuffle: for k in 0..20, swap v[k] with v[pick()], pick() being rand() % 21.
  static void shuffle(std::array<unsigned, kCount>& v, const std::function<int()>& pick);
  // Any count: pick() is rand() % count.
  static void shuffle(std::vector<unsigned>& v, const std::function<int()>& pick);
  // The text scale of an opened crate's value at time t (1.0 -> 1.5 -> 1.0 over 800 ms, then 1.0).
  static float valueScale(double t);
  // The whammy's pulse scale (a 0.5 .. 1.0 triangle wave of period 1000 ms); 0.25 for a blank crate.
  static float whammyScale(double t, bool blank);

  // ---- for tests and the dump ----
  struct Crate {
    State state = State::Waiting;
    unsigned value = 0;
    double time = 0;
    bool final = false;  // opened by the reveal: small red text
    bool bomb = false;
    float x = 0, y = 0;  // centre, in the window's coordinates
    gfx::RectI rect;     // the widened text rectangle
  };
  const Crate& crate(int index) const { return crates_[static_cast<std::size_t>(index - 1)]; }
  int count() const { return spec_->count; }
  bool prepared() const { return prepared_; }

 private:
  void sound(const char* key) const;
  std::shared_ptr<gfx::Texture> load(const std::string& name) const;
  std::string dollars(unsigned v) const;

  script::ScriptContext& ctx_;
  const CratesSpec* spec_;
  std::vector<Crate> crates_;
  bool prepared_ = false;
  std::shared_ptr<gfx::Texture> closed_, open_, mask_, whammy_;
  // The value text built for each crate, and the pixel size it was built at (rebuilt when the size changes).
  struct Text {
    std::unique_ptr<gfx::text::TextLayout> layout;
    int size = 0;
  };
  mutable std::vector<Text> text_;
};

}  // namespace choco::game
