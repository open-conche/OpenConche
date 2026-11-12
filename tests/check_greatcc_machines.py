"""The Great Chocolate Chase (docs/design/greatcc.md slice 11): a chocolate made from nothing through the mixer and the press, on the
real scripts and art.

Runs `--headless --userdir <scratch> --seed 1` once and asserts: exit 0; no [lua-error] / [engine-error] / [native-error] / [content];
the [unimplemented] and [unimplemented-type] sets are empty; the only textures the scripts name that the install does not ship are the shop's menu overlay, its full-screen backdrop and the
trim of the yes/no dialog.

Level 1 (a new player through Story Mode), with real mouse clicks, Tutorials switched off in the Options screen first (the first customer's tutorial would lock the level): the dark
button of the mixer sends her to the mixer, which runs its five steps (the arm, the tube, the spray with the chocolate wiping into
the bucket, the tube back, the arm back) with the dump showing the machine locked until the bucket can be picked up; a click on the
mixer collects the bucket (she holds `chocolate_1(dark)`); the bar button of the press takes her to the press, the bucket goes in
and comes out shaped (the barrel falls through the door, the weight stamps and the bar shows), and a click on the press puts the
finished bar in her hand: `chocolate_1(dark,bar)`. Screenshots of each stage are taken and compared at the machines.

Slice 15 adds six more runs (levels are jumped to with `SetCurrentLevel(n)` and `LevelSetup()`, which is what a restart does):
"level3" a new player with Tutorials on plays the third level's tutorial, the one that points at the Ingredient Station (step 6, "Now
click the Banana button on the new Ingredient Station"): the station is drawn, the button answers the pointer, the tutorial goes on
through the machine's pump to the stamped bar and the delivered order; "levels" every level of every location loads with the customers
its file names (level 28, whose file has an unclosed <order>, through fix F1; level 58 names a Krakatoa customer that Gaberoun does not
know); "nofix" the same two levels with the fixes off (`--config fixbugs=0`): level 28 loads the part of the file before the error;
"infuse" level 12: the infusion machine makes a bar from a bucket; "deliver" level 14: an infusion bar with a topping is made and
boxed at the delivery station, the order is paid, the zeppelin comes for the box and goes; "belt" level 51: a bar put on the conveyor
belt rides past the counters and a customer takes it as a sample (+patience), and a bar kept in hand decays and is dropped.
"""
import argparse, os, re, shutil, struct, subprocess, sys, zlib

ap = argparse.ArgumentParser()
ap.add_argument("--openconche", required=True)
ap.add_argument("--assets", required=True)
ap.add_argument("--workdir", required=True)
a = ap.parse_args()
shutil.rmtree(a.workdir, ignore_errors=True)
os.makedirs(a.workdir)
errors = []

# (idle_angry.png: the posse yachtsman's angry idle sheet names a PNG the install does not ship)
MISSING = {"ui/button/hud_menu_overlay", "ui/fullscreendialog", "ui/story/btn_trim_left", "idle_angry.png"}
TYPES = set()
CALLS = set()

BOOT = """expect-window scripts/mainmenu.lua 20000
wait 1000
click 333 447
wait 1500
type Zed
wait 100
click 310 368
wait 3500
click 625 488
wait 3000
click 450 565
wait 1500
click 362 395
wait 300
click 404 486
wait 1500
move 150 258
wait 700
click 150 258
"""
# Level 1 opens with its diary page. The level's clock starts when the dialog has bounced away (about 970 ms after the page is up); a wait that
# follows is what the old script's was, less the 1570 ms it took the old one to reach the start of the level and plus the 416 ms between
# the click on OK and that start.
DIARY = """expect-window scripts/plotwindow.lua 30000
wait 450
move 585 500
wait 50
move 585 500
wait 50
click 585 500
"""


