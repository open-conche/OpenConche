-- Later siblings are in front. "t1" exists twice in "box": the later one (dup) is found first.
MakeDialog
{
	name = "dlg",
	Window
	{
		name = "box", x = 10, y = 20, w = 100, h = 50,
		Text { name = "t1", label = "#first", x = 1, y = 2, w = 30, h = 8 },
		Text { name = "t1", label = "#dup" },
	},
	Window { name = "other", x = kCenter, y = -5, w = kMax },
	Button { name = "ok", label = "OK" },
}
gBuiltDepth = GetWindowDepth()
