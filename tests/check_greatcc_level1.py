"""The Great Chocolate Chase (docs/design/greatcc.md slice 13): level 1 played from the first customer to the results, with real clicks,
on the real scripts and art.

Every run is `--headless --userdir <scratch> --seed 1` and asserts: exit 0; no [lua-error] / [engine-error] / [native-error] /
[content]; the [unimplemented] and [unimplemented-type] sets equal the run's allow-lists (`DoMetrics` in the pause menu
and nothing else); the
only textures the scripts name that the install does not ship are the shop's menu overlay, its full-screen backdrop, the trim of the
yes/no dialog and the yachtsman's angry-idle image (the sheet names `idle_angry.png`, the install ships `idle_angry.jpg`).

Run "pass": the Options screen's Tutorials box is switched off (the tutorials have their own scenario, check_greatcc_tutorial1.py),
then Story Mode, level 1. The customers arrive at their counters with a menu bubble; she visits
one, which reads the menu for five seconds and then shows its order; she takes a bottle from the sauce machine and serves it, the
customer fades out and leaves its money on the counter, the next visit collects it (the register counts up, "$23" rises from the
pile); a bar goes through the mixer and the press and is served the same way, and two more bars and a second bottle follow. The
dump is checked at each step for the customers' states, the price owed, her hands and the level's total. When the last customer
has gone the results say "Level Passed" with the goal and master scores and six customers served, level 2 is the current level, the
highest completed level is 1 and the cash is saved; Continue loads level 2, and its pause menu's Map leads to the world map where
globe 1 is done and globe 2 is the current one.
Run "again": a second process with the same player: the saved cash, the highest level and the current level are still there, the
map shows the same, and level 2 starts with the cash of the first.
Run "angry": nobody plays level 1: the customers lose patience (the faces and bubbles change through the moods) and leave angry, no
money is left, the results say "Level Failed" with four customers lost and level 1 is still the current level.
Run "locked": with Tutorials on, the first customer's tutorial locks the level: a click on the sauce machine does nothing, a click on
the customer is the one that works.
Screenshots of each stage are 800x600 PNGs; the pixels the scripts name are checked."""
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
TYPES = set()
CALLS = set()
END_CALLS = CALLS

NEW_PLAYER = """expect-window scripts/mainmenu.lua 20000
wait 1000
click 333 447
wait 1500
type Zed
wait 100
click 310 368
wait 3500
"""
TUTORIALS_OFF = """click 625 488
wait 3000
click 450 565
wait 1500
click 362 395
wait 300
dump
click 404 486
wait 1500
dump
"""
STORY = """move 150 258
wait 700
click 150 258
"""
# Level 1 opens with its diary page; the clock starts when the dialog has bounced away after OK.
DIARY = """expect-window scripts/plotwindow.lua 30000
wait 450
move 585 500
wait 50
move 585 500
wait 50
click 585 500
"""
STORY_FIRST = """click 625 488
wait 3000
""" + STORY


def run(tag, script_text, calls, shots=(), user=None):
    user = os.path.join(a.workdir, "user_" + (user or tag))
    log = os.path.join(a.workdir, tag + ".log")
    script = os.path.join(a.workdir, tag + ".script")
    paths = {s: os.path.join(a.workdir, "%s_%s.png" % (tag, s)) for s in shots}
    for name, path in paths.items():
        script_text = script_text.replace("@%s@" % name, path)
    with open(script, "w") as f:
        f.write(script_text)
    p = subprocess.run([a.openconche, "--headless", "--assets", a.assets, "--userdir", user, "--seed", "1", "--script", script,
                        "--log", log], capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=3600)
    out = (open(log, encoding="utf-8", errors="replace").read() if os.path.exists(log) else "") + "\n" + p.stdout + "\n" + p.stderr
    if p.returncode != 0:
        errors.append("%s: exit status %d" % (tag, p.returncode))
    for t in ("[lua-error]", "[engine-error]", "[native-error]", "[content]", "[script] FAIL", "[expect-lua]"):
        if t in out:
            errors.append("%s: %s in the output: %s" % (tag, t, next(l for l in out.split("\n") if t in l)))
    seen = set(re.findall(r"^(?:\[\+\d+ms\] )?\[unimplemented\] (\S+)", out, re.M))
    if seen != calls:
        errors.append("%s: [unimplemented] set differs: extra %s, missing %s" % (tag, sorted(seen - calls), sorted(calls - seen)))
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