def run(tag, script_text, shots=(), extra=()):
    user = os.path.join(a.workdir, "user_" + tag)
    log = os.path.join(a.workdir, tag + ".log")
    script = os.path.join(a.workdir, tag + ".script")
    paths = {s: os.path.join(a.workdir, "%s_%s.png" % (tag, s)) for s in shots}
    for name, path in paths.items():
        script_text = script_text.replace("@%s@" % name, path)
    with open(script, "w") as f:
        f.write(script_text)
    p = subprocess.run([a.openconche, "--headless", "--assets", a.assets, "--userdir", user, "--seed", "1", "--script", script,
                        "--log", log] + list(extra), capture_output=True, text=True, encoding="utf-8",
                       errors="replace", timeout=420)
    out = (open(log, encoding="utf-8", errors="replace").read() if os.path.exists(log) else "") + "\n" + p.stdout + "\n" + p.stderr
    if p.returncode != 0:
        errors.append("%s: exit status %d" % (tag, p.returncode))
    for t in ("[lua-error]", "[engine-error]", "[native-error]", "[content]", "[script] FAIL", "[expect-lua]"):
        if t in out:
            errors.append("%s: %s in the output: %s" % (tag, t, next(l for l in out.split("\n") if t in l)))
    seen = set(re.findall(r"^(?:\[\+\d+ms\] )?\[unimplemented\] (\S+)", out, re.M))
    if seen != CALLS:
        errors.append("%s: [unimplemented] set differs: extra %s, missing %s" % (tag, sorted(seen - CALLS), sorted(CALLS - seen)))
    types = set(re.findall(r"^(?:\[\+\d+ms\] )?\[unimplemented-type\] (\S+)", out, re.M))
    if types != TYPES:
        errors.append("%s: [unimplemented-type] set differs: extra %s, missing %s" % (tag, sorted(types - TYPES), sorted(TYPES - types)))
    gone = set(re.findall(r"^(?:\[\+\d+ms\] )?\[gfx\] texture '([^']+)' not found", out, re.M))
    if not gone <= MISSING:
        errors.append("%s: textures the install does not ship beyond the known ones: %s" % (tag, sorted(gone - MISSING)))
    dumps = [d for d in re.split(r"^(?=\[windows\] )", p.stdout, flags=re.M) if d.startswith("[windows] ")]
    return out, dumps, paths


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




def expect(cond, msg):
    if not cond:
        errors.append(msg)




def dumpline(d):
    m = re.search(r'^\s*ShopWindow "sprite_window" .*$', d, re.M)
    return m.group(0) if m else ""


def hands(d):
    m = re.search(r" hands=(\S+)\|(\S+)", dumpline(d))
    return (m.group(1), m.group(2)) if m else None


def machine(d, name):
    m = re.search(r"%s:s=(\d+),o=(-?\d+),L=(\d),P=(\d),held=(\S+?)(?:;|$|\s)" % re.escape(name), dumpline(d))
    if not m:
        return None
    return {"s": int(m.group(1)), "o": int(m.group(2)), "L": m.group(3) == "1", "P": m.group(4) == "1", "held": m.group(5)}


def expect(cond, msg):
    if not cond:
        errors.append(msg)


MIXER, PRESS = "station14/machine_mixer", "station12/machine_press"


def click(x, y):
    return "move %d %d\nwait 50\nclick %d %d\n" % (x, y, x, y)


text = BOOT + DIARY + "wait 5350\ndump\nscreenshot @idle@\n"
text += click(80, 525)  # the mixer's dark button
text += "wait 1200\ndump\nscreenshot @mix1@\n"
text += "wait 500\ndump\nscreenshot @mix2@\n"
text += "wait 500\ndump\nscreenshot @mix3@\n"
text += "wait 3000\ndump\nscreenshot @mixed@\n"
text += click(155, 440)  # the mixer's tank: collect the bucket
text += "wait 300\ndump\nscreenshot @held@\n"
text += click(456, 575)  # the press's bar button
text += "wait 1250\ndump\nscreenshot @press1@\n"
text += "wait 300\ndump\nscreenshot @press2@\n"
text += "wait 400\ndump\nscreenshot @press3@\n"
text += "wait 2000\ndump\nscreenshot @pressed@\n"
text += click(500, 480)  # the press: collect the bar
text += "wait 300\ndump\nscreenshot @bar@\n"
names = ("idle", "mix1", "mix2", "mix3", "mixed", "held", "press1", "press2", "press3", "pressed", "bar")
out, d, paths = run("machines", text, shots=names)
if len(d) != 11:
    errors.append("machines: expected 11 window dumps, got %d" % len(d))
