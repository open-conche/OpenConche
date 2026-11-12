"""The Great Chocolate Chase (docs/design/greatcc.md slice 16): the upgrades and the pre-level shop, with real clicks, on the real scripts and art.

Every run is `--headless --userdir <scratch> --seed 1` and asserts: exit 0; no [lua-error] / [engine-error] / [native-error] /
[content]; the [unimplemented] and [unimplemented-type] sets are empty; the only textures the scripts name that the
install does not ship are the shop's menu overlay, its full-screen backdrop and the trim of the yes/no dialog.

Levels are reached the way the game's own map cheats reach them (`DoCheatMap` on the world map: highest level complete, cash for the open
location, play level n), so the loading dialog, the level load and the pre-level shop are the shipped flow.

Run "shop": a new player with Tutorials on and $600 in Iquitos reaches level 4, the first level with an upgrade shop. The tutorial of
the shop (`tutorial_upgrades`, 7 steps) is up with all its text drawn; a click outside what the step makes clickable (another tab, Play)
does nothing; the Decoration tab lists the offers cheapest-available first with the rest "Unavailable"; hovering the Parquet Flooring offer
previews it in the shop picture (the floor changes) and leaving takes it away; buying it takes $100 off the cash register and closes the
gap in the list; the Machines tab's Sauce Refill Speed offer makes its machine glow while hovered, and is bought; the Power-ups tab's
Serving Shoes are bought; Play closes the shop and the level starts with the floor of the upgrade. Afterwards the progress file holds
the three purchases and the cash of Iquitos ($250), and the tutorial's state is 3.
Run "again": a second process, the same player, level 4 again: no tutorial, the register says $250, the three offers are gone from their
lists, and the scene shows the bought floor.
Run "next": a new player, Tutorials off, buys nothing and plays level 4 to the results (the level's total is set high so it passes), then
continues to level 5: the loading dialog, the level load, and the pre-level shop again, with the offers of level 5.
Run "display": level 2 with Tutorials on, with real clicks up to the Display Case step (`tutorial_2_3` of `tutorial_4`): all 14 wrapped
lines are drawn, and the pixels of the shot say where the text sits against the panel art."""
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
END_CALLS = set()

NEW_PLAYER = """expect-window scripts/mainmenu.lua 20000
wait 1000
click 333 447
wait 1500
type %s
wait 100
click 310 368
wait 3500
"""
# the Options screen's Tutorials switch (as the machines scenario does it), then the map
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
# A level opens with its diary page (level 1 at once, the later levels after the shop); the OK button closes it.
DIARY = """expect-window scripts/plotwindow.lua 30000
wait 450
move 585 500
wait 50
move 585 500
wait 50
click 585 500
"""
# the map's own cheats: highest level complete, cash for the open location, play a level
def cheats(highest, cash, play):
    t = "expect-lua 100 DoCheatMap(5, '%d') == 'Set highest level complete to %d'\nwait 500\n" % (highest, highest)
    if cash:
        t += "expect-lua 100 DoCheatMap(3, '%d') == 'Added %d to location 1 and lifetime total'\n" % (cash, cash)
    t += "expect-lua 100 DoCheatMap(4, '%d') == ''\n" % play
    return t


def run(tag, script_text, calls, shots=(), user=None):
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
    seen = set(re.findall(r"^(?:\[\+\d+ms\] )?\[unimplemented\] (\S+)", out, re.M))
    if seen != calls:
        errors.append("%s: [unimplemented] set differs: extra %s, missing %s" % (tag, sorted(seen - calls), sorted(calls - seen)))
    types = set(re.findall(r"^(?:\[\+\d+ms\] )?\[unimplemented-type\] (\S+)", out, re.M))
    if types != TYPES:
        errors.append("%s: [unimplemented-type] set differs: extra %s, missing %s" % (tag, sorted(types - TYPES), sorted(TYPES - types)))
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
    # two moves first: a window under a tutorial or a bouncing window sees the pointer one frame late
    return "move %d %d\nwait 50\nmove %d %d\nwait 50\nclick %d %d\n" % (x, y, x, y, x, y)