def click(x, y):
    return "move %d %d\nwait 50\nclick %d %d\n" % (x, y, x, y)


def shop(d):
    """The window line of the shop and what it says: the customers, her hands, the total, the machines."""
    m = re.search(r'^\s*ShopWindow "sprite_window" .*$', d, re.M)
    if not m:
        return None
    line = m.group(0)
    out = {"line": line}
    for key, rx in (("level", r" level=(\d+)"), ("active", r" active=(\d+)"), ("pending", r" pending=(\d+)"), ("over", r" over=(\d+)"),
                    ("hands", r" hands=(\S+)\|(\S+)"), ("total", r" total=(\S+)"), ("combo", r" combo=(\d+)/(\d+)"), ("popup", r" popup=(\d)")):
        mm = re.search(rx, line)
        out[key] = mm.groups() if mm and len(mm.groups()) > 1 else (mm.group(1) if mm else None)
    out["total"] = float(out["total"]) if out["total"] else 0.0
    cs = {}
    mm = re.search(r" customers=(\S+)", line)
    for c in (mm.group(1).split(";") if mm else []):
        cm = re.match(r"([^@]+)@(\w+):s=(\d+),m=(\d+),p=(-?[\d.]+),\$=(\d+),o=(\d+),w=(\S*)", c)
        if cm:
            cs[cm.group(1)] = {"station": cm.group(2), "s": int(cm.group(3)), "m": int(cm.group(4)), "p": float(cm.group(5)),
                               "price": int(cm.group(6)), "orders": int(cm.group(7)), "wanted": cm.group(8)}
    out["customers"] = cs
    return out


def at(cs, station):
    """The customer at a counter (the customers without a name in the level file have generated ones)."""
    for n, c in cs.items():
        if c["station"] == station:
            return c
    return None


def top(d):
    m = re.findall(r'^  Modal "([^"]*)" .*script="([^"]*)"', d, re.M)
    return m[-1][1] if m else None


def timer(d):
    m = re.search(r'^\s*RTimer "game_timer" .* label="([^"]*)" .* timer=(\d+)/(\d+) (running|stopped) (down|up)$', d, re.M)
    return {"label": m.group(1), "elapsed": int(m.group(2)), "limit": int(m.group(3)), "running": m.group(4) == "running"} if m else None


# Where things are on screen (centres): the customers, the piles of money, the machines.
C1, C2, C4, C6 = (65, 300), (155, 200), (378, 140), (553, 150)
M1, M2, M4, M6 = (128, 293), (202, 247), (372, 209), (553, 222)
MIXB, TANK, PRESSB, PRESS, SAUCE = (80, 525), (155, 440), (456, 575), (500, 480), (700, 270)
OK, YES = (740, 550), (310, 373)
MENU, MAP = (630, 35), (397, 230)
REGISTER = (680, 15, 790, 30)  # the odometer strip at the top right
PILE1 = (100, 270, 165, 315)   # the first counter's pile of money


class Steps:
    """A script made of clicks and waits with a dump after each step that asks for one."""

    def __init__(self, head):
        self.text = head
        self.n = 0
        self.names = {}

    def wait(self, ms, name=None, shot=None):
        self.text += "wait %d\n" % ms
        if name:
            self.names[name] = self.n
            self.n += 1
            self.text += "dump\n"
        if shot:
            self.text += "screenshot @%s@\n" % shot

    def click(self, p, ms, name=None, shot=None):
        self.text += click(*p)
        self.wait(ms, name, shot)


