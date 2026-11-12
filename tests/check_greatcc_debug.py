"""The Great Chocolate Chase (docs/design/greatcc.md slice 20): the developer tools, with real keys and clicks on the real scripts and art.

Every run is `--headless --userdir <scratch> --seed 1` (with `--cheatmode` unless said) and asserts: exit 0; no [lua-error] / [engine-error] /
[native-error] / [content]; no [unimplemented-type]; the [unimplemented] set is DoMetrics only; the only missing textures are the known ones.

Run "console": level 1 (a new player, Tutorials switched off in Options so nothing else holds the keys). `key ctrl+c` opens the console inside the game's
modal with the focus in its field, and no "c" is typed into it; `setvalue` through the OK button changes a managed value; Close removes the console;
Ctrl+C opens it again (the key focus came back to the shop); `addminutes 1` moves the level's clock a minute on. The
chord does not ask to quit (no close request is logged).
Run "overlays": `key ctrl+s` shows the mouse text, `key ctrl+t` the asset text; with both only the mouse text shows; each toggles off again. The
panel is an opaque black rectangle at the top left with red text.
Run "zones": `--config zones_debug=1 --config game_debug=1`: the zone points and lines are drawn over the shop (red squares); the Debug button is
there and a click on it (the zones are read again) changes nothing on the screen. Without zones_debug nothing is drawn.
Run "achievements": the cheats dialog's Choose Achievements opens the window with a toggle per achievement; Acquire All, Release All, one toggle and
Test Notify do their work.
Run "ctrl_d": on the main menu Ctrl+D runs MainDebug with cheatmode and does nothing without it."""
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

def click(p, wait=1500):
    return "move %d %d\nwait 300\nclick %d %d\nwait %d\n" % (p + p + (wait,))


