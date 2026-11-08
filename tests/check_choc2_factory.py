"""Chocolatier 2 factory mini-game (docs/design/choc2-minigames.md 2.1 and 6), end to end on the real Chocolatier 2 scripts.

A fresh Story game reaches the San Francisco port (the tutor dialogs closed as in check_choc2_trade.py). A `lua` line gives 1000 of
the starter bar's two ingredients and turns the first-miss warning on (`gSim.warnOnMiss`); the factory building opens the tutor's
dialog (OK) and then ui/factory_status.lua; Make Chocolates opens ui/factory_minigame.lua (the native FactoryGame).

  1. Intro (screenshot): idle, the Lua's info dialog over the sprites, the counter 0 and the timer 1:00.
  2. Go: the countdown digit 2 shows (screenshot). At the start of play, one deliberate shot along the bottom edge of the screen is
     a miss: MinigameWarn paused the game and the dialog reads factory_waste (screenshot, the first miss marker is in the stack
     panel's first cell). OK resumes.
  3. Escape pauses with factory_paused and the dialog stays open (screenshot); Resume. The Pause button does the same through
     the PauseMinigame native; Resume.
  4. `autoshoot` plays the rest (screenshot mid-run); the game over fills the end panel: `performance` is GetProduction() and the text is
     the <port>_production_* key the cut-offs 0/11/19/55 with the product's handicap give (screenshot).
  5. Done: the factory's rate equals the production and the first-miss warning is off. The ledger's Factories tab shows FactoryStatus
     with the rate (screenshot).
  6. The factory again; Try Again from the end panel restarts the countdown with the counter at 0 (screenshot).
  7. The same seed gives the same production on a second run.

Asserts: exit 0, no [lua-error] / [engine-error] / [native-error] / [native-warning], every expect-* line ok, only the allow-listed natives
unimplemented, the dump values above, the audio `fired` counts, and screenshots that are 800x600, not black and mutually different.
The pictures are meant to be looked at too."""
import argparse, os, re, shutil, struct, subprocess, sys, zlib

ap = argparse.ArgumentParser()
ap.add_argument("--openconche", required=True)
ap.add_argument("--assets", required=True)
ap.add_argument("--workdir", required=True)
ap.add_argument("--once", action="store_true", help="skip the second run that compares the production")
a = ap.parse_args()
shutil.rmtree(a.workdir, ignore_errors=True)
os.makedirs(a.workdir)
errors = []

# StartMovie is Chocolatier 3's intro movie (an empty name, skipped); bsgDate is the badge's. The gun_aimer and plain
# cursors the factory asks for are set for real through the app's cursor service, so nothing cursor-related is allowed.
ALLOWED_UNIMPLEMENTED = {"StartMovie", "bsgDate"}
ALLOWED_PLACEHOLDER_TYPES = {"Sprite"}
SHOTS = ("quest", "intro", "countdown", "waste", "paused", "midrun", "gameover", "ledger", "tryagain")
BASE = "@W@/%s.png"

# Screen positions (800x600): the info dialog is a Frame at (20,100); its buttons are at (89,190) Go and Done, (152,230) Resume, Try Again
# and Cancel, so their centres are (194,310) and (226,350). The Pause TextButton is at (565+82, 50) with a 85x50 size.
GO = "click 194 310"
RESUME = "click 226 350"
PAUSE_BUTTON = "click 689 75"

PREFIX = """expect-window ui/mainmenu.lua 20000
wait 1500
click 400 345
expect-window ui/entername.lua 5000
wait 500
type Tester
key Return
wait 2000
click 400 300
expect-window ui/portview.lua 5000
wait 1500
click 208 304
wait 700
click 300 303
wait 700
click 300 303
wait 1500
expect-window ui/portview.lua 5000
wait 300
lua gSim:AdjustMoney(5000); gSim.rank = 1; gSim.weeks = 1; gSim.warnOnMiss = true; for _, n in ipairs({'sugar', 'cacao'}) do LItem:ByName(n).inventory = 1000 end; gSim:InventoryChanged(); gFac = LBuilding:ByName('sf_factory')
wait 500
expect-lua 100 gSim.warnOnMiss == true and gFac.rate == 0
click 190 300
wait 2000
click 300 303
expect-window ui/factory_status.lua 5000
wait 800
dump
click 402 200
expect-window ui/factory_minigame.lua 5000
wait 800
screenshot-logical %s
dump
""" % (BASE % "intro")

