-- The shape of scripts/cheats_achievements.lua without its art: the window, its scroller with the inner sprite window, and the buttons.
CheatCheckStyle = { type = kToggle }
MakeDialog{
    name = "choose_dialog",
    VChooseAchievements{
        checkstyle = CheatCheckStyle,
        x = 273, y = 5, w = 415, h = 500,
        RScrollable{
            name = "scroller",
            x = 5, y = 5, w = kMax - 10, h = kMax - 75,
            VSpriteWindow{ name = "sprite_window", w = 405 },
        },
        Button{ name = "btn_acquire", x = 5, y = kMax - 75, label = "#Acquire All",
                command = function() PostMessage(CreateNamedMessage(kButtonPress, "btn_acquire")) end },
        Button{ name = "btn_release", x = 105, y = kMax - 75, label = "#Release All" },
        Button{ name = "btn_notify", x = 200, y = kMax - 75, label = "#Test Notify" },
    },
}