def hover(x, y):
    return "move %d %d\nwait 50\nmove %d %d\n" % (x, y, x, y)


# ---- what the window dump says ----

def modals(d):
    return re.findall(r'^  Modal "([^"]*)" .*script="([^"]*)"', d, re.M)


def top(d):
    m = modals(d)
    return m[-1][1] if m else None


def shop(d):
    m = re.search(r'^\s*ShopWindow "sprite_window" .*$', d, re.M)
    return m.group(0) if m else ""


def floor(d):
    m = re.search(r" floor=(\S+)", shop(d))
    return m.group(1) if m else None


def bought(d):
    m = re.search(r" bought=(\S+)", shop(d))
    return set(m.group(1).split(",")) if m else set()


def prelevel(d):
    m = re.search(r'^\s*VPrelevelWindow "prelevel_window" .* category=(\S+) tabs=(\d+)$', d, re.M)
    return (m.group(1), int(m.group(2))) if m else None


def tabs(d):
    """upgrade name -> (tab look, upgrade state, previewing) for the offers of the shop."""
    out = {}
    for m in re.finditer(r'^\s*VUpgradeTab "upgrade_tab_(\w+)" .* rect=(\d+),(-?\d+),\d+,\d+ upgrade=\w+ tab=(\d) state=(\d)( preview)?', d, re.M):
        out[m.group(1)] = (int(m.group(4)), int(m.group(5)), bool(m.group(6)), int(m.group(3)))
    return out


def buy_button_hidden(d):
    """upgrade name -> whether its tab's price button is hidden in the dump."""
    out, lines = {}, d.split("\n")
    for i, l in enumerate(lines):
        m = re.match(r'^(\s*)VUpgradeTab "upgrade_tab_(\w+)"', l)
        if not m:
            continue
        n = len(m.group(1))
        for l2 in lines[i + 1:]:
            if len(l2) - len(l2.lstrip()) <= n:
                break
            if 'RDisableButton "btn_buy_upgrade"' in l2:
                out[m.group(2)] = " hidden " in l2
    return out


def cash(d):
    m = re.search(r'^\s*VRegister "sprite_window" .* context="prelevel" digits=\d+ value=(\d+)', d, re.M)
    return int(m.group(1)) if m else None


def tutorial(d):
    """(tutorial, step, steps, drawn lines, wrapped lines) of the tutorial window, or None."""
    m = re.search(r'^\s*VTutorialWindow "tutorial_window" .* tutorial=(\S+) step=(\d+)/(\d+)( done)?(?: lines=(\d+)/(\d+))?', d, re.M)
    if not m or m.group(4):
        return None
    return m.group(1), int(m.group(2)), int(m.group(3)), int(m.group(5) or 0), int(m.group(6) or 0)


class Script:
    """Clicks and waits with a dump after each action, a screenshot where asked; the dumps are kept by name."""

    def __init__(self, head):
        self.text = head
        self.keys = []
        self.shots = []

    def act(self, key, move=None, ms=0, shot=None, pointer=None, diary=False):
        if move:
            self.text += click(*move)
        if diary:
            self.text += DIARY
        if pointer:
            self.text += hover(*pointer)
        self.text += "wait %d\ndump\n" % ms
        self.keys.append(key)
        if shot:
            self.text += "screenshot @%s@\n" % shot
            self.shots.append(shot)


DECOR, MACH, POWER = (103, 111), (190, 111), (277, 111)
BUY, TAB1 = (261, 228), (200, 200)  # the first offer's price button, and a place on that offer
PLAY, SKIP = (740, 540), (22, 575)
PREVIEW = (342, 93, 742, 393)  # the shop's picture in its frame

