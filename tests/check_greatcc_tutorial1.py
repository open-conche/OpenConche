"""The Great Chocolate Chase (docs/design/greatcc.md slice 14): the tutorials of level 1, with real clicks, on the real scripts and art.

Every run is `--headless --userdir <scratch> --seed 1` and asserts: exit 0; no [lua-error] / [engine-error] / [native-error] /
[content]; the [unimplemented] and [unimplemented-type] sets are empty; the only textures the scripts name that the
install does not ship are the shop's menu overlay, its full-screen backdrop and the trim of the yes/no dialog.

Run "play": a new player with Tutorials on plays level 1 through all three tutorials (5 + 10 + 14 steps) to the results. At each step
the window dump says which tutorial and which step is up, the level clock is stopped while a tutorial is (0 ms through the first, 1200
through the second, 2400 through the third), and a screenshot of the panel is checked: the panel is cream where its text is, the arrow
is orange where the tables put it, the "Skip Tutorials" button is at the corner and the scene is dimmed. A click outside what the step
makes clickable (the sauce machine during the first step, the mixer's tank during the dark-chocolate button's step) does nothing. The
state of each tutorial is saved under its own name; the results say Level Passed and seven customers served.
Run "again": a second process, the same player: the finished tutorial is shown again at the replay of level 1 (it is `repeatable`).
Run "skip": the Skip button during the second step turns Tutorials off: the tutorial fades out and closes, the lock ends, the clicks
the tutorial did not allow now work, the next customer arrives with no tutorial, and the option is saved off. Run "off": a second
process, the same player: level 1 starts with no tutorial and no lockdown. Run "shared": with the fixes off (`fixbugs=0`) every
tutorial writes the one key `tutorial.shared`."""
import argparse, glob, os, re, shutil, struct, subprocess, sys, zlib

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

NEW_PLAYER = """expect-window scripts/mainmenu.lua 20000
wait 1000
click 333 447
wait 1500
type %s
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
# A level opens with its diary page; the clock starts when the dialog has bounced away after OK.
DIARY = """expect-window scripts/plotwindow.lua 30000
wait 450
move 585 500
wait 50
move 585 500
wait 50
click 585 500
"""
# a map for the player who has played before: Story, then globe 1
STORY_AGAIN = """expect-window scripts/mainmenu.lua 20000
wait 1500
click 333 447
wait 3500
move 150 258
wait 700
click 150 258
"""


def run(tag, script_text, calls, shots=(), user=None, extra=()):
    user_dir = os.path.join(a.workdir, "user_" + (user or tag))
    log = os.path.join(a.workdir, tag + ".log")
    script = os.path.join(a.workdir, tag + ".script")
    paths = {s: os.path.join(a.workdir, "%s_%s.png" % (tag, s)) for s in shots}
    for name, path in paths.items():
        script_text = script_text.replace("@%s@" % name, path)
    with open(script, "w") as f:
        f.write(script_text)
    p = subprocess.run([a.openconche, "--headless", "--assets", a.assets, "--userdir", user_dir, "--seed", "1", "--script", script,
                        "--log", log] + list(extra), capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=3600)
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


def progress_file(tag_user):
    found = glob.glob(os.path.join(a.workdir, "user_" + tag_user, "profiles", "*", "greatcc-progress.txt"))
    if not found:
        return {}
    kv = {}
    for line in open(found[0], encoding="utf-8").read().split("\n")[1:]:
        if "=" in line:
            k, v = line.split("=", 1)
            kv[k] = v
    return kv


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


def mean(img, x0, y0, x1, y1):
    """Mean of the three channels over the box (clipped to the picture)."""
    x0, y0, x1, y1 = max(x0, 0), max(y0, 0), min(x1, 800), min(y1, 600)
    t = n = 0
    for y in range(y0, y1):
        for x in range(x0, x1):
            t += sum(px(img, x, y))
            n += 3
    return t / n if n else 0


def count(img, x0, y0, x1, y1, pred):
    x0, y0, x1, y1 = max(x0, 0), max(y0, 0), min(x1, 800), min(y1, 600)
    return sum(1 for y in range(y0, y1) for x in range(x0, x1) if pred(px(img, x, y)))


def orange(c):
    return c[0] > 200 and 110 < c[1] < 200 and c[2] < 70


def green(c):
    return c[1] > c[0] + 15 and c[1] > c[2] + 15


def expect(cond, msg):
    if not cond:
        errors.append(msg)


def click(x, y):
    # two moves: the tutorial window swallows the first move after a click on a panel (its hover is one frame old)
    return "move %d %d\nwait 50\nmove %d %d\nwait 50\nclick %d %d\n" % (x, y, x, y, x, y)


def shop(d):
    m = re.search(r'^\s*ShopWindow "sprite_window" .*$', d, re.M)
    if not m:
        return None
    line = m.group(0)
    out = {"line": line}
    for key, rx in (("level", r" level=(\d+)"), ("active", r" active=(\d+)"), ("pending", r" pending=(\d+)"), ("queue", r" queue=(\d+)"),
                    ("dest", r" dest=(\S+)"), ("trail", r" trail=(\S+)"), ("hands", r" hands=(\S+)\|(\S+)"), ("total", r" total=(\S+)")):
        mm = re.search(rx, line)
        out[key] = mm.groups() if mm and len(mm.groups()) > 1 else (mm.group(1) if mm else None)
    out["total"] = float(out["total"]) if out["total"] else 0.0
    cs = {}
    mm = re.search(r" customers=(\S+)", line)
    for c in (mm.group(1).split(";") if mm else []):
        cm = re.match(r"([^@]+)@(\w+):s=(\d+),m=(\d+),p=(-?[\d.]+),\$=(\d+),o=(\d+),w=(\S*)", c)
        if cm:
            cs[cm.group(1)] = {"station": cm.group(2), "s": int(cm.group(3)), "m": int(cm.group(4)), "p": float(cm.group(5)),
                               "price": int(cm.group(6)), "wanted": cm.group(8)}
    out["customers"] = cs
    return out


def tut(d):
    """(tutorial, step, steps, dim) of the tutorial window in the dump, or None."""
    m = re.search(r'^\s*VTutorialWindow "tutorial_window" .* tutorial=(\S+) step=(\d+)/(\d+)( done)?(?: lines=\d+/\d+)?(?: dim=([\d.]+))?$', d, re.M)
    if not m or m.group(4):
        return None
    return m.group(1), int(m.group(2)), int(m.group(3)), float(m.group(5)) if m.group(5) else 0.0


def lines(d):
    """(drawn, wrapped) lines of the step's panel text in the dump, or None for a step with no panel."""
    m = re.search(r'^\s*VTutorialWindow "tutorial_window" .* lines=(\d+)/(\d+)', d, re.M)
    return (int(m.group(1)), int(m.group(2))) if m else None


