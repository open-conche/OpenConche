"""The Great Chocolate Chase (docs/design/greatcc.md slice 19): the story from the first location to the credits, with the game's own cheats,
on the real scripts and art. A smoke test: it asserts that every screen on the way comes up and that nothing is logged as an error.

One run, `--headless --cheatmode --userdir <scratch> --seed 1`, asserts: exit 0; no [lua-error] / [engine-error] / [native-error] /
[content]; no [unimplemented-type]; the [unimplemented] set is DoMetrics only (the pause and results screens' analytics call); no cheat was
refused; the only missing textures are the known ones.

A new player starts Story Mode (the introduction over the map is closed). The map's Cheats buttons are used as the dialog calls them
(`DoCheatMap(5, n)` sets the highest level complete, `DoCheatMap(4, n)` plays level n), and in a level `DoCheatGame(1, '')` is Pass Lvl.
For each of the six locations (Iquitos, Grytviken, Namche, Krakatoa, Kamchatka, Gaberoun):
  - its first level (1, 11, 21, 31, 41, 51) is entered from the map (the upgrade shop first from level 4, then the diary page), passed with
    the cheat, and the results screen is left with Quit and Story Mode back to the map;
  - its last level (10, 20, 30, 40, 50) is passed the same way and the results' OK goes to the map, which plays the arrival animation of the
    next location: the next globe is the open one and the current level is the next location's first (screenshots of the first and the
    last transition);
  - passing level 60 shows the closing diary page (`endgame.lua`), whose Credits button runs the credits (`credits.lua`) and whose Close
    leaves the game: the main menu comes back after one click (the screen is black until a click or a key reaches the script, which
    is what the engine does with the loop of DoMainWindow; unverified against the original).
Screenshots are 800x600 PNGs; the arrival animation's globe and the closing page are checked by pixels."""
import argparse, os, re, shutil, struct, subprocess, sys, zlib

ap = argparse.ArgumentParser()
ap.add_argument("--openconche", required=True)
ap.add_argument("--assets", required=True)
ap.add_argument("--workdir", required=True)
a = ap.parse_args()
shutil.rmtree(a.workdir, ignore_errors=True)
os.makedirs(a.workdir)
errors = []

MISSING = {"ui/button/hud_menu_overlay", "ui/fullscreendialog", "ui/story/btn_trim_left", "idle_angry.png"}
CALLS = {"DoMetrics"}
LOCATIONS = ["Iquitos", "Grytviken", "Namche", "Krakatoa", "Kamchatka", "Gaberoun"]

NEW_PLAYER = """expect-window scripts/mainmenu.lua 20000
wait 1000
click 333 447
wait 1500
type Zed
wait 100
click 310 368
wait 3500
click 625 488
wait 3000
expect-lua 100 DoCheatMap ~= nil
"""
DIARY = """wait 2800
move 585 500
wait 80
move 585 500
wait 80
click 585 500
wait 4850
"""
shots = []
dumps = []
T = NEW_PLAYER


def step(name):
    """A dump after the step; its index is remembered under the name."""
    global T
    T += "dump\n"
    dumps.append(name)


def enter(level, highest):
    """From the map: highest level complete, play `level`, through the upgrade shop (from level 4) and the diary page, in the level."""
    global T
    if highest >= 1:  # (the new player's highest level is already 0, which the cheat does not take)
        T += "expect-lua 100 DoCheatMap(5, '%d') == 'Set highest level complete to %d'\nwait 500\n" % (highest, highest)
    T += "expect-lua 100 DoCheatMap(4, '%d') == ''\n" % level
    if level > 3:
        T += "expect-window scripts/prelevel.lua 30000\nwait 2500\nclick 740 540\n"
    T += DIARY
    T += "expect-lua 100 GetCurrentLevel() == %d\n" % level


def finish(level):
    """In a level: Pass Lvl, then the results screen is up."""
    global T
    T += "expect-lua 100 DoCheatGame(1, '') == ''\nexpect-window scripts/endlevel.lua 60000\nwait 2500\n"
    T += "expect-lua 100 GetHighestComplete() == %d\n" % level


shot_names = []
for loc in range(6):
    first, last = 10 * loc + 1, 10 * loc + 10
    # the first level: Quit from the results, then Story Mode (the map) again
    enter(first, first - 1)
    finish(first)
    step("first%d" % loc)
    T += "click 75 550\nwait 1500\nclick 310 347\nexpect-window scripts/mainmenu.lua 20000\nwait 2500\nclick 333 447\nexpect-window scripts/worldmap.lua 20000\nwait 3500\n"
    T += "expect-lua 100 DoCheatMap ~= nil\n"
    step("map%d" % loc)
    # the last level: the results' OK goes on
    enter(last, last - 1)
    finish(last)
    if loc < 5:
        T += "click 740 550\nwait 4000\n"
        T += "expect-window scripts/worldmap.lua 20000\nwait 9000\n"
        T += "expect-lua 100 GetCurrentLevel() == %d\n" % (last + 1)
        if loc in (0, 4):
            shot_names.append("transition%d" % loc)
            T += "screenshot-logical @transition%d@\n" % loc
        step("transition%d" % loc)
    else:
        T += "click 740 550\nexpect-window scripts/endgame.lua 30000\nwait 3000\n"
        shot_names.append("endgame")
        T += "screenshot-logical @endgame@\n"
        step("endgame")
        T += "move 570 486\nwait 300\nclick 570 486\nexpect-window scripts/credits.lua 30000\nwait 3000\n"
        shot_names.append("credits")
        T += "screenshot-logical @credits@\n"
        step("credits")
        T += "wait 15000\nmove 400 511\nwait 300\nclick 400 511\nwait 4000\ndump\n"
        dumps.append("black")
        T += "click 400 300\nexpect-window scripts/mainmenu.lua 20000\nwait 2500\n"
        T += "expect-lua 100 GetHighestComplete() == 60\nexpect-lua 100 GetCurrentLevel() == 61\n"
        step("menu")