# --------------------------------------------------------------------------------------------------------------------- pass
s = Steps(NEW_PLAYER + TUTORIALS_OFF + STORY)
s.names["opt"] = None
# (the two dumps of the options screen come first: 0 = tutorials off in the options, 1 = back at the main menu)
s.n = 2
s.text += DIARY
s.wait(4850, "arrive", shot="arrive")
s.click(C1, 300, "reading", shot="reading")
s.click(C2, 300)
s.click(MIXB, 1500)
s.click(SAUCE, 3000, "bottle", shot="order")
s.click(C1, 3500, "served", shot="served")
s.click(M1, 500, "paid", shot="paid")
s.click(TANK, 1500)
s.click(PRESSB, 4500)
s.click(PRESS, 800, "bar")
s.click(C2, 2500, "served2")
s.click(M2, 1000, "paid2")
for c in (C4, C6, C1, C2):
    s.click(c, 300)


def bar(target, pile, tag):
    s.click(MIXB, 7000)
    s.click(TANK, 1500)
    s.click(PRESSB, 4500)
    s.click(PRESS, 800)
    s.click(target, 3000, "served" + tag)
    s.click(pile, 1000, "paid" + tag)


bar(C4, M4, "3")
bar(C6, M6, "4")
bar(C2, M2, "5")
s.click(SAUCE, 3500)
s.click(C1, 3000, "served6")
s.click(M1, 1000, "paid6")
s.text += "expect-window scripts/endlevel.lua 60000\nwait 1500\ndump\nscreenshot @results@\n"
s.names["results"] = s.n
s.n += 1
s.text += ("""expect-lua 100 GetHighestComplete() == 1
expect-lua 100 GetCurrentLevel() == 2
expect-lua 100 GetManagedInt('total_worth', 0) >= 100
expect-lua 100 GetManagedInt('current_worth', 0) >= 100
click %d %d
""" + DIARY + """wait 4850
dump
key Escape
wait 1500
click 397 230
wait 1500
click %d %d
wait 3500
dump
screenshot @map@
""") % (OK + YES)
s.names["level2"] = s.n
s.names["map"] = s.n + 1
SHOTS = ("arrive", "reading", "order", "served", "paid", "results", "map")
out, d, paths = run("pass", s.text, END_CALLS | {"DoMetrics"}, shots=SHOTS)
N = s.names
if len(d) != s.n + 2:
    errors.append("pass: expected %d window dumps, got %d" % (s.n + 2, len(d)))