# ------------------------------------------------------------------------------------------------------------------------- shop
S = Script(NEW_PLAYER % "Zed" + STORY + cheats(3, 600, 4) + "expect-window scripts/prelevel.lua 30000\nwait 2500\n")
S.act("open", ms=0, shot="open")
S.act("stray_tab", move=POWER, ms=700)     # not what the step asks for: nothing happens
S.act("stray_play", move=PLAY, ms=1200)    # nor does Play
S.act("decor", move=DECOR, ms=900, shot="decor")
S.act("hover1", pointer=TAB1, ms=700, shot="hover1")
S.act("leave1", pointer=(400, 520), ms=700, shot="leave1")
S.act("bought1", move=BUY, ms=1700, shot="bought1")
S.act("machines", move=MACH, ms=900, shot="machines")
S.act("hover2", pointer=TAB1, ms=700, shot="hover2")
S.act("bought2", move=BUY, ms=1700)
S.act("powerups", move=POWER, ms=900)
S.act("bought3", move=BUY, ms=1700, shot="bought3")
S.act("play", move=PLAY, ms=2400, shot="play", diary=True)
SHOTS = tuple(S.shots)
out, d, paths = run("shop", S.text, set(), SHOTS)
D = dict(zip(S.keys, d))
if len(d) != len(S.keys):
    errors.append("shop: expected %d window dumps, got %d" % (len(S.keys), len(d)))