def windows(d):
    return re.findall(r'^\s*VTutorialWindow "tutorial_window"', d, re.M)


def timer(d):
    m = re.search(r'^\s*RTimer "game_timer" .* timer=(\d+)/(\d+) (running|stopped) (down|up)$', d, re.M)
    return {"elapsed": int(m.group(1)), "limit": int(m.group(2)), "running": m.group(3) == "running"} if m else None


def top(d):
    m = re.findall(r'^  Modal "([^"]*)" .*script="([^"]*)"', d, re.M)
    return m[-1][1] if m else None


# Where things are on screen (centres): the customers, the piles of money, the machines.
C1, C2, C4, C6 = (65, 300), (155, 200), (378, 140), (553, 150)
M1, M2, M4, M6 = (128, 293), (202, 247), (372, 209), (553, 222)
MIXB, TANK, PRESSB, PRESS, SAUCE = (80, 525), (155, 440), (456, 575), (500, 480), (700, 270)
SKIP = (22, 575)

# The panels of tutorials.lua: (x, y, w, h) and the default arrow's tip (x, y), by tutorial and step; None where there is none.
PANELS = {
    ("tutorial_1", 1): ((125, 17, 200, 185), (71, 175)), ("tutorial_1", 2): ((125, 17, 200, 165), None),
    ("tutorial_1", 3): ((440, 100, 240, 165), (700, 200)), ("tutorial_1", 4): ((125, 17, 200, 165), (71, 175)),
    ("tutorial_1", 5): ((155, 17, 200, 175), (126, 275)),
    ("tutorial_2", 1): ((225, 17, 200, 195), (171, 100)), ("tutorial_2", 2): ((225, 17, 200, 165), None),
    ("tutorial_2", 3): ((240, 410, 200, 175), (74, 495)), ("tutorial_2", 4): ((240, 410, 200, 165), None),
    ("tutorial_2", 5): ((240, 410, 200, 165), (200, 430)), ("tutorial_2", 6): ((575, 410, 200, 165), (455, 550)),
    ("tutorial_2", 7): None, ("tutorial_2", 8): ((575, 410, 200, 165), (495, 495)),
    ("tutorial_2", 9): ((225, 17, 200, 195), (171, 100)), ("tutorial_2", 10): ((265, 17, 225, 195), (191, 225)),
    ("tutorial_3", 1): ((125, 17, 245, 175), (71, 175)), ("tutorial_3", 2): ((225, 17, 200, 165), (160, 105)),
    ("tutorial_3", 3): ((225, 17, 200, 185), None), ("tutorial_3", 4): ((450, 100, 200, 250), (700, 200)),
    ("tutorial_3", 5): ((125, 17, 200, 165), (71, 175)), ("tutorial_3", 6): ((240, 340, 200, 235), (74, 495)),
    ("tutorial_3", 7): None, ("tutorial_3", 8): ((240, 410, 200, 165), (200, 430)),
    ("tutorial_3", 9): ((575, 410, 200, 165), (455, 550)), ("tutorial_3", 10): None,
    ("tutorial_3", 11): ((575, 410, 200, 165), (495, 495)), ("tutorial_3", 12): ((225, 17, 200, 195), (171, 100)),
    ("tutorial_3", 13): ((265, 17, 235, 195), (191, 225)), ("tutorial_3", 14): ((155, 17, 207, 182), (126, 275)),
}
STEPS = {"tutorial_1": 5, "tutorial_2": 10, "tutorial_3": 14}


