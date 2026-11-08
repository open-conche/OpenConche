"""Chocolatier 2 Story Mode: a new user plays the intro, a click ends it, and the run reaches ui/portview.lua; a second run with the
same user folder lists the user in Change Player and resumes Story to ui/portview.lua.

Run 1 (empty user dir): Story Mode opens ui/entername.lua, the name "Tester" is
typed and accepted, the Lua creates the user and a Story game and calls DoIntro: the dump shows intro_button (no longer hidden) with
intro_text under it; the text does not move (the intro is a fade out of the menu art, a fade in of the text, a 22 s hold and a fade out;
a screenshot 12 s into the intro shows the held text; the clock is simulated, the run takes under a second). A click on the intro button runs MainMenuFinishIntro, which swaps to ui/portview.lua (screenshot and dump). Run 2 (same user
dir): the welcome label names Tester, Change Player lists Tester, OK closes it, and Story Mode (tutor00b is still incomplete) plays the
intro again and, after a click, reaches ui/portview.lua.

Run 3 (docs/design/choc2-port-and-ledger.md 4): Story Mode again; the tutor's chartalk shows the Character (the body and a
mood face, dump and screenshot) over a Port that has its buildings and moving path sprites. "Yes, Please!" and two OKs close the tutor
dialogs and return to the live port view (only "I Know How To Play" goes on to the logo editor, a later slice). The building names show
on arrival (screenshot 300 ms in); two screenshots seven seconds apart differ in the sky band (the path sprites move); hovering a
building opens a popup with its name (screenshot); and clicking it opens its dialog through the `command` closure (ui/marketui.lua;
screenshot).

Asserts: exit 0, no [lua-error] / [engine-error] / [native-error] / [native-warning], the [game] line says choc2, no placeholder for
Window, Frame, MainMenu, Options or HighScores, only allow-listed natives unimplemented, only the allow-listed window types
placeholders (Port, Ledger, DynamicWindow and Character wait for their slices), save.txt written for the user, two non-black 800x600
screenshots that differ."""
import argparse, os, re, shutil, struct, subprocess, sys, zlib

ap = argparse.ArgumentParser()
ap.add_argument("--openconche", required=True)
ap.add_argument("--assets", required=True)
ap.add_argument("--workdir", required=True)
a = ap.parse_args()
shutil.rmtree(a.workdir, ignore_errors=True)  # a fresh user dir: no users on the first run
os.makedirs(a.workdir)
user = os.path.join(a.workdir, "user")
errors = []

# Natives that the new-user Story path may still log as unimplemented. The audio ones wait for the audio slice, StartMovie is Chocolatier 3's intro movie (an empty name, skipped).
ALLOWED_UNIMPLEMENTED = {"StartMovie", "SetMusicState", "SetAmbient"}
# Window types that are still logged placeholders on this path (plain containers: the port view needs them to exist).
ALLOWED_PLACEHOLDER_TYPES = set()
REAL_TYPES = ("Window", "Frame", "MainMenu", "Options", "HighScores", "Port", "Character")


def run(tag, text):
    script = os.path.join(a.workdir, tag + ".script")
    with open(script, "w") as f:
        f.write(text)
    unimpl = os.path.join(a.workdir, tag + "-unimplemented.txt")
    r = subprocess.run([a.openconche, "--headless", "--assets", a.assets, "--userdir", user, "--script", script, "--log-unimplemented",
                        unimpl], cwd=a.workdir, capture_output=True, text=True, timeout=180)
    out = r.stdout
    if r.returncode != 0:
        errors.append("%s: exit %d, tail %r" % (tag, r.returncode, out[-600:]))
    for bad in ("[lua-error]", "[engine-error]", "[native-error]", "[native-warning]", "FAIL"):
        if bad in out:
            errors.append("%s: output contains %s" % (tag, bad))
    if "[game] choc2 " not in out:
        errors.append("%s: no '[game] choc2' line (game detection)" % tag)
    for m in re.finditer(r"^\[unimplemented-type\] (\S+)", out, flags=re.M):
        if m.group(1) in REAL_TYPES or m.group(1) not in ALLOWED_PLACEHOLDER_TYPES:
            errors.append("%s: unexpected placeholder window type %s" % (tag, m.group(1)))
    if os.path.exists(unimpl):
        for line in open(unimpl):
            name = line.split("\t")[0]
            if name not in ALLOWED_UNIMPLEMENTED:
                errors.append("%s: unexpected unimplemented native %s" % (tag, name))
    return out


def dumps(out):
    """Each window dump as (modal depth, [modal script paths], text)."""
    res = []
    for block in re.split(r"(?=^\[windows\] modal depth)", out, flags=re.M):
        m = re.match(r"\[windows\] modal depth (\d+)", block)
        if m:
            res.append((int(m.group(1)), re.findall(r'^  Modal "[^"]*" .*? script="([^"]*)"', block, flags=re.M), block))
    return res


