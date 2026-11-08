#pragma once

#include <string_view>

namespace choco::game {

// The fingerprint of Reforged's bare `Player` literal (sim/player.lua) serialised before Reset/LoadGame ever ran: at the top
// level of the table `knownRecipes={}` and neither `money` nor `time` (Reset always writes both; the literal has them nil). The
// `integrity` table is NOT a discriminator: BuildSaveGameString adds one even to the bare literal. Only depth-1 keys count, so
// nested tables cannot match. Narrow on purpose: it refuses nothing else. Writing it over a healthy save lost the user's recipes.
bool isUninitialisedPlayerSave(std::string_view data);

}  // namespace choco::game