class Script:
    """Clicks and waits with a dump after each action; `expect` holds what each dump must show."""

    def __init__(self, head):
        self.text = head
        self.exp = []   # per dump: (tutorial, step) or None, a note
        self.shots = []

    def act(self, p, ms, want, note="", shot=None):
        if p:
            self.text += click(*p)
        self.text += "wait %d\ndump\n" % ms
        self.exp.append((want, note))
        if shot:
            self.text += "screenshot @%s@\n" % shot
            self.shots.append(shot)

    def at(self, name, step):
        return (name, step)


# --------------------------------------------------------------------------------------------------------------------- play
S = Script(NEW_PLAYER % "Zed" + STORY_FIRST + DIARY + "wait 4850\n")
t1, t2, t3 = "tutorial_1", "tutorial_2", "tutorial_3"
shot = lambda n, k: "%s_%02d" % (n, k)
S.act(None, 0, (t1, 1), "arrive", shot(t1, 1))
S.act(SAUCE, 1500, (t1, 1), "outside click")                      # the sauce machine is not clickable yet
S.act(C1, 900, (t1, 2), "", shot(t1, 2))                           # the customer, then arrival at the counter
S.act(None, 5500, (t1, 3), "menu read", shot(t1, 3))               # kResume of the customer
S.act(SAUCE, 3000, (t1, 4), "", shot(t1, 4))
S.act(C1, 3500, (t1, 5), "served", shot(t1, 5))
S.act(M1, 2500, (t2, 1), "paid", shot(t2, 1))                      # tutorial 1 is over; customer 2 comes at once
S.act(C2, 900, (t2, 2), "", shot(t2, 2))
S.act(None, 5500, (t2, 3), "", shot(t2, 3))
S.act(TANK, 1500, (t2, 3), "outside click")                        # only the dark-chocolate button is clickable now
S.act(MIXB, 1500, (t2, 4), "", shot(t2, 4))
S.act(None, 4000, (t2, 5), "mixed", shot(t2, 5))
S.act(TANK, 1500, (t2, 6), "", shot(t2, 6))
S.act(PRESSB, 1500, (t2, 8), "press resumed (step 7 has no panel)")
S.act(None, 3000, (t2, 8), "", shot(t2, 8))
S.act(PRESS, 1500, (t2, 9), "", shot(t2, 9))
S.act(C2, 3500, (t2, 10), "served", shot(t2, 10))
S.act(M2, 2000, (t3, 1), "paid", shot(t3, 1))
S.act(C1, 1500, (t3, 2), "", shot(t3, 2))
S.act(C2, 1500, (t3, 3), "", shot(t3, 3))
S.act(None, 5500, (t3, 4), "", shot(t3, 4))
S.act(SAUCE, 3000, (t3, 5), "", shot(t3, 5))
S.act(C1, 3500, (t3, 6), "served", shot(t3, 6))
S.act(MIXB, 1500, (t3, 7), "step 7 has no panel")
S.act(None, 4000, (t3, 8), "", shot(t3, 8))
S.act(TANK, 1500, (t3, 9), "", shot(t3, 9))
S.act(PRESSB, 1500, (t3, 11), "press resumed (step 10 has no panel)")
S.act(None, 3000, (t3, 11), "", shot(t3, 11))
S.act(PRESS, 1500, (t3, 12), "", shot(t3, 12))
S.act(C2, 3500, (t3, 13), "served", shot(t3, 13))
S.act(M2, 1500, (t3, 14), "", shot(t3, 14))
S.act(M1, 2000, None, "the last step ends the tutorials", "after")
# the rest of the level: customers 5 and 6 want bars, 7 a sauce
S.act(None, 800, None, "")
S.act(C4, 300, None, "")
S.act(None, 2700, None, "")
S.act(C6, 300, None, "")
for target, pile in ((C4, M4), (C6, M6)):
    S.act(MIXB, 7000, None, "")
    S.act(TANK, 1500, None, "")
    S.act(PRESSB, 4500, None, "")
    S.act(PRESS, 800, None, "")
    S.act(target, 3000, None, "")
    S.act(pile, 1000, None, "")