def png_size(path):
    with open(path, "rb") as f:
        d = f.read(24)
    return struct.unpack(">II", d[16:24])


def png_not_black(path):
    """True if the PNG is not (nearly) all black (scanline filters are not undone: a black picture is all-zero bytes)."""
    data = open(path, "rb").read()
    pos, idat = 8, b""
    while pos < len(data):
        n, typ = struct.unpack(">I4s", data[pos:pos + 8])
        if typ == b"IDAT":
            idat += data[pos + 8:pos + 8 + n]
        pos += 12 + n
    raw = zlib.decompress(idat)
    return sum(1 for b in raw if b) > len(raw) // 100


def intro_line(block, name):
    m = re.search(r'^\s*%s "%s" .*$' % ("Button" if name == "intro_button" else "Text", name), block, flags=re.M)
    return m.group(0) if m else None


# The ledger badge is a real DynamicWindow now, and its date line calls bsgDate (two arguments; semantics unknown, so a stub).
ALLOWED_UNIMPLEMENTED |= {"bsgDate"}
MENU_READY ="expect-window ui/mainmenu.lua 20000\nwait 1500\n"  # the menu's Update has run by then
shots = {k: os.path.join(a.workdir, k + ".png") for k in ("intro", "port", "port2")}

# ---- run 1: a new user, Story Mode ----
script = (MENU_READY + "dump\nclick 400 345\nexpect-window ui/entername.lua 5000\nwait 500\ntype Tester\nkey Return\n"
          "wait 2000\ndump\nwait 10000\nscreenshot-logical %s\nclick 400 300\nexpect-window ui/portview.lua 5000\nwait 1500\n"
          "screenshot-logical %s\ndump\nquit\n" % (shots["intro"], shots["port"]))
out = run("run1", script)
d = dumps(out)
if len(d) != 3:
    errors.append("run 1: expected 3 window dumps, got %d" % len(d))
else:
    before, intro, port = d
    line = intro_line(before[2], "intro_button")
    if line is None or " hidden" not in line:
        errors.append("run 1: the intro button is not hidden before Story Mode: %r" % line)
    line = intro_line(intro[2], "intro_button")
    if line is None or " hidden" in line:
        errors.append("run 1: DoIntro did not show intro_button: %r" % line)
    text = intro_line(intro[2], "intro_text")
    if text is None or "budding Chocolatier" not in text:
        errors.append("run 1: intro_text is missing or has the wrong text: %r" % text)
    if intro[1] != ["ui/mainmenu.lua"]:
        errors.append("run 1: the intro is not on the main menu modal: %r" % (intro[1],))
    if "ui/portview.lua" not in port[1]:
        errors.append("run 1: the port view is not the base modal: %r" % (port[1],))
    if "Frame " not in port[2] or "ports/sanfrancisco" not in port[2]:
        errors.append("run 1: the port dump has no frame or no San Francisco image")
for k in ("intro", "port"):
    try:
        if png_size(shots[k]) != (800, 600):
            errors.append("%s screenshot is %dx%d" % (k, *png_size(shots[k])))
        elif not png_not_black(shots[k]):
            errors.append("the %s screenshot is (nearly) all black" % k)
    except OSError as e:
        errors.append("no %s screenshot: %s" % (k, e))
try:
    if open(shots["intro"], "rb").read() == open(shots["port"], "rb").read():
        errors.append("the intro and port screenshots are equal")
except OSError:
    pass
if not os.path.exists(os.path.join(user, "users.txt")) or "Tester" not in open(os.path.join(user, "users.txt")).read():
    errors.append("run 1: users.txt does not list Tester")
saves = [os.path.join(r, f) for r, _, fs in os.walk(os.path.join(user, "profiles")) for f in fs if f == "save.txt"]
if not saves or os.path.getsize(saves[0]) < 100:
    errors.append("run 1: no (or an empty) save.txt for the new user")

# ---- run 2: the same user dir ----
script = (MENU_READY + "dump\nclick 117 417\nexpect-window ui/chooseplayer.lua 5000\nwait 600\ndump\nclick 490 500\nwait 600\n"
          "click 400 345\nwait 2000\ndump\nclick 400 300\nexpect-window ui/portview.lua 5000\nwait 1500\n"
          "screenshot-logical %s\ndump\nquit\n" % shots["port2"])
out = run("run2", script)
d = dumps(out)
if len(d) != 4:
    errors.append("run 2: expected 4 window dumps, got %d" % len(d))