else:
    o = D["open"]
    expect(top(o) == "scripts/prelevel.lua", "shop: the upgrade shop is not on top: %s" % top(o))
    expect(shop(o) != "" and " level=4 " in shop(o), "shop: the level behind the shop is not level 4: %s" % shop(o)[:80])
    expect(prelevel(o) == ("station", 3), "shop: the shop does not start on the Machines list with its three offers: %s" % (prelevel(o),))
    expect(cash(o) == 600, "shop: the cash register does not say 600: %s" % cash(o))
    t = tutorial(o)
    expect(t is not None and t[:3] == ("tutorial_upgrades", 1, 7), "shop: the shop's tutorial is not at its first step: %s" % (t,))
    expect(t is not None and t[3] == t[4] and t[4] > 0, "shop: the tutorial's first panel draws %s of its wrapped lines" % (t and t[3:],))
    to = tabs(o)
    expect(to.get("ksauce2", (0,))[0] == 0 and to.get("ksauce2", (0, 0))[1] == 1, "shop: Sauce Refill Speed is not on offer: %s" % (to.get("ksauce2"),))
    expect(all(v[0] == 3 for k, v in to.items() if k != "ksauce2") and len(to) == 3, "shop: the other two offers are not locked: %s" % to)
    for key, what in (("stray_tab", "Power-ups tab"), ("stray_play", "Play")):
        expect(top(D[key]) == "scripts/prelevel.lua" and prelevel(D[key]) == ("station", 3) and tutorial(D[key])[1] == 1,
               "shop: the click on %s during the first step did something: %s %s" % (what, prelevel(D[key]), tutorial(D[key])))
    dc = D["decor"]
    expect(prelevel(dc) == ("aesthetic", 8) and tutorial(dc)[:2] == ("tutorial_upgrades", 2), "shop: the Decoration tab: %s %s" % (prelevel(dc), tutorial(dc)))
    td = tabs(dc)
    # the available offer first, then the rest by price
    order = sorted(td, key=lambda k: td[k][3])
    expect(order[0] == "kbackground1" and td["kbackground1"][:2] == (0, 1), "shop: Parquet Flooring is not the first, available offer: %s" % order)
    prices = [k for k in order[1:]]
    expect(all(td[k][0] == 3 for k in prices), "shop: the offers below the first are not all locked: %s" % td)
    expect(td[order[1]][3] == 90 and td[order[-1]][3] == 90 * 7, "shop: the offers are not stacked 90 apart: %s" % [(k, td[k][3]) for k in order])
    bh = buy_button_hidden(dc)
    expect(bh.get("kbackground1") is False and all(bh.get(k) is True for k in td if k != "kbackground1"),
           "shop: a locked offer shows its price button (or the available one does not), by the shipped upgrade.lua: %s" % bh)
    h1 = D["hover1"]
    expect(tabs(h1)["kbackground1"][:3] == (1, 4, True), "shop: hovering Parquet Flooring does not preview it: %s" % (tabs(h1)["kbackground1"],))
    expect(floor(h1) == "upgrade1" and "kbackground1(preview)" in bought(h1), "shop: the scene does not show the previewed floor: %s %s" % (floor(h1), bought(h1)))
    l1 = D["leave1"]
    expect(floor(l1) == "default" and tabs(l1)["kbackground1"][:3] == (0, 1, False), "shop: leaving the offer does not take the preview away: %s %s" % (floor(l1), tabs(l1)["kbackground1"]))
    b1 = D["bought1"]
    expect(cash(b1) == 500, "shop: the register does not say 500 after Parquet Flooring: %s" % cash(b1))
    expect(prelevel(b1) == ("aesthetic", 7) and "kbackground1" not in tabs(b1), "shop: the bought offer is still listed: %s" % (prelevel(b1),))
    expect(tabs(b1)["kstation1"][3] == 0, "shop: the offers did not slide up to the top: %s" % (tabs(b1)["kstation1"],))
    expect(floor(b1) == "upgrade1" and "kbackground1" in bought(b1), "shop: the floor of the scene is not the bought one: %s %s" % (floor(b1), bought(b1)))
    expect(tutorial(b1)[:2] == ("tutorial_upgrades", 3), "shop: the tutorial did not go on after the first purchase: %s" % (tutorial(b1),))
    m2 = D["machines"]
    expect(prelevel(m2) == ("station", 3) and tutorial(m2)[:2] == ("tutorial_upgrades", 4), "shop: the Machines tab: %s %s" % (prelevel(m2), tutorial(m2)))
    expect(tabs(D["hover2"])["ksauce2"][:3] == (1, 4, True), "shop: hovering Sauce Refill Speed does not preview it")
    b2 = D["bought2"]
    expect(cash(b2) == 350 and prelevel(b2) == ("station", 2) and tutorial(b2)[1] == 5, "shop: after Sauce Refill Speed: %s %s %s" % (cash(b2), prelevel(b2), tutorial(b2)))
    p3 = D["powerups"]
    expect(prelevel(p3) == ("gameplay", 8) and tutorial(p3)[1] == 6, "shop: the Power-ups tab: %s %s" % (prelevel(p3), tutorial(p3)))
    b3 = D["bought3"]
    expect(cash(b3) == 250 and prelevel(b3) == ("gameplay", 7) and tutorial(b3)[1] == 7, "shop: after Serving Shoes: %s %s %s" % (cash(b3), prelevel(b3), tutorial(b3)))
    expect({"kbackground1", "ksauce2", "kmovement1"} <= bought(b3), "shop: the three purchases are not owned: %s" % bought(b3))
    # every one of the seven panels draws all of its wrapped lines (steps 1..7 are the dumps of these keys)
    for step, key in enumerate(("open", "decor", "bought1", "machines", "bought2", "powerups", "bought3"), 1):
        tt = tutorial(D[key])
        expect(tt is not None and tt[:3] == ("tutorial_upgrades", step, 7), "shop: step %d: the tutorial window says %s" % (step, (tt,)))
        expect(tt is not None and tt[3] == tt[4] and tt[4] > 0, "shop: step %d draws %s of its wrapped lines" % (step, (tt and tt[3:],)))
    pl = D["play"]
    expect(top(pl) == "scripts/game.lua" and prelevel(pl) is None and tutorial(pl) is None, "shop: Play did not close the shop: %s" % top(pl))
    expect(" waiting=0 " in shop(pl) and " active=1 " in shop(pl), "shop: the level did not start after the shop: %s" % shop(pl)[:140])
    expect(floor(pl) == "upgrade1", "shop: the level does not have the bought floor: %s" % floor(pl))
    pf = progress_file("shop")
    expect(pf.get("cash.iquitos") == "250", "shop: the cash of Iquitos is not 250 in the progress file: %s" % pf.get("cash.iquitos"))
    for k in ("kbackground1", "ksauce2", "kmovement1", "kmixer1"):
        expect(pf.get("upgrade.iquitos." + k) == "1", "shop: upgrade.iquitos.%s is not saved: %s" % (k, pf.get("upgrade.iquitos." + k)))
    expect(pf.get("tutorial.tutorial_upgrades") == "3", "shop: the shop's tutorial is not saved as done: %s" % pf.get("tutorial.tutorial_upgrades"))
    expect(pf.get("value.num_char_upgrades") == "1", "shop: the power-up was not counted: %s" % pf.get("value.num_char_upgrades"))
    expect(not any(k.startswith("upgrade.") and "gaberoun" in k for k in pf), "shop: an upgrade of another location was saved")
    imgs = {k: png(v) for k, v in paths.items()}
    if all(imgs.values()):
        # the first panel: cream where its text is, the scene behind it
        expect(count(imgs["open"], 380, 110, 580, 310, cream) > 15000, "shop: the first tutorial panel is not cream where its text is")
        expect(count(imgs["open"], 70, 160, 300, 420, cream) < 3000, "shop: the list is covered by the panel")
        # the preview changes the picture of the shop, and leaving it puts it back
        expect(ndiff(imgs["decor"], imgs["hover1"], 625, 150, 740, 330) > 3000, "shop: the floor in the picture does not change while Parquet Flooring is hovered")
        expect(ndiff(imgs["decor"], imgs["leave1"], 625, 150, 740, 330) < 300, "shop: the picture does not go back after leaving the offer")
        # the glow of the bound machine
        expect(ndiff(imgs["machines"], imgs["hover2"], 650, 190, 720, 250) > 300, "shop: the sauce machine does not glow while Sauce Refill Speed is hovered")
        # the cash register drew the new total (the digits differ from the first shot's)
        expect(ndiff(imgs["open"], imgs["bought3"], 330, 450, 580, 490) > 200, "shop: the cash register did not change")

