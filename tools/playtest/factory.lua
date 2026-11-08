-- Zurich chocolate factory: own it, stock the pantry, rank up and open the factory screen.
-- Run with:  openconche --cheatmode --run tools/playtest/factory.lua
Player:AddMoney(1000000)
if not zur_factory:IsOwned() then zur_factory:MarkOwned() end
for name in pairs(_AllIngredients) do
	Player:AddIngredient(name, 50)
end
if Player.rank < 3 then Player:SetRank(3) end
Player:SetPort("zurich")
ReleaseLedger()
SwapToModal("ui/portview.lua")
QueueCommand(function()
	zur_factory:EnterBuilding()
end)