else:
    idle, mix1, mix2, mix3, mixed, held, press1, press2, press3, pressed, bar = d
    m0 = machine(idle, MIXER)
    expect(m0 is not None and m0["s"] == 0 and not m0["L"] and m0["held"] == "-", "machines: the mixer is not idle at the start: %s" % m0)
    expect(machine(idle, PRESS) is not None, "machines: no press in the dump (it should be enabled in level 1)")
    expect(hands(idle) == ("-", "-"), "machines: her hands are not empty at the start: %s" % (hands(idle),))
    # the mixer runs: locked with a dark bucket, in the steps in order
    ms = [machine(x, MIXER) for x in (mix1, mix2, mix3)]
    expect(all(ms), "machines: no mixer in a dump during mixing")
    if all(ms):
        expect(all(x["held"] == "(dark)" for x in ms), "machines: the bucket is not dark: %s" % ms)
        expect(ms[0]["s"] >= 1 and ms[0]["L"], "machines: the mixer is not working after the click: %s" % ms[0])
        expect(ms[0]["s"] <= ms[1]["s"] <= ms[2]["s"], "machines: the steps do not run in order: %s" % [x["s"] for x in ms])
        expect(ms[2]["s"] >= 3, "machines: the mixer is not at its third step after 2.2 s: %s" % ms[2])
    mm = machine(mixed, MIXER)
    expect(mm is not None and mm["s"] == 0 and not mm["L"] and mm["P"] and mm["held"] == "(dark)",
           "machines: the finished bucket is not waiting in the mixer: %s" % mm)
    # collected
    expect(hands(held) is not None and hands(held)[0].endswith("(dark)") and hands(held)[0].startswith("chocolate_1"),
           "machines: she does not hold the bucket: %s" % (hands(held),))
    mh = machine(held, MIXER)
    expect(mh is not None and mh["held"] == "-", "machines: the mixer still holds the bucket after the pick-up: %s" % mh)
    # the press takes the bucket and shapes it
    pp = [machine(x, PRESS) for x in (press1, press2, press3)]
    expect(all(pp), "machines: no press in a dump during pressing")
    if all(pp):
        expect(pp[0]["held"] == "(dark,bar)" and pp[0]["s"] >= 1, "machines: the press did not take the bucket: %s" % pp[0])
        expect(hands(press1) == ("-", "-"), "machines: the bucket is still in her hand at the press: %s" % (hands(press1),))
        expect(pp[0]["s"] <= pp[1]["s"] <= pp[2]["s"], "machines: the press steps do not run in order: %s" % [x["s"] for x in pp])
    pd = machine(pressed, PRESS)
    expect(pd is not None and pd["s"] == 0 and not pd["L"] and pd["P"] and pd["held"] == "(dark,bar)",
           "machines: the finished bar is not waiting in the press: %s" % pd)
    hb = hands(bar)
    expect(hb is not None and hb[0].startswith("chocolate_1") and hb[0].endswith("(dark,bar)") and hb[1] == "-",
           "machines: she does not hold the bar: %s" % (hb,))
    pb = machine(bar, PRESS)
    expect(pb is not None and pb["held"] == "-", "machines: the press still holds the bar after the pick-up: %s" % pb)
    imgs = {k: png(v) for k, v in paths.items()}
    if all(imgs.values()):
        # the mixer corner (x 40..300, y 380..600): the bucket and the arm change it, the finished bucket stays
        expect(region_diff(imgs["idle"], imgs["mix2"], 100, 400, 300, 560) > 300, "machines: the mixer does not look different while it works")
        expect(region_diff(imgs["idle"], imgs["mixed"], 150, 420, 270, 520) > 200, "machines: no bucket on the mixer when it is done")
        expect(region_diff(imgs["mixed"], imgs["held"], 150, 420, 270, 520) > 200, "machines: the bucket is still on the mixer after she took it")
        # the press (x 380..600, y 400..600): the barrel, the door and the bar
        expect(region_diff(imgs["held"], imgs["press1"], 380, 400, 600, 560) > 300, "machines: the press does not look different at the start")
        expect(region_diff(imgs["press1"], imgs["press3"], 380, 400, 600, 560) > 300, "machines: the press does not change while it works")
        expect(region_diff(imgs["idle"], imgs["pressed"], 440, 440, 560, 530) > 200, "machines: no bar on the press when it is done")
        expect(region_diff(imgs["pressed"], imgs["bar"], 440, 440, 560, 530) > 150, "machines: the bar is still on the press after she took it")


# ================================================================ slice 15: the remaining machines, delivery, belt, decay, all levels

def mach(d, name):
    """The machine `name` ("station13/machine_ingredient") of a dump: state, choice, locked, processed, held, and the rest of its entry."""
    m = re.search(r"%s:s=(\d+),o=(-?\d+),L=(\d),P=(\d),held=(\([^)]*\)|-)([^;\s]*)" % re.escape(name), dumpline(d))
    if not m:
        return None
    return {"s": int(m.group(1)), "o": int(m.group(2)), "L": m.group(3) == "1", "P": m.group(4) == "1", "held": m.group(5), "rest": m.group(6)}


