"""The Great Chocolate Chase (docs/design/greatcc.md slice 18): Endless Sweets mode with real clicks on the real scripts and art.

Every run is `--headless --userdir <scratch> --seed 1` and asserts: exit 0; no [lua-error] / [engine-error] / [native-error] / [content];
no [unimplemented] / [unimplemented-type]; the only missing textures are the known ones.

Run "angry": a new player in Endless Sweets (Hard: five angry customers end the run) clicks through the two steps of the first
tutorial (the only input after the map's pin and the difficulty) and then does nothing. The clock starts and counts up, customers arrive
by themselves, the second tutorial comes up when the first threshold (30 s in Hard) is crossed and holds the clock, the anger gauge
climbs, and when the fifth customer has left angry the end waits two seconds and `endlevel_endless` shows: Total Score, Play Time and
Customers Served, no Master Score and no Customers Lost. The log has the score line.
Run "shop": the same start with the thresholds lowered by SetManagedValue so the run reaches pseudo-level 4 within a second; the HUD's
Upgrades button opens the upgrade shop over the running level (the clock stops under it), the Decoration tab offers the parquet floor
for $100, the buy button pays out of the cash (300 -> 200), Play returns to the level, the floor is the new one and the clock runs on."""
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

# a new player in Endless Sweets, the pin of the first location, Hard, then the two steps of the first tutorial
START = """expect-window scripts/mainmenu.lua 20000
wait 1000
click 480 447
wait 1500
type Zed
wait 100
click 310 368
wait 3500
move 144 361
wait 300
click 144 361
wait 2500
click 400 335
wait 4500
move 430 337
wait 300
click 430 337
wait 1000
move 430 357
wait 300
click 430 357
wait 1000
"""
# the Continue button of the second tutorial (the machines come with the money)
CONTINUE2 = """move 110 238
wait 300
click 110 238
wait 500
"""


def run(tag, script_text, shots=()):
    user_dir = os.path.join(a.workdir, "user_" + tag)
    log = os.path.join(a.workdir, tag + ".log")
    script = os.path.join(a.workdir, tag + ".script")
    paths = {s: os.path.join(a.workdir, "%s_%s.png" % (tag, s)) for s in shots}
    for name, path in paths.items():
        script_text = script_text.replace("@%s@" % name, path)
    with open(script, "w") as f:
        f.write(script_text)
    p = subprocess.run([a.openconche, "--headless", "--assets", a.assets, "--userdir", user_dir, "--seed", "1", "--script", script,
                        "--log", log], capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=1800)
    out = (open(log, encoding="utf-8", errors="replace").read() if os.path.exists(log) else "") + "\n" + p.stdout + "\n" + p.stderr
    if p.returncode != 0:
        errors.append("%s: exit status %d" % (tag, p.returncode))
    for t in ("[lua-error]", "[engine-error]", "[native-error]", "[content]", "[script] FAIL", "[expect-lua]"):
        if t in out:
            errors.append("%s: %s in the output: %s" % (tag, t, next(l for l in out.split("\n") if t in l)))
    for kind in ("[unimplemented]", "[unimplemented-type]"):
        seen = set(re.findall(r"^(?:\[\+\d+ms\] )?%s (\S+)" % re.escape(kind), out, re.M))
        if seen:
            errors.append("%s: %s calls: %s" % (tag, kind, sorted(seen)))
    gone = set(re.findall(r"^(?:\[\+\d+ms\] )?\[gfx\] texture '([^']+)' not found", out, re.M))
    if not gone <= MISSING | {"idle_angry.png"}:
        errors.append("%s: textures the install does not ship beyond the known ones: %s" % (tag, sorted(gone - MISSING)))
    # endlevel.lua asks the goal and the mastery of a level an endless run does not have: the natives answer 0 and say so
    unexpected = [l for l in out.split("\n") if "[lua-arg]" in l and "GetGoalScore" not in l and "GetMasteryScore" not in l]
    if unexpected:
        errors.append("%s: unexpected argument warning: %s" % (tag, unexpected[0]))
    dumps = [d for d in re.split(r"^(?=\[windows\] )", p.stdout, flags=re.M) if d.startswith("[windows] ")]
    return out, dumps, paths


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


