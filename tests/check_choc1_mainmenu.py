"""Chocolatier 1 (its own asset folder) boots through ui/mainloop.lua to its main menu on this engine.

Docs: docs/re/choc1-fit.md (slices 1 to 7). A fresh user dir, boot, wait for ui/mainmenu.lua as the top modal, dump, screenshot.
Asserts: exit 0, no [lua-error] / [engine-error] / [native-error], the [game] line says choc1, only allow-listed natives and
window types still unimplemented, the menu is the only modal and has its buttons, the screenshot is 800x600 and not black."""
import argparse, os, re, shutil, struct, subprocess, sys, zlib

ap = argparse.ArgumentParser()
ap.add_argument("--openconche", required=True)
ap.add_argument("--assets", required=True)
ap.add_argument("--workdir", required=True)
a = ap.parse_args()
shutil.rmtree(a.workdir, ignore_errors=True)
os.makedirs(a.workdir)
user = os.path.join(a.workdir, "user")
errors = []

# StartMovie is the SDK's intro movie (an empty name, skipped). No placeholder window type is left on this path (OptionsDialog
# and CreditsWindow are real).
ALLOWED_UNIMPLEMENTED = {"StartMovie"}
ALLOWED_TYPES = set()

# (button, x, y, the dialog's script path, the name its modal is popped by). Positions are the menu's own (mainmenu.lua).
BUTTONS = [("Change Player (no user yet)", 117, 395, "ui/okdialog.lua", "ui/okdialog.lua"),
           ("Free Play (no user yet)", 230, 445, "ui/okdialog.lua", "ui/okdialog.lua"),
           ("Options", 343, 478, "ui/options.lua", "options"),
           ("Help", 460, 478, "ui/help1.lua", "help_dlg"),
           ("High Scores", 568, 440, "ui/hiscore.lua", "hiscorescreen"),
           ("Quit", 682, 395, "ui/yndialog.lua", "ui/yndialog.lua"),
           ("Story (no user yet)", 400, 298, "ui/entername.lua", "ui/entername.lua")]

shot = os.path.join(a.workdir, "menu.png")
script = os.path.join(a.workdir, "menu.script")
with open(script, "w") as f:
    f.write("expect-window ui/mainmenu.lua 20000\nwait 1500\ndump\nscreenshot-logical %s\n" % shot)
    # Audio: the menu's StartMusic is playing; the sound names resolve (UI_click.ogg is found as ui_click.ogg, an alias
    # maps to its file) and SetAmbient{name, loop} loops audio/sfx/<loop>.ogg until SetAmbient({}) stops it.
    f.write('lua PlaySound("ui_click"); PlaySound("open"); SetAmbient({name = "port", loop = "port_loop"})\nwait 300\ndump\n')
    f.write("lua SetAmbient({})\nwait 300\ndump\n")
    # Every button opens its dialog (the top modal's script path), which is closed again; the Credits button of the
    # options dialog; then, once a user exists, Change Player opens the player chooser.
    # Every dump written below is paired with (modal depth, top modal script) so the dialogs are proven to open on top and to
    # close again (expect-window alone matches anything in the modal chain). Depth 2 is the Screen plus the menu.
    expected = [(2, "ui/mainmenu.lua")] * 3
    def click(n, x, y, path, popname, shot_name):
        f.write("click %d %d\nexpect-window %s 3000\nwait 400\ndump\nscreenshot-logical %s\n" % (x, y, path, os.path.join(a.workdir, shot_name)))
        expected.append((3, path))
        f.write("lua DoPopModal('%s')\nexpect-window ui/mainmenu.lua 3000\nwait 200\ndump\n" % popname)
        expected.append((2, "ui/mainmenu.lua"))
    for i, (label, x, y, path, popname) in enumerate(BUTTONS):
        if label.startswith("Options"):
            f.write("click %d %d\nexpect-window %s 3000\nwait 400\ndump\nscreenshot-logical %s\n" % (x, y, path, os.path.join(a.workdir, "options.png")))
            expected.append((3, path))
            # Drag the sound-effect knob (its rail is at screen x 238..558, y 243; the knob starts at 750) to a quarter of the
            # rail and click the mute box; the dump shows the live level and the mute box on; OK stores them (SaveVolumes) and
            # closes; the reopened dialog shows what was stored.
            f.write("down 471 243\nmove 400 243\nmove 318 243\nup 318 243\nclick 421 342\nwait 300\ndump\n")
            expected.append((3, "ui/options.lua"))
            f.write("click 287 367\nexpect-window ui/mainmenu.lua 3000\nwait 400\ndump\n")
            expected.append((2, "ui/mainmenu.lua"))
            f.write("click %d %d\nexpect-window ui/options.lua 3000\nwait 400\ndump\n" % (x, y))
            expected.append((3, "ui/options.lua"))
            f.write("click 505 366\nexpect-window ui/credits.lua 3000\nwait 400\ndump\nscreenshot-logical %s\n" % os.path.join(a.workdir, "credits.png"))
            expected.append((4, "ui/credits.lua"))
            f.write("wait 5000\nscreenshot-logical %s\n" % os.path.join(a.workdir, "credits2.png"))
            f.write("lua DoPopModal('credits')\nexpect-window ui/options.lua 3000\nwait 200\ndump\n")
            expected.append((3, "ui/options.lua"))
            f.write("lua DoPopModal('options')\nexpect-window ui/mainmenu.lua 3000\nwait 200\ndump\n")
            expected.append((2, "ui/mainmenu.lua"))
        else:
            click(i, x, y, path, popname, "button%d.png" % i)
    f.write("lua CreateNewUser('Ann'); SetLabel('welcome', GetString('title_welcome', GetCurrentUserName()))\nwait 300\ndump\n")
    expected.append((2, "ui/mainmenu.lua"))
    click(99, 117, 395, "ui/chooseplayer.lua", "chooseplayer", "changeplayer.png")
    # The high-score screen with a real score behind it (LogScore's HiscoreStore rules) and the C1 text of THiscore:
    # the score column through `formatScore` ("$N/week"), the gamemode label `empire_mode`, enabled local rows.
    f.write("lua LogScore(2000, '1-000000001-0000000002000', '')\nwait 200\n")
    f.write("click 568 440\nexpect-window ui/hiscore.lua 3000\nwait 400\ndump\nscreenshot-logical %s\n" % os.path.join(a.workdir, "hiscore.png"))
    expected.append((3, "ui/hiscore.lua"))
    f.write("lua DoPopModal('hiscorescreen')\nexpect-window ui/mainmenu.lua 3000\nwait 200\ndump\n")
    expected.append((2, "ui/mainmenu.lua"))
    f.write("quit\n")