def tut(d):
    m = re.search(r'VTutorialWindow "tutorial_window" .* tutorial=(\S+) step=(\d+)/(\d+)', d)
    return (m.group(1), int(m.group(2)), int(m.group(3))) if m else None


def guests(d):
    """name -> (station, state, patience) of the customers in the shop."""
    out = {}
    for m in re.finditer(r"(?:customers=|;)([^;@\s=]+)@(station\d+):s=(\d+),m=\d+,p=(-?[\d.]+)", dumpline(d)):
        out[m.group(1)] = (m.group(2), int(m.group(3)), float(m.group(4)))
    return out


def field(d, key):
    m = re.search(r" %s=(\S+)" % key, dumpline(d))
    return m.group(1) if m else None


NEW_PLAYER = """expect-window scripts/mainmenu.lua 20000
wait 1000
click 333 447
wait 1500
type Zed
wait 100
click 310 368
wait 3500
"""
STORY_FIRST = """click 625 488
wait 3000
move 150 258
wait 700
click 150 258
"""


def jump(n):
    """The first load of the shop, then a jump to level n before its first customer comes (the tutorials as the player has them)."""
    return "expect-window scripts/game.lua 20000\nlua SetCurrentLevel(%d)\nlua LevelSetup()\n" % n + DIARY


# ---- run "level3": the tutorial that points at the Ingredient Station ----
text = NEW_PLAYER + STORY_FIRST + jump(3) + "wait 4050\n"
text += click(70, 250) + "wait 9000\ndump\nscreenshot @step3@\n"            # 0 tutorial_5 step 3: click the Dark Chocolate button
text += click(75, 518) + "wait 6000\ndump\n"                                   # 1 the mixer has run; step 5: pick up the bucket
text += click(155, 440) + "wait 4000\ndump\nmove 257 562\nwait 300\ndump\nscreenshot @banana@\n"  # 2, 3 step 6: the Banana button; hovering it
text += click(257, 562) + "wait 1200\ndump\nscreenshot @ing1@\n"              # 4 the ingredient machine at work
text += "wait 1000\ndump\nscreenshot @ing2@\n"                                 # 5 the pump has run, the bucket can be taken
text += "wait 2500\ndump\n"                                                    # 6 step 8: pick up the barrel
text += click(300, 490) + "wait 1800\ndump\nscreenshot @barrel@\n"            # 7 step 9: the barrel is hers
text += click(456, 575) + "wait 4500\ndump\n"                                  # 8 the press has stamped it
text += click(500, 480) + "wait 1800\ndump\nscreenshot @bar3@\n"              # 9 step 12: the finished bar
text += click(70, 250) + "wait 1800\ndump\nscreenshot @served@\n"             # 10 delivered
shots3 = ("step3", "banana", "ing1", "ing2", "barrel", "bar3", "served")
out, d, paths = run("level3", text, shots=shots3)
if len(d) != 11:
    errors.append("level3: expected 11 window dumps, got %d" % len(d))
