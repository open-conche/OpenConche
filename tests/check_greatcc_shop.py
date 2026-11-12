"""The Great Chocolate Chase (docs/design/greatcc.md slice 9): the shop shell on the real scripts.

Runs `--headless --userdir <scratch> --seed 1` once per run below and asserts, per run: exit 0; no [lua-error] / [engine-error] /
[native-error] / [content]; the [unimplemented] and [unimplemented-type] sets equal that run's allow-list (`DoMetrics`, the
analytics the pause menu reports, and nothing else); the only textures the scripts name that the install does not ship are
the shop's menu overlay, its full-screen backdrop and the trim of the yes/no dialog.

Run "play": a new player, Tutorials switched off in the Options screen, through Story Mode to level 1. The shop screen is on top of the modal chain with its window loaded, the
level's 17 stations, the HUD's gauges and the clock (0:30 counting down after the pre-level second), the goal and mastery texts
and the screenshot shows the Iquitos background, the counters and the HUD. Escape opens the pause dialog and the clock stops;
Resume returns and it runs on; the HUD's Menu button opens the dialog too; Map asks first (Cancel keeps the dialog), then leaves for
the world map with the shop still loaded; a globe on the map loads the level again and the clock starts over.
Run "quit": Escape, Quit, OK goes back to the main menu (the shop is gone and the game no longer loaded); a second game from there
loads the level again.
Run "restart": Escape, Restart, OK loads the same level again (the clock starts over).
Run "end": nobody plays level 1; the customers lose their patience and leave angry, and two seconds after the last one has gone the
end-of-level dialog says "Level Failed" with the goal and master scores, the level has not advanced; Try Again loads the level once more.
Run "endquit": the same end, then Quit and OK from the dialog return to the main menu (QuitToMainMenu runs inside the dialog's own
button command).
Runs "fixon" and "fixoff": level 26 is loaded through the script; the log says F2 was applied only with the fixes on.
The screenshots are 800x600 PNGs; the pixels the scripts name (the Iquitos background, the HUD's clock) are checked."""
import argparse, os, re, shutil, struct, subprocess, sys, zlib

ap = argparse.ArgumentParser()
ap.add_argument("--openconche", required=True)
ap.add_argument("--assets", required=True)
ap.add_argument("--workdir", required=True)
a = ap.parse_args()
shutil.rmtree(a.workdir, ignore_errors=True)
os.makedirs(a.workdir)
errors = []

# Shipped art the scripts name that the install does not ship (the original shows nothing there as well): the shop's menu overlay
# and full-screen dialog backdrop (hud.lua, game.lua) and the trim of the name and yes/no dialogs.
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


def run(tag, script_text, allow_calls, extra_args=(), shots=()):
    user = os.path.join(a.workdir, "user_" + tag)
    log = os.path.join(a.workdir, tag + ".log")
    script = os.path.join(a.workdir, tag + ".script")
    paths = {s: os.path.join(a.workdir, "%s_%s.png" % (tag, s)) for s in shots}
    for name, path in paths.items():
        script_text = script_text.replace("@%s@" % name, path)
    with open(script, "w") as f:
        f.write(script_text)
    p = subprocess.run([a.openconche, "--headless", "--assets", a.assets, "--userdir", user, "--seed", "1", "--script", script,
                        "--log", log] + list(extra_args), capture_output=True, text=True, encoding="utf-8", errors="replace",
                       timeout=420)
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
    if types != TYPES:
        errors.append("%s: [unimplemented-type] set differs: extra %s, missing %s" % (tag, sorted(types - TYPES), sorted(TYPES - types)))
    gone = set(re.findall(r"^(?:\[\+\d+ms\] )?\[gfx\] texture '([^']+)' not found", out, re.M))
    if not gone <= MISSING:
        errors.append("%s: textures the install does not ship beyond the known ones: %s" % (tag, sorted(gone - MISSING)))
    dumps = [d for d in re.split(r"^(?=\[windows\] )", p.stdout, flags=re.M) if d.startswith("[windows] ")]
    return out, dumps, paths, (open(log, encoding="utf-8", errors="replace").read() if os.path.exists(log) else "")


def modals(d):
    return re.findall(r'^  Modal "([^"]*)" .*script="([^"]*)"', d, re.M)


def top(d):
    m = modals(d)
    return m[-1][1] if m else None


def scripts(d):
    return [s for _, s in modals(d)]