unimpl = os.path.join(a.workdir, "unimplemented.txt")
log = os.path.join(a.workdir, "log.txt")
r = subprocess.run([a.openconche, "--headless", "--assets", a.assets, "--userdir", user, "--script", script, "--log-unimplemented", unimpl,
                    "--dump-windows", "--log", log],
                   cwd=a.workdir, capture_output=True, text=True, timeout=180)
out = r.stdout
if r.returncode != 0:
    errors.append("exit %d, tail %r" % (r.returncode, out[-600:]))
for bad in ("[lua-error]", "[engine-error]", "[native-error]", "FAIL"):
    if bad in out:
        errors.append("output contains %s" % bad)
if "[game] choc1 " not in out:
    errors.append("no '[game] choc1' line (game detection)")
for t in set(re.findall(r"\[unimplemented-type\] (\w+)", out)) - ALLOWED_TYPES:
    errors.append("unexpected placeholder window type %s" % t)
if os.path.exists(unimpl):
    for line in open(unimpl):
        name = line.split("\t")[0].strip()
        if name and name not in ALLOWED_UNIMPLEMENTED:
            errors.append("unexpected unimplemented native %s" % name)
else:
    errors.append("no --log-unimplemented file")

audio = [l for l in out.splitlines() if l.startswith("audio ")]
music = [l for l in audio if l.startswith("audio music ")]
env = [l for l in audio if l.startswith("audio env ")]
if len(music) < 3 or not all(l.startswith("audio music audio/music/menu.ogg") for l in music):
    errors.append("the menu music is not audio/music/menu.ogg in every dump: %r" % music)
if len(env) < 3 or env[0] != "audio env none" or env[1] != "audio env audio/sfx/port_loop.ogg" or env[2] != "audio env none":
    errors.append("SetAmbient did not loop and stop port_loop: %r" % env)
logtext = open(log).read() if os.path.exists(log) else ""
for want in ("[audio] music file audio/music/menu.ogg once", "[audio] file audio/sfx/ui_click.ogg",
             "[audio] file audio/sfx/window_open.ogg", "[audio] ambient audio/sfx/port_loop.ogg", "[audio] ambient stop"):
    if want not in logtext:
        errors.append("log lacks %r" % want)
for bad in ("[audio] rejected", "[audio] missing file"):
    if bad in logtext:
        errors.append("log has %r" % bad)

