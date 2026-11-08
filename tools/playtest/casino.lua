-- Reforged only: the vanilla game has no `las_casino` building global.
-- Las Vegas casino: unlock the port, fly there and open the slot machine selection.
-- Run with:  openconche --cheatmode --run tools/playtest/casino.lua    (F9 runs it again)
lasvegas:Unlock()
Player:SetPort("lasvegas")
ReleaseLedger()
SwapToModal("ui/portview.lua")
-- The port view builds on the next frame; the dialog opens on top of it.
QueueCommand(function()
	las_casino:EnterBuilding()
end)
