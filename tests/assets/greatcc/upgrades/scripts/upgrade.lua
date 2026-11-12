-- A synthetic stand-in for scripts/upgrade.lua: one offer with the five looks, the picture, the texts and the price button.
MakeDialog{
	VUpgradeTab {
		name = "upgrade_tab",
		x = 0, y = 0, w = 235, h = 90,
		SelectLayer(0),
		Window { name = "look0", x = 3, y = 3, w = 200, h = 80 },
		SelectLayer(1),
		Window { name = "look1", x = 3, y = 3, w = 200, h = 80 },
		SelectLayer(2),
		Window { name = "look2", x = 3, y = 3, w = 200, h = 80 },
		SelectLayer(3),
		Window { name = "look3", x = 3, y = 3, w = 200, h = 80 },
		SelectLayer(4),
		Window { name = "look4", x = 3, y = 3, w = 200, h = 80 },
		SelectLayer(-1),
		Bitmap { name = "img_thumbnail", x = 10, y = 0 },
		Window {
			x = 0, y = 0, w = 235, h = 90, name = "upgrade_contents",
			Text { name = "txt_header", x = 72, y = 12, w = 160, h = 14 },
			Text { name = "txt_content", x = 72, y = 26, w = 160, h = 50 },
		},
		RDisableButton {
			name = "btn_buy_upgrade",
			x = 163, y = 62, w = 72, h = 28,
			Text { name = "txt_price_shadow", x = 2, y = 3, w = 72, h = 28 },
			Text { name = "txt_price", x = 0, y = 0, w = 72, h = 28 },
		},
		-- as the shipped file, the build ends on the locked layer (the overlay icons): the tab's first SetCurrentLayer(3) is then a
		-- no-op, which is what leaves the price button hidden on a locked offer
		SelectLayer(3),
		Window { name = "icon_locked", x = 10, y = 8, w = 63, h = 64 },
	},
}