else:
    ING = "station13/machine_ingredient"
    ts = [tut(x) for x in d]
    expect(ts[0] == ("tutorial_5", 3, 12), "level3: the tutorial is not at its third step (click the Dark Chocolate button): %s" % (ts[0],))
    expect(field(d[0], "level") == "3", "level3: the shop is not at level 3: %s" % field(d[0], "level"))
    m0 = mach(d[0], ING)
    expect(m0 is not None and m0["s"] == 0 and not m0["L"] and m0["held"] == "-",
           "level3: the Ingredient Station is not there and idle at level 3: %s" % (m0,))
    expect(ts[1] == ("tutorial_5", 5, 12), "level3: the tutorial is not at step 5 after the mixer: %s" % (ts[1],))
    expect(ts[2] == ("tutorial_5", 6, 12) and hands(d[2]) is not None and hands(d[2])[0].endswith("(dark)"),
           "level3: not at step 6 with the dark bucket in hand: %s %s" % (ts[2], hands(d[2])))
    expect(ts[3] == ("tutorial_5", 6, 12) and "hover=btn_ingred_1" in dumpline(d[3]),
           "level3: the Banana button does not answer the pointer (the Ingredient Station is not clickable): %s" % field(d[3], "hover"))
    mi = mach(d[4], ING)
    expect(ts[4] is not None and ts[4][1] == 7 and mi is not None and mi["L"] and mi["held"] == "(dark,ingredient:0)" and mi["s"] >= 1,
           "level3: the machine is not working on the bucket at step 7: %s %s" % (ts[4], mi))
    mi5 = mach(d[5], ING)
    expect(ts[5] is not None and ts[5][1] == 8 and mi5 is not None and not mi5["L"] and mi5["s"] >= 1,
           "level3: the pump has not finished and unlocked the machine at step 8: %s %s" % (ts[5], mi5))
    mi6 = mach(d[6], ING)
    expect(mi6 is not None and mi6["s"] == 0 and mi6["P"] and mi6["held"] == "(dark,ingredient:0)",
           "level3: the finished barrel is not waiting in the machine: %s" % (mi6,))
    expect(ts[7] is not None and ts[7][1] == 9 and hands(d[7]) is not None and hands(d[7])[0].endswith("(dark,ingredient:0)"),
           "level3: she does not hold the banana barrel at step 9: %s %s" % (ts[7], hands(d[7])))
    expect(mach(d[7], ING)["held"] == "-", "level3: the machine still holds the barrel after the pick-up")
    mp = mach(d[8], "station12/machine_press")
    expect(mp is not None and mp["P"] and mp["held"] == "(dark,ingredient:0,bar)" and ts[8] is not None and ts[8][1] == 11,
           "level3: the press has not made the banana bar at step 11: %s %s" % (mp, ts[8]))
    expect(ts[9] is not None and ts[9][1] == 12 and hands(d[9]) is not None and hands(d[9])[0].endswith("(dark,ingredient:0,bar)"),
           "level3: she does not hold the banana bar at the last step: %s %s" % (ts[9], hands(d[9])))
    g = guests(d[10])
    expect(ts[10] is None, "level3: the tutorial is still up after the bar was delivered: %s" % (ts[10],))
    expect(g.get("tut_cust_1", (0, 0, 0))[1] in (6, 7) and "$=20" in dumpline(d[10]),
           "level3: the first customer is not paid after the banana bar: %s" % (g,))
    imgs = {k: png(v) for k, v in paths.items()}
    if all(imgs.values()):
        # the Ingredient Station (x 220..400, y 440..560): the tanks and the barrel
        expect(region_diff(imgs["banana"], imgs["ing1"], 230, 440, 400, 560) > 300, "level3: the ingredient machine does not change when it starts")
        expect(region_diff(imgs["ing1"], imgs["ing2"], 230, 440, 400, 560) > 200, "level3: the ingredient machine does not move through its pump")
        # the arrow of the Banana step points at the button (orange, as in the first level's tutorial)
        orange = sum(1 for yy in range(450, 560) for xx in range(225, 300)
                     if px(imgs["banana"], xx, yy)[0] > 190 and 90 < px(imgs["banana"], xx, yy)[1] < 160 and px(imgs["banana"], xx, yy)[2] < 60)
        expect(orange > 120, "level3: no orange arrow over the Banana button (%d orange pixels)" % orange)


# ---- run "levels": every level of every location loads with the customers its file names ----
FILE_CUSTOMERS = [8, 9, 9, 11, 16, 13, 16, 20, 22, 26, 14, 13, 14, 14, 17, 18, 24, 23, 27, 28, 20, 20, 22, 22, 22, 25, 29, 30, 34, 37,
                  17, 17, 21, 25, 28, 29, 29, 34, 38, 46, 23, 25, 28, 26, 27, 29, 33, 34, 38, 37, 28, 28, 29, 29, 34, 38, 46, 50, 54, 54]
EXPECTED = list(FILE_CUSTOMERS)
EXPECTED[57] = 49  # level 58 names `kra_female`, a Krakatoa local that Gaberoun's customers do not have
text = BOOT + DIARY + "wait 3000\n"
for n in range(1, 61):
    text += "lua SetCurrentLevel(%d)\nlua LevelSetup()\nwait 300\ndump\n" % n
out, d, paths = run("levels", text)
if len(d) != 60:
    errors.append("levels: expected 60 window dumps, got %d" % len(d))
