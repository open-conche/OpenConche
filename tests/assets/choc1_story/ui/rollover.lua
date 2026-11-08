-- Stand-in for Chocolatier 1's ui/rollover.lua: records the global the Rollover set and builds a 60x30 popup.
gSeenRollover = _rollover
gPopupBuilds = (gPopupBuilds or 0) + 1
MakeDialog{ Rectangle{ x = 0, y = 0, w = 60, h = 30 } }