def png(path):
    """(width, height, channels, rows of bytes) of an 8-bit RGB or RGBA PNG, or None."""
    if not os.path.exists(path):
        errors.append("no screenshot %s" % os.path.basename(path))
        return None
    raw = open(path, "rb").read()
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


def count(img, x0, y0, x1, y1, pred):
    return sum(1 for y in range(y0, y1) for x in range(x0, x1) if pred(px(img, x, y)))


def expect(cond, msg):
    if not cond:
        errors.append(msg)


def mods(d):
    return re.findall(r'^  Modal "[^"]*" .*script="([^"]*)"', d, re.M)


user_dir = os.path.join(a.workdir, "user")
log = os.path.join(a.workdir, "story.log")
script = os.path.join(a.workdir, "story.script")
paths = {s: os.path.join(a.workdir, "story_%s.png" % s) for s in shot_names}
for name, path in paths.items():
    T = T.replace("@%s@" % name, path)
open(script, "w").write(T)
p = subprocess.run([a.openconche, "--headless", "--cheatmode", "--assets", a.assets, "--userdir", user_dir, "--seed", "1", "--script", script,
                    "--log", log], capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=3000)
out = (open(log, encoding="utf-8", errors="replace").read() if os.path.exists(log) else "") + "\n" + p.stdout + "\n" + p.stderr
if p.returncode != 0:
    errors.append("exit status %d" % p.returncode)
for t in ("[lua-error]", "[engine-error]", "[native-error]", "[content]", "[script] FAIL", "[expect-lua]", "refused"):
    if t in out:
        errors.append("%s in the output: %s" % (t, next(l for l in out.split("\n") if t in l)))
seen = set(re.findall(r"^(?:\[\+\d+ms\] )?\[unimplemented\] (\S+)", out, re.M))
if not seen <= CALLS:
    errors.append("[unimplemented] calls beyond the allow-list: %s" % sorted(seen - CALLS))
if re.search(r"^(?:\[\+\d+ms\] )?\[unimplemented-type\] ", out, re.M):
    errors.append("[unimplemented-type] in the output")
gone = set(re.findall(r"^(?:\[\+\d+ms\] )?\[gfx\] texture '([^']+)' not found", out, re.M))
if not gone <= MISSING:
    errors.append("textures the install does not ship beyond the known ones: %s" % sorted(gone - MISSING))
d = [x for x in re.split(r"^(?=\[windows\] )", p.stdout, flags=re.M) if x.startswith("[windows] ")]
if len(d) != len(dumps):
    errors.append("expected %d window dumps, got %d" % (len(dumps), len(d)))
else:
    at = dict(zip(dumps, d))
    for loc in range(6):
        expect(mods(at["first%d" % loc])[-1:] == ["scripts/endlevel.lua"], "%s: the first level's results are not on top: %s" % (LOCATIONS[loc], mods(at["first%d" % loc])))
        expect(mods(at["map%d" % loc])[-1:] == ["scripts/worldmap.lua"], "%s: Story Mode did not return to the map: %s" % (LOCATIONS[loc], mods(at["map%d" % loc])))
        if loc < 5:
            m = re.search(r'VWorldMap "world_map_window" .* open=(\w*)', at["transition%d" % loc])
            expect(m is not None and m.group(1) == LOCATIONS[loc + 1].lower(),
                   "%s: the map did not open the next location after level %d: %s" % (LOCATIONS[loc], 10 * loc + 10, m.group(1) if m else None))
    expect(mods(at["endgame"])[-1:] == ["scripts/endgame.lua"], "level 60 did not show the closing page: %s" % mods(at["endgame"]))
    expect(mods(at["credits"])[-2:] == ["scripts/endgame.lua", "scripts/credits.lua"], "the credits are not over the closing page: %s" % mods(at["credits"]))
    expect(mods(at["black"]) == [], "after the credits the modal stack is not empty: %s" % mods(at["black"]))
    expect(mods(at["menu"])[-1:] == ["scripts/mainmenu.lua"], "the main menu did not come back: %s" % mods(at["menu"]))
imgs = {k: png(v) for k, v in paths.items()}
if all(imgs.values()):
    for k in ("transition0", "transition4"):
        # the new location's globe: a red pin head (the open location shows its dial of numbers, the others pins)
        red = lambda c: c[0] > 130 and c[1] < 60 and c[2] < 80
        expect(count(imgs[k], 0, 120, 800, 560, red) > 600, "%s: no red pin heads on the map screenshot" % k)
    white = lambda c: c[0] > 235 and c[1] > 230 and c[2] > 215
    cream = lambda c: c[0] > 215 and c[1] > 205 and c[2] > 180
    expect(count(imgs["endgame"], 40, 40, 780, 560, cream) > 60000, "the closing diary page is not on the screenshot")
    light = lambda c: c[0] > 200 and c[1] > 200 and c[2] > 200
    expect(count(imgs["credits"], 280, 90, 520, 135, light) > 400, "the credits heading is not drawn")
    expect(count(imgs["credits"], 250, 150, 550, 450, light) > 500, "the credits' text is not drawn")

if errors:
    print("FAIL")
    for e in errors:
        print(" -", e)
    sys.exit(1)
print("ok")