else:
    for n, x in enumerate(d, 1):
        expect(field(x, "level") == str(n), "levels: level %d did not load (the shop says level %s)" % (n, field(x, "level")))
        expect(field(x, "pending") == str(EXPECTED[n - 1]), "levels: level %d has %s customers to come, expected %d" % (n, field(x, "pending"), EXPECTED[n - 1]))
        expect(field(x, "stations") == "17", "levels: level %d has %s stations, not 17" % (n, field(x, "stations")))
    expect("[fix] F1 levels/level28.xml" in out, "levels: fix F1 did not say it closed an order in level 28")
    expect("does not parse" not in out, "levels: a level file did not parse with the fixes on")
    # the machines each level has (upgrades.xml): the ingredient station from level 3, the infusion machine from 11 (Grytviken), the topping
    # machine from 13, the delivery from 14, and the belt at Gaberoun's first level 51
    expect("machine_ingredient" not in d[1] and "machine_ingredient" in d[2], "levels: the ingredient machine is not enabled from level 3")
    expect("machine_infusion" not in d[9] and "machine_infusion" in d[10], "levels: the infusion machine is not enabled from level 11")
    expect("machine_topping" not in d[11] and "machine_topping" in d[12], "levels: the topping machine is not enabled from level 13")
    expect("machine_delivery" not in d[12] and "machine_delivery" in d[13], "levels: the delivery is not enabled from level 14")
    for first, shown in ((27, 26), (38, 37), (42, 41), (51, 51)):  # Namche 27, Krakatoa 38, Kamchatka 42, Gaberoun 1 (level 51)
        before = " belt=" in d[first - 2] if first > 1 and first != 51 else True
        expect(" belt=" in d[first - 1] and (first == 51 or " belt=" not in d[first - 2]),
               "levels: the conveyor is not shown from level %d (and not before it)" % first)
    expect(" belt=" not in d[0] and " belt=" not in d[25], "levels: the conveyor is shown before Namche's level 27")

# ---- run "nofix": the same with the fixes off ----
text = BOOT + DIARY + "wait 3000\n"
for n in (26, 28):
    text += "lua SetCurrentLevel(%d)\nlua LevelSetup()\nwait 300\ndump\n" % n
out, d, paths = run("nofix", text, extra=("--config", "fixbugs=0"))
if len(d) != 2:
    errors.append("nofix: expected 2 window dumps, got %d" % len(d))
else:
    expect(field(d[0], "pending") == "25", "nofix: level 26 has %s customers, not 25" % field(d[0], "pending"))
    expect(field(d[1], "pending") == "28", "nofix: level 28 has %s customers with the fixes off, not the 28 before the error" % field(d[1], "pending"))
    expect("level28.xml does not parse" in out and "[fix] F1" not in out, "nofix: level 28 did not say that it does not parse, or F1 ran")


# ---- run "infuse": level 12, the infusion machine ----
text = BOOT + DIARY + "wait 3000\nlua SetCurrentLevel(12)\nlua LevelSetup()\nwait 3000\n"
text += click(75, 518) + "wait 3500\n" + click(155, 440) + "wait 1500\ndump\nscreenshot @held12@\n"   # 0 the dark bucket in hand
text += click(778, 346) + "wait 1700\ndump\nscreenshot @inf1@\n"                                        # 1 the infusion machine takes it and runs
text += "wait 350\ndump\nscreenshot @inf2@\n"                                                          # 2 the door drops the barrel; the bar is up
text += "wait 1800\ndump\nscreenshot @inf3@\n"                                                         # 3 done
text += click(730, 372) + "wait 2500\ndump\nscreenshot @inf4@\n"                                       # 4 collected
shots12 = ("held12", "inf1", "inf2", "inf3", "inf4")
out, d, paths = run("infuse", text, shots=shots12)
INF = "station10/machine_infusion"
if len(d) != 5:
    errors.append("infuse: expected 5 window dumps, got %d" % len(d))
else:
    expect(hands(d[0]) is not None and hands(d[0])[0].endswith("(dark)"), "infuse: she does not hold the dark bucket: %s" % (hands(d[0]),))
    expect("machine_drink" in d[0] and "machine_infusion" in d[0], "infuse: the drink and the infusion machines are not both on at level 12")
    m1 = mach(d[1], INF)
    expect(m1 is not None and m1["s"] >= 1 and m1["L"] and m1["held"] == "(dark,infusion,filling:0)", "infuse: the machine is not working on the bucket: %s" % (m1,))
    expect(hands(d[1]) == ("-", "-"), "infuse: the bucket is still in her hand at the machine: %s" % (hands(d[1]),))
    m2 = mach(d[2], INF)
    expect(m2 is not None and m2["s"] == 2 and m2["L"], "infuse: the machine is not in its second step, locked: %s" % (m2,))
    m3 = mach(d[3], INF)
    expect(m3 is not None and m3["s"] == 0 and not m3["L"] and m3["P"] and m3["held"] == "(dark,infusion,filling:0)", "infuse: the finished bar is not waiting: %s" % (m3,))
    expect(hands(d[4]) is not None and hands(d[4])[0].endswith("(dark,infusion,filling:0)") and mach(d[4], INF)["held"] == "-",
           "infuse: she does not hold the infusion bar: %s" % (hands(d[4]),))
    imgs = {k: png(v) for k, v in paths.items()}
    if all(imgs.values()):
        box = (580, 300, 800, 520)
        expect(region_diff(imgs["held12"], imgs["inf1"], *box) > 300, "infuse: the infusion machine does not change when it starts")
        expect(region_diff(imgs["inf1"], imgs["inf2"], *box) > 150, "infuse: the infusion machine does not move through its steps")
        expect(region_diff(imgs["inf3"], imgs["inf4"], *box) > 100, "infuse: the bar is still on the machine after she took it")


