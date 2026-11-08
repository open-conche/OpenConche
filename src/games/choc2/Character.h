#pragma once

#include <memory>
#include <string>

#include "gfx/Texture.h"
#include "ui/widgets/InputWidgets.h"

namespace choco::game::choc2 {

// Character (Chocolatier 2, docs/re/choc2-port-and-ledger.md 3 and 8.1, docs/design/choc2-port-and-ledger.md 2.4). A portrait
// of PNGs, not a sheet: the body `characters/<name>.png` (172 x 240) and, for the 45 characters that have all three,
// `<name>_displeased/_neutral/_pleased.png` faces. The body has a blank face; the face PNG is drawn over it with its top-left
// at the body's top-left (fact for 34 of 45 characters, within 2 px for most of the rest: 8.1). The faces cross-fade over
// instantly (TCharacter::SetMood 0x500c, docs/re/choc2-port-binary.md 3.2): no cross-fade. Keys read: `char`, a table of
// which `name` and `mood` (default 50) are read.
//
// FACT: displeased when mood <= kAngry (25), neutral below kDefault (50), else pleased; kHappy (70) is never read. Every
// character starts at mood 50 and so starts pleased.
class Character : public ui::BSGWindow {
 public:
  enum Face { Displeased = 0, Neutral = 1, Pleased = 2 };
  static constexpr double kAngry = 25, kDefault = 50;

  explicit Character(ui::WindowManager& wm) : BSGWindow(wm) {}
  std::string_view typeName() const override { return "Character"; }
  void postChildrenInit(const ui::Style& style) override;
  void draw(ui::DrawContext& ctx) const override;
  bool drawsOutside() const override { return false; }
  std::string detail() const override;

  // The face for a mood in 0..100.
  static Face faceFor(double mood);
  // SetCharacterMood: swaps the face at once. Does nothing without the three faces.
  void setMood(double mood);

  const std::string& characterName() const { return name_; }
  const std::string& appearance() const { return appearance_; }
  bool hasFaces() const { return faces_[0] && faces_[1] && faces_[2]; }
  int shownFace() const { return face_; }

 private:
  std::string name_;        // the table's `name`: what SetCharacterMood matches
  std::string appearance_;  // the table's `appearance` (default: name_): the pictures are characters/<appearance>
  double mood_ = 50;
  std::shared_ptr<gfx::Texture> body_, faces_[3];
  int face_ = Pleased;
};

}  // namespace choco::game::choc2