# ChoosePlayer is a real window. With one user (Ann) the chooser shows her slot, selected (radio on, edit and delete
# enabled), and the "new player" entry under it (radio off); the slots after that stay disabled (hidden).
chooser = next((c for c in re.split(r"(?=\[windows\] modal depth )", out) if 'script="ui/chooseplayer.lua"' in c), "")
if "ChoosePlayer " not in chooser:
    errors.append("the chooser dump has no ChoosePlayer window")


def chooser_line(kind, name):
    m = re.search(r'^\s*%s "%s" .*$' % (kind, name), chooser, flags=re.M)
    return m.group(0) if m else None


for name, want_hidden in (("player0", False), ("player1", False), ("player2", True), ("edit0", False), ("delete0", False), ("edit1", True),
                          ("delete1", True), ("label2", True)):
    line = chooser_line("Button" if not name.startswith("label") else "Text", name)
    if line is None:
        errors.append("chooser: no %s in the dump" % name)
    elif (" hidden" in line) != want_hidden:
        errors.append("chooser: %s should be %s: %r" % (name, "hidden" if want_hidden else "shown", line))
line = chooser_line("Button", "player0")
if line and " on " not in line + " ":
    errors.append("chooser: Ann's radio is not on: %r" % line)
line = chooser_line("Button", "player1")
if line and " off" not in line:
    errors.append("chooser: the new-player radio is not off: %r" % line)
for name, text in (("label0", "Ann"), ("label1", "add another player")):
    line = chooser_line("Text", name)
    if line is None or 'label="%s"' % text not in line:
        errors.append("chooser: %s should say %r: %r" % (name, text, line))

# Each dump: its modal depth and the script of the top modal (dumps list the modals bottom-up, so the last script= wins).
# --dump-windows adds one more dump at exit, which is not in the expected list.
dumps = []
for chunk in re.split(r"(?=\[windows\] modal depth )", out)[1:]:
    depth = int(re.match(r"\[windows\] modal depth (\d+)", chunk).group(1))
    scripts = re.findall(r'script="([^"]+)"', chunk)
    dumps.append((depth, scripts[-1] if scripts else None))
if dumps[:len(expected)] != expected:
    errors.append("dialog open/close sequence differs:\n  got      %r\n  expected %r" % (dumps, expected))
# The OptionsDialog (TOptions) is a real window. First dump: the three sliders at the 750 default and both toggles off;
# after the drag the effects slider is a quarter of the rail and the mute box is on; the reopened dialog (after OK) shows them kept.
option_dumps = [c for c in re.split(r"(?=\[windows\] modal depth )", out) if 'script="ui/options.lua"' in c and 'script="ui/credits.lua"' not in c]


def slider_value(chunk, name):
    m = re.search(r'Slider "%s" id=\d+ rect=\S+ value=([0-9.]+)' % name, chunk)
    return float(m.group(1)) if m else None


def toggle_on(chunk, name):
    m = re.search(r'Button "%s" id=\d+ rect=\S+ label="[^"]*" type=toggle state=\d+ layer=\d+ (on|off)' % name, chunk)
    return m.group(1) == "on" if m else None


if len(option_dumps) < 3:
    errors.append("expected three options dumps, got %d" % len(option_dumps))
else:
    first, dragged, reopened = option_dumps[0], option_dumps[1], option_dumps[2]
    for n in ("sfxslider", "musicslider", "ambientslider"):
        if slider_value(first, n) != 0.75:
            errors.append("options: %s should start at 0.750: %r" % (n, slider_value(first, n)))
    if toggle_on(first, "mute") is not False or toggle_on(first, "fullscreen") is not False:
        errors.append("options: the mute and fullscreen boxes should start off")
    # The checkbox buttons keep their explicit w=120/h=26 (TButton::Init fixes the size from the style before the
    # image fit), and their Text labels at x=26 with w=kMax fill the rest of the button and ARE laid out. The
    # "layout into a 0x26 box ignored" line of a w=0 label was this check's reason.
    for name, x, text in (("fullscreen", 45, "Full Screen"), ("mute", 175, "Mute Audio")):
        if not re.search(r'Button "%s" id=\d+ rect=%d,110,120,26' % (name, x), first):
            errors.append("options: the %s checkbox button is not 120x26: %r" %
                          (name, re.findall(r'Button "%s" id=\S+' % name, first)))
        m = re.search(r'Text "" id=\d+ rect=26,0,94,26 label="%s" size=20 flags=4 lines=1' % text, first)
        if not m:
            errors.append("options: the %s checkbox label is not laid out 94x26 with lines=1: %r" %
                          (name, re.findall(r'Text "" id=\S+ label="%s"[^\n]*' % text, first)))
    if "layout into a 0x26 box ignored" in out:
        errors.append("options: a label was laid out into a zero-width box")
    v = slider_value(dragged, "sfxslider")
    if v is None or not 0.2 <= v <= 0.3:
        errors.append("options: dragging the sfx knob to a quarter of the rail gave %r" % v)
    if slider_value(dragged, "musicslider") != 0.75:
        errors.append("options: the music slider moved with the sfx one")
    if toggle_on(dragged, "mute") is not True:
        errors.append("options: the mute box did not turn on")
    if not re.search(r"audio levels sfx=0\.2\d* ", dragged):
        errors.append("options: the sfx level was not applied live: %r" % re.findall(r"audio levels .*", dragged))
    if slider_value(reopened, "sfxslider") != v:
        errors.append("options: OK did not store the level (%r after reopening, %r before)" % (slider_value(reopened, "sfxslider"), v))
    if toggle_on(reopened, "mute") is not True:
        errors.append("options: the reopened dialog does not show mute on")
    if slider_value(reopened, "ambientslider") != 0.75:
        errors.append("options: the ambient slider changed")
