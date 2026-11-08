"""Chocolatier 2's Options, High Scores and Credits windows, played headless against the pre-installed assets.pfp.

Docs: docs/re/choc1-natives.md section 9.

  options    A user "Tester" exists. Options opens with the sliders at the stored defaults (0.75 each: sound, ambient,
             music; the audio dump agrees). Dragging the sound slider and the ambient slider changes the live levels in the
             audio dump at once, before anything is stored. Mute is clicked (the audio dump says mute=1, the levels stay) and
             OK stores the volumes and closes. Reopening in the same run shows the knobs where they were left and the Mute
             box ticked; a second process on the same user dir does too, and its audio dump has the stored levels.
  fullscreen The Full Screen box runs ToggleFullScreen: the box ends up ticked, or, where the display refuses, the
             "opt_nofullscreen" dialog opens (ui/okdialog.lua).
  scores     Four users each log a score with bsgLogScore (as Simulator:LogScore does: score, "rank-weeks-money", medal
             flags). High Scores opens ui/hiscore_local.lua with the four rows best first (the tie shares a rank), unused rows
             hidden, the mode text "Story Mode"; the next-mode button shows "Free Play Mode" with an empty list and back.
  credits    Options -> Credits opens ui/credits.lua with the Credits window; two screenshots 6 s apart differ in the text
             area (the credits are pages that fade, 4.8 s each: the second is the next page), and OK closes it.

Asserts: exit 0, no [lua-error] / [engine-error] / [native-error], no [unimplemented] native on these paths."""
import argparse, os, re, shutil, struct, subprocess, sys, zlib

ap = argparse.ArgumentParser()
ap.add_argument("--openconche", required=True)
ap.add_argument("--assets", required=True)
ap.add_argument("--workdir", required=True)
a = ap.parse_args()
a.openconche, a.assets, a.workdir = (os.path.abspath(p) for p in (a.openconche, a.assets, a.workdir))
shutil.rmtree(a.workdir, ignore_errors=True)
os.makedirs(a.workdir)
errors = []

MENU_READY = "expect-window ui/mainmenu.lua 20000\nwait 1500\n"
# Main menu buttons (centres) and the Options dialog's controls in logical screen coordinates: the frame sits at (200,178) and
# the Lua's children at x=5.. (sliders, 390 wide, knob 40 wide), y=35/70/105, the check boxes at (140,130/160), OK at (225,378).
OPTIONS_BTN, SCORES_BTN = (343, 475), (570, 455)
SFX_Y, AMB_Y, MUS_Y = 226, 262, 296
MUTE, FULLSCREEN, OK = (356, 355), (356, 325), (310, 398)


def run(tag, text, user, keep=False):
    if not keep:
        shutil.rmtree(user, ignore_errors=True)
    script = os.path.join(a.workdir, tag + ".script")
    with open(script, "w") as f:
        f.write(text)
    unimpl = os.path.join(a.workdir, tag + "-unimplemented.txt")
    r = subprocess.run([a.openconche, "--headless", "--assets", a.assets, "--userdir", user, "--script", script, "--log-unimplemented",
                        unimpl], cwd=a.workdir, capture_output=True, text=True, timeout=240)
    out = r.stdout
    if r.returncode != 0:
        errors.append("%s: exit %d, tail %r" % (tag, r.returncode, out[-600:]))
    for bad in ("[lua-error]", "[engine-error]", "[native-error]", "FAIL"):
        if bad in out:
            errors.append("%s: output contains %s: %s" % (tag, bad, [l for l in out.splitlines() if bad in l][:3]))
    if os.path.exists(unimpl):
        for line in open(unimpl):
            name = line.split("\t")[0]
            if name not in ("StartMovie", "SetMusicState", "SetAmbient", "ResetColorMap"):  # StopProductionAnim is real
                errors.append("%s: unexpected unimplemented native %s" % (tag, name))
    return out


def split_dumps(out):
    return [b for b in re.split(r"(?=^\[windows\] modal depth)", out, flags=re.M) if b.startswith("[windows]")]