def run(tag, script_text, shots=(), user=None, extra=("--cheatmode",)):
    user_dir = os.path.join(a.workdir, "user_" + (user or tag))
    log = os.path.join(a.workdir, tag + ".log")
    script = os.path.join(a.workdir, tag + ".script")
    paths = {s: os.path.join(a.workdir, "%s_%s.png" % (tag, s)) for s in shots}
    for name, path in paths.items():
        script_text = script_text.replace("@%s@" % name, path)
    with open(script, "w") as f:
        f.write(script_text)
    p = subprocess.run([a.openconche, "--headless", *extra, "--assets", a.assets, "--userdir", user_dir, "--seed", "1", "--script",
                        script, "--log", log], capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=1800)
    out = (open(log, encoding="utf-8", errors="replace").read() if os.path.exists(log) else "") + "\n" + p.stdout + "\n" + p.stderr
    if p.returncode != 0:
        errors.append("%s: exit status %d" % (tag, p.returncode))
    for t in ("[lua-error]", "[engine-error]", "[native-error]", "[content]", "[script] FAIL", "[expect-lua]"):
        if t in out:
            errors.append("%s: %s in the output: %s" % (tag, t, next(l for l in out.split("\n") if t in l)))
    seen = set(re.findall(r"^(?:\[\+\d+ms\] )?\[unimplemented\] (\S+)", out, re.M))
    if seen != CALLS and seen != set():
        errors.append("%s: [unimplemented] calls beyond the allow-list: %s" % (tag, sorted(seen - CALLS)))
    if re.search(r"^(?:\[\+\d+ms\] )?\[unimplemented-type\] ", out, re.M):
        errors.append("%s: [unimplemented-type] in the output" % tag)
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
expect-window scripts/plotwindow.lua 30000
wait 450
move 585 500
wait 50
move 585 500
wait 50
click 585 500
wait 4850
move 400 300
wait 200
"""
OK_BUTTON, CLOSE_BUTTON = (125, 117), (245, 117)

red = lambda c: c[0] > 200 and c[1] < 70 and c[2] < 70
black = lambda c: c[0] < 8 and c[1] < 8 and c[2] < 8


def expect(cond, msg):
    if not cond:
        errors.append(msg)


def top(d):
    m = re.findall(r'^  Modal "[^"]*" .*script="([^"]*)"', d, re.M)
    return m[-1] if m else None


def has(d, s):
    return s in d


def textedit(d):
    m = re.search(r'TextEdit "command_textedit" [^\n]*? text="([^"]*)"([^\n]*)', d)
    return (m.group(1), m.group(2).rstrip().endswith("focused")) if m else None


def shop_debug(d):
    m = re.search(r'ShopWindow "sprite_window" .*? sprites=\d+( debug=(\d+))?', d)
    return int(m.group(2) or 0) if m else None


# ----------------------------------------------------------------------------------------------------------------------- console
text_ = BOOT + """key ctrl+c
wait 500
dump
screenshot-logical @open@
type setvalue dbg_probe 42
wait 200
""" + click(OK_BUTTON, 500) + """expect-lua 100 GetManagedInt('dbg_probe', 0) == 42
dump
""" + click(CLOSE_BUTTON, 800) + """dump
key ctrl+c
wait 500
dump
type addminutes 1
wait 200
""" + click(OK_BUTTON, 500) + """wait 500
dump
"""
out, d, paths = run("console", text_, ("open",))
if len(d) != 5:
    errors.append("console: expected 5 window dumps, got %d" % len(d))
else:
    expect(has(d[0], 'console_window') and textedit(d[0]) == ("", True), "console: after Ctrl+C the field is not empty and focused: %s" % (textedit(d[0]),))
    expect(re.search(r'^    RWindow "console_window" id=\d+ rect=10,10,350,150', d[0], re.M), "console: console_window is not a child of the game modal at (10,10)")
    expect(len(re.findall(r'console_window', d[0])) == 1, "console: more than one console")
    expect(textedit(d[1]) is not None and textedit(d[1])[0] == "setvalue dbg_probe 42", "console: the command was not typed: %s" % (textedit(d[1]),))
    expect(not has(d[2], 'console_window'), "console: Close left the console up")
    expect(has(d[3], 'console_window') and textedit(d[3]) == ("", True), "console: Ctrl+C did not open the console again")
    t0, t4 = re.search(r'RTimer "game_timer" .* timer=(\d+)/', d[0]), re.search(r'RTimer "game_timer" .* timer=(\d+)/', d[4])
    expect(t0 and t4 and int(t4.group(1)) - int(t0.group(1)) >= 60000, "console: addminutes 1 did not move the level clock a minute on: %s -> %s" % (t0 and t0.group(1), t4 and t4.group(1)))
    expect("close request" not in out, "console: Ctrl+C asked to quit")
    img = png(paths["open"])
    if img:
        expect(count(img, 20, 15, 350, 150, lambda c: c[0] > 120 and c[1] < 60 and c[2] < 80) > 8000, "console: the console panel is not on the screenshot")
        expect(count(img, 30, 52, 330, 78, lambda c: c[0] > 240 and c[1] > 240 and c[2] > 240) > 5000, "console: the text field is not drawn white")

# --------------------------------------------------------------------------------------------------------------------- overlays
text_ = BOOT + """dump
screenshot-logical @none@
key ctrl+s
move 300 300
wait 300
move 301 301
wait 300
dump
screenshot-logical @mouse@
key ctrl+t
wait 300
dump
screenshot-logical @both@
key ctrl+s
wait 300
dump
screenshot-logical @assets@
key ctrl+t
wait 300
dump
screenshot-logical @off@
"""
out, d, paths = run("overlays", text_, ("none", "mouse", "both", "assets", "off"))
if len(d) != 5:
    errors.append("overlays: expected 5 window dumps, got %d" % len(d))
else:
    expect([shop_debug(x) for x in d] == [0, 2, 3, 1, 0], "overlays: the flags are %s, expected [0, 2, 3, 1, 0]" % [shop_debug(x) for x in d])
    imgs = {k: png(v) for k, v in paths.items()}
    if all(imgs.values()):
        panel = lambda im: count(im, 0, 0, 40, 25, black)
        reds = lambda im: count(im, 0, 0, 200, 140, red)
        expect(panel(imgs["none"]) < 20 and reds(imgs["none"]) < 20, "overlays: something is drawn with no flag on")
        for k in ("mouse", "assets", "both"):
            expect(panel(imgs[k]) > 600, "overlays: %s: no black panel at the top left (%d black pixels)" % (k, panel(imgs[k])))
            expect(reds(imgs[k]) > 15, "overlays: %s: no red text (%d red pixels)" % (k, reds(imgs[k])))
        expect(panel(imgs["off"]) < 20 and reds(imgs["off"]) < 20, "overlays: the panel stays after both are toggled off")
        # with both on, the text of Ctrl+S replaces the asset text: the picture is the Ctrl+S one
        expect(count(imgs["both"], 0, 0, 200, 140, red) == count(imgs["mouse"], 0, 0, 200, 140, red), "overlays: both on is not the mouse text alone")
        expect(count(imgs["assets"], 0, 0, 200, 140, red) != count(imgs["mouse"], 0, 0, 200, 140, red), "overlays: the asset text looks like the mouse text")

# ------------------------------------------------------------------------------------------------------------------------ zones
text_ = BOOT + """screenshot-logical @zones@
move 60 516
wait 300
click 60 516
wait 1000
screenshot-logical @after@
dump
"""
out, d, paths = run("zones", text_, ("zones", "after"), extra=("--cheatmode", "--config", "zones_debug=1", "--config", "game_debug=1"))
text0 = BOOT + "screenshot-logical @plain@\ndump\n"
out0, d0, paths0 = run("zones_off", text0, ("plain",))
if len(d) != 1 or len(d0) != 1:
    errors.append("zones: expected one dump per run")
else:
    expect(re.search(r'Button "game_debug" .*', d[0]) and not re.search(r'Button "game_debug" [^\n]*hidden', d[0]), "zones: the Debug button is not shown with game_debug=1")
    expect(re.search(r'Button "game_debug" [^\n]*hidden', d0[0]), "zones: the Debug button shows without game_debug")
    on, after, off = png(paths["zones"]), png(paths["after"]), png(paths0["plain"])
    if on and after and off:
        n_on = count(on, 0, 0, 800, 600, red)
        n_off = count(off, 0, 0, 800, 600, red)
        n_after = count(after, 0, 0, 800, 600, red)
        expect(n_on > n_off + 200, "zones: the zone overlay is not drawn (%d red pixels vs %d without)" % (n_on, n_off))
        expect(abs(n_after - n_on) < 40, "zones: the Debug button's reload changed the picture (%d -> %d red pixels)" % (n_on, n_after))
    expect("DoDebug" not in " ".join(re.findall(r"\[unimplemented\] (\S+)", out)), "zones: DoDebug is still a stub")

# ------------------------------------------------------------------------------------------------------------------ achievements
text_ = BOOT + """move 60 346
wait 300
click 60 346
wait 2500
""" + click((130, 245), 2500) + """dump
screenshot-logical @choose@
""" + click((334, 466), 800) + """dump
screenshot-logical @acquired@
""" + click((433, 466), 800) + """dump
""" + click((297, 32), 800) + """dump
""" + click((530, 466), 1500) + """dump
screenshot-logical @notify@
"""
out, d, paths = run("achievements", text_, ("choose", "acquired", "notify"))
def choose(x):
    m = re.search(r'VChooseAchievements "sprite_window" id=\d+ rect=273,5,415,500 sprites=\d+ toggles=(\d+) selected=(\d+)', x)
    return (int(m.group(1)), int(m.group(2))) if m else None
if len(d) != 5:
    errors.append("achievements: expected 5 window dumps, got %d" % len(d))
else:
    expect(top(d[0]) == "scripts/cheats_achievements.lua", "achievements: the window did not open: %s" % top(d[0]))
    r = [choose(x) for x in d]
    expect(r[0] and r[0][0] >= 40 and r[0][1] == 0, "achievements: toggles at the start: %s" % (r[0],))
    n = r[0][0] if r[0] else 0
    expect(r[1] == (n, n), "achievements: Acquire All did not select every toggle: %s" % (r[1],))
    expect(r[2] == (n, 0), "achievements: Release All did not clear every toggle: %s" % (r[2],))
    expect(r[3] == (n, 1), "achievements: one toggle did not select exactly one: %s" % (r[3],))
    expect(has(d[4], "VRewardNotify") or has(d[4], "rewardnotify"), "achievements: Test Notify showed no notification window")
    imgs = {k: png(v) for k, v in paths.items()}
    if all(imgs.values()):
        green = lambda c: c[1] > c[0] + 25 and c[1] > c[2] + 25
        expect(count(imgs["choose"], 280, 20, 650, 420, lambda c: c[0] > 200 and c[1] > 200 and c[2] > 200) > 500, "achievements: the labels are not drawn")
        expect(count(imgs["acquired"], 285, 20, 330, 420, lambda c: c != px(imgs["choose"], 290, 100)) > 0, "achievements: the screenshot after Acquire All is the same")

# ------------------------------------------------------------------------------------------------------------------------ ctrl_d
text_ = """expect-window scripts/mainmenu.lua 20000
wait 1500
key ctrl+d
wait 500
"""
out, d, paths = run("ctrl_d_on", text_)
expect("[greatcc] MainDebug: all achievements acquired" in out, "ctrl_d: Ctrl+D did nothing on the main menu with cheatmode")
out, d, paths = run("ctrl_d_off", text_, extra=())
expect("MainDebug" not in out, "ctrl_d: Ctrl+D acted without cheatmode")

if errors:
    print("FAIL")
    for e in errors:
        print(" -", e)
    sys.exit(1)
print("ok")