# ------------------------------------------------------------------------------------------------------------------------- again
A = Script("""expect-window scripts/mainmenu.lua 20000
wait 1500
click 333 447
wait 3500
""" + cheats(3, 0, 4) + "expect-window scripts/prelevel.lua 30000\nwait 2500\n")
A.act("open", ms=0, shot="open")
A.act("decor", move=DECOR, ms=900, shot="decor")
A.act("machines", move=MACH, ms=900)
A.act("powerups", move=POWER, ms=900)
out, d, paths = run("again", A.text, set(), tuple(A.shots), user="shop")
D = dict(zip(A.keys, d))
if len(d) != len(A.keys):
    errors.append("again: expected %d window dumps, got %d" % (len(A.keys), len(d)))
else:
    o = D["open"]
    expect(top(o) == "scripts/prelevel.lua" and tutorial(o) is None, "again: the shop has a tutorial again: %s" % (tutorial(o),))
    expect(cash(o) == 250, "again: the register does not say 250: %s" % cash(o))
    expect(prelevel(o) == ("station", 2) and "ksauce2" not in tabs(o), "again: Sauce Refill Speed is on offer again: %s %s" % (prelevel(o), sorted(tabs(o))))
    expect(prelevel(D["decor"]) == ("aesthetic", 7) and "kbackground1" not in tabs(D["decor"]), "again: Parquet Flooring is on offer again")
    expect(prelevel(D["powerups"]) == ("gameplay", 7) and "kmovement1" not in tabs(D["powerups"]), "again: Serving Shoes are on offer again")
    expect({"kbackground1", "ksauce2", "kmovement1"} <= bought(o) and floor(o) == "upgrade1", "again: the level does not own what was bought: %s %s" % (bought(o), floor(o)))
    imgs = {k: png(v) for k, v in paths.items()}
    first = png(os.path.join(a.workdir, "shop_open.png"))
    if all(imgs.values()) and first:
        expect(ndiff(first, imgs["open"], 625, 150, 740, 330) > 3000, "again: the picture of the shop shows the old floor")

