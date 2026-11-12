-- A synthetic stand-in for scripts/rewardnotify.lua: the same shape, with no art.
MakeDialog{
	name = "achievement_dialog",
	VRewardNotify {
		name = "reward_notify",
		duration = 7000,
		transition = 500,
		x = 525, y = 0, w = 275, h = 150,
		spacing = 10,
		background = {
			x = 275, y = 0, w = 283, h = 150,
			x1 = 30, y1 = 32, x2 = 37, y2 = 40,
		},
		item = {
			x = 35, y = 40, w = 230, h = 70,
			x1 = 9, y1 = 5, x2 = 0, y2 = 4,
			children = { icon = { name = "icon" } },
			text = {
				header = { x = 0, y = 0, w = 230, h = 16, font = { "none", 15 }, margin = { 18, 0, 0, 0 } },
				description = { x = 0, y = 16, w = 230, h = 54, font = { "none", 13 }, margin = { 25, 0, 0, 0 } },
			},
		},
	},
}