S.act(C2, 300, None, "")
S.act(None, 5500, None, "")
S.act(SAUCE, 3500, None, "")
S.act(C2, 3000, None, "")
S.act(M2, 1500, None, "")
S.text += "expect-window scripts/endlevel.lua 120000\nwait 1500\ndump\nscreenshot @results@\n"
S.shots.append("results")
S.exp.append((None, "results"))
SHOTS = tuple(S.shots)
END_CALLS = set()
out, d, paths = run("play", S.text, END_CALLS, shots=SHOTS)
if len(d) != len(S.exp):
    errors.append("play: expected %d window dumps, got %d" % (len(S.exp), len(d)))
else:
    # the tutorial and the step in each dump, the level clock and the customers while it runs
    for i, (want, note) in enumerate(S.exp[:-1]):
        got = tut(d[i])
        label = "play %d (%s)" % (i, note or (want and "%s/%d" % want) or "after")
        if want:
            expect(got is not None and (got[0], got[1]) == want, "%s: the tutorial window says %s, not %s/%d" % (label, got, *want))
            if got:
                expect(got[2] == STEPS[want[0]], "%s: %s has %d steps, not %d" % (label, want[0], got[2], STEPS[want[0]]))
                expect(len(windows(d[i])) == 1, "%s: not exactly one tutorial window" % label)
            tm = timer(d[i])
            clock = {t1: 0, t2: 1200, t3: 2400}[want[0]]
            expect(tm is not None and not tm["running"] and tm["elapsed"] == clock,
                   "%s: the level clock is not stopped at %d ms: %s" % (label, clock, tm))
        else:
            expect(got is None, "%s: a tutorial is still up: %s" % (label, got))
    # every panel draws all of its wrapped lines (the draw clip is the sprite's full height, not the layout's)
    seen = set()
    for i, (want, note) in enumerate(S.exp[:-1]):
        ln = lines(d[i]) if want else None
        if ln:
            seen.add(want)
            expect(ln[0] == ln[1] and ln[1] > 0, "play: %s/%d draws %d of its %d wrapped lines" % (want[0], want[1], ln[0], ln[1]))
    panels = {k for k, v in PANELS.items() if v}
    expect(seen == panels, "play: panels never seen with text: %s" % sorted(panels - seen))
    sh0 = shop(d[0])
    expect(sh0 is not None and sh0["level"] == "1" and int(sh0["active"]) == 1 and int(sh0["pending"]) == 7,
           "play: only the first customer is in the shop while the first tutorial runs: %s" % ((sh0 and (sh0["active"], sh0["pending"])),))
    expect(sh0 and sh0["customers"].get("tut_cust_1", {}).get("s") == 1, "play: the first customer has not come to its counter")
    # a click outside what the step makes clickable does nothing
    o1 = shop(d[1])
    expect(o1 is not None and o1["queue"] == "0" and o1["hands"] == ("-", "-") and o1["trail"] in ("-", None) and o1["dest"] == "station01",
           "play: the click on the sauce machine during the first step moved her: %s" % ((o1 and (o1["queue"], o1["hands"], o1["trail"], o1["dest"])),))
    expect(o1["customers"]["tut_cust_1"]["s"] == 1, "play: the first customer was visited by a click that should do nothing")
    o2 = shop(d[9])
    before, after = shop(d[8]), o2
    expect(after is not None and after["hands"] == before["hands"] and after["dest"] == before["dest"] and after["queue"] == "0",
           "play: the click on the tank during the dark-chocolate button's step did something: %s -> %s" % (before and before["dest"], after and after["dest"]))
    # the click that is allowed does what it does: the customer is visited, then the bottle is taken
    s2 = shop(d[2])
    expect(s2 and s2["customers"]["tut_cust_1"]["s"] == 2, "play: the customer was not visited by the click on it")
    s4 = shop(d[4])
    expect(s4 and s4["hands"][0].startswith("object_"), "play: she does not carry the bottle at the fourth step: %s" % (s4 and s4["hands"],))
    # the dim: the dump of the first step has the dim up
    t0 = tut(d[0])
    expect(t0 is not None and t0[3] > 0.45, "play: the dim has not come up by the first step: %s" % (t0,))
    # the end of the tutorials: no window, the clock runs, nothing is locked
    idx = S.exp.index((None, "the last step ends the tutorials"))
    expect(not windows(d[idx + 1]), "play: the tutorial window is still there after the tutorials")
    tm = timer(d[idx + 1])
    expect(tm is not None and tm["running"] and tm["elapsed"] > 2400, "play: the level clock does not run after the tutorials: %s" % (tm,))
    res = d[-1]
    expect(top(res) == "scripts/endlevel.lua" and "Level Passed" in res, "play: the results do not say Level Passed")
    expect(re.search(r'"endlevel_served_amt" [^\n]*label="7"', res) is not None, "play: the results do not say seven customers served")
    expect(re.search(r'"endlevel_lost_amt" [^\n]*label="0"', res) is not None, "play: the results do not say nobody was lost")
    pf = progress_file("play")
    for name in (t1, t2, t3):
        expect(pf.get("tutorial." + name) == "3", "play: tutorial.%s is %s in the progress file, not 3" % (name, pf.get("tutorial." + name)))
    expect("tutorial.shared" not in pf, "play: the shared slot is written with the fixes on")
    expect(pf.get("value.current_level") == "2" and pf.get("level.1", "").startswith("1,"), "play: level 1 is not passed in the file: %s" % pf)
