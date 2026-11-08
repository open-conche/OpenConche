-- Reforged only: Free Play is Player:LoadGame("free") / Player.mode there; the vanilla game has no such API.
-- Free Play: it needs a saved Story game, so save the current one, switch to the Free Play sidecar and open its map.
-- Run with:  openconche --cheatmode --run tools/playtest/freeplay.lua
-- (To use the main menu's Free Play button instead, run just the Player:SaveGame() line, then Main Menu from the pause menu.)
Player:SaveGame()
if Player:LoadGame("free") then
	ReleaseLedger()
	SwapToModal("ui/mapview.lua")
else
	DebugOut("RUN", "freeplay.lua: Free Play could not be loaded")
end
