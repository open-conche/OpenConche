-- A synthetic stand-in for scripts/prelevel.lua: the window, the list with its slider and buttons, the three category buttons.
MakeDialog{
	name = "prelevel_dialog",
	Window{
		name = "prelevel_container", x = 0, y = 0, w = 800, h = 600,
		VPrelevelWindow {
			name = "prelevel_window", x = 0, y = 0, w = 800, h = 600,
			VUpgradeList {
				name = "upgrade_list", x = 62, y = 152, w = 261, h = 277, tick = 43,
				Button { name = "slider_start", x = 235, y = 0, w = 25, h = 22 },
				Button { name = "slider_end", x = 235, y = 255, w = 25, h = 22 },
				except = {"slider_end", "slider_start"},
				TSlider { name = "upgrade_slider", x = 235, y = 22, w = 25, h = 233 },
			},
			RDisableButton { name = "toggle_upgrades_aesthetic", type = kRadio, x = 51, y = 77, w = 100, h = 60 },
			RDisableButton { name = "toggle_upgrades_gameplay", type = kRadio, x = 138, y = 77, w = 100, h = 60 },
			RDisableButton { name = "toggle_upgrades_player", type = kRadio, x = 225, y = 77, w = 100, h = 60 },
		},
	},
}