imgs = {k: png(v) for k, v in paths.items()}
if all(imgs.values()):
    for (name, k), (box, arrow) in ((key, val) for key, val in PANELS.items() if val):
        img = imgs.get(shot(name, k))
        if img is None:
            errors.append("play: no screenshot of %s step %d" % (name, k))
            continue
        x, y, w, h = box
        # the panel is cream paper where the text is, not the dimmed scene
        m = mean(img, x + 30, y + 30, x + w - 30, y + h - 30)
        expect(m > 200, "play: the panel of %s step %d is not light (mean %d)" % (name, k, m))
        if arrow:
            ax, ay = arrow
            n = count(img, ax - 36, ay - 112, ax + 37, ay, orange)
            expect(n > 400, "play: the arrow of %s step %d is not orange where it should be (%d pixels)" % (name, k, n))
        # the Skip Tutorials button sits at the bottom left corner
        expect(green(px(img, SKIP[0], SKIP[1])), "play: no skip button at the corner of %s step %d: %s" % (name, k, px(img, *SKIP)))
    # the scene is dimmed while a tutorial runs and not after it: the same floor, before and after
    floor = (250, 300, 650, 420)
    dim = mean(imgs[shot(t1, 1)], *floor)
    bright = mean(imgs[shot(t3, 14)], *floor)
    expect(dim < 0.75 * 108, "play: the scene is not dimmed at the first step (floor %d)" % dim)
    c = px(imgs["results"], 400, 300)
    expect(c[0] > 200 and c[1] > 190 and c[2] > 180, "play: the results sheet is not cream: %s" % (c,))

# -------------------------------------------------------------------------------------------------------------------- again
text = STORY_AGAIN + DIARY + "wait 4850\ndump\nscreenshot @replay@\n"
out, d, paths = run("again", text, set(), shots=("replay",), user="play")
if len(d) != 1:
    errors.append("again: expected 1 window dump, got %d" % len(d))
else:
    got = tut(d[0])
    sh = shop(d[0])
    expect(sh is not None and sh["level"] == "1", "again: globe 1 did not load level 1: %s" % (sh and sh["level"]))
    expect(got is not None and got[:2] == (t1, 1), "again: the finished tutorial is not shown again at the replay (it is repeatable): %s" % (got,))
    expect(top(d[0]) == "scripts/game.lua", "again: not in the game: %s" % top(d[0]))

