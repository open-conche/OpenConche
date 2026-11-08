-- Chocolatier 1 only: the ending screen through the shipped award path - the LAST medal ("master") snapshots
-- gSim.mcWeeks/mcMoney and opens ui/reward_finale.lua: the Finale window's chocolate rain over the main menu
-- backdrop, the medal picture, the three texts and the close button, with the win fanfare.
-- Be in a port view first (a loaded story game; the master "score" text you see is from the current game state).
-- Run with:  openconche --assets <choc1 assets> --run tools/playtest/c1_finale.lua    (F9 runs it again)
Simulator.AwardMedal(gSim, table.getn(Simulator.MedalData))
