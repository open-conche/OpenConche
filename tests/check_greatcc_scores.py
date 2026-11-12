"""The Great Chocolate Chase (docs/design/greatcc.md slice 19): the high-score screen and the scores that reach it, with real clicks on
the real scripts and art.

Every run is `--headless --cheatmode --userdir <scratch> --seed 1` and asserts: exit 0; no [lua-error] / [engine-error] / [native-error] /
[content]; no [unimplemented-type]; the [unimplemented] set is DoMetrics only (the pause menu's analytics call); the only missing textures
are the known ones.

Play is shortened with the game's own cheats and the state natives, which are the only things set by script: `DoCheatGame(1, '')` is the
cheat the level's Cheats dialog calls for Pass Lvl; `SetManagedValue('current_worth', 250)` stands in for earning $250 in an endless run
(the cheat for money in an endless run only moves the level's total, which the original never logs).
Run "story": a new player passes level 1 (the cheat moves the total into the goal's band and ends the level) and quits to the main menu from
level 2's pause menu. High Scores opens on Story Mode with one row: rank "1.", the player's name and the lifetime total $100 (the
total_worth that ShopWindow::LogScore logs after a passed level); the other three modes are empty; the arrows wrap Story - Easy - Medium -
Hard - Story and a step to the left from Story is Hard; the pressed arrow sets the game mode. The screenshots show the name and score
lines drawn where the row is, and nothing under it.
Run "endless": a new player's Hard endless run ends with $250 (the cheat that fails the level adds ten angry customers). The score is logged
into the Hard table: High Scores opens on Endless Sweets Hard (the mode the run left set) with the one row 250; Story is empty.
Run "again": a second process on the same user folder: the mode the last run left (Story Mode, from its last arrow step) is still set and the
Hard table still holds the row."""
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

SCORES_BUTTON = (228, 510)
ARROW_LEFT, ARROW_RIGHT = (236, 154), (566, 154)
BACK = (288, 525)
MODES = ["Story Mode", "Endless Sweets Easy", "Endless Sweets Medium", "Endless Sweets Hard"]

# new player, Story Mode (the game's introduction shows over the map), level 1 and its diary page
STORY_START = """expect-window scripts/mainmenu.lua 20000
wait 1000
click 333 447
wait 1500
type Zed
wait 100
click 310 368
wait 3500
click 625 488
wait 3000
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
"""
DIARY_OK = """expect-window scripts/plotwindow.lua 30000
wait 450
move 585 500
wait 50
move 585 500
wait 50
click 585 500
wait 4850
"""
# new player, Endless Sweets, the first pin, Hard
ENDLESS_START = """expect-window scripts/mainmenu.lua 20000
wait 1000
click 480 447
wait 1500
type Eve
wait 100
click 310 368
wait 3500
move 144 361
wait 300
click 144 361
wait 2500
click 400 335
wait 4500
"""
OPEN_SCORES = """expect-window scripts/mainmenu.lua 20000
wait 2500
move %d %d
wait 300
click %d %d
wait 3000
""" % (SCORES_BUTTON + SCORES_BUTTON)


def click(p, wait=1500):
    return "move %d %d\nwait 300\nclick %d %d\nwait %d\n" % (p + p + (wait,))


