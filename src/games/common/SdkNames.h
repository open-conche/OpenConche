#pragma once

#include "sdk/HiscoreStore.h"
#include "sdk/Sprite.h"
#include "sdk/SpriteScript.h"
#include "sdk/TextSprite.h"
#include "sdk/Ticker.h"
#include "sdk/Users.h"

// The sdk names the shared game code uses unqualified. Nested namespaces (choco::game::choc2) find them through
// the enclosing namespace.
namespace choco::game {
using sdk::FixedStep;
using sdk::HiscoreRow;
using sdk::HiscoreStore;
using sdk::Sprite;
using sdk::SpriteContext;
using sdk::SpriteScript;
using sdk::SpriteSpec;
using sdk::TextSprite;
using sdk::Ticker;
using sdk::UserStore;
}  // namespace choco::game
