#pragma once

// The globals GREATCC's animation scripts (the Lua text inside `.anm` files) call beyond the engine's own set
// (docs/re/greatcc-sprites-and-zones.md 1.3, docs/re/greatcc-boot-and-natives.md 2.1). They are added to a sprite's own Lua
// state, before its first play(), by whoever owns the sprite:
//   getdelay()            VSprite::GetDelay: the sprite's frame delay in ms (default 60), as an unsigned number.
//   playsound(file, loop) VSprite::NewScript's binding of VSoundManager::PlaySFX (group 1000, no dedupe).
//   unlock(), drop()      added only to the named children of a press or infuser (VPress/VInfuse::Init): each runs a
//                         callback the machine supplies.

#include <functional>

#include "sdk/SpriteScript.h"
#include "script/ScriptContext.h"

namespace choco::games::greatcc {

// getdelay and playsound. `delayMs` supplies the sprite's current delay (the scene's VSprite keeps it); `ctx` must outlive
// the script, and the world is fetched from it on each playsound call (it dies before the window tree).
void addSpriteScriptNatives(sdk::SpriteScript& script, script::ScriptContext& ctx, std::function<unsigned()> delayMs);

// A no-argument global that runs `fn`: `unlock` and `drop`. The machine decides which child scripts get which.
void addMachineCallback(sdk::SpriteScript& script, const std::string& name, std::function<void()> fn);

}  // namespace choco::games::greatcc