def top(d):
    m = re.findall(r'^  Modal "[^"]*" .*script="([^"]*)"', d, re.M)
    return m[-1] if m else None


def shop(d):
    m = re.search(r'^\s*ShopWindow "sprite_window" id=\d+ rect=0,0,800,600 sprites=(\d+) level=(\d+) endless=(\d) countdown=(\d+) '
                  r'waiting=(\d) over=(\d+) pending=(\d+) stations=(\d+) active=(\d+)', d, re.M)
    return dict(zip(("sprites", "level", "endless", "countdown", "waiting", "over", "pending", "stations", "active"),
                    (int(x) for x in m.groups()))) if m else None


def timer(d):
    m = re.search(r'^\s*RTimer "game_timer" .* label="([^"]*)" .* timer=(\d+)/(\d+) (running|stopped) (down|up)$', d, re.M)
    return {"label": m.group(1), "elapsed": int(m.group(2)), "running": m.group(4) == "running", "dir": m.group(5)} if m else None


def anger(d):
    m = re.search(r'Text "anger_score" .* label="(\d+)/(\d+)"', d)
    return (int(m.group(1)), int(m.group(2))) if m else None


def register(d):
    m = re.search(r'VRegister "register_window" .* value=(\d+)', d)
    return int(m.group(1)) if m else None


def label(d, name):
    m = re.search(r'Text "%s" .*? (hidden )?label="([^"]*)"' % name, d)
    return (m.group(2), bool(m.group(1))) if m else None


# ------------------------------------------------------------------------------------------------------------------------ angry
text = START + """dump
screenshot @hud0@
wait 20000
dump
screenshot @hud@
wait 12000
dump
screenshot @tut2@
""" + CONTINUE2 + """wait 1000
dump
expect-window scripts/endlevel.lua 200000
wait 2500
dump
screenshot @end@
"""
out, d, paths = run("angry", text, ("hud0", "hud", "tut2", "end"))
if len(d) != 5:
    errors.append("angry: expected 5 window dumps, got %d" % len(d))
else:
    s0, t0 = shop(d[0]), timer(d[0])
    expect(s0 is not None and s0["endless"] == 1 and s0["pending"] == 0, "angry: no endless ShopWindow without scripted customers: %s" % s0)
    expect(t0 is not None and t0["dir"] == "up" and t0["running"] and t0["elapsed"] < 3000, "angry: the clock is not counting up from 0: %s" % t0)
    expect(anger(d[0]) == (0, 5), "angry: Hard should start at 0/5: %s" % (anger(d[0]),))
    s1, t1 = shop(d[1]), timer(d[1])
    expect(s1 is not None and s1["active"] >= 2, "angry: customers did not arrive by themselves in 20 s: %s" % s1)
    expect(t1 is not None and 19000 < t1["elapsed"] < 23000 and t1["label"] in ("0:19", "0:20", "0:21", "0:22"),
           "angry: the clock is off 20 s in: %s" % t1)
    t2 = timer(d[2])
    expect(t2 is not None and not t2["running"] and 30000 <= t2["elapsed"] < 31500,
           "angry: the second tutorial did not hold the clock at the first threshold (30 s in Hard): %s" % t2)
    expect(top(d[3]) == "scripts/game.lua" and timer(d[3])["running"], "angry: the clock did not go on after the tutorial: %s" % timer(d[3]))
    a4 = anger(d[4])
    expect(a4 == (5, 5), "angry: the run did not end on five angry customers: %s" % (a4,))
    expect(top(d[4]) == "scripts/endlevel.lua", "angry: endlevel.lua is not on top: %s" % top(d[4]))
    s4 = shop(d[4])
    expect(s4 is not None and s4["over"] > 0, "angry: the end was not noticed: %s" % s4)
    for name, shown in (("endlevel_goal", True), ("endlevel_today", True), ("endlevel_served", True), ("endlevel_mastery", False),
                        ("endlevel_lost", False)):
        l = label(d[4], name)
        expect(l is not None and l[1] == (not shown), "angry: %s should be %s: %s" % (name, "shown" if shown else "hidden", l))
    expect((label(d[4], "endlevel_goal") or ("",))[0] == "Total Score:", "angry: the goal line is not Total Score")
    expect((label(d[4], "endlevel_today") or ("",))[0] == "Play Time:", "angry: the time line is not Play Time")
    expect(re.match(r"^\d+:\d\d$", (label(d[4], "endlevel_today_amt") or ("",))[0]) is not None, "angry: Play Time is not a clock")
    expect(re.search(r"\[greatcc\] LogScore endless score=\d+ seconds=\d+ difficulty=2", out) is not None, "angry: the score was not logged")
