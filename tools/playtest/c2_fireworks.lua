-- Chocolatier 2 only, and only in SAN FRANCISCO: the Baumeister sign quest variable set to its final stage
-- (bcsign > 5), then the port view rebuilt so sim/port.lua's Fireworks closure runs: the player's logo on the
-- Baumeister sign and five fireworks above the port (the layer slot between the planes and the ships).
-- Be in San Francisco's port view first (any loaded game).
-- Run with:  openconche --assets <choc2 assets> --run tools/playtest/c2_fireworks.lua    (F9 runs it again)
LQuest._Variables.bcsign = 6
table.insert(gCommandQueue, function() DoModal("ui/portview.lua") end)