def depth(block):
    return int(re.match(r"\[windows\] modal depth (\d+)", block).group(1))


def slider(block, name):
    m = re.search(r'Slider "%s" .*? value=([0-9.]+)' % name, block)
    return float(m.group(1)) if m else None


def toggle_on(block, name):
    m = re.search(r'Button "%s" .*? type=toggle .*? (on|off) command' % name, block)
    return m.group(1) == "on" if m else None


def audio(block):
    m = re.search(r"audio levels sfx=([0-9.]+) music=([0-9.]+) env=([0-9.]+) mute=(\d)", block)
    return (float(m.group(1)), float(m.group(2)), float(m.group(3)), int(m.group(4))) if m else None


def near(x, y, tol=0.02):
    return x is not None and abs(x - y) <= tol


def drag(x0, x1, y):
    return "move %d %d\ndown %d %d\nmove %d %d\nmove %d %d\nup %d %d\nwait 200\n" % (x0, y, x0, y, (x0 + x1) // 2, y, x1, y, x1, y)


def read_png(path):
    data = open(path, "rb").read()
    pos, idat = 8, b""
    while pos < len(data):
        n, typ = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        pos += 12 + n
        if typ == b"IHDR":
            w, h, depth_, ctype = struct.unpack(">IIBB", body[:10])
            bpp = 3 if ctype == 2 else 4
        elif typ == b"IDAT":
            idat += body
    raw = zlib.decompress(idat)
    stride = w * bpp
    rows, prev = [], bytearray(stride)
    for y in range(h):
        f = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            x = line[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if f == 1: line[i] = (line[i] + x) & 255
            elif f == 2: line[i] = (line[i] + b) & 255
            elif f == 3: line[i] = (line[i] + (x + b) // 2) & 255
            elif f == 4:
                p = x + b - c
                pa, pb, pc = abs(p - x), abs(p - b), abs(p - c)
                line[i] = (line[i] + (x if pa <= pb and pa <= pc else (b if pb <= pc else c))) & 255
        rows.append(line)
        prev = line
    return w, h, bpp, rows


def dark_pixels(path, x0, y0, x1, y1):
    """The number of dark (text) pixels in a rectangle, and a hash of the rectangle."""
    w, h, bpp, rows = read_png(path)
    n, acc = 0, 0
    for y in range(y0, y1):
        for x in range(x0, x1):
            px = rows[y][x * bpp:x * bpp + 3]
            if px[0] < 90 and px[1] < 90 and px[2] < 90:
                n += 1
        acc = zlib.crc32(bytes(rows[y][x0 * bpp:x1 * bpp]), acc)
    return n, acc


def check_options():
    user = os.path.join(a.workdir, "user_options")
    shot = lambda n: os.path.join(a.workdir, n + ".png")
    text = (MENU_READY + "lua CreateNewUser('Tester')\nclick %d %d\nexpect-window ui/options.lua 5000\nwait 500\ndump\n" % OPTIONS_BTN +
            "screenshot-logical %s\n" % shot("options_open") +
            drag(487, 312, SFX_Y) + drag(487, 225, AMB_Y) + "dump\n" +                       # sound .75 -> ~.27, ambient .75 -> ~.05
            "click %d %d\nwait 300\ndump\nscreenshot-logical %s\n" % (MUTE + (shot("options_moved"),)) +  # mute on
            "click %d %d\nwait 600\ndump\n" % OK +                                         # OK: SaveVolumes, close
            "click %d %d\nexpect-window ui/options.lua 5000\nwait 500\ndump\nscreenshot-logical %s\nquit\n" % (OPTIONS_BTN + (shot("options_reopen"),)))
    out = run("options", text, user)
    d = split_dumps(out)
    if len(d) != 5:
        errors.append("options: expected 5 dumps, got %d" % len(d))
        return
    opened, dragged, muted, closed, reopened = d
    if not (near(slider(opened, "sfxslider"), 0.75) and near(slider(opened, "ambientslider"), 0.75) and near(slider(opened, "musicslider"), 0.75)):
        errors.append("options: the sliders did not take the default levels: %r" % [slider(opened, n) for n in ("sfxslider", "ambientslider", "musicslider")])
    if audio(opened) != (0.75, 0.75, 0.75, 0):
        errors.append("options: audio at open is %r" % (audio(opened),))
    if toggle_on(opened, "mute") or toggle_on(opened, "fullscreen"):
        errors.append("options: a check box starts ticked")
    sfx, amb = slider(dragged, "sfxslider"), slider(dragged, "ambientslider")
    au = audio(dragged)
    if not (sfx is not None and 0.15 < sfx < 0.4 and amb is not None and amb < 0.15):
        errors.append("options: the drags did not move the knobs: sfx %r ambient %r" % (sfx, amb))
    elif au is None or not (near(au[0], sfx) and near(au[2], amb) and near(au[1], 0.75) and au[3] == 0):
        errors.append("options: the live levels do not follow the sliders: sliders %r %r, audio %r" % (sfx, amb, au))
    am = audio(muted)
    if not (toggle_on(muted, "mute") and am and am[3] == 1 and near(am[0], sfx) and near(am[2], amb)):
        errors.append("options: Mute did not mute and keep the live levels: %r %r" % (toggle_on(muted, "mute"), am))
    if depth(closed) != 2:
        errors.append("options: OK did not close the dialog (depth %d)" % depth(closed))
    if not (near(slider(reopened, "sfxslider"), sfx) and near(slider(reopened, "ambientslider"), amb) and near(slider(reopened, "musicslider"), 0.75)):
        errors.append("options: the knobs did not persist across reopening: %r" % [slider(reopened, n) for n in ("sfxslider", "ambientslider", "musicslider")])
    if not toggle_on(reopened, "mute"):
        errors.append("options: the Mute box is not ticked after reopening")
    # A second process on the same user dir: the stored levels and mute are applied at boot and shown on open.
    text2 = MENU_READY + "dump\nclick %d %d\nexpect-window ui/options.lua 5000\nwait 500\ndump\nquit\n" % OPTIONS_BTN
    out2 = run("options2", text2, user, keep=True)
    d2 = split_dumps(out2)
    if len(d2) != 2:
        errors.append("options2: expected 2 dumps, got %d" % len(d2))
        return
    boot, again = d2
    ab = audio(boot)
    if not (ab and near(ab[0], sfx) and near(ab[2], amb) and ab[3] == 1):
        errors.append("options2: the stored levels were not applied at boot: %r (sliders %r %r)" % (ab, sfx, amb))
    if not (near(slider(again, "sfxslider"), sfx) and near(slider(again, "ambientslider"), amb) and toggle_on(again, "mute")):
        errors.append("options2: a new process lost the Options state")


def check_fullscreen():
    user = os.path.join(a.workdir, "user_fullscreen")
    text = (MENU_READY + "lua CreateNewUser('Tester')\nclick %d %d\nexpect-window ui/options.lua 5000\nwait 500\n" % OPTIONS_BTN +
            "click %d %d\nwait 800\ndump\nquit\n" % FULLSCREEN)
    d = split_dumps(run("fullscreen", text, user))
    if len(d) != 1:
        errors.append("fullscreen: expected 1 dump, got %d" % len(d))
        return
    refused = "ui/okdialog.lua" in d[0]
    if not refused and not toggle_on(d[0], "fullscreen"):
        errors.append("fullscreen: neither the box ticked nor the refusal dialog opened")


def check_scores():
    user = os.path.join(a.workdir, "user_scores")
    shot = lambda n: os.path.join(a.workdir, n + ".png")
    logs = ""
    for name, score in (("Anna", 1500), ("Bruno", 2400), ("Chloe", 900), ("Dmitri", 900)):
        logs += "lua CreateNewUser('%s'); bsgLogScore(%d, '2-000000052-000078000', 3)\n" % (name, score)
    text = (MENU_READY + logs + "click %d %d\nexpect-window ui/hiscore_local.lua 5000\nwait 500\ndump\nscreenshot-logical %s\n" % (SCORES_BTN + (shot("scores"),)) +
            "click 555 125\nwait 400\ndump\nscreenshot-logical %s\nclick 245 125\nwait 400\ndump\nquit\n" % shot("scores_free"))
    d = split_dumps(run("scores", text, user))
    if len(d) != 3:
        errors.append("scores: expected 3 dumps, got %d" % len(d))
        return

    def label(block, name):
        m = re.search(r'Text "%s" id=\d+ rect=\S+( hidden)? label="([^"]*)"' % re.escape(name), block)
        return None if not m else (None if m.group(1) else m.group(2))

    story = [label(d[0], "name%d" % i) for i in range(1, 11)]
    if story[:4] != ["Bruno", "Anna", "Chloe", "Dmitri"] or any(s is not None for s in story[4:]):
        errors.append("scores: the rows are %r" % story)
    if [label(d[0], "score%d" % i) for i in (1, 2)] != ["$2,400/week", "$1,500/week"]:
        errors.append("scores: the score column is %r" % [label(d[0], "score%d" % i) for i in (1, 2)])
    if [label(d[0], str(i)) for i in range(1, 5)] != ["1.", "2.", "3.", "3."]:
        errors.append("scores: the rank column is %r" % [label(d[0], str(i)) for i in range(1, 5)])
    if label(d[0], "mode") != "Story Mode":
        errors.append("scores: mode text %r" % label(d[0], "mode"))
    if label(d[1], "mode") != "Free Play Mode" or label(d[1], "name1") is not None:
        errors.append("scores: the Free Play view is %r / %r" % (label(d[1], "mode"), label(d[1], "name1")))
    if label(d[2], "mode") != "Story Mode" or label(d[2], "name1") != "Bruno":
        errors.append("scores: previous mode did not return to the Story list")
    if "submit" in d[0].lower().replace("highscores", ""):
        errors.append("scores: the offline screen shows a submit control")


def check_credits():
    user = os.path.join(a.workdir, "user_credits")
    shot = lambda n: os.path.join(a.workdir, n + ".png")
    text = (MENU_READY + "click %d %d\nexpect-window ui/options.lua 5000\nwait 300\nclick 490 398\nexpect-window ui/credits.lua 5000\nwait 600\n" % OPTIONS_BTN +
            "dump\nscreenshot-logical %s\nwait 6000\nscreenshot-logical %s\nclick 400 545\nwait 600\ndump\nquit\n" % (shot("credits_a"), shot("credits_b")))
    out = run("credits", text, user)
    d = split_dumps(out)
    if len(d) != 2:
        errors.append("credits: expected 2 dumps, got %d" % len(d))
        return
    if depth(d[0]) != 4 or '"ui/credits.lua"' not in d[0] or not re.search(r'Credits "" id=\d+ rect=0,0,550,\d+ pages=\d+', d[0]):
        errors.append("credits: the Credits window is not open: %s" % [l for l in d[0].splitlines() if "Credits" in l or "Modal" in l])
    if depth(d[1]) != 3:
        errors.append("credits: OK did not close the credits (depth %d)" % depth(d[1]))
    # The text area: inside the frame (x 150..650, y 40..500 on screen).
    na, ha = dark_pixels(shot("credits_a"), 150, 40, 650, 500)
    nb, hb = dark_pixels(shot("credits_b"), 150, 40, 650, 500)
    if na < 500 or nb < 500:
        errors.append("credits: little text on screen (%d and %d dark pixels)" % (na, nb))
    if ha == hb:
        errors.append("credits: the credits page did not change between the two screenshots (6 s apart)")


check_options()
check_fullscreen()
check_scores()
check_credits()

if errors:
    print("FAIL: Chocolatier 2 options/scores/credits:")
    for e in errors:
        print("  " + e)
    sys.exit(1)
print("choc2 options, scores and credits ok")
