"""The Great Chocolate Chase (docs/design/greatcc.md slice 8): the world map on the real scripts.

Runs `--headless --userdir <scratch> --seed 1` four times and asserts, per run: exit 0; no [lua-error] / [engine-error] /
[native-error] / [content]; the [unimplemented] and [unimplemented-type] sets are empty; the only texture the scripts name that the install does not ship is ui/story/btn_trim_left.

Run "story": a new player through Story Mode; the map's intro dialog; OK; the Iquitos pin opens with ten globes (globe 1 pink and
current, globe 2 grey and locked); hovering globe 1 shows the score box (level 1, best, goal $100, mastery $160) and leaving
fades it out; globe 2 is a hover target whose click does nothing; clicking globe 1 swaps to scripts/game.lua (the loading
dialog on top, then the shop) and the current level is 1.
Run "arrival": a player who has completed level 10 and has the map-transition flag set; after the intro the dot trail to
Grytviken plays (13 dots, 100 ms apart), the Grytviken pin appears, glows and opens; Play then loads the game.
Run "endless": Endless Sweets Mode through a new player: the header says so, the pins are closed, the pin is a hover target and
a click on it asks for a difficulty; Back stays on the map; Hard sets the mode to 3 and swaps to scripts/game.lua.
Run "cheat": DoCheatMap(5, 12) moves the map to Grytviken with levels 11 and 12 complete; DoCheatMap(3, ...) pays the open
location; both return the cheat window's texts.
The screenshots are 800x600 PNGs; the pixels the scripts name (the pink globe, the score box) are checked."""
import argparse, os, re, shutil, struct, subprocess, sys, zlib

ap = argparse.ArgumentParser()
ap.add_argument("--openconche", required=True)
ap.add_argument("--assets", required=True)
ap.add_argument("--workdir", required=True)
a = ap.parse_args()
shutil.rmtree(a.workdir, ignore_errors=True)
os.makedirs(a.workdir)
errors = []

# Shipped art the scripts name that the install does not ship (the original shows nothing there as well): the Enter Name
# dialog's trim, and the shop's menu overlay and full-screen dialog backdrop (game.lua, hud.lua).
NAME_ART = {"ui/story/btn_trim_left"}
GAME_ART = {"ui/button/hud_menu_overlay", "ui/fullscreendialog"}
GAME_TYPES = set()
GAME_CALLS = set()


def run(tag, script_text, allow_calls, allow_types, missing, shots=()):
    user = os.path.join(a.workdir, "user_" + tag)
    log = os.path.join(a.workdir, tag + ".log")
    script = os.path.join(a.workdir, tag + ".script")
    paths = {s: os.path.join(a.workdir, "%s_%s.png" % (tag, s)) for s in shots}
    with open(script, "w") as f:
        f.write(script_text % paths if "%(" in script_text else script_text)
    p = subprocess.run([a.openconche, "--headless", "--cheatmode", "--assets", a.assets, "--userdir", user, "--seed", "1", "--script", script,
                        "--log", log], capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=240)
    out = (open(log, encoding="utf-8", errors="replace").read() if os.path.exists(log) else "") + "\n" + p.stdout + "\n" + p.stderr
    if p.returncode != 0:
        errors.append("%s: exit status %d" % (tag, p.returncode))
    for t in ("[lua-error]", "[engine-error]", "[native-error]", "[content]", "[script] FAIL", "[expect-lua]"):
        if t in out:
            errors.append("%s: %s in the output: %s" % (tag, t, next(l for l in out.split("\n") if t in l)))
    seen = set(re.findall(r"^(?:\[\+\d+ms\] )?\[unimplemented\] (\S+)", out, re.M))
    if seen != allow_calls:
        errors.append("%s: [unimplemented] set differs: extra %s, missing %s" % (tag, sorted(seen - allow_calls), sorted(allow_calls - seen)))
    types = set(re.findall(r"^(?:\[\+\d+ms\] )?\[unimplemented-type\] (\S+)", out, re.M))
    if types != allow_types:
        errors.append("%s: [unimplemented-type] set differs: extra %s, missing %s" % (tag, sorted(types - allow_types), sorted(allow_types - types)))
    gone = set(re.findall(r"^(?:\[\+\d+ms\] )?\[gfx\] texture '([^']+)' not found", out, re.M))
    if gone != missing:
        errors.append("%s: missing-texture set differs: %s (expected %s)" % (tag, sorted(gone), sorted(missing)))
    dumps = [d for d in re.split(r"^(?=\[windows\] )", p.stdout, flags=re.M) if d.startswith("[windows] ")]
    return out, dumps, paths


