#pragma once

// VCarriable (docs/re/greatcc-machines-and-chocolates.md 3): an object the player can hold. The only thing it adds to a VObject
// is how many hands it needs. Chocolates, sauce bottles and drink cups are carriables (the chocolate and sauce slices extend
// it); the player's hand slots hold them.

#include "games/greatcc/scene/VObject.h"

namespace choco::games::greatcc {

class VCarriable : public VObject {
 public:
  explicit VCarriable(script::ScriptContext& ctx, int hands = 1, std::string name = {}, std::string type = {})
      : VObject(ctx, std::move(name), std::move(type)), hands_(hands) {}
  static std::shared_ptr<VCarriable> create(script::ScriptContext& ctx, int hands = 1) { return std::make_shared<VCarriable>(ctx, hands); }
  const char* kind() const override { return "VCarriable"; }
  int hands() const { return hands_; }

 private:
  int hands_;
};

}  // namespace choco::games::greatcc