# ---- run "deliver": level 14, the delivery station and the zeppelin ----
text = BOOT + DIARY + "wait 3000\nlua SetCurrentLevel(14)\nlua LevelSetup()\nwait 3000\n"
text += click(75, 518) + "wait 3500\n" + click(155, 440) + "wait 1500\n"
text += click(778, 437) + "wait 3500\n"                                    # the third infusion
text += click(730, 372) + "wait 2500\ndump\n"                              # 0 the infusion bar in hand
text += click(627, 557) + "wait 3500\n"                                    # the first topping
text += click(640, 470) + "wait 2500\ndump\nscreenshot @order14@\n"        # 1 the topped bar in hand
text += "wait 14000\ndump\nscreenshot @bubble14@\n"                        # 2 the order shows (30 s after the level began)
text += click(630, 240) + "wait 2200\ndump\nscreenshot @box1@\n"           # 3 the box
text += "wait 1000\ndump\nscreenshot @box2@\n"                             # 4 the zeppelin is flying in
text += "wait 1500\ndump\nscreenshot @zep1@\n"                             # 5
text += "wait 1500\ndump\nscreenshot @zep2@\n"                             # 6 it has the box
text += "wait 5000\ndump\nscreenshot @zep3@\n"                             # 7 it has gone
shots14 = ("order14", "bubble14", "box1", "box2", "zep1", "zep2", "zep3")
out, d, paths = run("deliver", text, shots=shots14)
DEL = "station07/machine_delivery"
if len(d) != 8:
    errors.append("deliver: expected 8 window dumps, got %d" % len(d))
else:
    d0 = mach(d[0], DEL)
    expect(d0 is not None and "ds=0" in d0["rest"] and "zep=0" in d0["rest"], "deliver: the delivery station is not on and waiting at level 14: %s" % (d0,))
    expect(hands(d[0]) is not None and hands(d[0])[0].endswith("(dark,infusion,filling:2)"), "deliver: she does not hold the infusion bar: %s" % (hands(d[0]),))
    expect(hands(d[1]) is not None and hands(d[1])[0].endswith("(dark,infusion,filling:2,topping:0)"), "deliver: she does not hold the topped bar: %s" % (hands(d[1]),))
    expect("machine_topping" in d[1] and "machine_infusion" in d[1], "deliver: the topping and infusion machines are not both on at level 14")
    d2 = mach(d[2], DEL)
    expect(d2 is not None and "ds=1" in d2["rest"] and "want=(,infusion,filling:2,topping:0)" in d2["rest"],
           "deliver: the order is not showing, or it is not the level's first (an infusion bar, option 2, topping 0): %s" % (d2,))
    pat = re.search(r"pat=([\d.]+)", d2["rest"]) if d2 else None
    expect(pat is not None and 90 < float(pat.group(1)) <= 100, "deliver: the order's patience is not about 100 when it shows: %s" % (pat and pat.group(1),))
    d3 = mach(d[3], DEL)
    expect(d3 is not None and ("ds=2" in d3["rest"] or "ds=3" in d3["rest"]) and hands(d[3]) == ("-", "-"), "deliver: the bar was not boxed: %s %s" % (d3, hands(d[3])))
    expect(re.search(r" total=30\.0", dumpline(d[3])) is not None, "deliver: the order was not paid 30 (shape 10 + infusion 10 + topping 10): %s" % field(d[3], "total"))
    states = [mach(x, DEL) for x in d[3:8]]
    zeps = [re.search(r"zep=(\d+)", st["rest"]).group(1) if st else None for st in states]
    expect("6" in zeps, "deliver: the zeppelin never took the box (its state never 6): %s" % zeps)
    expect(zeps[-1] == "0", "deliver: the zeppelin is still about at the end: %s" % zeps)
    expect(states[-1] is not None and "ds=0" in states[-1]["rest"], "deliver: the delivery station is not waiting for the next order at the end")
    imgs = {k: png(v) for k, v in paths.items()}
    if all(imgs.values()):
        expect(region_diff(imgs["order14"], imgs["bubble14"], 560, 150, 800, 300) > 250, "deliver: no order bubble over the delivery station")
        expect(region_diff(imgs["bubble14"], imgs["box1"], 560, 150, 800, 300) > 250, "deliver: the delivery station does not change when it boxes")
        expect(region_diff(imgs["box2"], imgs["zep3"], 0, 0, 800, 200) > 400, "deliver: the zeppelin does not show over the shop")