def modals(d):
    return re.findall(r'^  Modal "([^"]*)" .*script="([^"]*)"', d, re.M)


def top(d):
    m = modals(d)
    return m[-1][1] if m else None


def mapline(d):
    m = re.search(r'^\s*VWorldMap "world_map_window" id=\d+ rect=0,0,800,600 sprites=(\d+) pins=(\d+) open=(\S*) trail=(\S*) hover=(\S*)$', d, re.M)
    if not m:
        return None
    return {"sprites": int(m.group(1)), "pins": int(m.group(2)), "open": m.group(3), "trail": m.group(4), "hover": m.group(5)}


def png(path):
    """(width, height, channels, rows of bytes) of an 8-bit RGB or RGBA PNG, or None."""
    if not os.path.exists(path):
        errors.append("no screenshot %s" % os.path.basename(path))
        return None
    raw = open(path, "rb").read()
    if raw[:8] != b"\x89PNG\r\n\x1a\n":
        errors.append("%s is not a PNG" % os.path.basename(path))
        return None
    pos, idat, w, h, ct = 8, b"", 0, 0, 0
    while pos < len(raw):
        n, kind = struct.unpack(">I4s", raw[pos:pos + 8])
        body = raw[pos + 8:pos + 8 + n]
        if kind == b"IHDR":
            w, h, depth, ct = struct.unpack(">IIBB", body[:10])
        elif kind == b"IDAT":
            idat += body
        pos += 12 + n
    if (w, h) != (800, 600):
        errors.append("%s is %dx%d, not 800x600" % (os.path.basename(path), w, h))
        return None
    ch = {2: 3, 6: 4}.get(ct)
    if ch is None:
        errors.append("%s has colour type %d" % (os.path.basename(path), ct))
        return None
    data = zlib.decompress(idat)
    stride = w * ch
    rows, prev = [], bytearray(stride)
    for y in range(h):
        f = data[y * (stride + 1)]
        cur = bytearray(data[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            left = cur[i - ch] if i >= ch else 0
            up = prev[i]
            ul = prev[i - ch] if i >= ch else 0
            if f == 1:
                cur[i] = (cur[i] + left) & 255
            elif f == 2:
                cur[i] = (cur[i] + up) & 255
            elif f == 3:
                cur[i] = (cur[i] + ((left + up) >> 1)) & 255
            elif f == 4:
                pa, pb, pc = abs(up - ul), abs(left - ul), abs(left + up - 2 * ul)
                pr = left if pa <= pb and pa <= pc else (up if pb <= pc else ul)
                cur[i] = (cur[i] + pr) & 255
        rows.append(cur)
        prev = cur
    return w, h, ch, rows


def px(img, x, y):
    return tuple(img[3][y][x * img[2]:x * img[2] + 3])


def region_diff(a_img, b_img, x0, y0, x1, y1):
    """Number of pixels in the box that differ by more than 24 in some channel."""
    n = 0
    for y in range(y0, y1):
        for x in range(x0, x1):
            if max(abs(p - q) for p, q in zip(px(a_img, x, y), px(b_img, x, y))) > 24:
                n += 1
    return n


def pinkish(c):
    return c[0] > 200 and c[2] > c[1] + 30


def greyish(c):
    return abs(c[0] - c[1]) < 14 and abs(c[1] - c[2]) < 14


def expect(cond, msg):
    if not cond:
        errors.append(msg)


# Where the pieces of the first pin are (the pin at (144,361), a globe at its offset minus (67,125)).
GLOBE1 = (150, 258)
GLOBE1_EDGE = (144, 252)  # a pixel of the pink ball away from its number
GLOBE2 = (179, 270)
GLOBE2_EDGE = (176, 262)
BOX = (222, 305, 338, 398)  # the score box beside the pin

# -------------------------------------------------------------------------------------------------------------------- story
text = """expect-window scripts/mainmenu.lua 20000
wait 1000
click 333 447
wait 1500
type Zed
wait 100
click 310 368
wait 3500
dump
screenshot %(intro)s
click 625 488
wait 3000
dump
screenshot %(map)s
move 150 258
wait 700
dump
screenshot %(hover1)s
move 190 262
wait 1500
dump
screenshot %(hover2)s
click 179 270
wait 500
dump
move 150 258
wait 100
click 150 258
wait 4000
dump
screenshot %(game)s
expect-lua 100 GetCurrentLevel() == 1
"""
out, d, paths = run("story", text, GAME_CALLS, GAME_TYPES, NAME_ART | GAME_ART, ("intro", "map", "hover1", "hover2", "game"))
if len(d) != 6:
    errors.append("story: expected 6 window dumps, got %d" % len(d))
else:
    expect(top(d[0]) == "scripts/intro.lua", "story: the intro dialog is not on top after the first run's name")
    expect('script="scripts/worldmap.lua"' in d[0], "story: the map is not under the intro")
    expect(top(d[1]) == "scripts/worldmap.lua", "story: OK did not close the intro")
    m = mapline(d[1])
    expect(m is not None, "story: no VWorldMap line in the dump")
    if m:
        expect(m["pins"] == 6 and m["sprites"] == 6, "story: the map should hold six pins: %s" % m)
        expect(m["open"] == "iquitos", "story: Iquitos is not the open pin: %s" % m)
        expect(m["trail"] == "", "story: a trail is playing on a new game")
    m2, m3, m4 = mapline(d[2]), mapline(d[3]), mapline(d[4])
    expect(m2 is not None and m2["hover"] == "level_btn_01", "story: globe 1 is not the hover target: %s" % m2)
    expect(m3 is not None and m3["hover"] == "level_btn_02", "story: the locked globe 2 is not a hover target: %s" % m3)
    expect(top(d[4]) == "scripts/worldmap.lua", "story: a click on the locked globe left the map")
    expect(m4 is not None and m4["open"] == "iquitos", "story: the locked click changed the pins")
    mods = [s for _, s in modals(d[5])]
    expect("scripts/game.lua" in mods and mods[-1] in ("scripts/game.lua", "scripts/loading.lua", "scripts/plotwindow.lua"),
           "story: globe 1 did not swap to game.lua (with the loading dialog or the level's diary on top): %s" % mods)
    expect("scripts/worldmap.lua" not in mods, "story: the map is still in the modal chain after the swap")
imgs = {k: png(v) for k, v in paths.items()}
if all(imgs.values()):
    expect(pinkish(px(imgs["map"], *GLOBE1_EDGE)), "story: globe 1 is not pink: %s" % (px(imgs["map"], *GLOBE1_EDGE),))
    expect(greyish(px(imgs["map"], *GLOBE2_EDGE)), "story: globe 2 is not grey: %s" % (px(imgs["map"], *GLOBE2_EDGE),))
    box_hover = region_diff(imgs["map"], imgs["hover1"], *BOX)
    expect(box_hover > 4000, "story: the score box did not appear on hover (%d pixels changed)" % box_hover)
    c = px(imgs["hover1"], 240, 330)
    expect(c[0] > c[1] + 60 and c[0] < 200, "story: the score box is not the dark red panel: %s" % (c,))
    gone = region_diff(imgs["map"], imgs["hover2"], *BOX)
    expect(gone < 400, "story: the score box is still showing after leaving globe 1 (%d pixels differ from the bare map)" % gone)
    expect(region_diff(imgs["map"], imgs["intro"], 100, 100, 700, 400) > 20000, "story: the intro screenshot looks like the bare map")

# ------------------------------------------------------------------------------------------------------------------ arrival
text = """expect-window scripts/mainmenu.lua 20000
wait 800
lua CreateNewUser('Zed')
lua SetManagedValue('highest_level', 10, false)
lua SetManagedValue('current_level', 11, false)
lua SetManagedValue('do_map_transition', 1, false)
wait 100
click 333 447
wait 3500
click 625 488
wait 300
dump
wait 600
dump
screenshot %(trail)s
wait 3500
dump
screenshot %(open)s
wait 1500
dump
click 722 536
wait 4000
dump
expect-lua 100 GetCurrentLevel() == 11
"""
# (level 11's customers include the yachtsman, whose angry-idle sheet names `idle_angry.png`; the install ships `idle_angry.jpg`)
out, d, paths = run("arrival", text, GAME_CALLS, GAME_TYPES, GAME_ART | {"idle_angry.png"}, ("trail", "open"))
if len(d) != 5:
    errors.append("arrival: expected 5 window dumps, got %d" % len(d))
else:
    m0, m1, m2, m3 = mapline(d[0]), mapline(d[1]), mapline(d[2]), mapline(d[3])
    expect(m0 is not None and m0["trail"] == "grytviken", "arrival: the Grytviken trail is not playing after the intro: %s" % m0)
    expect(m0 is not None and m0["open"] == "", "arrival: a pin is open before the trail ends: %s" % m0)
    expect(m1 is not None and 6 < m1["sprites"] < 19 and m1["trail"] == "grytviken", "arrival: the trail is not part way: %s" % m1)
    expect(m2 is not None and m2["trail"] == "" and m2["sprites"] == 19, "arrival: the trail did not finish with 13 dots: %s" % m2)
    expect(m3 is not None and m3["open"] == "grytviken", "arrival: the Grytviken pin did not open: %s" % m3)
    mods = [s for _, s in modals(d[4])]
    expect("scripts/game.lua" in mods, "arrival: Play did not load the game: %s" % mods)
imgs = {k: png(v) for k, v in paths.items()}
if all(imgs.values()):
    def dots(img):
        # the olive dots of the trail between Iquitos and Grytviken, over the brown of South America
        n = 0
        for y in range(360, 405):
            for x in range(140, 200):
                c = px(img, x, y)
                if c[1] > 95 and c[1] > c[0] + 10 and c[1] > c[2] + 45:
                    n += 1
        return n
    expect(dots(imgs["open"]) > 30, "arrival: no dots are visible after the trail")
    expect(region_diff(imgs["trail"], imgs["open"], 150, 400, 330, 560) > 2000, "arrival: the Grytviken pin did not show up")

# ------------------------------------------------------------------------------------------------------------------ endless
text = """expect-window scripts/mainmenu.lua 20000
wait 1000
click 480 447
wait 1500
type Zed
wait 100
click 310 368
wait 3500
dump
screenshot %(map)s
move 144 361
wait 300
dump
click 144 361
wait 2500
dump
screenshot %(ask)s
click 400 420
wait 2500
dump
move 144 361
wait 300
click 144 361
wait 2500
click 400 335
wait 4500
dump
expect-lua 100 GetCurrentGameMode() == 3
"""
out, d, paths = run("endless", text, GAME_CALLS, GAME_TYPES, NAME_ART | GAME_ART, ("map", "ask"))
if len(d) != 5:
    errors.append("endless: expected 5 window dumps, got %d" % len(d))
else:
    expect(top(d[0]) == "scripts/worldmap.lua", "endless: the map is not on top")
    m = mapline(d[0])
    expect(m is not None and m["open"] == "" and m["pins"] == 6, "endless: the pins should all be closed: %s" % m)
    m = mapline(d[1])
    expect(m is not None and m["hover"] != "", "endless: the pin is not a hover target: %s" % m)
    expect(top(d[2]) == "scripts/choosedifficulty.lua", "endless: the click on the pin did not ask for a difficulty")
    expect(top(d[3]) == "scripts/worldmap.lua", "endless: Back did not return to the map")
    mods = [s for _, s in modals(d[4])]
    expect("scripts/game.lua" in mods, "endless: Hard did not load the game: %s" % mods)
imgs = {k: png(v) for k, v in paths.items()}
if all(imgs.values()):
    expect(region_diff(imgs["map"], imgs["ask"], 290, 140, 510, 465) > 30000, "endless: the difficulty dialog is not on the screenshot")

# -------------------------------------------------------------------------------------------------------------------- cheat
text = """expect-window scripts/mainmenu.lua 20000
wait 800
lua CreateNewUser('Zed')
wait 100
click 333 447
wait 3500
click 625 488
wait 3000
expect-lua 100 DoCheatMap(9, '1') == 'Invalid Cheat'
expect-lua 100 DoCheatMap(5, '0') == 'Invalid Level'
expect-lua 100 DoCheatMap(5, '12') == 'Set highest level complete to 12'
wait 3500
dump
screenshot %(map)s
expect-lua 100 DoCheatMap(3, '500') == 'Added 500 to location 11 and lifetime total'
expect-lua 100 GetHighestComplete() == 12
"""
out, d, paths = run("cheat", text, GAME_CALLS, set(), set(), ("map",))
if len(d) != 1:
    errors.append("cheat: expected 1 window dump, got %d" % len(d))
else:
    m = mapline(d[0])
    expect(m is not None and m["open"] == "grytviken", "cheat: the map did not move to Grytviken: %s" % m)
imgs = {k: png(v) for k, v in paths.items()}
if all(imgs.values()):
    # Grytviken's pin at (213,500): the globes of levels 11 and 12 are green, 13 is the pink current one
    greens = pinks = 0
    for y in range(380, 520):
        for x in range(150, 330):
            c = px(imgs["map"], x, y)
            if c[1] > 150 and c[0] < 120 and c[2] < 110:
                greens += 1
            if pinkish(c):
                pinks += 1
    expect(greens > 60, "cheat: no green (complete) globes on the Grytviken pin (%d pixels)" % greens)
    expect(pinks > 60, "cheat: no pink (current) globe on the Grytviken pin (%d pixels)" % pinks)

for e in errors:
    print("FAIL:", e)
print("greatcc_worldmap: %s" % ("FAILED" if errors else "ok"))
sys.exit(1 if errors else 0)
