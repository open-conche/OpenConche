"""The Great Chocolate Chase (docs/design/greatcc.md slice 12): the sauce machine, the disposal and the display case, on the real
scripts and art.

Two runs of `--headless --userdir <scratch> --seed 1`, each asserting: exit 0; no [lua-error] / [engine-error] / [native-error] /
[content]; the [unimplemented] and [unimplemented-type] sets are empty; the only
textures the scripts name that the install does not ship are the shop's menu overlay, its full-screen backdrop and the trim of the
yes/no dialog.

Level 1 (a new player through Story Mode), real clicks, Tutorials switched off in the Options screen first (the first customer's tutorial would lock the level): the bin at the left is on the screen (the
display case is not: it needs `kDisplay1`, from level 2) and the avatar walks straight; a click on the sauce machine sends her to it
and she takes the bottle (the dump shows her hand, the machine's cooldown and an empty hand slot in the machine); the tank
refills as a fill bar during the cooldown; a click on the bin throws the bottle (the counters `num_disposed` and
`disposed_this_level`, the game state `++TrashUsed` and `TrashUsedItemID`, the bin's animation). When the cooldown ends the
machine runs its four states (1 arm down, 2 fill, 3 bottle there and unlocked, 0), and afterwards the machine looks exactly as at the
start. A second bottle is taken, and the level is loaded again in the middle of the next refill: the machine starts clean.

Level 2 (the same player; the level is set with `SetCurrentLevel(2)` and `LevelSetup()` is called again, which is what a restart
does): the centre table is on the screen and the avatar's walk to the sauce machine goes through the zone point `station16_north`
instead of straight; the bottle goes on the table (the dump shows it, the screenshot shows it on the table's rim) and a click on
the table takes it back; a bar made at the mixer and the press goes on the table and comes back the same way; the bin throws what
she carries. Screenshots of each stage are looked at by the region checks below.
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

MISSING = {"ui/button/hud_menu_overlay", "ui/fullscreendialog", "ui/story/btn_trim_left"}
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



def run(tag, script_text, shots=()):
    user = os.path.join(a.workdir, "user_" + tag)
    log = os.path.join(a.workdir, tag + ".log")
    script = os.path.join(a.workdir, tag + ".script")
    paths = {s: os.path.join(a.workdir, "%s_%s.png" % (tag, s)) for s in shots}
    for name, path in paths.items():
        script_text = script_text.replace("@%s@" % name, path)
    with open(script, "w") as f:
        f.write(script_text)
    p = subprocess.run([a.openconche, "--headless", "--assets", a.assets, "--userdir", user, "--seed", "1", "--script", script,
                        "--log", log], capture_output=True, text=True, encoding="utf-8",
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


def sauce(d):
    m = re.search(r"station08/machine_sauce:s=(\d+),o=-?\d+,L=(\d),P=(\d),held=-,cd=(\d+),bottle=(\S+?)(?:;|$|\s)", dumpline(d))
    if not m:
        return None
    return {"s": int(m.group(1)), "L": m.group(2) == "1", "P": m.group(3) == "1", "cd": int(m.group(4)), "bottle": m.group(5)}


def display(d):
    m = re.search(r" display=(\d),(\d)/(\d),(\S+)", dumpline(d))
    if not m:
        return None
    return {"shown": m.group(1) == "1", "count": int(m.group(2)), "cap": int(m.group(3)), "items": m.group(4)}


def trash(d):
    m = re.search(r" trash=(\d+),(\S+)", dumpline(d))
    return (int(m.group(1)), m.group(2)) if m else None


def trail(d):
    m = re.search(r" trail=(\S+)", dumpline(d))
    return m.group(1).split(",") if m else None


def holds(h, i, prefix):
    return h is not None and h[i].startswith(prefix)


def click(x, y):
    return "move %d %d\nwait 50\nclick %d %d\n" % (x, y, x, y)


SAUCE, BIN, TABLE = (700, 270), (105, 395), (412, 385)
SAUCE_BOX = (640, 190, 740, 310)
BIN_BOX = (60, 350, 160, 440)
TABLE_BOX = (340, 290, 490, 460)
AWAY = "move 770 40\nwait 50\n"

# ---- level 1 ----
text = BOOT + DIARY + "wait 5350\ndump\nscreenshot @idle@\n"                      # 0
text += click(*SAUCE) + "wait 5000\n" + "dump\nscreenshot @held@\n"          # 1
text += "wait 2000\n" + AWAY + "screenshot @cool@\n"                          # (no dump)
text += click(*BIN) + "wait 3000\ndump\nscreenshot @bin@\n"                   # 2
text += "wait 1500\ndump\n"                                                    # 3
text += "expect-lua 500 GetManagedInt('disposed_this_level',0)==1 and GetManagedInt('num_disposed',0)==1\n"
text += AWAY + "wait 5250\ndump\nscreenshot @r1@\n"                            # 4 the cooldown has just ended
text += "wait 400\ndump\n"                                                     # 5
text += "wait 600\ndump\nscreenshot @r2@\n"                                    # 6
text += "wait 1000\ndump\nscreenshot @r3@\n"                                   # 7
text += "wait 1000\ndump\nscreenshot @done@\n"                                 # 8
text += "lua LevelSetup()\nwait 5000\n"                                         # the level is loaded again (a fresh 30 s)
text += click(*SAUCE) + "wait 4000\ndump\n"                                    # 9 the new bottle is taken
text += "wait 14600\ndump\n"                                                   # 10 the next refill, in its first state
text += "lua LevelSetup()\nwait 5000\ndump\nscreenshot @reload@\n"            # 11 the level is loaded again in the middle of it
text += click(*SAUCE) + "wait 4000\ndump\n"                                    # 12
names1 = ("idle", "held", "cool", "bin", "r1", "r2", "r3", "done", "reload")
out, d, paths = run("level1", text, shots=names1)
if len(d) != 13:
    errors.append("level1: expected 13 window dumps, got %d" % len(d))
else:
    s0 = sauce(d[0])
    expect(s0 == {"s": 0, "L": False, "P": False, "cd": 0, "bottle": "sauce"}, "level1: the sauce machine does not start with a bottle and no cooldown: %s" % s0)
    expect(display(d[0]) is not None and not display(d[0])["shown"], "level1: the display case is shown on level 1: %s" % display(d[0]))
    expect(hands(d[0]) == ("-", "-"), "level1: her hands are not empty at the start: %s" % (hands(d[0]),))
    s1 = sauce(d[1])
    expect(holds(hands(d[1]), 0, "object_") and hands(d[1])[1] == "-", "level1: she does not hold the bottle: %s" % (hands(d[1]),))
    expect(s1 is not None and s1["bottle"] == "-" and 0 < s1["cd"] <= 15000 and s1["s"] == 0 and not s1["L"],
           "level1: the machine after the pick-up: %s" % s1)
    expect(trail(d[1]) is not None and "station16_north" not in trail(d[1]), "level1: she did not walk straight: %s" % (trail(d[1]),))
    # the bin
    expect(hands(d[2]) == ("-", "-"), "level1: the bin did not take the bottle: %s" % (hands(d[2]),))
    expect(trash(d[2]) == (1, "sauce"), "level1: the trash game state is not 1,sauce: %s" % (trash(d[2]),))
    expect(trail(d[2]) is not None and trail(d[2])[-1] == "station15", "level1: she is not at the bin: %s" % (trail(d[2]),))
    # the refill runs its states in order and the bottle is back
    seq = [sauce(x) for x in d[4:9]]
    expect(all(seq), "level1: no sauce machine in a refill dump")
    if all(seq):
        states = [x["s"] for x in seq]
        expect(states[0] in (0, 1) and states[1] == 1 and states[2] == 2 and states[3] == 3 and states[4] == 0, "level1: the refill states are %s, not 1,1,2,3,0" % states)
        expect(seq[1]["L"] and seq[2]["L"] and not seq[3]["L"] and not seq[4]["L"], "level1: the locks of the refill are %s" % [x["L"] for x in seq])
        expect(all(x["cd"] == 0 for x in seq[1:]), "level1: a cooldown is running during the refill: %s" % seq)
        expect(seq[4]["bottle"] == "sauce" and seq[4]["P"], "level1: the new bottle is not there at the end: %s" % seq[4])
    s9 = sauce(d[9])
    expect(s9 is not None and s9["bottle"] == "-" and s9["cd"] > 0 and holds(hands(d[9]), 0, "object_"), "level1: the second bottle was not taken: %s %s" % (s9, hands(d[9])))
    s10 = sauce(d[10])
    expect(s10 is not None and s10["s"] >= 1 and s10["L"] and s10["bottle"] == "sauce", "level1: the second refill is not running before the reload: %s" % s10)
    s11 = sauce(d[11])
    expect(s11 == {"s": 0, "L": False, "P": False, "cd": 0, "bottle": "sauce"}, "level1: the machine is not clean after the reload: %s" % s11)
    expect(hands(d[11]) == ("-", "-"), "level1: her hands are not empty after the reload: %s" % (hands(d[11]),))
    s12 = sauce(d[12])
    expect(s12 is not None and s12["bottle"] == "-" and s12["cd"] > 0 and holds(hands(d[12]), 0, "object_"), "level1: no bottle can be taken after the reload: %s" % s12)
    imgs = {k: png(v) for k, v in paths.items()}
    if all(imgs.values()):
        expect(region_diff(imgs["idle"], imgs["held"], *SAUCE_BOX) > 400, "level1: the sauce machine does not change when the bottle is taken")
        expect(region_diff(imgs["held"], imgs["cool"], *SAUCE_BOX) > 150, "level1: the fill bar does not move during the cooldown")
        expect(region_diff(imgs["idle"], imgs["bin"], *BIN_BOX) > 150, "level1: the bin does not light up when it takes the bottle")
        expect(region_diff(imgs["r1"], imgs["r2"], *SAUCE_BOX) > 300, "level1: the machine does not move during the refill")
        expect(region_diff(imgs["r2"], imgs["r3"], *SAUCE_BOX) > 300, "level1: the bottle does not appear and fill")
        expect(region_diff(imgs["idle"], imgs["done"], *SAUCE_BOX) < 40, "level1: the machine does not look as at the start after the refill")
        expect(region_diff(imgs["idle"], imgs["reload"], *SAUCE_BOX) < 40, "level1: the machine does not look as at the start after the reload")

# ---- level 2 ----
LEVEL2 = "lua SetCurrentLevel(2)\nlua LevelSetup()\nwait 5000\n"
text = BOOT + DIARY + "wait 2000\n" + LEVEL2 + "dump\nscreenshot @idle@\n"                           # 0
text += click(*SAUCE) + "wait 3500\ndump\nscreenshot @sauce@\n"                              # 1
text += click(*TABLE) + "wait 7500\n" + AWAY + "dump\nscreenshot @put@\n"                    # 2 the bottle is on the table
text += click(*TABLE) + "wait 6500\n" + AWAY + "dump\nscreenshot @taken@\n"                  # 3 and back in her hand
text += click(*BIN) + "wait 6000\ndump\n"                                                    # 4
text += click(80, 525) + "wait 7500\ndump\n"                                                 # 5 the dark bucket is made
text += click(155, 440) + "wait 600\ndump\n"                                                 # 6 and collected
text += click(456, 575) + "wait 6000\ndump\n"                                                # 7 pressed
text += click(500, 480) + "wait 1500\ndump\n"                                                # 8 the bar is in her hand
text += click(*TABLE) + "wait 5500\n" + AWAY + "dump\nscreenshot @bar@\n"                    # 9 and on the table
text += click(*TABLE) + "wait 6500\n" + AWAY + "dump\n"                                      # 10 and back
names2 = ("idle", "sauce", "put", "taken", "bar")
out, d, paths = run("level2", text, shots=names2)
if len(d) != 11:
    errors.append("level2: expected 11 window dumps, got %d" % len(d))
else:
    expect(display(d[0]) == {"shown": True, "count": 0, "cap": 1, "items": "-"}, "level2: the display case is not shown and empty at the start: %s" % display(d[0]))
    t1 = trail(d[1])
    expect(t1 is not None and t1[:2] == ["station01", "station16_north"] and "station08" in t1,
           "level2: her walk to the sauce machine does not go through the north point of the table: %s" % (t1,))
    expect(holds(hands(d[1]), 0, "object_"), "level2: she does not hold the bottle: %s" % (hands(d[1]),))
    expect(display(d[2]) == {"shown": True, "count": 1, "cap": 1, "items": "sauce"} and hands(d[2]) == ("-", "-"),
           "level2: the bottle is not on the table: %s %s" % (display(d[2]), hands(d[2])))
    expect(display(d[3])["count"] == 0 and holds(hands(d[3]), 0, "object_"), "level2: the table did not give the bottle back: %s %s" % (display(d[3]), hands(d[3])))
    expect(hands(d[4]) == ("-", "-") and trash(d[4]) == (1, "sauce"), "level2: the bin did not take the bottle: %s %s" % (hands(d[4]), trash(d[4])))
    expect(holds(hands(d[8]), 0, "chocolate_1") and hands(d[8])[0].endswith("(dark,bar)"), "level2: she does not hold the bar: %s" % (hands(d[8]),))
    expect(display(d[9]) == {"shown": True, "count": 1, "cap": 1, "items": "chocolate(dark,bar)"} and hands(d[9]) == ("-", "-"),
           "level2: the bar is not on the table: %s %s" % (display(d[9]), hands(d[9])))
    expect(display(d[10])["count"] == 0 and holds(hands(d[10]), 0, "chocolate_1") and hands(d[10])[0].endswith("(dark,bar)"),
           "level2: the table did not give the bar back: %s %s" % (display(d[10]), hands(d[10])))
    imgs = {k: png(v) for k, v in paths.items()}
    # the level-1 picture is not kept here: the table is looked for in its own box against the bar-free scenes
    if all(imgs.values()):
        expect(region_diff(imgs["idle"], imgs["put"], *TABLE_BOX) > 150, "level2: the bottle does not show on the table")
        expect(region_diff(imgs["put"], imgs["taken"], *TABLE_BOX) > 150, "level2: the bottle is still on the table after she took it")
        expect(region_diff(imgs["idle"], imgs["bar"], *TABLE_BOX) > 150, "level2: the bar does not show on the table")

for e in errors:
    print("FAIL:", e)
print("greatcc_stations: %s" % ("FAILED" if errors else "ok"))
sys.exit(1 if errors else 0)