def shop(d):
    m = re.search(r'^\s*ShopWindow "sprite_window" id=\d+ rect=0,0,800,600 sprites=(\d+) level=(\d+) endless=(\d) countdown=(\d+) '
                  r'waiting=(\d) over=(\d+) pending=(\d+) stations=(\d+) active=(\d+)(?: player=.*)?$', d, re.M)
    if not m:
        return None
    k = ("sprites", "level", "endless", "countdown", "waiting", "over", "pending", "stations", "active")
    return dict(zip(k, (int(x) for x in m.groups())))


def timer(d):
    m = re.search(r'^\s*RTimer "game_timer" .* label="([^"]*)" .* timer=(\d+)/(\d+) (running|stopped) (down|up)$', d, re.M)
    if not m:
        return None
    return {"label": m.group(1), "elapsed": int(m.group(2)), "limit": int(m.group(3)), "running": m.group(4) == "running"}


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

# A level opens with its diary page (a replayed level shows it again); the clock starts when the dialog has bounced away after OK.
DIARY = """expect-window scripts/plotwindow.lua 30000
wait 450
move 585 500
wait 50
move 585 500
wait 50
click 585 500
"""


# Where things are on screen: the pause dialog's buttons (centres), the yes/no dialog's, the HUD's Menu button.
RESUME, MAP, OPTIONS, RESTART, QUIT = (397, 160), (397, 230), (397, 300), (397, 370), (397, 440)
YES, NO = (310, 373), (489, 373)
MENU = (630, 35)
CLOCK = (718, 70, 758, 100)  # the clock's face (the timer text sits in its lower half)

# ---------------------------------------------------------------------------------------------------------------------- play
text = (BOOT + DIARY + """wait 3350
dump
screenshot @shop@
wait 2000
dump
key Escape
wait 1500
dump
screenshot @pause@
wait 2500
dump
expect-lua 100 AreInsideGame() == false
click %d %d
wait 1500
dump
expect-lua 100 AreInsideGame() == true
wait 1500
dump
click %d %d
wait 1500
dump
click %d %d
wait 1500
click %d %d
wait 1500
dump
click %d %d
wait 1500
click %d %d
wait 4500
dump
expect-lua 100 IsGameLoaded() == true
move 150 258
wait 700
click 150 258
""" + DIARY + """wait 4350
dump
screenshot @again@
expect-lua 100 GetCurrentLevel() == 1
""") % (RESUME + MENU + MAP + NO + MAP + YES)
out, d, paths, logtext = run("play", text, CALLS | {"DoMetrics"}, shots=("shop", "pause", "again"))
if len(d) != 10:
    errors.append("play: expected 10 window dumps, got %d" % len(d))
else:
    s0, t0 = shop(d[0]), timer(d[0])
    expect(top(d[0]) == "scripts/game.lua", "play: the shop is not the top screen: %s" % scripts(d[0]))
    expect(s0 is not None and s0["level"] == 1 and s0["endless"] == 0, "play: no ShopWindow of level 1 in the dump: %s" % s0)
    if s0:
        expect(s0["stations"] == 17, "play: the level should hold 17 stations: %s" % s0)
        expect(s0["pending"] + s0["active"] == 8 and s0["active"] >= 1, "play: level 1 has 8 scripted customers, some already in: %s" % s0)
        expect(s0["waiting"] == 0 and s0["countdown"] == 0, "play: the level has not started after the pre-level second: %s" % s0)
        expect(s0["sprites"] > 20, "play: too few sprites in the scene: %s" % s0)
    expect(t0 is not None and t0["running"] and t0["limit"] == 30000, "play: the clock is not a running 30 s clock: %s" % t0)
    expect(t0 is not None and 0 < t0["elapsed"] < 4000 and t0["label"] in ("0:29", "0:28", "0:27", "0:26"),
           "play: the clock is not counting down from 0:30: %s" % t0)
    t1 = timer(d[1])
    expect(t0 is not None and t1 is not None and 1500 < t1["elapsed"] - t0["elapsed"] < 2500,
           "play: two seconds later the clock is off: %s -> %s" % (t0, t1))
    expect(top(d[2]) == "scripts/pause.lua" and "scripts/game.lua" in scripts(d[2]), "play: Escape did not open the pause dialog: %s" % scripts(d[2]))
    t2, t3 = timer(d[2]), timer(d[3])
    expect(t2 is not None and t3 is not None and t2["elapsed"] == t3["elapsed"],
           "play: the clock moved under the pause dialog: %s -> %s" % (t2, t3))
    expect(top(d[3]) == "scripts/pause.lua", "play: the pause dialog went away by itself")
    t4 = timer(d[4])
    expect(top(d[4]) == "scripts/game.lua", "play: Resume did not return to the game: %s" % scripts(d[4]))
    expect(t3 is not None and t4 is not None and t4["elapsed"] >= t3["elapsed"], "play: the clock went back after Resume")
    t5 = timer(d[5])
    expect(t4 is not None and t5 is not None and t5["elapsed"] - t4["elapsed"] > 1000, "play: the clock did not run after Resume: %s -> %s" % (t4, t5))
    expect(top(d[6]) == "scripts/pause.lua", "play: the HUD's Menu button did not open the pause dialog: %s" % scripts(d[6]))
    expect(top(d[7]) == "scripts/pause.lua" and "scripts/yesno.lua" not in scripts(d[7]),
           "play: Cancel on the yes/no dialog should leave the pause dialog: %s" % scripts(d[7]))
    expect(top(d[8]) == "scripts/worldmap.lua", "play: Map and OK did not leave for the world map: %s" % scripts(d[8]))
    expect("scripts/game.lua" in scripts(d[8]), "play: the shop is not kept under the map: %s" % scripts(d[8]))
    s9, t9 = shop(d[9]), timer(d[9])
    expect(s9 is not None and s9["level"] == 1 and s9["stations"] == 17 and s9["pending"] + s9["active"] == 8 and s9["active"] <= 2,
           "play: a globe on the map did not load the level again: %s" % s9)
    expect(t9 is not None and t9["running"] and t9["elapsed"] < 5000, "play: the clock did not start over: %s" % t9)