else:
    expect('Button "options_tutorials_checkbox"' in d[0] and re.search(r'Button "options_tutorials_checkbox" .* off', d[0]),
           "pass: the Options screen did not switch Tutorials off")
    expect(top(d[1]) == "scripts/worldmap.lua", "pass: Close did not go back to the map: %s" % top(d[1]))
    arrive = shop(d[N["arrive"]])
    expect(arrive is not None and arrive["level"] == "1", "pass: no shop of level 1 in the dump")
    cs = arrive["customers"] if arrive else {}
    expect("tut_cust_1" in cs and "tut_cust_2" in cs and cs["tut_cust_1"]["s"] == 1 and cs["tut_cust_2"]["s"] == 1,
           "pass: the first two customers have not arrived at their counters: %s" % cs)
    expect(cs.get("tut_cust_1", {}).get("station") == "station01" and cs.get("tut_cust_2", {}).get("station") == "station02",
           "pass: the customers are not at the counters their level file names: %s" % cs)
    expect(arrive is not None and int(arrive["active"]) == 2 and int(arrive["pending"]) == 6,
           "pass: two customers are in the shop and six wait for their time at this point: %s" % ((arrive and (arrive["active"], arrive["pending"])),))
    expect(all(c["p"] > 90 for c in cs.values()), "pass: a customer that has just arrived has lost patience: %s" % cs)
    r = shop(d[N["reading"]])
    expect(r is not None and r["customers"]["tut_cust_1"]["s"] == 2, "pass: a visit did not start the menu reading: %s" % (r and r["customers"]))
    b = shop(d[N["bottle"]])
    expect(b is not None and b["hands"][0].startswith("object_") and b["hands"][1] == "-", "pass: she does not carry a bottle: %s" % (b and b["hands"],))
    expect(b is not None and b["customers"]["tut_cust_1"]["s"] == 3 and b["customers"]["tut_cust_1"]["wanted"] == "sauce",
           "pass: the first customer does not want a sauce after the menu: %s" % (b and b["customers"].get("tut_cust_1"),))
    sv = shop(d[N["served"]])
    c1 = sv["customers"].get("tut_cust_1") if sv else None
    expect(c1 is not None and c1["s"] == 6 and c1["price"] == 15 and c1["p"] > 100, "pass: the sauce was not paid for with its bonus: %s" % c1)
    expect(sv is not None and sv["hands"] == ("-", "-"), "pass: the bottle is still in her hand after serving: %s" % (sv and sv["hands"],))
    expect(sv is not None and sv["total"] == 3.0, "pass: nothing but the menu chain's first bonus should be in the total before the money is taken: %s" % (sv and sv["total"],))
    pd = shop(d[N["paid"]])
    expect(pd is not None and "tut_cust_1" not in pd["customers"], "pass: the paid customer is still in the shop: %s" % (pd and pd["customers"].keys()))
    expect(pd is not None and 25 < pd["total"] < 29, "pass: the sale and tip of the first customer are not 24 on the level's total: %s" % (pd and pd["total"],))
    expect(pd is not None and "tut_cust_3" in pd["customers"], "pass: no customer took the free counter: %s" % (pd and pd["customers"].keys()))
    bar1 = shop(d[N["bar"]])
    expect(bar1 is not None and bar1["hands"][0].endswith("(dark,bar)"), "pass: she does not hold a dark bar: %s" % (bar1 and bar1["hands"],))
    s2 = shop(d[N["served2"]])
    expect(s2 is not None and s2["customers"].get("tut_cust_2", {}).get("s") == 6 and s2["customers"]["tut_cust_2"]["price"] == 10,
           "pass: the bar did not pay 10: %s" % (s2 and s2["customers"].get("tut_cust_2"),))
    p2 = shop(d[N["paid2"]])
    expect(p2 is not None and p2["total"] > pd["total"] + 15, "pass: the second customer's money did not reach the total: %s -> %s" % (pd["total"], p2 and p2["total"]))
    last = shop(d[N["paid6"]])
    expect(last is not None and last["total"] >= 100, "pass: the level's total is under the goal at the end: %s" % (last and last["total"],))
    expect(last is not None and int(last["active"]) == 0, "pass: customers are still in the shop at the end: %s" % (last and last["customers"],))
    res = d[N["results"]]
    expect(top(res) == "scripts/endlevel.lua", "pass: the results dialog is not on top: %s" % top(res))
    expect("Level Passed" in res, "pass: the results do not say Level Passed")
    expect('label="$100"' in res and 'label="$160"' in res, "pass: the goal and master scores are not shown")
    expect(re.search(r'"endlevel_today_amt" [^\n]*label="\$1\d\d"', res) is not None, "pass: the day's score is not shown")
    expect(re.search(r'"endlevel_served_amt" [^\n]*label="6"', res) is not None, "pass: the results do not say six customers were served")
    l2 = shop(d[N["level2"]])
    expect(top(d[N["level2"]]) == "scripts/game.lua" and l2 is not None and l2["level"] == "2", "pass: Continue did not load level 2: %s" % (l2 and l2["level"]))
    expect(top(d[N["map"]]) == "scripts/worldmap.lua", "pass: Map did not lead to the world map: %s" % top(d[N["map"]]))
