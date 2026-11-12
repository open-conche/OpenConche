"""The Great Chocolate Chase (docs/design/greatcc.md slice 17): the plot, the quests, the achievement windows and the end of the game, with
real clicks on the real scripts and art.

Every run is `--headless --userdir <scratch> --seed 1` and asserts: exit 0; no [lua-error] / [engine-error] / [native-error] /
[content]; no [unimplemented] and no [unimplemented-type] at all (the plot, the quest panel and the achievement windows are all built);
the only textures the scripts name that the install does not ship are the shop's menu overlay, its full-screen backdrop and the trim of
the yes/no dialog.

Levels are reached the way the game's own map cheats reach them (`DoCheatMap` on the world map: highest level complete, play level n), so
the loading dialog, the level load, the pre-level shop and the diary are the shipped flow. State natives set up what would take hours:
`SetManagedValue` for the figures a level collects, and the level's total to pass it.

Run "diary": a new player, level 1: the diary dialog opens at the start of the level with the first page, every wrapped line drawn, the OK
button closes it with the dialog bouncing away and the level goes on (the timer is held while it is up). Run "again": the same player
starts level 1 again and the diary shows again (the original never saves the page as done; the progress file has no plot key).
Run "quest_fail": a new player at level 8 (shop, then the diary page `story_iquitos_8`, then the level): the quest panel lists "Customer
Appreciation" in the colour of a quest in progress; the level is passed with too few customers served and the results say the quest
failed (all lines of the text drawn). Run "quest_win": the same player plays level 8 again: the quest is offered again, goes green when 16
are served, and at the end the results name the reward (kIngredientQ2) and the achievement notification comes up over the results, holds
until it is hidden, slides away and goes. The progress file then has the quest done, the upgrade owned and the achievement. Run
"quest_done": level 8 again: no quest. Run "rewards": the Sweet Rewards screen of a player with four rewards: the lid is on and slides away,
the chocolates not earned are dim, the pointer shows the box with the title and the description (all lines drawn). Run "endgame": level 60
passed: the final diary page, then `scripts/endgame.lua` with its page and the Credits button, which opens the credits."""
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

NEW_PLAYER = """expect-window scripts/mainmenu.lua 20000
wait 1000
click 333 447
wait 1500
type %s
wait 100
click 310 368
wait 3500
"""
# the Options screen's Tutorials switch, then the map
TUTORIALS_OFF = """click 625 488
wait 3000
click 450 565
wait 1500
click 362 395
wait 300
click 404 486
wait 1500
"""
STORY = """click 625 488
wait 3000
"""
CONTINUE = """expect-window scripts/mainmenu.lua 20000
wait 1500
click 333 447
wait 3500
"""
GLOBE1 = """move 150 258
wait 700
click 150 258
"""
# the map's own cheats: highest level complete, play a level
def cheats(highest, play):
    t = "expect-lua 100 DoCheatMap(5, '%d') == 'Set highest level complete to %d'\nwait 500\n" % (highest, highest)
    t += "expect-lua 100 DoCheatMap(4, '%d') == ''\n" % play
    return t


