:"stop_menu"
stop menu.ogg

:"stop_env"
stop bed_a.ogg
stop bed_b.ogg

:"click"
play Sfx click.ogg

:"hum"
play Sfx hum.ogg LOOP

:"stop_win"
stop win.ogg

:"win"
"stop_win"
play Sfx win.ogg

:"menu"
"stop_menu"
play Music menu.ogg Loop

:"sting"
"stop_menu"
stop mainmenumusic.ogg
play Music sting.ogg

:"bed_a"
"stop_env"
play Environments bed_a.ogg loop

:"bed_b"
"stop_env"
play Environments bed_b.ogg,loop

:"quiet"
play sfx quiet.ogg

:"stop_music"
stop mainmenumusic.ogg
stop menu.ogg
stop sting.ogg

:"mainmenu"
play Sfx mainmenumusic.ogg
play Sfx extra.ogg

:"click"
play Sfx other.ogg
