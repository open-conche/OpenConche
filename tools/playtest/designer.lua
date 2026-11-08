-- Recipe designer: every ingredient unlocked and in the pantry, then the test kitchen opens (press its design button).
-- Run with:  openconche --cheatmode --run tools/playtest/designer.lua
Player:AddMoney(100000)
for name, ing in pairs(_AllIngredients) do
	ing:Unlock()
	Player:AddIngredient(name, 20)
end
QueueCommand(function()
	DisplayDialog { "ui/ui_kitchen.lua" }
end)