# ---- run "belt": level 51, the conveyor belt and the decay ----
text = BOOT + DIARY + "wait 3000\nlua SetCurrentLevel(51)\nlua LevelSetup()\nwait 2500\n"
text += click(75, 518) + "wait 3500\n" + click(155, 440) + "wait 1500\n"
text += click(456, 573) + "wait 2500\n" + click(500, 480) + "wait 1500\ndump\n"          # 0 the dark bar in hand
text += "expect-lua 10 GetManagedReal('chocolate_decay_rate',0) > 0.004 and GetManagedReal('chocolate_decay_rate',0) < 0.006\n"
text += click(612, 300) + "wait 1500\ndump\nscreenshot @belt1@\n"                       # 1 on the belt
text += "wait 2500\ndump\nscreenshot @belt2@\n"                                          # 2 past the first counters
text += "wait 4000\ndump\nscreenshot @belt3@\n"                                          # 3 taken by now
text += "wait 3000\ndump\n"                                                              # 4
text += click(75, 518) + "wait 3500\n" + click(155, 440) + "wait 1500\n"
text += click(456, 573) + "wait 2500\n" + click(500, 480) + "wait 1500\ndump\n"          # 5 a second bar in hand
text += "wait 15000\ndump\n"                                                             # 6 not yet spent (20 s of life)
text += "wait 10000\ndump\nscreenshot @gone51@\n"                                        # 7 spent and dropped
text += "expect-lua 10 GetManagedInt('decayed_this_level',0) == 1\n"
shots51 = ("belt1", "belt2", "belt3", "gone51")
out, d, paths = run("belt", text, shots=shots51)
if len(d) != 8:
    errors.append("belt: expected 8 window dumps, got %d" % len(d))
else:
    expect(hands(d[0]) is not None and hands(d[0])[0].endswith("(dark,bar)"), "belt: she does not hold the dark bar: %s" % (hands(d[0]),))
    expect(" belt=-" in d[0], "belt: the belt is not empty at the start: %s" % field(d[0], "belt"))
    b1 = field(d[1], "belt")
    expect(b1 not in (None, "-") and hands(d[1]) == ("-", "-"), "belt: the bar is not on the belt: %s %s" % (b1, hands(d[1])))
    b2 = field(d[2], "belt")
    expect(b2 not in (None, "-") and b1 not in (None, "-") and float(b2.split("@")[0]) > float(b1.split("@")[0]),
           "belt: the bar did not move along the belt: %s -> %s" % (b1, b2))
    # a customer took it: some customer's patience rose by more than the drain between two dumps
    rose = 0
    for k in (1, 2, 3, 4):
        g0, g1 = guests(d[k]), guests(d[k + 1])
        for name, v in g0.items():
            if name in g1:
                rose = max(rose, g1[name][2] - v[2])
    expect(rose > 15, "belt: no customer took the sample from the belt (largest patience rise %.1f)" % rose)
    expect(" belt=-" in d[3] or " belt=-" in d[4], "belt: the bar is still on the belt after the customers had their turn: %s" % field(d[4], "belt"))
    expect(hands(d[5]) is not None and hands(d[5])[0].endswith("(dark,bar)"), "belt: she does not hold the second bar: %s" % (hands(d[5]),))
    expect(hands(d[6]) is not None and hands(d[6])[0].endswith("(dark,bar)"), "belt: the second bar is gone before its 20 s are up: %s" % (hands(d[6]),))
    expect(hands(d[7]) == ("-", "-"), "belt: the decayed bar is still in her hand: %s" % (hands(d[7]),))
    imgs = {k: png(v) for k, v in paths.items()}
    if all(imgs.values()):
        expect(region_diff(imgs["belt1"], imgs["belt2"], 100, 200, 700, 330) > 100, "belt: the belt looks the same while the bar rides it")

for e in errors:
    print("FAIL:", e)
print("greatcc_machines: %s" % ("FAILED" if errors else "ok"))
sys.exit(1 if errors else 0)