imgs = {k: png(v) for k, v in paths.items()}
if all(imgs.values()):
    # the menu bubbles and the customers: the scene differs from the bare level around the counters' customers
    expect(region_diff(imgs["arrive"], imgs["reading"], 20, 100, 200, 380) > 100, "pass: the first customer looks the same while it reads the menu")
    expect(region_diff(imgs["reading"], imgs["order"], 20, 100, 200, 220) > 150, "pass: the order does not show over the first customer")
    # serving: the customer and its bubble are gone, a pile of money is on the counter (a reddish dollar over the bamboo)
    expect(region_diff(imgs["order"], imgs["served"], 20, 100, 200, 380) > 400, "pass: serving did not change the first counter")
    red = 0
    for y in range(PILE1[1], PILE1[3]):
        for x in range(PILE1[0], PILE1[2]):
            c = px(imgs["served"], x, y)
            if c[0] > 100 and c[0] > c[1] + 40 and c[0] > c[2] + 40 and c[1] < 90:
                red += 1
    expect(red > 15, "pass: no dollar on the first counter (%d reddish pixels)" % red)
    # the register: the odometer shows the level's total after the pickup, and not before
    expect(region_diff(imgs["served"], imgs["paid"], *REGISTER) > 40, "pass: the register did not change when the money was collected")
    # the results sheet: the cream paper in the middle of the dialog
    c = px(imgs["results"], 400, 300)
    expect(c[0] > 200 and c[1] > 190 and c[2] > 180, "pass: the results sheet is not cream: %s" % (c,))
    # the map after level 1: globe 1 is no longer pink (done), globe 2 is (the current one)
    g1, g2 = px(imgs["map"], 144, 252), px(imgs["map"], 176, 262)
    expect(not (g1[0] > 200 and g1[2] > g1[1] + 30), "pass: globe 1 is still the pink current one on the map: %s" % (g1,))
    expect(g2[0] > 200 and g2[2] > g2[1] + 30, "pass: globe 2 is not the current globe on the map: %s" % (g2,))

# -------------------------------------------------------------------------------------------------------------------- again
text = """expect-window scripts/mainmenu.lua 20000
wait 1500
dump
expect-lua 100 GetHighestComplete() == 1
expect-lua 100 GetManagedInt('total_worth', 0) >= 100
click 333 447
wait 3500
dump
screenshot @map@
move 179 270
wait 700
click 179 270
""" + DIARY + """wait 4850
dump
expect-lua 100 GetCurrentLevel() == 2
expect-lua 100 GetManagedInt('current_worth', 0) == GetManagedInt('total_worth', -1)
"""
out, d, paths = run("again", text, CALLS, shots=("map",), user="pass")
if len(d) != 3:
    errors.append("again: expected 3 window dumps, got %d" % len(d))
else:
    expect(top(d[0]) == "scripts/mainmenu.lua", "again: the saved player is not at the main menu: %s" % top(d[0]))
    expect(top(d[1]) == "scripts/worldmap.lua", "again: Story did not open the map: %s" % top(d[1]))
    sh = shop(d[2])
    expect(sh is not None and sh["level"] == "2", "again: globe 2 did not load level 2: %s" % (sh and sh["level"]))
imgs = {k: png(v) for k, v in paths.items()}
if all(imgs.values()):
    g1, g2 = px(imgs["map"], 144, 252), px(imgs["map"], 176, 262)
    expect(not (g1[0] > 200 and g1[2] > g1[1] + 30), "again: globe 1 is the pink current one on the saved map: %s" % (g1,))
    expect(g2[0] > 200 and g2[2] > g2[1] + 30, "again: globe 2 is not the current globe on the saved map: %s" % (g2,))

# -------------------------------------------------------------------------------------------------------------------- angry
s = Steps(NEW_PLAYER + TUTORIALS_OFF + STORY)
s.n = 2
s.text += DIARY
s.wait(2450, "t4", shot="early")
s.wait(30000, "t34", shot="t34")
s.wait(40000, "t74", shot="t74")
s.wait(30000, "t104", shot="t104")
s.wait(25000, "t124", shot="t124")
s.text += "expect-window scripts/endlevel.lua 100000\nwait 1500\ndump\nscreenshot @results@\n"
s.names["results"] = s.n
s.n += 1
s.text += """expect-lua 100 GetCurrentLevel() == 1
expect-lua 100 GetHighestComplete() == 0
expect-lua 100 GetManagedInt('total_worth', -1) == 0
"""
out, d, paths = run("angry", s.text, CALLS, shots=("early", "t34", "t74", "t104", "t124", "results"))
N = s.names
if len(d) != s.n:
    errors.append("angry: expected %d window dumps, got %d" % (s.n, len(d)))