def run(tag, script_text, shots=(), user=None):
    user_dir = os.path.join(a.workdir, "user_" + (user or tag))
    log = os.path.join(a.workdir, tag + ".log")
    script = os.path.join(a.workdir, tag + ".script")
    paths = {s: os.path.join(a.workdir, "%s_%s.png" % (tag, s)) for s in shots}
    for name, path in paths.items():
        script_text = script_text.replace("@%s@" % name, path)
    with open(script, "w") as f:
        f.write(script_text)
    p = subprocess.run([a.openconche, "--headless", "--cheatmode", "--assets", a.assets, "--userdir", user_dir, "--seed", "1", "--script",
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


def hiscore(d):
    m = re.search(r'HiscoreWindow "" id=\d+ rect=0,0,800,600 state=(\d+) rows=(\d+)', d)
    return (int(m.group(1)), int(m.group(2))) if m else None


def text(d, name):
    """(label, hidden, lines) of the Text (or RShrinkText) of that name."""
    m = re.search(r'(?:RShrinkText|Text) "%s" id=\d+ rect=\S+( hidden)? label="([^"]*)" .*? lines=(\d+)' % re.escape(name), d)
    return (m.group(2), bool(m.group(1)), int(m.group(3))) if m else None


def heading(d):
    t = text(d, "gamemode")
    return re.sub(r"</?outline[^>]*>", "", t[0]) if t else None


light = lambda c: c[0] > 200 and c[1] > 200 and c[2] > 200


def row_checks(tag, d, img, rows, names, scores):
    """The visible rows read as given in the dump and have their text drawn in the screenshot; the rows under them are hidden and bare."""
    expect(hiscore(d) == (0, rows), "%s: HiscoreWindow state/rows are %s, expected (0, %d)" % (tag, hiscore(d), rows))
    for i in range(1, 11):
        n, s, r = text(d, "name%d" % i), text(d, "score%d" % i), text(d, "%d" % i)
        if i <= rows:
            expect(n and not n[1] and n[0] == names[i - 1] and n[2] >= 1, "%s: row %d name: %s" % (tag, i, n))
            expect(s and not s[1] and s[0] == scores[i - 1] and s[2] >= 1, "%s: row %d score: %s" % (tag, i, s))
            expect(r and not r[1] and r[0] == "%d." % i and r[2] >= 1, "%s: row %d rank: %s" % (tag, i, r))
        else:
            expect(n and n[1] and s and s[1] and r and r[1], "%s: row %d is not hidden: %s %s %s" % (tag, i, n, s, r))
    if img:
        # the table's panel starts at x=252, y=175; the rows are 21 px apart from y=231, names at x=303, scores right-aligned to 537
        for i in range(10):
            y = 231 + 21 * i
            name_px = count(img, 300, y, 470, y + 20, light)
            score_px = count(img, 470, y, 545, y + 20, light)
            if i < rows:
                expect(name_px > 25 and score_px > 25, "%s: row %d is not drawn (name %d, score %d light pixels)" % (tag, i + 1, name_px, score_px))
            else:
                expect(name_px < 3 and score_px < 3, "%s: row %d shows pixels with no row (name %d, score %d)" % (tag, i + 1, name_px, score_px))
        # the heading texts are drawn: "High Scores", the mode, "Top Players"
        expect(count(img, 280, 90, 520, 135, light) > 400, "%s: the High Scores heading is not drawn" % tag)
        expect(count(img, 300, 140, 500, 168, light) > 150, "%s: the mode heading is not drawn" % tag)
        expect(count(img, 340, 182, 470, 206, light) > 80, "%s: the Top Players heading is not drawn" % tag)


# ------------------------------------------------------------------------------------------------------------------------- story
steps = [("mode0", 0)]
text_ = STORY_START + """lua DoCheatGame(1, '')
expect-window scripts/endlevel.lua 60000
wait 2500
dump
click 740 550
wait 3000
""" + DIARY_OK + """key Escape
wait 1500
click 397 440
wait 1500
click 310 373
""" + OPEN_SCORES + "expect-lua 100 GetCurrentGameMode() == 0\ndump\nscreenshot-logical @story@\n"
for k in (1, 2, 3):
    text_ += click(ARROW_RIGHT, 1000) + "expect-lua 100 GetCurrentGameMode() == %d\ndump\nscreenshot-logical @mode%d@\n" % (k, k)
text_ += click(ARROW_RIGHT, 1000) + "expect-lua 100 GetCurrentGameMode() == 0\ndump\n"
text_ += click(ARROW_LEFT, 1000) + "expect-lua 100 GetCurrentGameMode() == 3\ndump\n"
text_ += click(ARROW_RIGHT, 1000) + click(BACK, 3000) + "dump\n"
out, d, paths = run("story", text_, ("story", "mode1", "mode2", "mode3"))
if len(d) != 8:
    errors.append("story: expected 8 window dumps, got %d" % len(d))
else:
    expect(top(d[0]) == "scripts/endlevel.lua", "story: the cheat did not end the level: %s" % top(d[0]))
    expect(re.search(r"\[greatcc\] LogScore endless", out) is None, "story: an endless score was logged")
    imgs = {k: png(v) for k, v in paths.items()}
    row_checks("story", d[1], imgs["story"], 1, ["Zed"], ["100"])
    expect(heading(d[1]) == MODES[0], "story: the heading is %s" % heading(d[1]))
    expect(text(d[1], "topplayers") and not text(d[1], "topplayers")[1], "story: Top Players is not shown")
    expect(text(d[1], "local") and text(d[1], "local")[1], "story: the Local heading shows in the local-only screen")
    for k in (1, 2, 3):
        row_checks("story mode %d" % k, d[1 + k], imgs["mode%d" % k], 0, [], [])
        expect(heading(d[1 + k]) == MODES[k], "story: step %d heading is %s" % (k, heading(d[1 + k])))
    row_checks("story wrap", d[5], None, 1, ["Zed"], ["100"])
    expect(heading(d[5]) == MODES[0], "story: the right arrow from Hard did not wrap to Story: %s" % heading(d[5]))
    expect(heading(d[6]) == MODES[3], "story: the left arrow from Story did not go to Hard: %s" % heading(d[6]))
    expect(top(d[7]) == "scripts/mainmenu.lua", "story: Back did not return to the main menu: %s" % top(d[7]))
    expect(hiscore(d[7]) is None, "story: the high score window is still there after Back")

# ------------------------------------------------------------------------------------------------------------------------ endless
text_ = ENDLESS_START + """lua SetManagedValue('current_worth', 250)
lua DoCheatGame(0, '')
expect-window scripts/endlevel.lua 60000
wait 2500
dump
click 75 550
""" + OPEN_SCORES + "dump\nscreenshot-logical @hard@\n" + click(ARROW_RIGHT, 1000) + "dump\n"
out, d, paths = run("endless", text_, ("hard",))
if len(d) != 3:
    errors.append("endless: expected 3 window dumps, got %d" % len(d))
else:
    expect(top(d[0]) == "scripts/endlevel.lua", "endless: the cheat did not end the run: %s" % top(d[0]))
    expect(re.search(r"\[greatcc\] LogScore endless score=250 seconds=\d+ difficulty=2 table=greatchocolatechase_endlesshard", out),
           "endless: the score is not in the log")
    imgs = {k: png(v) for k, v in paths.items()}
    expect(heading(d[1]) == MODES[3], "endless: High Scores did not open on the mode the run was in: %s" % heading(d[1]))
    row_checks("endless", d[1], imgs["hard"], 1, ["Eve"], ["250"])
    expect(heading(d[2]) == MODES[0], "endless: the right arrow from Hard is not Story Mode: %s" % heading(d[2]))
    row_checks("endless story", d[2], None, 0, [], [])

# ------------------------------------------------------------------------------------------------------------------------- again
text_ = OPEN_SCORES + "dump\n" + click(ARROW_LEFT, 1000) + "dump\nscreenshot-logical @again@\n"
out, d, paths = run("again", text_, ("again",), user="endless")
if len(d) != 2:
    errors.append("again: expected 2 window dumps, got %d" % len(d))
else:
    imgs = {k: png(v) for k, v in paths.items()}
    # the run before left the screen on Story Mode (its last arrow step): the mode is a saved preference
    expect(heading(d[0]) == MODES[0], "again: the mode did not survive the restart: %s" % heading(d[0]))
    row_checks("again story", d[0], None, 0, [], [])
    expect(heading(d[1]) == MODES[3], "again: the left arrow from Story is not Hard: %s" % heading(d[1]))
    row_checks("again", d[1], imgs["again"], 1, ["Eve"], ["250"])

if errors:
    print("FAIL")
    for e in errors:
        print(" -", e)
    sys.exit(1)
print("ok")
