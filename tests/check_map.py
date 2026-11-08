"""Game-windows slices 9 and 10: the world map and travel, played headless against the real Reforged game.

  --case map     A new player is taken through the opening quest dialogs to the port view, then the map is entered
                 (the ledger's port_map button is not built yet, so the swap is called
                 directly). The dump shows the MapWindow with its markers, labels, ring and one Rollover per visible port;
                 the clouds drift and an animated marker changes frame; hovering the current port shows the travel tag
                 popup, and clicking it enters the port again.
  --case travel  The same start, then Tangier is unlocked, the map is entered and Cape Town clicked. The vehicle appears,
                 moves between two dumps and leaves trail dots; gTravelActive is set; Player.time advances by the
                 route's weeks, the money is spent, and the arrival event puts the player in Cape Town's port view.
  --case kitchen A bug report from real play: from Reykjavik the test kitchen is opened and cancelled, the map entered and Zurich
                 flown to; the plane reached Zurich and never landed, and no button or Escape worked, because the arrival
                 raised a Lua error that ended the script coroutine. The cause was an engine gap: the game's Lua can index
                 strings (`("tea").name` is nil), and sim/tips.lua reads `item.category.name` on an ingredient whose category is
                 a string while an active tip names a category. The script plants such a tip and expects the landing, no Lua error,
                 and a working Escape (the pause menu) afterwards.
Run at each of --displays (e.g. "scale1,fullscreen"). Screenshots are left in the work folder to be looked at."""
import argparse, os, re, shutil, subprocess, sys

ap = argparse.ArgumentParser()
ap.add_argument("--openconche", required=True)
ap.add_argument("--assets", required=True)
ap.add_argument("--workdir", required=True)
ap.add_argument("--case", required=True, choices=["map", "travel", "kitchen"])
ap.add_argument("--displays", default="scale1")
a = ap.parse_args()
os.makedirs(a.workdir, exist_ok=True)
errors = []

DISPLAYS = {"scale1": ("--scale", "1"), "scale2": ("--scale", "2"),
            "fullscreen": ("--fullscreen", "--display-size", "2560x1440")}

# New player, then the chained opening dialogs (offer, "I Know How to Play", "Thank You", "Ok"), each clicked on its button.
START = """expect-window ui/mainmenu.lua 60000
wait 1300
click 310 345
expect-window ui/ui_entername.lua 5000
wait 600
type Tester
key Return
expect-window ui/ui_difficulty.lua 3000
wait 800
click 328 400
wait 4000
click 401 346
wait 1500
click 503 346
wait 1500
click 400 346
wait 1500
click 400 346
wait 1500
click 400 346
wait 1500
click 400 346
wait 1500
expect-window ui/portview.lua 3000
"""
# The round Map / Port button on the ledger's badge (the real port_map button).
MAP_BUTTON = "click 115 435\nexpect-window ui/mapview.lua 3000\n"

MAP_SCRIPT = START + """lua tangiers:Unlock()
@MAP_BUTTON@expect-sprite marker:zurich 1
wait 500
dump
screenshot @W@/map.png
wait 300
dump
wait 300
dump
wait 300
dump
move 400 50
wait 400
dump
screenshot @W@/popup.png
click 400 50
expect-window ui/portview.lua 3000
wait 300
dump
quit
"""

TRAVEL_SCRIPT = START + """lua capetown:Unlock()
lua T0 = Player.time; M0 = Player.money; W = Player:GetPort():GetRoute(capetown).time; F = Player.portsCost["capetown"]
@MAP_BUTTON@wait 500
move 426 270
wait 400
dump
click 426 270
expect-lua 2000 gTravelActive
expect-sprite vehicle 1 2000
wait 1200
dump
screenshot @W@/flight.png
wait 600
dump
expect-sprite dot 5 1000
expect-lua 8000 Player.portName == "capetown" and not gTravelActive
expect-window ui/portview.lua 3000
expect-lua 1000 Player.time == T0 + W
expect-lua 1000 F > 0 and Player.money == M0 - F
dump
quit
"""


KITCHEN_SCRIPT = START + """lua zur_factory:MarkOwned()
lua reykjavik:Unlock()
lua reykjavik:EnterPort()
wait 1500
click 540 230
wait 1500
click 400 346
wait 1500
click 400 346
wait 1500
click 400 346
wait 1500
click 300 345
expect-window ui/ui_kitchen.lua 5000
wait 1000
click 700 30
expect-dump 5000 modal depth 2,
click 115 435
expect-window ui/mapview.lua 3000
wait 500
lua Player.activeTips = { { port = "zurich", category = "bar", modifier = 1.2, endTime = Player.time + 50 } }
lua zurich:OnClick()
expect-lua 2000 gTravelActive
expect-sprite vehicle 1 2000
expect-lua 12000 Player.portName == "zurich" and not gTravelActive
expect-window ui/portview.lua 3000
lua P = Player:GetPort()
expect-lua 1000 P.name == "zurich" and Tips.GetPriceModifier("milk", "zurich") == 1.0
key Escape
wait 1500
dump
screenshot @W@/after.png
quit
"""