imgs = {k: png(v) for k, v in paths.items()}
if all(imgs.values()):
    # Iquitos: the wooden floor is a warm red-brown over most of the lower half, the bamboo counters are yellow
    warm = yellow = 0
    for y in range(300, 590, 6):
        for x in range(10, 790, 6):
            c = px(imgs["shop"], x, y)
            if c[0] > c[1] + 25 and c[0] > c[2] + 25:
                warm += 1
            if c[0] > 150 and c[1] > 120 and c[2] < 110:
                yellow += 1
    expect(warm > 600, "play: the lower half is not the Iquitos floor (%d warm pixels)" % warm)
    expect(yellow > 200, "play: no bamboo counters (%d yellow pixels)" % yellow)
    # the HUD: the cream register strip over the dark red floor at the top right, the gold frame
    c = px(imgs["shop"], 720, 15)
    expect(c[0] > 200 and c[1] > 190 and c[2] > 150, "play: the register strip is not cream: %s" % (c,))
    # the pause dialog is drawn over the shop
    expect(region_diff(imgs["shop"], imgs["pause"], 310, 108, 490, 493) > 30000, "play: the pause dialog is not on the screenshot")
    expect(region_diff(imgs["shop"], imgs["pause"], 0, 520, 300, 600) < 400, "play: the scene changed under the pause dialog")
    # the quest panel is hidden: the top-left corner shows the scene, not the dark red panel
    c = px(imgs["shop"], 120, 100)
    expect(not (abs(c[0] - 150) < 30 and c[1] < 50 and c[2] < 70), "play: the quest panel is showing at the top left: %s" % (c,))

# ---------------------------------------------------------------------------------------------------------------------- quit
text = (BOOT + DIARY + """wait 3350
key Escape
wait 1500
click %d %d
wait 1500
click %d %d
wait 4500
dump
expect-lua 100 IsGameLoaded() == false
click 333 447
wait 3500
move 150 258
wait 700
click 150 258
""" + DIARY + """wait 4350
dump
expect-lua 100 IsGameLoaded() == true
""") % (QUIT + YES)
out, d, paths, logtext = run("quit", text, CALLS | {"DoMetrics"})
if len(d) != 2:
    errors.append("quit: expected 2 window dumps, got %d" % len(d))
else:
    expect(scripts(d[0]) == ["scripts/mainmenu.lua"], "quit: Quit and OK did not leave only the main menu: %s" % scripts(d[0]))
    expect(shop(d[0]) is None, "quit: a ShopWindow is still in the tree after Quit")
    expect("scripts/game.lua" in scripts(d[1]) and shop(d[1]) is not None and shop(d[1])["level"] == 1,
           "quit: the second game did not load level 1: %s" % scripts(d[1]))
    t = timer(d[1])
    expect(t is not None and t["running"] and t["elapsed"] < 4500, "quit: the second game's clock is not fresh: %s" % t)

# ------------------------------------------------------------------------------------------------------------------- restart
text = (BOOT + DIARY + """wait 6350
dump
key Escape
wait 1500
click %d %d
wait 1500
click %d %d
""" + DIARY + """wait 3850
dump
expect-lua 100 GetCurrentLevel() == 1
""") % (RESTART + YES)
out, d, paths, logtext = run("restart", text, CALLS | {"DoMetrics"})
if len(d) != 2:
    errors.append("restart: expected 2 window dumps, got %d" % len(d))