imgs = {k: png(v) for k, v in paths.items()}
if all(imgs.values()):
    blue = lambda c: c[2] > c[0] + 50 and c[2] > 110
    expect(count(imgs["hud0"], 730, 140, 790, 176, blue) > 400, "angry: the Upgrades button is not on the HUD")
    white = lambda c: c[0] > 235 and c[1] > 230 and c[2] > 215
    expect(count(imgs["tut2"], 80, 80, 320, 260, white) > 8000, "angry: the second tutorial's box is not on the screenshot")
    cream = lambda c: c[0] > 215 and c[1] > 205 and c[2] > 180
    expect(count(imgs["end"], 240, 160, 560, 430, cream) > 20000, "angry: the results paper is not on the screenshot")

# ------------------------------------------------------------------------------------------------------------------------- shop
text = START + """lua SetManagedValue('endless_threshold_1', 0, false)
lua SetManagedValue('endless_threshold_2', 0, false)
lua SetManagedValue('endless_threshold_3', 0, false)
wait 2000
""" + CONTINUE2 + """lua SetManagedValue('current_level_total', 300)
wait 500
dump
move 750 158
wait 300
click 750 158
wait 3000
dump
move 103 110
wait 300
click 103 110
wait 1500
dump
screenshot @list@
move 262 225
wait 300
click 262 225
wait 1500
dump
move 740 553
wait 300
click 740 553
wait 3000
dump
screenshot @back@
wait 2000
dump
"""
out, d, paths = run("shop", text, ("list", "back"))
if len(d) != 6:
    errors.append("shop: expected 6 window dumps, got %d" % len(d))
else:
    expect(top(d[0]) == "scripts/game.lua" and register(d[0]) == 300, "shop: the cash is not 300 over the running level: %s" % register(d[0]))
    expect(top(d[1]) == "scripts/prelevel.lua", "shop: the HUD's Upgrades button did not open the upgrade shop: %s" % top(d[1]))
    expect(shop(d[1]) is not None, "shop: the level is not kept under the shop")
    expect(top(d[2]) == "scripts/prelevel.lua" and top(d[3]) == "scripts/prelevel.lua", "shop: the shop closed by itself")
    expect(top(d[4]) == "scripts/game.lua", "shop: Play did not return to the level: %s" % top(d[4]))
    r4, t4, t5 = register(d[4]), timer(d[4]), timer(d[5])
    expect(r4 == 200, "shop: the parquet floor did not cost $100 out of the cash: %s" % r4)
    expect(t4 is not None and t5 is not None and t4["running"] and t5["elapsed"] - t4["elapsed"] > 1000,
           "shop: the clock did not run after the shop: %s -> %s" % (t4, t5))
    s5 = shop(d[5])
    expect(s5 is not None and s5["endless"] == 1 and s5["stations"] == 17, "shop: the endless level is not running: %s" % s5)
imgs = {k: png(v) for k, v in paths.items()}
if all(imgs.values()):
    green = lambda c: c[1] > c[0] + 20 and c[1] > c[2] + 40 and c[1] > 100
    expect(count(imgs["list"], 225, 215, 300, 238, green) > 300, "shop: no green $100 button on the first Decoration upgrade")
    # the parquet floor: a light tan weave where the old floor was dark red-brown
    tan = lambda c: c[0] > 150 and c[1] > 110 and c[2] < 120 and abs(c[0] - c[1]) < 70
    expect(count(imgs["back"], 0, 420, 800, 600, tan) > 8000, "shop: the floor is not the parquet after the purchase")

if errors:
    print("FAIL")
    for e in errors:
        print(" -", e)
    sys.exit(1)
print("ok")