# ------------------------------------------------------------------------------------------------------------------------- next
N = Script(NEW_PLAYER % "Ann" + TUTORIALS_OFF + cheats(3, 600, 4) + "expect-window scripts/prelevel.lua 30000\nwait 2500\n")
N.act("shop4", ms=0, shot="shop4")
N.act("buy", move=DECOR, ms=900)
N.act("bought", move=BUY, ms=1700)
N.act("play", move=PLAY, ms=1350, diary=True)
N.text += "lua SetManagedValue('current_level_total', 100000, false)\nwait 1000\n"
N.text += "expect-window scripts/endlevel.lua 600000\nwait 2500\ndump\nscreenshot @results@\n"
N.keys.append("results")
N.shots.append("results")
N.text += click(740, 550) + "expect-window scripts/prelevel.lua 60000\nwait 2500\ndump\nscreenshot @shop5@\n"
N.keys.append("shop5")
N.shots.append("shop5")
N.text += "wait 500\n" + click(103, 111) + "wait 900\ndump\n"
N.keys.append("decor5")
out, d, paths = run("next", N.text, END_CALLS, tuple(N.shots))
D = dict(zip(N.keys, d))
if len(d) != len(N.keys):
    errors.append("next: expected %d window dumps, got %d" % (len(N.keys), len(d)))
else:
    s4 = D["shop4"]
    expect(top(s4) == "scripts/prelevel.lua" and " level=4 " in shop(s4) and tutorial(s4) is None, "next: level 4's shop: %s %s" % (top(s4), tutorial(s4)))
    expect(cash(s4) == 600, "next: the register does not say 600: %s" % cash(s4))
    expect(prelevel(D["bought"]) == ("aesthetic", 7) and cash(D["bought"]) == 500, "next: Parquet Flooring was not bought: %s %s" % (prelevel(D["bought"]), cash(D["bought"])))
    pl = D["play"]
    expect(top(pl) == "scripts/game.lua" and " waiting=0 " in shop(pl), "next: the level did not start: %s" % top(pl))
    r = D["results"]
    expect(top(r) == "scripts/endlevel.lua" and ("Level Passed" in r or "Level Mastered" in r or "Mastered" in r), "next: the results do not say passed: %s" % top(r))
    s5 = D["shop5"]
    expect(top(s5) == "scripts/prelevel.lua" and " level=5 " in shop(s5), "next: level 5 did not open its shop: %s %s" % (top(s5), shop(s5)[:90]))
    expect(floor(s5) == "upgrade1" and "kbackground1" in bought(s5), "next: level 5 does not have the floor bought at level 4: %s %s" % (floor(s5), bought(s5)))
    c5 = cash(s5)
    expect(c5 == 500 + 100000, "next: the cash of level 4 (500 left, 100000 earned) did not carry over: %s" % c5)
    d5 = D["decor5"]
    t5 = tabs(d5)
    expect(prelevel(d5) is not None and prelevel(d5)[0] == "aesthetic" and "kbackground1" not in t5, "next: level 5's Decoration list: %s" % (prelevel(d5),))
    expect(t5.get("kstation1", (3, 0))[:2] == (0, 1), "next: the Banana Leaf Table (minlevel 5) is not on offer at level 5: %s" % (t5.get("kstation1"),))
    imgs = {k: png(v) for k, v in paths.items()}
    if all(imgs.values()):
        expect(count(imgs["shop5"], 70, 160, 300, 420, lambda c: c[0] > 200 and c[1] > 190 and c[2] > 170) > 5000, "next: level 5's list is empty on the shot")
pf = progress_file("next")
expect(pf.get("upgrade.iquitos.kbackground1") == "1" and pf.get("level.4", "").startswith(("1,", "2,")), "next: the progress after level 4: %s %s" % (pf.get("upgrade.iquitos.kbackground1"), pf.get("level.4")))

# ------------------------------------------------------------------------------------------------------------------------- display
M = Script(NEW_PLAYER % "Bea" + STORY + cheats(1, 0, 2) + DIARY + "wait 4850\n")
M.act("mix", move=(80, 525), ms=1500)
M.act("mixing", ms=4500)
M.act("tank", move=(155, 440), ms=1500)
M.act("pressbtn", move=(456, 575), ms=1500)
M.act("pressing", ms=3000)
M.act("press", move=(500, 480), ms=2500)
M.act("display", move=(410, 325), ms=3000, shot="panel")
out, d, paths = run("display", M.text, set(), tuple(M.shots))
D = dict(zip(M.keys, d))
if len(d) != len(M.keys):
    errors.append("display: expected %d window dumps, got %d" % (len(M.keys), len(d)))