else:
    t0, t1 = timer(d[0]), timer(d[1])
    expect(t0 is not None and t0["elapsed"] > 4000, "restart: the first clock should have run for a few seconds: %s" % t0)
    expect(t1 is not None and t1["elapsed"] < t0["elapsed"] if t0 and t1 else False, "restart: the clock did not start over: %s -> %s" % (t0, t1))
    expect(top(d[1]) == "scripts/game.lua", "restart: the game is not on top after the restart: %s" % scripts(d[1]))
    s = shop(d[1])
    expect(s is not None and s["level"] == 1 and s["stations"] == 17 and s["pending"] + s["active"] == 8, "restart: the level did not load again: %s" % s)

# ----------------------------------------------------------------------------------------------------------------------- end
text = BOOT + DIARY + """expect-window scripts/endlevel.lua 200000
wait 1500
dump
screenshot @end@
click 730 550
""" + DIARY + """wait 4350
dump
expect-lua 100 GetCurrentLevel() == 1
"""
out, d, paths, logtext = run("end", text, CALLS, shots=("end",))
if len(d) != 2:
    errors.append("end: expected 2 window dumps, got %d" % len(d))
else:
    expect(top(d[0]) == "scripts/endlevel.lua", "end: the end-of-level dialog is not on top: %s" % scripts(d[0]))
    expect('label="Level Failed"' in d[0] or "Level Failed" in d[0], "end: the dialog does not say Level Failed")
    expect('label="$100"' in d[0] and 'label="$160"' in d[0], "end: the goal and master scores are not shown")
    t = timer(d[0])
    expect(t is not None and t["label"] == "0:00" and t["elapsed"] >= 30000, "end: the clock did not run to 0:00: %s" % t)
    expect(top(d[1]) == "scripts/game.lua", "end: Try Again did not load the level again: %s" % scripts(d[1]))
    t1 = timer(d[1])
    expect(t1 is not None and t1["elapsed"] < 8000 and t1["running"], "end: the second attempt's clock did not start over: %s" % t1)
imgs = {k: png(v) for k, v in paths.items()}
if all(imgs.values()):
    # the results sheet: the cream paper in the middle of the dialog, the teal frame around it
    c = px(imgs["end"], 400, 300)
    expect(c[0] > 200 and c[1] > 190 and c[2] > 180, "end: the receipt paper is not cream: %s" % (c,))
    c = px(imgs["end"], 120, 260)
    expect(c[1] > c[0] + 20 and c[2] > c[0] - 10, "end: the dialog's frame is not teal: %s" % (c,))

# -------------------------------------------------------------------------------------------------------------------- endquit
text = BOOT + DIARY + """expect-window scripts/endlevel.lua 200000
wait 1500
click 70 555
wait 1500
click %d %d
wait 5000
dump
expect-lua 100 IsGameLoaded() == false
""" % YES
out, d, paths, logtext = run("endquit", text, CALLS)
if len(d) != 1:
    errors.append("endquit: expected 1 window dump, got %d" % len(d))
else:
    expect(scripts(d[0]) == ["scripts/mainmenu.lua"], "endquit: Quit from the results did not leave only the main menu: %s" % scripts(d[0]))
    expect(shop(d[0]) is None, "endquit: a ShopWindow is still in the tree")

# ------------------------------------------------------------------------------------------------------------------- the fixes
FIX = BOOT + DIARY + """wait 3350
lua SetManagedValue('current_level', 26, true)
lua LevelSetup()
wait 500
dump
"""
out, d, paths, logtext = run("fixon", FIX, CALLS)
expect(logtext.count("[fix] F2") == 1, "fixon: F2 should be logged once with the fixes on (%d)" % logtext.count("[fix] F2"))
expect(len(d) == 1 and shop(d[0]) is not None and shop(d[0])["level"] == 26, "fixon: level 26 did not load: %s" % (shop(d[0]) if d else None))
out, d, paths, logtext = run("fixoff", FIX, CALLS, extra_args=("--config", "fixbugs=0"))
expect("[fix] F2" not in logtext, "fixoff: F2 was applied with the fixes off")
expect("[greatcc] fixbugs off" in out, "fixoff: the world did not read fixbugs=0")
expect(len(d) == 1 and shop(d[0]) is not None and shop(d[0])["level"] == 26, "fixoff: level 26 did not load: %s" % (shop(d[0]) if d else None))

for e in errors:
    print("FAIL:", e)
print("greatcc_shop: %s" % ("FAILED" if errors else "ok"))
sys.exit(1 if errors else 0)