credits_dump = next((c for c in re.split(r"(?=\[windows\] modal depth )", out) if 'script="ui/credits.lua"' in c), "")
m = re.search(r'CreditsWindow "" id=\d+ rect=(\S+) time=20000 intropause=3000 scrollheight=(\d+) lines=(\d+)', credits_dump)
if not m or int(m.group(2)) < 500 or int(m.group(3)) < 50:
    errors.append("credits: the CreditsWindow is not laid out with the file's text: %r" % (m.group(0) if m else None))
if "[credits] cannot open" in out:
    errors.append("credits: the file was not found")
if 'script="ui/mainmenu.lua"' not in out:
    errors.append("ui/mainmenu.lua is not in the window dump")
# THiscore's own dressing of the shared screen: one row for Ann at 2000, score through formatScore.
scored = [c for c in re.split(r"(?=\[windows\] modal depth )", out) if 'script="ui/hiscore.lua"' in c and 'label="Ann"' in c]
if len(scored) < 1:
    errors.append("hiscore: no dump with the Ann row")
else:
    h = scored[-1]
    if not re.search(r'Text "score1" id=\d+ rect=\S+ label="\$2,000/week"', h):
        errors.append("hiscore: the score column does not read formatScore ($2,000/week): %r" % re.findall(r'Text "score1"[^\n]*', h))
    if not re.search(r'Text "name1" id=\d+ rect=\S+ label="Ann"', h):
        errors.append("hiscore: the row's name is not Ann: %r" % re.findall(r'Text "name1"[^\n]*', h))
    if not re.search(r'Text "gamemode" id=\d+ rect=\S+ label="Story Mode"', h):
        errors.append("hiscore: the gamemode label is not empire_mode (Story Mode): %r" % re.findall(r'Text "gamemode"[^\n]*', h))
    if not re.search(r'HiscoreWindow "" id=\d+ rect=0,0,800,600 state=0 rows=1', h):
        errors.append("hiscore: the window is not the shared local screen with one row: %r" % re.findall(r'HiscoreWindow [^\n]*', h))
    if re.search(r'Text "name2" id=\d+ [^\n]*label="[^"]+"', h):
        errors.append("hiscore: rows beyond the score are not empty")
if not re.search(r'Bitmap "loading" id=\d+ rect=\S+ hidden', out):
    errors.append('the "loading" overlay is still shown after the menu built (UtilDisableWindow)')
if "Welcome to Chocolatier, Ann." not in out:
    errors.append("the welcome text did not follow the new user")
for label, x, y, path, popname in BUTTONS + [("Change Player", 0, 0, "ui/chooseplayer.lua", "")]:
    if 'script="%s"' % path not in out:
        errors.append("%s: no window dump shows %s" % (label, path))
for label in ("Story Mode", "Free Play Mode", "Options", "Help", "High Scores", "Change Player", "Quit",
              "Copyright", "PlayFirst"):  # the publisher's notice line, by its parts
    if label not in out:
        errors.append("%r is not in the menu's window dump" % label)


def png_not_black(path):
    data = open(path, "rb").read()
    pos, idat = 8, b""
    while pos < len(data):
        n, typ = struct.unpack(">I4s", data[pos:pos + 8])
        if typ == b"IDAT":
            idat += data[pos + 8:pos + 8 + n]
        pos += 12 + n
    raw = zlib.decompress(idat)
    return sum(1 for b in raw if b) > len(raw) // 100