# -------------------------------------------------------------------------------------------------------------------- skip
S2 = Script(NEW_PLAYER % "Ann" + STORY_FIRST + DIARY + "wait 4850\n")
S2.act(None, 0, (t1, 1), "arrive")
S2.act(C1, 900, (t1, 2), "")
S2.act(SKIP, 1500, None, "skipped")                              # the window fades out and closes
S2.act(None, 6000, None, "the next customer comes with no tutorial")
S2.act(SAUCE, 3500, None, "a click the tutorial did not allow")  # the lock is gone
text = S2.text
out, d, paths = run("skip", text, set(), user="skip")
if len(d) != len(S2.exp):
    errors.append("skip: expected %d window dumps, got %d" % (len(S2.exp), len(d)))
else:
    expect(tut(d[0]) is not None and tut(d[1]) is not None, "skip: the tutorial did not run before the skip")
    expect(not windows(d[2]) and tut(d[2]) is None, "skip: the tutorial is still up after Skip")
    tm = timer(d[2])
    expect(tm is not None and tm["running"], "skip: the level clock is not running after Skip: %s" % (tm,))
    sh = shop(d[3])
    expect(sh is not None and "tut_cust_2" in sh["customers"], "skip: the second customer has not come after Skip: %s" % (sh and list(sh["customers"])))
    expect(tut(d[3]) is None and not windows(d[3]), "skip: a tutorial started for the second customer after Skip")
    after = shop(d[4])
    expect(after is not None and after["hands"][0].startswith("object_"), "skip: the click on the sauce machine did nothing after Skip: %s" % (after and after["hands"],))
    pf = progress_file("skip")
    expect(pf.get("tutorials.show") == "0", "skip: the option is not saved as off: %s" % pf.get("tutorials.show"))
    expect(pf.get("tutorial.tutorial_1") == "1", "skip: the skipped tutorial is not saved as started: %s" % pf.get("tutorial.tutorial_1"))

# ---------------------------------------------------------------------------------------------------------------------- off
text = STORY_AGAIN + DIARY + "wait 3450\ndump\n" + click(*C1) + "wait 900\ndump\n"
out, d, paths = run("off", text, set(), user="skip")
if len(d) != 2:
    errors.append("off: expected 2 window dumps, got %d" % len(d))
else:
    sh, tm = shop(d[0]), timer(d[0])
    expect(tut(d[0]) is None and not windows(d[0]), "off: a tutorial shows with the option off: %s" % (tut(d[0]),))
    expect(sh is not None and sh["level"] == "1", "off: level 1 did not load: %s" % (sh and sh["level"]))
    # the first three seconds of level 1 are locked only while Tutorials are on: a click now (about a second in) goes through
    expect(tm is not None and tm["running"] and 0 < tm["elapsed"] < 2900, "off: the click does not come inside the first three seconds: %s" % (tm,))
    visited = shop(d[1])
    expect(visited is not None and visited["customers"].get("tut_cust_1", {}).get("s") == 2,
           "off: the click inside the first three seconds did not reach the customer: %s" % (visited and visited["customers"].get("tut_cust_1"),))

# ------------------------------------------------------------------------------------------------------------------- shared
S3 = Script(NEW_PLAYER % "Bea" + STORY_FIRST + DIARY + "wait 4850\n")
S3.act(None, 0, (t1, 1), "arrive")
S3.act(C1, 900, (t1, 2), "")
S3.act(None, 5500, (t1, 3), "")
S3.act(SAUCE, 3000, (t1, 4), "")
S3.act(C1, 3500, (t1, 5), "")
S3.act(M1, 1500, (t2, 1), "")
out, d, paths = run("shared", S3.text, set(), user="shared", extra=("--config", "fixbugs=0"))
if len(d) != len(S3.exp):
    errors.append("shared: expected %d window dumps, got %d" % (len(S3.exp), len(d)))
else:
    expect("fixbugs off" in out, "shared: the run did not log fixbugs off")
    pf = progress_file("shared")
    expect(not [k for k in pf if k.startswith("tutorial.") and k != "tutorial.shared"], "shared: per-tutorial keys are written with the fixes off: %s" % [k for k in pf if k.startswith("tutorial.")])
    # tutorial 1 saved 3, then tutorial 2 started and wrote its own state (1) over it: the one slot holds the last word
    expect(pf.get("tutorial.shared") == "1", "shared: the one key does not hold the last state written: %s" % pf.get("tutorial.shared"))

for e in errors:
    print("FAIL:", e)
print("greatcc_tutorial1: %s" % ("FAILED" if errors else "ok"))
sys.exit(1 if errors else 0)