def run(name, display, text):
    work = os.path.join(a.workdir, name)
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    script = os.path.join(work, "t.script")
    with open(script, "w") as f:
        f.write(text.replace("@W@", work))
    r = subprocess.run([a.openconche, "--headless", *DISPLAYS[display], "--assets", a.assets, "--userdir",
                        os.path.join(work, "user"), "--script", script],
                       cwd=work, capture_output=True, text=True, timeout=1200)
    open(os.path.join(work, "out.txt"), "w").write(r.stdout + r.stderr)
    return r, work


def clean(tag, r):
    if r.returncode != 0 or "[lua-error]" in r.stdout or "FAIL" in r.stdout:
        errors.append("%s: exit %d, tail %r" % (tag, r.returncode, r.stdout[-600:]))
        return False
    return True


def dumps(out):
    return [d for d in re.split(r"(?=\[windows\] modal depth)", out) if d.startswith("[windows]")]


def sprite_pos(dump, name):
    m = re.search(r"sprite %s layer=\d+ pos=(-?[\d.]+),(-?[\d.]+)" % re.escape(name), dump)
    return (float(m.group(1)), float(m.group(2))) if m else None


for display in a.displays.split(","):
    tag = "%s/%s" % (a.case, display)
    if a.case == "map":
        r, work = run("map_" + display, display, MAP_SCRIPT.replace("@MAP_BUTTON@", MAP_BUTTON))
        if not clean(tag, r):
            continue
        d = dumps(r.stdout)
        if len(d) < 6:
            errors.append("%s: expected 6 dumps, got %d" % (tag, len(d)))
            continue
        first = d[0]
        n_roll = len(re.findall(r'Rollover "" id=\d+ rect=\S+ contents="\w+:MapRolloverPopup\(\)" click="\w+:OnClick\(\)"', first))
        if n_roll < 10:
            errors.append("%s: only %d port rollovers" % (tag, n_roll))
        for needle in ("MapWindow", "sprite bg layer=0", "sprite clouds layer=11", "sprite marker:zurich layer=2",
                       "sprite label:zurich layer=3", "sprite label-shadow:zurich layer=-1", "sprite ring layer=0",
                       "sprite marker:tangiers layer=2"):
            if needle not in first:
                errors.append("%s: no '%s' in the map dump" % (tag, needle))
        clouds = [sprite_pos(x, "clouds") for x in d[:4]]
        if len(set(clouds)) < 4:
            errors.append("%s: the clouds do not drift: %r" % (tag, clouds))
        frames = {re.search(r"sprite marker:tangiers layer=2 [^\n]*frame=(\d+)/", x).group(1) for x in d[:4]}
        if len(frames) < 2:
            errors.append("%s: the 'new' marker does not animate (frames %r)" % (tag, frames))
        popup = d[4]
        if "PopupWindow" not in popup or "image/traveltag" not in popup:
            errors.append("%s: no travel tag popup while hovering the current port" % tag)
        if not os.path.getsize(os.path.join(work, "map.png")) > 100000:
            errors.append("%s: map.png looks empty" % tag)
        if "MapWindow" in d[5]:
            errors.append("%s: the map is still there after entering the current port" % tag)
    elif a.case == "kitchen":
        r, work = run("kitchen_" + display, display, KITCHEN_SCRIPT)
        if not clean(tag, r):
            continue
        d = dumps(r.stdout)
        if not d or "ui/ui_pause.lua" not in d[-1]:
            errors.append("%s: Escape did not open the pause menu after landing" % tag)
    else:
        r, work = run("travel_" + display, display, TRAVEL_SCRIPT.replace("@MAP_BUTTON@", MAP_BUTTON))
        if not clean(tag, r):
            continue
        d = dumps(r.stdout)
        if len(d) < 4:
            errors.append("%s: expected 4 dumps, got %d" % (tag, len(d)))
            continue
        popup = d[0]
        if "image/traveltag" not in popup:
            errors.append("%s: no travel tag popup over Cape Town" % tag)
        p1, p2 = sprite_pos(d[1], "vehicle"), sprite_pos(d[2], "vehicle")
        if not p1 or not p2 or p1 == p2:
            errors.append("%s: the vehicle did not move between dumps: %r %r" % (tag, p1, p2))
        t = [re.search(r"travel t=([\d.]+)", x) for x in (d[1], d[2])]
        if not all(t) or not float(t[1].group(1)) > float(t[0].group(1)) > 0:
            errors.append("%s: travel t did not advance: %r" % (tag, [x and x.group(1) for x in t]))
        dots1, dots2 = (len(re.findall(r"sprite dot ", x)) for x in (d[1], d[2]))
        if not 0 < dots1 < dots2:
            errors.append("%s: trail dots %d then %d" % (tag, dots1, dots2))
        if "MapWindow" in d[3]:
            errors.append("%s: the map survived the arrival" % tag)
        if not os.path.getsize(os.path.join(work, "flight.png")) > 100000:
            errors.append("%s: flight.png looks empty" % tag)

if errors:
    print("FAIL:\n  " + "\n  ".join(errors))
    sys.exit(1)
print("ok: %s at %s" % (a.case, a.displays))