def png_pixels(path):
    """(width, height, rows of RGB(A) byte strings) of an 8-bit RGB/RGBA PNG, unfiltered."""
    data = open(path, "rb").read()
    w, h, depth, ctype = struct.unpack(">IIBB", data[16:26])
    assert depth == 8 and ctype in (2, 6), "unsupported PNG"
    bpp = 3 if ctype == 2 else 4
    pos, idat = 8, b""
    while pos < len(data):
        n, typ = struct.unpack(">I4s", data[pos:pos + 8])
        if typ == b"IDAT":
            idat += data[pos + 8:pos + 8 + n]
        pos += 12 + n
    raw = zlib.decompress(idat)
    rows, prev, stride = [], bytearray(w * bpp), w * bpp
    for y in range(h):
        f, line = raw[y * (stride + 1)], bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if f == 1: line[i] = (line[i] + a) & 255
            elif f == 2: line[i] = (line[i] + b) & 255
            elif f == 3: line[i] = (line[i] + (a + b) // 2) & 255
            elif f == 4:
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(bytes(line))
        prev = line
    return w, h, rows, bpp


def strip_differs(path, x0, y0, x1, y1, ref_xy):
    """Share of the pixels in [x0,x1) x [y0,y1) that differ clearly from the pixel at ref_xy (the frame's fill colour)."""
    w, h, rows, bpp = png_pixels(path)
    ref = rows[ref_xy[1]][ref_xy[0] * bpp:ref_xy[0] * bpp + 3]
    n = diff = 0
    for y in range(y0, y1):
        for x in range(x0, x1):
            px = rows[y][x * bpp:x * bpp + 3]
            n += 1
            diff += sum(abs(px[i] - ref[i]) for i in range(3)) > 60
    return diff / n


# The high-score screen (button4.png) is one Frame over the whole screen. The quit/ok dialogs use a Bitmap border, so
# they do not prove the Frame; here the screen's top and left edge strips (gold border art, 10 px) must differ from the maroon
# fill sampled in the middle, and so must the inner Frame's bottom edge (rect 22,86 to 778,385 in the screenshot).
try:
    mid = (400, 200)
    # Rows/columns 5..9 lie inside the fill inset, so with no frame art they would be the fill colour: the share must be high.
    for what, box in (("top edge", (100, 5, 700, 10)), ("left edge", (5, 100, 10, 500)), ("inner frame bottom edge", (60, 378, 740, 384))):
        share = strip_differs(os.path.join(a.workdir, "button4.png"), *box, mid)
        if share < 0.8:
            errors.append("high scores: the Frame %s is not drawn (only %.0f%% of the strip differs from the fill)" % (what, share * 100))
except (OSError, AssertionError) as e:
    errors.append("frame pixel check: %s" % e)

# The credits roll: the text is dark brown on the pale shelf art, and 5 s later (the intro pause is 3 s) it has scrolled.
try:
    c1, c2 = os.path.join(a.workdir, "credits.png"), os.path.join(a.workdir, "credits2.png")
    w1, h1, rows1, bpp1 = png_pixels(c1)
    w2, h2, rows2, bpp2 = png_pixels(c2)

    def dark(rows, bpp):
        return sum(1 for y in range(30, 520) for x in range(60, 740) if max(rows[y][x * bpp:x * bpp + 3]) < 120)

    d1, d2 = dark(rows1, bpp1), dark(rows2, bpp2)
    if d1 < 300:
        errors.append("credits: the first screenshot has no text (%d dark pixels)" % d1)
    changed = sum(1 for y in range(30, 520, 2) for x in range(60, 740, 2) if rows1[y][x * bpp1:x * bpp1 + 3] != rows2[y][x * bpp2:x * bpp2 + 3])
    if changed < 2000:
        errors.append("credits: the text did not scroll in 5 s (%d sampled pixels changed)" % changed)
except (OSError, AssertionError) as e:
    errors.append("credits screenshots: %s" % e)

try:
    d = open(shot, "rb").read(24)
    if struct.unpack(">II", d[16:24]) != (800, 600):
        errors.append("the screenshot is not 800x600")
    if not png_not_black(shot):
        errors.append("the menu screenshot is (nearly) all black")
except OSError as e:
    errors.append("no screenshot: %s" % e)

if errors:
    print("FAIL choc1_mainmenu:\n  " + "\n  ".join(errors))
    sys.exit(1)
print("choc1_mainmenu ok")