def run(tag, script_text, shots=(), user=None):
    user_dir = os.path.join(a.workdir, "user_" + (user or tag))
    log = os.path.join(a.workdir, tag + ".log")
    script = os.path.join(a.workdir, tag + ".script")
    paths = {s: os.path.join(a.workdir, "%s_%s.png" % (tag, s)) for s in shots}
    for name, path in paths.items():
        script_text = script_text.replace("@%s@" % name, path)
    with open(script, "w") as f:
        f.write(script_text)
    p = subprocess.run([a.openconche, "--headless", "--cheatmode", "--assets", a.assets, "--userdir", user_dir, "--seed", "1", "--script", script,
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
    dumps = [d for d in re.split(r"^(?=\[windows\] )", p.stdout, flags=re.M) if d.startswith("[windows] ")]
    return out, dumps, paths


def progress_file(user):
    found = glob.glob(os.path.join(a.workdir, "user_" + user, "profiles", "*", "greatcc-progress.txt"))
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


def ndiff(p, q, x0, y0, x1, y1, th=30):
    """How many pixels of the box differ between two shots by more than `th` (sum of the three channels)."""
    return sum(1 for y in range(y0, y1) for x in range(x0, x1) if sum(abs(u - v) for u, v in zip(px(p, x, y), px(q, x, y))) > th)


def count(img, x0, y0, x1, y1, pred):
    return sum(1 for y in range(y0, y1) for x in range(x0, x1) if pred(px(img, x, y)))


def cream(c):
    return c[0] > 235 and c[1] > 220 and c[2] > 190


def expect(cond, msg):
    if not cond:
        errors.append(msg)


def click(x, y):
    # two moves first: a bouncing window sees the pointer one frame late
    return "move %d %d\nwait 50\nmove %d %d\nwait 50\nclick %d %d\n" % (x, y, x, y, x, y)


def hover(x, y):
    return "move %d %d\nwait 100\nmove %d %d\nwait 400\n" % (x, y, x, y)


# ---- what the window dump says ----

def modals(d):
    return re.findall(r'^  Modal "([^"]*)" .*script="([^"]*)"', d, re.M)


def top(d):
    m = modals(d)
    return m[-1][1] if m else None


def shop(d):
    m = re.search(r'^\s*ShopWindow "sprite_window" .*$', d, re.M)
    return m.group(0) if m else ""


def diary(d):
    """(page, shown, left, drawn lines, wrapped lines, closed, done) of the diary window, or None."""
    m = re.search(r'^\s*VPlotWindow "plot_window" .* page=(\S+)( done)? shown=(\d+) left=(\d+)( closed)?(?: lines=(\d+)/(\d+))?', d, re.M)
    if not m:
        return None
    return m.group(1), int(m.group(3)), int(m.group(4)), int(m.group(6) or 0), int(m.group(7) or 0), bool(m.group(5)), bool(m.group(2))


def quest_panel(d):
    """(quest lines, title label, status label) of the quest panel, or None. The panel is hidden when the dump says so."""
    m = re.search(r'^\s*VQuestWindow "quest_window" id=\d+ rect=\S+( hidden)? quests=(\d+)', d, re.M)
    if not m:
        return None
    names = re.search(r'Text "txt_quest_names" [^\n]*? label="([^"]*)"', d)
    status = re.search(r'Text "txt_quest_status" [^\n]*? label="([^"]*)"', d)
    return bool(m.group(1)), int(m.group(2)), names.group(1) if names else "", status.group(1) if status else ""


def notify(d):
    """(rect, phase, items, drawn lines, wrapped lines) of the achievement notification, or None."""
    m = re.search(r'^\s*VRewardNotify "reward_notify" id=\d+ rect=(\d+),(\d+),(\d+),(\d+) .* phase=(\d) items=(\d+) box=(-?\d+),(-?\d+),\S+ lines=(\d+)/(\d+)', d, re.M)
    if not m:
        return None
    return tuple(int(m.group(i)) for i in range(1, 5)), int(m.group(5)), int(m.group(6)), int(m.group(9)), int(m.group(10)), int(m.group(7))


def extra_info(d):
    m = re.search(r'Text "txt_extra_info" id=\d+ rect=\d+,\d+,(\d+),(\d+) label="([^"]*)" size=\d+ flags=\d+ lines=(\d+) lh=(\d+)', d)
    return (m.group(3), int(m.group(2)), int(m.group(4)), int(m.group(5))) if m else None


def rewards(d):
    m = re.search(r'^\s*VRewardsWindow "sprite_window" .* rewards=(\d+) earned=(\d+) lid=(-?\d+),(-?\d+)(?: hover=(\S+))?(?: tip=(\S+) lines=(\d+)/(\d+))?', d, re.M)
    if not m:
        return None
    return int(m.group(1)), int(m.group(2)), (int(m.group(3)), int(m.group(4))), m.group(5), m.group(6), int(m.group(7) or 0), int(m.group(8) or 0)


class Script:
    """Clicks and waits with a dump after each action, a screenshot where asked; the dumps are kept by name."""

    def __init__(self, head):
        self.text = head
        self.keys = []
        self.shots = []

    def act(self, key, move=None, ms=0, shot=None, pointer=None, lua=None):
        if lua:
            self.text += "lua %s\n" % lua
        if move:
            self.text += click(*move)
        if pointer:
            self.text += hover(*pointer)
        self.text += "wait %d\ndump\n" % ms
        self.keys.append(key)
        if shot:
            self.text += "screenshot @%s@\n" % shot
            self.shots.append(shot)

    def raw(self, text):
        self.text += text


def check_dumps(tag, S, d):
    if len(d) != len(S.keys):
        errors.append("%s: expected %d window dumps, got %d" % (tag, len(S.keys), len(d)))
        return None
    return dict(zip(S.keys, d))


PLAY, OK = (740, 540), (585, 500)
RESULTS_OK = (740, 550)

# ------------------------------------------------------------------------------------------------------------------------- diary
D1 = Script(NEW_PLAYER % "Zed" + STORY + GLOBE1 + "expect-window scripts/plotwindow.lua 60000\nwait 1500\n")
D1.act("open", ms=0, shot="open")
D1.act("click_ok", move=OK, ms=150, shot="bouncing")
D1.act("gone", ms=1500)
D1.act("running", ms=2500, shot="level")
out, d, paths = run("diary", D1.text, tuple(D1.shots))
D = check_dumps("diary", D1, d)
if D:
    o = D["open"]
    expect(top(o) == "scripts/plotwindow.lua", "diary: the diary is not on top: %s" % top(o))
    expect(diary(o) is not None and diary(o)[:3] == ("story_iquitos_1", 1, 0) and not diary(o)[5], "diary: the first page of Iquitos is not up: %s" % (diary(o),))
    expect(diary(o) is not None and diary(o)[3] == diary(o)[4] and diary(o)[4] > 0, "diary: the page draws %s of its wrapped lines" % (diary(o) and diary(o)[3:5],))
    expect(" level=1 " in shop(o) and " waiting=1 " in shop(o), "diary: the level behind the diary is not held at its start: %s" % shop(o)[:120])
    expect(quest_panel(o) is not None and quest_panel(o)[0] and quest_panel(o)[1] == 0, "diary: level 1 has no quest, so the panel is hidden: %s" % (quest_panel(o),))
    b = D["click_ok"]
    expect(top(b) == "scripts/plotwindow.lua" and diary(b) is not None and diary(b)[6] and diary(b)[5],
           "diary: the click on OK did not end the page and close the diary (it is still on top while it bounces away): %s %s" % (top(b), diary(b)))
    g = D["gone"]
    expect(top(g) == "scripts/game.lua", "diary: the diary is still on top after OK: %s" % top(g))
    r = D["running"]
    expect(" waiting=0 " in shop(r), "diary: the level did not start after the diary: %s" % shop(r)[:120])
    imgs = {k: png(v) for k, v in paths.items()}
    if all(imgs.values()):
        expect(ndiff(imgs["open"], imgs["level"], 60, 280, 380, 420, 90) > 4000, "diary: the diary page is not on the shot (the text area equals the level's)")
        expect(ndiff(imgs["bouncing"], imgs["open"], 0, 0, 800, 600, 60) > 20000, "diary: the dialog did not move on the click (it bounces away)")
expect(not [k for k in progress_file("diary") if k.startswith(("plot", "quest.", "plot_"))], "diary: the original never saves a page as done, but the progress file has %s" %
       [k for k in progress_file("diary") if k.startswith("plot")])

AGAIN = CONTINUE + GLOBE1 + "expect-window scripts/plotwindow.lua 60000\nwait 1500\ndump\n"
out, d, paths = run("again", AGAIN, user="diary")
if len(d) != 1:
    errors.append("again: expected 1 window dump, got %d" % len(d))
else:
    expect(top(d[0]) == "scripts/plotwindow.lua" and diary(d[0]) is not None and diary(d[0])[0] == "story_iquitos_1",
           "again: the diary does not show again at level 1: %s %s" % (top(d[0]), diary(d[0])))

# ------------------------------------------------------------------------------------------------------------------------- quest, failed
def level8(name, new):
    """A script to the middle of level 8: the shop (Play at once), the diary (OK), then the level."""
    head = (NEW_PLAYER % name + TUTORIALS_OFF) if new else CONTINUE
    S = Script(head + cheats(7, 8) + "expect-window scripts/prelevel.lua 30000\nwait 2500\n")
    S.act("shop", ms=0)
    S.act("play", move=PLAY, ms=3500)
    S.raw("expect-window scripts/plotwindow.lua 30000\nwait 1500\n")
    S.act("diary", ms=0, shot="diary")
    S.act("level", move=OK, ms=2500, shot="panel")
    return S


F = level8("Ann", True)
F.act("few", lua="SetManagedValue('served_this_level', 3, false)", ms=700, shot="few")
F.raw("lua SetManagedValue('current_level_total', 700, false)\nexpect-window scripts/endlevel.lua 600000\nwait 2500\n")
F.act("results", ms=0, shot="results")
out, d, paths = run("quest_fail", F.text, tuple(F.shots), user="quest")
D = check_dumps("quest_fail", F, d)
if D:
    expect(top(D["shop"]) == "scripts/prelevel.lua", "quest_fail: level 8 does not open with its shop: %s" % top(D["shop"]))
    dd = D["diary"]
    expect(diary(dd) is not None and diary(dd)[0] == "story_iquitos_8" and diary(dd)[3] == diary(dd)[4] > 0,
           "quest_fail: the diary page of level 8 is not up with all its lines: %s" % (diary(dd),))
    expect(quest_panel(dd) is not None and not quest_panel(dd)[0] and quest_panel(dd)[1] == 1, "quest_fail: the quest panel is not up under the diary: %s" % (quest_panel(dd),))
    lv = D["level"]
    qp = quest_panel(lv)
    expect(top(lv) == "scripts/game.lua" and qp is not None and not qp[0] and qp[1] == 1, "quest_fail: the panel is not shown in the level: %s" % (qp,))
    expect(qp is not None and qp[2] == "<font color='ffff00'>Customer Appreciation</font>\n", "quest_fail: the title line is %r" % (qp and qp[2],))
    expect(qp is not None and qp[3] == "<font color='ffff00'>0/16 served</font>\n", "quest_fail: the status line is %r" % (qp and qp[3],))
    expect(quest_panel(D["few"])[3] == "<font color='ffff00'>3/16 served</font>\n", "quest_fail: the status line after 3 served is %r" % (quest_panel(D["few"])[3],))
    r = D["results"]
    ei = extra_info(r)
    expect(top(r) == "scripts/endlevel.lua" and ei is not None and "failed to complete the quest" in ei[0] and "Customer Appreciation" in ei[0],
           "quest_fail: the results do not say the quest failed: %s" % (ei and ei[0],))
    expect(ei is not None and ei[2] <= ei[1], "quest_fail: the quest text (%s high) is cut by its box (%s)" % (ei and ei[2], ei and ei[1]))
    imgs = {k: png(v) for k, v in paths.items()}
    if all(imgs.values()):
        # the panel: parchment-coloured frame at the top left, with a line of text
        expect(count(imgs["panel"], 0, 0, 245, 60, lambda c: c[0] > 100 and c[1] < 60 and c[2] < 80) > 3000, "quest_fail: no quest panel on the shot")
pf = progress_file("quest")
expect(pf.get("quest.quest8") == "2", "quest_fail: the quest is not saved as failed: %s" % pf.get("quest.quest8"))
expect("upgrade.iquitos.kingredientq2" not in pf, "quest_fail: the reward was given for a failed quest")

# ------------------------------------------------------------------------------------------------------------------------- quest, won
W = level8("", False)
W.act("pending", ms=0)
W.act("some", lua="SetManagedValue('served_this_level', 10, false)", ms=700)
W.act("done", lua="SetManagedValue('served_this_level', 17, false)", ms=700, shot="green")
W.raw("lua SetManagedValue('current_level_total', 700, false)\nlua SetManagedValue('num_served', 12, true)\nexpect-window scripts/endlevel.lua 600000\nwait 2500\n")
W.act("results", ms=0, shot="results")
W.act("hold", ms=6000, shot="held")
W.act("hiding", lua="HideNotification()", ms=50)
W.act("away", ms=1200, shot="away")
out, d, paths = run("quest_win", W.text, tuple(W.shots), user="quest")
D = check_dumps("quest_win", W, d)
if D:
    qp = quest_panel(D["level"])
    expect(qp is not None and not qp[0] and qp[1] == 1 and qp[3] == "<font color='ffff00'>0/16 served</font>\n",
           "quest_win: a quest that failed before is not offered again: %s" % (qp,))
    expect(quest_panel(D["some"])[3] == "<font color='ffff00'>10/16 served</font>\n", "quest_win: the status at 10 served: %r" % (quest_panel(D["some"])[3],))
    qd = quest_panel(D["done"])
    expect(qd[2] == "<font color='00ff00'>Customer Appreciation</font>\n" and qd[3] == "<font color='00ff00'>Complete</font>\n", "quest_win: the lines when 16 are served: %r %r" % (qd[2], qd[3]))
    r = D["results"]
    ei = extra_info(r)
    expect(ei is not None and "completed the quest" in ei[0] and "Arabica Coffee Bean" in ei[0], "quest_win: the results do not name the reward: %s" % (ei and ei[0],))
    expect(ei is not None and ei[2] <= ei[1], "quest_win: the quest text (%s high) is cut by its box (%s)" % (ei and ei[2], ei and ei[1]))
    n = notify(r)
    expect(n is not None, "quest_win: no achievement notification at the end of the level")
    if n:
        expect(n[0][0] == 525 and n[0][1] == 0 and n[0][2] == 275, "quest_win: the notification is not at the top right: %s" % (n[0],))
        expect(n[2] >= 1 and n[3] == n[4] > 0, "quest_win: the notification draws %s of its wrapped lines, %s items" % (n[3:5], n[2]))
        expect(n[1] == 1 and n[5] == 0, "quest_win: the notification is not at rest in its place: phase %s, box x %s" % (n[1], n[5]))
        expect(n[0][3] == 150 + (n[2] - 1) * 80 or n[0][3] > 100, "quest_win: the notification's height %s" % (n[0][3],))
    h = D["hold"]
    expect(notify(h) is not None and notify(h)[1] == 1, "quest_win: the notification did not wait for HideNotification: %s" % (notify(h),))
    hd = D["hiding"]
    expect(notify(hd) is not None and notify(hd)[1] == 2, "quest_win: HideNotification did not start the slide out: %s" % (notify(hd),))
    expect(notify(D["away"]) is None and top(D["away"]) == "scripts/endlevel.lua", "quest_win: the notification did not go: %s" % (notify(D["away"]),))
    imgs = {k: png(v) for k, v in paths.items()}
    if all(imgs.values()):
        expect(ndiff(imgs["results"], imgs["away"], 540, 10, 800, 380, 60) > 8000, "quest_win: the notification is not on the shot of the results")
        expect(count(imgs["green"], 0, 0, 245, 60, lambda c: c[1] > 200 and c[0] < 80 and c[2] < 80) > 20, "quest_win: no green line on the panel when the quest is done")
pf = progress_file("quest")
expect(pf.get("quest.quest8") == "1", "quest_win: the quest is not saved as done: %s" % pf.get("quest.quest8"))
expect(pf.get("upgrade.iquitos.kingredientq2") == "1", "quest_win: the reward upgrade is not owned: %s" % pf.get("upgrade.iquitos.kingredientq2"))
expect(pf.get("achievement.served10") == "1", "quest_win: the achievement is not saved: %s" % pf.get("achievement.served10"))
expect("upgrade.iquitos.kingredientq1" not in pf, "quest_win: the plot page's own quest reward was given (nothing evaluates it)")

# ------------------------------------------------------------------------------------------------------------------------- quest, done
Q = level8("", False)
out, d, paths = run("quest_done", Q.text, tuple(Q.shots), user="quest")
D = check_dumps("quest_done", Q, d)
if D:
    qp = quest_panel(D["level"])
    expect(qp is not None and qp[0] and qp[1] == 0, "quest_done: a quest that was done is offered again: %s" % (qp,))
    expect(diary(D["diary"]) is not None and diary(D["diary"])[0] == "story_iquitos_8", "quest_done: the diary page of level 8 is not shown again")

# ------------------------------------------------------------------------------------------------------------------------- rewards
R = Script(NEW_PLAYER % "Rae" + STORY + "lua SetManagedValue('num_served', 60, true)\nlua SetManagedValue('num_bars', 120, true)\nlua CheckAllAchievements('adventure', false, 0)\n" +
           click(330, 555) + "expect-window scripts/rewardswindow.lua 10000\nwait 300\n")
R.act("open", ms=0, shot="lid")
R.act("lifted", ms=3000, shot="lifted")
R.act("bars100", pointer=(645, 115), ms=0, shot="tip_earned")
R.act("truffle", pointer=(560, 130), ms=0, shot="tip_dim")
R.act("away", pointer=(300, 580), ms=400)
out, d, paths = run("rewards", R.text, tuple(R.shots))
D = check_dumps("rewards", R, d)
if D:
    o = rewards(D["open"])
    expect(top(D["open"]) == "scripts/rewardswindow.lua" and o is not None and o[0] == 50 and o[1] == 4 and o[2] == (0, 0),
           "rewards: the screen is not up with 50 rewards, 4 earned and the lid on: %s" % (o,))
    l = rewards(D["lifted"])
    expect(l is not None and l[2] == (600, -600), "rewards: the lid did not slide away: %s" % (l,))
    t = rewards(D["bars100"])
    expect(t is not None and t[3] == "bars100" and t[4] == "bars100" and t[5] == t[6] > 0, "rewards: the earned chocolate shows no box with all its lines: %s" % (t,))
    t = rewards(D["truffle"])
    expect(t is not None and t[4] == "truffle1000" and t[5] == t[6] > 0, "rewards: the chocolate not earned shows no box with all its lines: %s" % (t,))
    expect(rewards(D["away"])[4] is None, "rewards: the box stays after the pointer left: %s" % (rewards(D["away"]),))
    imgs = {k: png(v) for k, v in paths.items()}
    if all(imgs.values()):
        expect(ndiff(imgs["lid"], imgs["lifted"], 100, 100, 700, 500, 60) > 20000, "rewards: the lid does not move on the shots")
        expect(ndiff(imgs["tip_earned"], imgs["lifted"], 530, 80, 730, 190, 60) > 1500, "rewards: no box on the shot of the earned chocolate")
        expect(ndiff(imgs["tip_dim"], imgs["lifted"], 530, 160, 730, 260, 60) > 1500, "rewards: no box on the shot of the dim chocolate")

# ------------------------------------------------------------------------------------------------------------------------- endgame
E = Script(NEW_PLAYER % "Eve" + TUTORIALS_OFF + cheats(59, 60) + "expect-window scripts/prelevel.lua 30000\nwait 2500\n")
E.act("play", move=PLAY, ms=3500)
E.raw("expect-window scripts/plotwindow.lua 30000\nwait 1500\n")
E.act("diary", ms=0, shot="diary60")
E.act("level", move=OK, ms=2500)
E.raw("lua SetManagedValue('current_level_total', 100000, false)\nexpect-window scripts/endlevel.lua 900000\nwait 2500\n")
E.act("results", ms=0)
E.raw(click(*RESULTS_OK) + "expect-window scripts/endgame.lua 60000\nwait 3000\n")
E.act("endgame", ms=0, shot="endgame")
E.act("credits", move=(570, 486), ms=2500, shot="credits")
out, d, paths = run("endgame", E.text, tuple(E.shots))
D = check_dumps("endgame", E, d)
if D:
    dd = D["diary"]
    expect(diary(dd) is not None and diary(dd)[0] == "story_gaberoun_10" and diary(dd)[3] == diary(dd)[4] > 0,
           "endgame: level 60's diary page is not up with all its lines: %s" % (diary(dd),))
    expect("Level" in D["results"] and top(D["results"]) == "scripts/endlevel.lua", "endgame: level 60 did not end in the results: %s" % top(D["results"]))
    eg = D["endgame"]
    m = re.search(r'^\s*VObjectWindow "sprite_window" .* lines=(\d+)/(\d+)', eg, re.M)
    expect(top(eg) == "scripts/endgame.lua" and m is not None and m.group(1) == m.group(2) and int(m.group(2)) > 0,
           "endgame: scripts/endgame.lua is not up with its page drawn in full: %s %s" % (top(eg), m and m.groups()))
    expect(top(D["credits"]) == "scripts/credits.lua", "endgame: the Credits button did not open the credits: %s" % top(D["credits"]))
    imgs = {k: png(v) for k, v in paths.items()}
    if all(imgs.values()):
        expect(ndiff(imgs["endgame"], imgs["credits"], 0, 0, 800, 600, 60) > 30000, "endgame: the credits are not on the shot")
        expect(count(imgs["endgame"], 60, 270, 380, 400, lambda c: sum(c) < 250) > 150, "endgame: no text on the left page of the shot")
pf = progress_file("endgame")
expect(pf.get("level.60", "").startswith(("1,", "2,")), "endgame: level 60 is not saved as passed: %s" % pf.get("level.60"))

if errors:
    print("\n".join(errors))
    sys.exit(1)
print("ok")
