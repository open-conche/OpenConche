-- Money, every port, every recipe and every ingredient: the world is open.
-- Run with:  openconche --cheatmode --run tools/playtest/rich.lua
Player:AddMoney(10000000)
for _, port in pairs(_AllPorts) do
	port:Unlock()
end
for _, prod in pairs(_AllProducts) do
	if prod.category and prod.category.name ~= "user" then prod:Unlock() end
end
for _, ing in pairs(_AllIngredients) do
	ing:Unlock()
end
DebugOut("RUN", "rich.lua: $10m, all ports, recipes and ingredients unlocked")