else:
    menu, choose, intro, port = d
    if "Welcome back, Tester" not in menu[2]:
        errors.append("run 2: the welcome label does not name the user")
    if choose[1][-1:] != ["ui/chooseplayer.lua"] or 'label="Tester"' not in choose[2]:
        errors.append("run 2: Change Player does not list Tester: %r" % (choose[1],))
    if choose[0] != 3 or intro[0] != 2:
        errors.append("run 2: Change Player was not opened and closed (depth %d then %d)" % (choose[0], intro[0]))
    line = intro_line(intro[2], "intro_button")
    if line is None or " hidden" in line:
        errors.append("run 2: Story Mode did not resume into the intro: %r" % line)
    if "ui/portview.lua" not in port[1]:
        errors.append("run 2: Story Mode did not reach ui/portview.lua: %r" % (port[1],))
try:
    if png_size(shots["port2"]) != (800, 600) or not png_not_black(shots["port2"]):
        errors.append("the second run's port screenshot is wrong")
except OSError as e:
    errors.append("no port2 screenshot: %s" % e)

# ---- run 3: the port scene, a character, a hover popup and a building click ----
def png_raw(path):
    """The decompressed scanlines (filter bytes included) and the bytes per row, enough to see that two pictures differ."""
    data = open(path, "rb").read()
    channels = {2: 3, 6: 4}.get(data[25], 4)
    w = struct.unpack(">I", data[16:20])[0]
    pos, idat = 8, b""
    while pos < len(data):
        n, typ = struct.unpack(">I4s", data[pos:pos + 8])
        if typ == b"IDAT":
            idat += data[pos + 8:pos + 8 + n]
        pos += 12 + n
    return zlib.decompress(idat), w * channels + 1


def sky_band(path, rows):
    raw, rowbytes = png_raw(path)
    return raw[:rows * rowbytes]


SKY = 120  # rows from the top: clouds and planes, above the ledger and any dialog
for k in ("talk", "labels", "scene1", "scene2", "hover", "click"):
    shots[k] = os.path.join(a.workdir, "scene_" + k + ".png")
script = (MENU_READY + "click 400 345\nwait 2000\nclick 400 300\nexpect-window ui/portview.lua 5000\nwait 1500\n"
          "screenshot-logical %s\ndump\n"
          "click 208 304\nwait 700\nclick 300 303\nwait 700\nclick 300 303\nwait 1500\nexpect-window ui/portview.lua 5000\nwait 300\nscreenshot-logical %s\nwait 1200\nscreenshot-logical %s\n"
          "wait 7000\nscreenshot-logical %s\n"
          "move 575 194\nwait 400\nscreenshot-logical %s\ndump\n"
          "click 575 194\nexpect-window ui/marketui.lua 5000\nwait 800\nscreenshot-logical %s\ndump\nquit\n"
          % (shots["talk"], shots["labels"], shots["scene1"], shots["scene2"], shots["hover"], shots["click"]))
out = run("run3", script)
d = dumps(out)
if len(d) != 3:
    errors.append("run 3: expected 3 window dumps, got %d" % len(d))
else:
    talk, hover, market = d
    if "ui/chartalk.lua" not in talk[1]:
        errors.append("run 3: the tutor's chartalk is not open: %r" % (talk[1],))
    if not re.search(r'^\s*Character ".*" id=\d+ rect=\S+ character=sf_tutor mood=50 face=2', talk[2], flags=re.M):
        errors.append("run 3: the dump has no Character for sf_tutor with a pleased face (mood 50 is pleased: kDefault, docs/re/choc2-port-binary.md)")
    port = re.search(r'^\s*Port ".*" id=\d+ rect=\S+ buildings=(\d+) paths=(\d+)', talk[2], flags=re.M)
    if not port or int(port.group(1)) < 4 or int(port.group(2)) < 4:
        errors.append("run 3: the Port has no buildings or no path sprites: %r" % (port.group(0) if port else None))
    for name in ("building:sf_market", "building:sf_bchq", "path:clouds_01"):
        if "sprite " + name not in talk[2]:
            errors.append("run 3: the sprite tree has no %s" % name)
    if "hover=sf_market" not in hover[2] or "PopupWindow" not in hover[2] or 'label="Ferry Market"' not in hover[2]:
        errors.append("run 3: hovering sf_market did not open a popup with its name")
    if "ui/marketui.lua" not in market[1]:
        errors.append("run 3: clicking sf_market did not open ui/marketui.lua: %r" % (market[1],))
for k in ("talk", "labels", "scene1", "scene2", "hover", "click"):
    try:
        if png_size(shots[k]) != (800, 600) or not png_not_black(shots[k]):
            errors.append("the %s screenshot is wrong" % k)
    except OSError as e:
        errors.append("no %s screenshot: %s" % (k, e))
try:
    if sky_band(shots["scene1"], SKY) == sky_band(shots["scene2"], SKY):
        errors.append("the sky band is the same seven seconds apart: the path sprites do not move")
except (OSError, zlib.error) as e:
    errors.append("cannot compare the scene screenshots: %s" % e)

if errors:
    print("FAIL\n" + "\n".join(errors))
    sys.exit(1)
print("Chocolatier 2 Story Mode OK")
