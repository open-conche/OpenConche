"""Chocolatier 2 (the pre-installed assets.pfp) boots to its main menu on this engine, and every button opens its dialog.

Run 1 has no users: the menu renders (800x600 screenshot that is
not black, the welcome and copyright labels in the dump), Story Mode opens ui/entername.lua, Free Play (enabled through `lua`,
since the menu hides it without a user) opens ui/okdialog.lua, then Options, Help, High Scores and Save & Quit open
ui/options.lua, ui/help1.lua, ui/hiscore_local.lua and ui/yndialog.lua; each dialog is closed again and the modal depth is
checked on both sides (expect-window alone cannot tell, as it matches anything in the modal chain). Run 2 creates a user and
reopens the menu: Change Player opens ui/chooseplayer.lua and the welcome label names the user.

Asserts: exit 0, no [lua-error] / [engine-error] / [native-error], no placeholder for Window, Frame, MainMenu, Options or
HighScores, only allow-listed natives logged as unimplemented, the [game] line says choc2."""
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

# Natives that the menu path may still log as unimplemented. The audio ones wait for the audio slice, the map ones for the
# Map slice, StartMovie is Chocolatier 3's intro movie (an empty name, skipped).
ALLOWED_UNIMPLEMENTED = {"StartMovie", "SetMusicState", "SetAmbient", "ResetColorMap"}  # StopProductionAnim is real
REAL_TYPES = ("Window", "Frame", "MainMenu", "Options", "HighScores")

MENU_READY = "expect-window ui/mainmenu.lua 20000\nwait 1500\n"  # the menu's Update has run by then


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
    for bad in ("[lua-error]", "[engine-error]", "[native-error]", "FAIL"):
        if bad in out:
            errors.append("%s: output contains %s" % (tag, bad))
    if "[game] choc2 " not in out:
        errors.append("%s: no '[game] choc2' line (game detection)" % tag)
    for t in REAL_TYPES:
        if "[unimplemented-type] %s" % t in out:
            errors.append("%s: %s is still a placeholder type" % (tag, t))
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
    """True if the PNG is not (nearly) all black. The scanline filters are not undone: a black picture filters to all-zero
    bytes (bar one alpha byte and a filter byte per row), a drawn one leaves tens of thousands of non-zero bytes."""
    data = open(path, "rb").read()
    pos, idat = 8, b""
    while pos < len(data):
        n, typ = struct.unpack(">I4s", data[pos:pos + 8])
        if typ == b"IDAT":
            idat += data[pos + 8:pos + 8 + n]
        pos += 12 + n
    raw = zlib.decompress(idat)
    return sum(1 for b in raw if b) > len(raw) // 100


# ---- run 1: no users ----
shots = {k: os.path.join(a.workdir, k + ".png") for k in ("menu", "entername", "okdialog", "options", "help1", "hiscore", "quit")}
steps = [  # (button click, expected script path, close action, screenshot key)
    ("click 400 345", "ui/entername.lua", "click 490 343", "entername"),  # Story Mode / Cancel
    ("lua EnableWindow('free_mode', true)\nclick 230 455", "ui/okdialog.lua", "key Return", "okdialog"),  # Free Play
    ("click 343 475", "ui/options.lua", "key Return", "options"),
    ("click 457 475", "ui/help1.lua", "key Return", "help1"),
    ("click 570 455", "ui/hiscore_local.lua", "click 130 379", "hiscore"),  # High Scores / Back
    ("click 683 417", "ui/yndialog.lua", "click 490 279", "quit"),  # Save & Quit / No
]
script = MENU_READY + "dump\nscreenshot-logical %s\n" % shots["menu"]
for button, path, close, key in steps:
    script += "%s\nexpect-window %s 5000\nwait 600\nscreenshot-logical %s\ndump\n%s\nwait 600\ndump\n" % (button, path, shots[key], close)
script += "quit\n"
out = run("run1", script)

d = dumps(out)
if len(d) != 1 + 2 * len(steps):
    errors.append("run 1: expected %d window dumps, got %d" % (1 + 2 * len(steps), len(d)))
else:
    if d[0][0] != 2 or d[0][1] != ["ui/mainmenu.lua"]:
        errors.append("run 1: the menu is not the only modal at start: %r" % (d[0][:2],))
    for i, (button, path, close, key) in enumerate(steps):
        opened, closed = d[1 + 2 * i], d[2 + 2 * i]
        if opened[0] != 3 or opened[1][-1:] != [path]:
            errors.append("run 1: %s did not open: depth %d modals %r" % (path, opened[0], opened[1]))
        if closed[0] != 2:
            errors.append("run 1: %s was not closed (depth %d)" % (path, closed[0]))
menu = d[0][2] if d else out
for label in ("Copyright", "PlayFirst", 'Text "welcome"', "Story Mode", "Options", "Help", "High Scores", "Save & Quit",
              "Change Player", "Free Play Mode"):
    if label not in menu:
        errors.append("run 1: %r is not in the menu's window dump" % label)
for k, p in shots.items():
    try:
        if png_size(p) != (800, 600):
            errors.append("%s screenshot is %dx%d" % (k, *png_size(p)))
    except OSError as e:
        errors.append("no %s screenshot: %s" % (k, e))
try:
    if not png_not_black(shots["menu"]):
        errors.append("the menu screenshot is (nearly) all black")
    if open(shots["menu"], "rb").read() == open(shots["entername"], "rb").read():
        errors.append("the Story Mode screenshot equals the menu: no dialog was drawn")
except OSError:
    pass
for frame_piece in ("Frame ", "control/frame_corner"):
    if frame_piece not in out:
        errors.append("run 1: no Frame (%r) in the dialogs' dumps" % frame_piece)

# ---- run 2: with a user ----
shots["choose"] = os.path.join(a.workdir, "choose.png")
shots["menu2"] = os.path.join(a.workdir, "menu2.png")
out = run("run2a", MENU_READY + "lua CreateNewUser('Tester')\nwait 100\nquit\n")
if not os.path.exists(os.path.join(user, "users.txt")):
    errors.append("run 2: CreateNewUser wrote no users.txt")
out = run("run2", MENU_READY + "dump\nscreenshot-logical %s\nclick 117 417\nexpect-window ui/chooseplayer.lua 5000\nwait 600\n"
          "screenshot-logical %s\ndump\nquit\n" % (shots["menu2"], shots["choose"]))
d = dumps(out)
if len(d) != 2:
    errors.append("run 2: expected 2 window dumps, got %d" % len(d))
else:
    if "Tester" not in d[0][2]:
        errors.append("run 2: the welcome label does not name the user")
    if d[1][0] != 3 or d[1][1][-1:] != ["ui/chooseplayer.lua"]:
        errors.append("run 2: Change Player did not open ui/chooseplayer.lua: depth %d modals %r" % (d[1][0], d[1][1]))
    if "Tester" not in d[1][2]:
        errors.append("run 2: the player list does not show the user")

if errors:
    print("FAIL\n" + "\n".join(errors))
    sys.exit(1)
print("Chocolatier 2 main menu OK")
