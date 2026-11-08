-- A small stand-in for the game's ui/ledger.lua: the same child names, no art.
MakeDialog
{
	name = "ledger",
	Bitmap { x = 5, y = 5, name = "ledger_questgoals", image = "image/none" },
	Text { name = "money", x = 10, y = 10, w = 100, h = 20, label = "#0" },
	Text { name = "day", x = 10, y = 30, w = 100, h = 20, label = "" },
	Text { name = "rank", x = 10, y = 50, w = 100, h = 20, label = "" },
	Text { name = "score", x = 10, y = 70, w = 100, h = 20, label = "" },
	Text { name = "questText", x = 10, y = 90, w = 100, h = 20, label = "" },
	DookieDropper { name = "zur_factory", x = 200, y = 150, w = 79, h = 132 },
	DookieDropper { name = "cap_factory", x = 300, y = 150, w = 79, h = 132 },
	Window { name = "map_port", x = 0, y = 300, w = 50, h = 50 },
	Window { name = "port_map", x = 0, y = 300, w = 50, h = 50 },
}