ROUND1 = """%(go)s
wait 600
screenshot-logical %(countdown)s
dump
wait 2300
wait 2300
click 790 590
expect-dump 3000 paused=1
wait 400
screenshot-logical %(waste)s
dump
%(resume)s
wait 500
key Escape
wait 300
screenshot-logical %(paused)s
dump
%(resume)s
wait 500
%(pause)s
wait 300
dump
%(resume)s
wait 300
autoshoot 62000
wait 20000
screenshot-logical %(midrun)s
dump
expect-dump 60000 state=5
wait 500
screenshot-logical %(gameover)s
dump
lua gProd = GetProduction()
%(go)s
wait 1500
expect-lua 1000 gFac.rate == gProd and gProd >= 1 and gSim.warnOnMiss == false
dump
expect-window ui/chartalk.lua 5000
wait 800
screenshot-logical %(quest)s
click 300 303
expect-window ui/medal_award.lua 5000
wait 500
click 475 305
wait 1500
expect-window ui/portview.lua 5000
click 470 410
wait 800
screenshot-logical %(ledger)s
dump
"""

ROUND2 = """click 190 300
wait 2500
dump
expect-window ui/factory_status.lua 5000
click 402 200
expect-window ui/factory_minigame.lua 5000
wait 500
%(go)s
wait 3000
autoshoot 62000
expect-dump 70000 state=5
wait 500
dump
click 226 350
wait 700
screenshot-logical %(tryagain)s
dump
quit
"""


def make_script(work):
    shots = {k: os.path.join(work, k + ".png") for k in SHOTS}
    body = PREFIX.replace("@W@", work) + ROUND1 % dict(go=GO, resume=RESUME, pause=PAUSE_BUTTON, **shots) + ROUND2 % dict(go=GO, **shots)
    return body, shots


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


POPUP = """lua gFac.product.colorcount = 2
click 402 200
expect-window ui/factory_minigame.lua 5000
wait 800
%(go)s
wait 2800
autoshoot 50000
expect-dump 40000 popups=1
screenshot-logical %(popup)s
wait 4000
quit
"""


def make_popup_script(work):
    # b_01 has no colours; with colorcount 2 a colour match shows the "Nx" popup over its tray (1.5 s).
    shots = {"popup": os.path.join(work, "popup.png")}
    head = PREFIX.replace("@W@", work).split("click 402 200")[0]
    return head + POPUP % dict(go=GO, **shots), shots


def run(tag, make=None):
    work = os.path.join(a.workdir, tag)
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    script, shots = (make or make_script)(work)
    path = os.path.join(work, "factory.script")
    open(path, "w").write(script)
    unimpl = os.path.join(work, "unimplemented.txt")
    r = subprocess.run([a.openconche, "--headless", "--assets", a.assets, "--userdir", os.path.join(work, "user"), "--log", os.path.join(work, "engine.log"),
                        "--script", path, "--log-unimplemented", unimpl], cwd=work, capture_output=True, text=True, timeout=900)
    out = r.stdout
    open(os.path.join(work, "out.txt"), "w").write(out)
    if r.returncode != 0:
        errors.append("%s: exit %d, tail %r" % (tag, r.returncode, out[-600:]))
    for bad in ("[lua-error]", "[engine-error]", "[native-error]", "[native-warning]", "FAIL"):
        if bad in out:
            errors.append("%s: output contains %s" % (tag, bad))
    for m in re.finditer(r"^\[unimplemented-type\] (\S+)", out, flags=re.M):
        if m.group(1) not in ALLOWED_PLACEHOLDER_TYPES:
            errors.append("%s: placeholder window type %s" % (tag, m.group(1)))
    if os.path.exists(unimpl):
        for line in open(unimpl):
            name = line.split("\t")[0]
            if name not in ALLOWED_UNIMPLEMENTED:
                errors.append("%s: unexpected unimplemented native %s" % (tag, name))
    log = open(os.path.join(work, "engine.log"), errors="replace").read() if os.path.exists(os.path.join(work, "engine.log")) else ""
    if "[sprite] cannot load" in log:
        errors.append("%s: a picture is missing: %s" % (tag, re.findall(r"\[sprite\] cannot load '[^']*'", log)[:5]))
    factory_sound = r"sfx/(countdown_tick|minigame_|factory_|template_|ingredient_|color_match|colormatch|powerup|warning_bell)"
    if re.search(r"\[audio\] (missing file|rejected) " + factory_sound, log):
        errors.append("%s: a factory sound did not resolve: %s" % (tag, re.findall(r"\[audio\] (?:missing file|rejected) " + factory_sound + r"[^\n]*", log)[:5]))
    m = re.search(r"^\[choc2\] FactoryGame:.*$", out, re.M)
    if m:
        errors.append("%s: a FactoryGame warning: %s" % (tag, m.group(0)[:120]))
    oks = len(re.findall(r"^\[script\] ok expect-", out, flags=re.M))
    return work, out, shots, oks


