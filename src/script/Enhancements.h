#pragma once

#include <string>
#include <string_view>

namespace choco::script {

class ScriptHost;
struct ScriptContext;

// Enhancements (docs/design/enhancements.md sections 3 and 4). When the config key `enhancements` is "1" and the game has
// an overlay, loads lua/enhance.lua and the game's lua/enhance_<id>.lua, registers the Enhance* natives and installs the two window-manager hooks. Anything else installs nothing and registers nothing.
// Logs `[enhancements] on (<id>)` or `[enhancements] off`. An overlay file that fails to load or run is logged as
// `[enhancements] <error>`, the Enhance global and natives are removed, no hook is installed, and the boot goes on as
// if the switch were off.
void bootEnhancements(ScriptContext& ctx);

// The loading half of bootEnhancements with the overlay sources given: natives, the two chunks, then the hooks, or the
// off state when a chunk fails. The switch and the game id lookup are the caller's.
void loadOverlay(ScriptContext& ctx, const std::string& id, std::string_view shared, std::string_view gameCode);

// Removes the hooks (the host is going away).
void shutdownEnhancements(ScriptContext& ctx);

}  // namespace choco::script