else:
    mood = lambda name: [c["m"] for c in shop(d[N[name]])["customers"].values()]
    expect(all(m == 1 for m in mood("t4")), "angry: the customers are not happy when they come: %s" % mood("t4"))
    expect(all(1 <= m <= 3 for m in mood("t34")) and any(m >= 2 for m in mood("t34")), "angry: after half a minute the moods are not happy, content or neutral: %s" % mood("t34"))
    expect(all(3 <= m <= 4 for m in mood("t74")), "angry: after a minute and a half the moods are not neutral or annoyed: %s" % mood("t74"))
    expect(all(m >= 4 for m in mood("t104")) and any(m == 5 for m in mood("t104")), "angry: after almost two minutes they are not annoyed or angry: %s" % mood("t104"))
    t124 = shop(d[N["t124"]])
    expect(t124 is not None and int(t124["active"]) < 4, "angry: the angry customers did not start to leave: %s" % (t124 and t124["customers"].keys()))
    res = d[N["results"]]
    expect(top(res) == "scripts/endlevel.lua" and "Level Failed" in res, "angry: the results do not say Level Failed")
    expect(re.search(r'"endlevel_lost_amt" [^\n]*label="4"', res) is not None, "angry: the results do not say four customers were lost")
    expect(re.search(r'"endlevel_served_amt" [^\n]*label="0"', res) is not None, "angry: the results do not say nobody was served")
    end = shop(d[N["results"]])
    expect(end is not None and end["total"] == 0.0 and int(end["active"]) == 0, "angry: money was paid or customers are left: %s" % (end and end["total"],))
imgs = {k: png(v) for k, v in paths.items()}
if all(imgs.values()):
    # the faces change: the first customer's head at three moods
    expect(region_diff(imgs["early"], imgs["t34"], 20, 100, 110, 260) > 300, "angry: the first customer does not look different after half a minute")
    expect(region_diff(imgs["t34"], imgs["t104"], 20, 100, 110, 260) > 300, "angry: the first customer does not look different when angry")
    c = px(imgs["results"], 400, 300)
    expect(c[0] > 200 and c[1] > 190 and c[2] > 180, "angry: the results sheet is not cream: %s" % (c,))

# -------------------------------------------------------------------------------------------------------------------- locked
text = NEW_PLAYER + STORY_FIRST + DIARY + "wait 4850\n"
text += click(*SAUCE) + "wait 1500\ndump\n"      # the tutorial's first step makes only the customer and the counter clickable
text += click(*C1) + "wait 900\ndump\n"
out, d, paths = run("locked", text, CALLS)
if len(d) != 2:
    errors.append("locked: expected 2 window dumps, got %d" % len(d))
else:
    before, after = shop(d[0]), shop(d[1])
    expect(before is not None and before["customers"].get("tut_cust_1", {}).get("s") == 1, "locked: the customer was visited before the click on it: %s" % (before and before["customers"].get("tut_cust_1"),))
    expect(before is not None and before["hands"] == ("-", "-") and " queue=0 " in before["line"], "locked: the click on the sauce machine went through the lock: %s" % (before and before["hands"],))
    expect(after is not None and after["customers"].get("tut_cust_1", {}).get("s") == 2, "locked: the click on the customer did not reach it: %s" % (after and after["customers"].get("tut_cust_1"),))

for e in errors:
    print("FAIL:", e)
print("greatcc_level1: %s" % ("FAILED" if errors else "ok"))
sys.exit(1 if errors else 0)