def label(dump, name):
    m = re.search(r'Text "%s" id=\d+ rect=\S+(?: hidden)? label="([^"]*)"' % re.escape(name), dump)
    return m.group(1) if m else None


def game_line(dump):
    for l in dump.splitlines():
        if l.strip().startswith('FactoryGame ""'):
            return l
    return None


def field(line, key):
    m = re.search(r"\b%s=(\S+)" % key, line or "")
    return m.group(1) if m else None


def checks(tag, work, out, shots, oks):
    dumps = [d for d in re.split(r"(?=^\[windows\] modal depth)", out, flags=re.M) if d.startswith("[windows]")]
    # status, intro, countdown, waste, paused, button pause, midrun, gameover, after Done, ledger, round 2 status, round 2 end, try again
    if len(dumps) != 13:
        errors.append("%s: expected 13 window dumps, got %d" % (tag, len(dumps)))
        return None
    status, intro, count, waste, paused, button, mid, over, done, ledger, status2, over2, again = dumps
    production = None
    g = game_line(intro)
    if not g or field(g, "started") != "0" or field(g, "paused") != "1" or field(g, "state") != "1":
        errors.append("%s: the intro is not idle and paused: %r" % (tag, g))
    if label(intro, "counter") != "0" or label(intro, "timer") != "1:00" or label(intro, "countdown") != "":
        errors.append("%s: the intro texts: %r %r %r" % (tag, label(intro, "counter"), label(intro, "timer"), label(intro, "countdown")))
    if label(status, "rate") != "0":
        errors.append("%s: the factory does not start at rate 0" % tag)
    g = game_line(count)
    if not g or field(g, "started") != "1" or field(g, "paused") != "0" or field(g, "state") != "1":
        errors.append("%s: the countdown is not running: %r" % (tag, g))
    if label(count, "countdown") != "2":  # 2400 ms: blank for 400, then 2, then 1
        errors.append("%s: the countdown digit %r" % (tag, label(count, "countdown")))
    g = game_line(waste)
    if not g or field(g, "misses") != "1" or field(g, "paused") != "1":
        errors.append("%s: the deliberate miss did not pause with one miss: %r" % (tag, g))
    if "Wasted ingredient" not in (label(waste, "infotext") or ""):
        errors.append("%s: the miss dialog is not factory_waste: %r" % (tag, (label(waste, "infotext") or "")[:60]))
    if "Frame \"info_dialog\"" not in waste:
        errors.append("%s: no info_dialog in the waste dump" % tag)
    g = game_line(paused)
    if not g or field(g, "paused") != "1":
        errors.append("%s: Escape did not pause: %r" % (tag, g))
    if "I've paused the machines" not in (label(paused, "infotext") or ""):
        errors.append("%s: the Escape pause dialog is not factory_paused: %r" % (tag, (label(paused, "infotext") or "")[:60]))
    if 'Modal "minigame"' not in paused:
        errors.append("%s: Escape closed the minigame dialog" % tag)
    g = game_line(button)
    if not g or field(g, "paused") != "1" or "I've paused the machines" not in (label(button, "infotext") or ""):
        errors.append("%s: the Pause button did not pause through PauseMinigame" % tag)
    g = game_line(mid)
    if not g or field(g, "state") != "2" or field(g, "paused") != "0":
        errors.append("%s: the mid-run dump is not playing: %r" % (tag, g))
    g = game_line(over)
    if not g or field(g, "state") != "5" or int(field(g, "misses") or 0) < 1 or int(field(g, "misses") or 0) >= 8:
        errors.append("%s: the end dump: %r" % (tag, g))
    else:
        production = int(field(g, "score"))
        if production < 1:
            errors.append("%s: no production" % tag)
        if label(over, "performance") != str(production):
            errors.append("%s: the performance label %r is not GetProduction() %d" % (tag, label(over, "performance"), production))
        perf = 100.0 * production / 120 * 0.9  # b_01: ideal 120, handicap 0.9
        want = "stopped" if perf < 0 else "That wasn't too bad" if perf < 11 else "We're all set up" if perf < 19 else "fancy chocolate-making" if perf < 55 else "haven't seen chocolate-making"
        text = label(over, "infotext_alt") or ""
        if want not in text or ("%d cases" % production) not in text:
            errors.append("%s: the end text for %d cases (perf %.1f) is not '%s ... %d cases': %r" % (tag, production, perf, want, production, text[:90]))
        if "hidden" in re.search(r'Window "end_panel"[^\n]*', over).group(0) if re.search(r'Window "end_panel"[^\n]*', over) else True:
            errors.append("%s: the end panel is hidden" % tag)
    if production is not None and ("rate" not in (label(done, "rate") or "rate")):
        pass
    fs = re.search(r'FactoryStatus "" id=\d+ rect=\S+ bars=(\d+) products=(\d+)', ledger)
    if not fs or int(fs.group(2)) < 1 or int(fs.group(1)) < 2:
        errors.append("%s: the ledger has no FactoryStatus with bars and a product: %r" % (tag, fs and fs.group(0)))
    g = game_line(again)
    if not g or field(g, "started") != "1" or field(g, "state") not in ("1", "2"):
        errors.append("%s: Try Again did not start a new game: %r" % (tag, g))
    elif field(g, "score") != "0":
        errors.append("%s: Try Again did not reset the count: %r" % (tag, g))
    if label(again, "counter") != "0":
        errors.append("%s: the counter after Try Again is %r" % (tag, label(again, "counter")))
    if oks < 7:
        errors.append("%s: only %d expect-* lines passed" % (tag, oks))
    audio = re.findall(r"^audio fired (.*)$", out, flags=re.M)
    if not audio:
        errors.append("%s: no audio dump" % tag)
    else:
        fired = dict(kv.split("=") for kv in audio[-1].split())
        for name in ("sfx/countdown_tick.ogg", "sfx/minigame_starting_bell.ogg", "sfx/minigame_load_launch.ogg", "sfx/ingredient_failure_02.ogg",
                     "sfx/factory_snatch.ogg", "sfx/template_success.ogg"):
            if int(fired.get(name, "0")) < 1:
                errors.append("%s: audio %s never fired (%s)" % (tag, name, sorted(k for k in fired if "sfx/" in k)))
    return production


work, out, shots, oks = run("run1")
production = checks("run1", work, out, shots, oks)
blobs = {}
for k, p in shots.items():
    try:
        d = open(p, "rb").read()
        if struct.unpack(">II", d[16:24]) != (800, 600) or not png_not_black(p):
            errors.append("the %s screenshot is wrong" % k)
        blobs[k] = d
    except OSError as e:
        errors.append("no %s screenshot: %s" % (k, e))
if len(set(blobs.values())) != len(blobs):
    errors.append("two of the screenshots are equal")
if not a.once and production is not None:
    work2, out2, shots2, oks2 = run("run2")
    p2 = checks("run2", work2, out2, shots2, oks2)
    if p2 != production:
        errors.append("the same seed gave productions %r and %r" % (production, p2))

wp, outp, shotsp, oksp = run("popup", make_popup_script)
try:
    if not png_not_black(shotsp["popup"]):
        errors.append("the popup screenshot is black")
except OSError as e:
    errors.append("no popup screenshot: %s" % e)
if oksp < 3:
    errors.append("popup run: only %d expect-* lines passed" % oksp)

if errors:
    print("FAIL\n" + "\n".join(errors))
    sys.exit(1)
print("Chocolatier 2 factory OK, production %s" % production)