else:
    t = tutorial(D["display"])
    expect(t is not None and t[:3] == ("tutorial_4", 8, 9), "display: the Display Case step is not up: %s" % (t,))
    expect(t is not None and t[3] == t[4] and t[4] == 14, "display: the panel draws %s of its wrapped lines (14 expected)" % (t and t[3:],))
    img = png(paths["panel"])
    if img:
        # The panel art is cream; the text is dark on it. Where do they end?
        rows = [y for y in range(120, 440) if count(img, 520, y, 680, y + 1, cream) > 60]
        dark = [y for y in rows if count(img, 535, y, 668, y + 1, lambda c: sum(c) < 250) > 0]
        if rows and dark:
            print("display panel: cream rows %d..%d, text rows %d..%d (box 140..420)" % (rows[0], rows[-1], dark[0], dark[-1]))
            expect(dark[0] > rows[0] + 6 and dark[-1] < rows[-1] - 3, "display: the text runs out of the cream of the panel: cream %d..%d, text %d..%d" % (rows[0], rows[-1], dark[0], dark[-1]))
        else:
            errors.append("display: no panel on the shot")

# ------------------------------------------------------------------------------------------------------------------------- bounce
# The shop rises from the bottom of the screen in 400 ms; the picture of the shop in its frame is part of the window and rises with it.
B = NEW_PLAYER % "Cy" + TUTORIALS_OFF + cheats(3, 600, 4) + "expect-window scripts/prelevel.lua 30000\nwait 150\nscreenshot @early@\nwait 150\nscreenshot @mid@\nwait 2500\nscreenshot @rest@\n"
out, d, paths = run("bounce", B.replace("\nwait 2500\nscreenshot @rest@\n", "\nwait 2500\nscreenshot @rest@\n"), set(), ("early", "mid", "rest"))
imgs = {k: png(v) for k, v in paths.items()}
if all(imgs.values()):
    # the picture's frame is at (342, 93) to (742, 393) once the shop has arrived; early on the window is still below it
    for k in ("early", "mid"):
        n = ndiff(imgs[k], imgs["rest"], 640, 200, 735, 380, 60)
        expect(n > 6000, "bounce: %s: the shop's picture is already in its frame while the window is still rising (%d pixels differ from the shop at rest)" % (k, n))
        print("bounce %s: %d pixels differ from the shop at rest" % (k, n))

# ------------------------------------------------------------------------------------------------------------------------- quit
# The Quit button inside the shop: Close() (the previews end), the yes/no dialog, QuitToMainMenu.
Q = Script(NEW_PLAYER % "Dee" + TUTORIALS_OFF + cheats(3, 600, 4) + "expect-window scripts/prelevel.lua 30000\nwait 2500\n")
Q.act("decor", move=DECOR, ms=900)
Q.act("preview", pointer=TAB1, ms=700)
Q.act("ask", move=(75, 552), ms=1500)
Q.act("yes", move=(310, 373), ms=500)
Q.text += "expect-window scripts/mainmenu.lua 30000\nwait 1500\ndump\n"
Q.keys.append("menu")
out, d, paths = run("quit", Q.text, set(), ())
D = dict(zip(Q.keys, d))
if len(d) != len(Q.keys):
    errors.append("quit: expected %d window dumps, got %d" % (len(Q.keys), len(d)))
else:
    expect("kbackground1(preview)" in bought(D["preview"]), "quit: the offer is not previewed before Quit: %s" % bought(D["preview"]))
    expect(top(D["ask"]) not in ("scripts/prelevel.lua", "scripts/mainmenu.lua"), "quit: the yes/no dialog is not up: %s" % top(D["ask"]))
    expect(top(D["menu"]) == "scripts/mainmenu.lua" and not shop(D["menu"]), "quit: we are not at the main menu without the shop: %s" % top(D["menu"]))
pf = progress_file("quit")
expect(pf.get("upgrade.iquitos.kbackground1") is None and pf.get("cash.iquitos") == "600", "quit: a preview was kept or cash was spent: %s %s" % (pf.get("upgrade.iquitos.kbackground1"), pf.get("cash.iquitos")))

if errors:
    print("\n".join(errors))
    sys.exit(1)
print("greatcc_prelevel: ok")
