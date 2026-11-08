"""Vanilla v1.0 (assets.pfp) played headless end to end, modelled on check_endtoend.py.

--scenario factory   : new game, the three opening quest dialogs, the factory (Endrit's dialog, ui_factory.lua, the chocolate
                       minigame played by autoshoot, score 74), the inventory afterwards (74 cases, 26 sugar, 26 cacao).
--scenario endtoend  : the above, then sell all 74 cases at the Zurich shop (74 x $85 = $6,290), the station's $5,000 gift,
                       a sugar purchase at the market (+200), the map and the flight to Douala ("[travel] arrived"), the pause
                       menu's Menu -> Main Menu -> Save & Quit -> Yes (the save is written, the app exits by itself), then a
                       second run whose Start Game resumes on the map; the decoded save (Flat of loadstring(LoadGameString()))
                       must equal the one captured before the quit (ignoring lastSave).
Every run: exit 0, no [lua-error] / [engine-error] / Main died, all expect-window / expect-lua ok, only allow-listed stubs logged.
Shortcuts: none (every step is a real click); `lua` lines only read state."""
import argparse, os, re, shutil, subprocess, sys

ap = argparse.ArgumentParser()
ap.add_argument("--openconche", required=True)
ap.add_argument("--assets", required=True)
ap.add_argument("--workdir", required=True)
ap.add_argument("--scenario", choices=("factory", "endtoend"), required=True)
a = ap.parse_args()
shutil.rmtree(a.workdir, ignore_errors=True)
os.makedirs(a.workdir)
errors = []

# Natives not implemented yet; the run may hit nothing else (the factory's crate sparkle particles are logged by name;
# FireworksWindow is being implemented elsewhere; the factory's cursors are set for real now).
ALLOWED = {"SoundEvent", "ButtonSound", "StartMovie", "FireworksWindow"}
ALLOWED_PREFIX = ("SoundEvent.", "crate sparkle particles")

FLAT = ('lua function Flat(t, p) for k, v in pairs(t) do local key = p .. tostring(k) if type(v) == "table" then Flat(v, key .. ".") '
        'elseif type(v) == "string" then print("SAVESTATE", key .. "=" .. string.format("%q", v)) '
        'elseif type(v) ~= "function" then print("SAVESTATE", key .. "=" .. tostring(v)) end end end\n')

INTRO = """expect-window ui/mainmenu.lua 60000
wait 1300
click 400 345
expect-window ui/ui_entername.lua 5000
wait 600
type Tester
key Return
expect-window ui/portview.lua 5000
wait 3000
click 400 346
wait 1500
click 400 346
wait 1500
click 400 346
wait 1500
screenshot-logical @W@/01_port.png
click 428 270
expect-window ui/ui_quest_offer.lua 5000
wait 1200
click 400 346
wait 1500
expect-window ui/ui_factory.lua 8000
wait 500
screenshot-logical @W@/02_factory.png
expect-lua 1000 (Player.products.b01 or 0) == 0 and Player.ingredients.sugar == 100 and Player.ingredients.cacao == 100
click 555 336
expect-window ui/chocolate_factory.lua 5000
wait 500
click 327 398
wait 1500
wait 2800
click 100 590
wait 500
click 399 403
wait 300
autoshoot 62000
wait 20000
expect-dump 60000 state=5
screenshot-logical @W@/03_gameover.png
expect-lua 1000 GetScore() == 74
click 471 400
wait 2000
expect-lua 1000 Player.products.b01 == 74 and Player.ingredients.sugar == 26 and Player.ingredients.cacao == 26 and Player.money == 0
"""

FACTORY = INTRO + """click 400 346
wait 1500
click 400 346
wait 1500
screenshot-logical @W@/04_after.png
quit
"""

PLAY = INTRO + FLAT + """click 400 346
wait 1500
click 741 454
wait 1500
click 400 346
wait 1500
click 400 346
wait 1500
click 107 215
wait 2500
click 400 346
expect-window ui/ui_shop.lua 5000
wait 1500
lua M0 = Player.money
screenshot-logical @W@/05_shop.png
click 449 346
wait 2500
expect-lua 1000 Player.products.b01 == nil and Player.money == M0 + 6290
click 581 346
wait 2500
click 400 346
wait 2000
click 211 265
expect-window ui/ui_quest_offer.lua 5000
wait 2000
click 400 346
wait 1500
click 400 346
wait 2000
expect-lua 1000 Player.money == 11290
click 534 230
wait 2500
click 400 346
expect-window ui/ui_market.lua 5000
wait 1500
screenshot-logical @W@/06_market.png
click 490 203
expect-window ui/ui_buysell.lua 5000
wait 800
click 578 256
wait 300
click 578 256
wait 300
screenshot-logical @W@/07_register.png
lua M1 = Player.money; S1 = Player.ingredients.sugar
click 474 337
wait 2500
expect-lua 1000 Player.ingredients.sugar == S1 + 200 and Player.money < M1
click 581 346
wait 2500
click 400 346
wait 2000
lua T0 = Player.time; M2 = Player.money; C = Player.portsCost["douala"]
click 123 415
expect-window ui/mapview.lua 5000
wait 1500
click 404 155
wait 1500
expect-window ui/ui_generic.lua 5000
wait 500
click 400 401
wait 15000
expect-lua 1000 Player.portName == "douala" and not gTravelActive and Player.money == M2 - C and Player.time > T0
click 400 346
wait 2000
click 400 346
wait 2000
screenshot-logical @W@/08_douala.png
lua print("PRE", Player.money, Player.time, Player.subticks, Player.portName, Player.ingredients.sugar, Player.ingredients.cacao)
lua Flat(loadstring(Player:BuildSaveGameString())(), "")
click 63 553
expect-window ui/ui_pause.lua 5000
wait 1000
click 400 344
expect-window ui/mainmenu.lua 5000
wait 1500
click 683 417
wait 1500
click 313 396
wait 60000
"""

RESUME = """expect-window ui/mainmenu.lua 60000
wait 1300
click 400 345
expect-window ui/mapview.lua 15000
wait 1500
screenshot-logical @W@/09_resumed.png
lua print("POST", Player.money, Player.time, Player.subticks, Player.portName, Player.ingredients.sugar, Player.ingredients.cacao)
""" + FLAT + """lua Flat(loadstring(LoadGameString())(), "")
quit
"""


def run(name, text, expect_exit=None):
    script = os.path.join(a.workdir, name + ".script")
    with open(script, "w") as f:
        f.write(text.replace("@W@", a.workdir))
    unimpl = os.path.join(a.workdir, name + ".unimpl.txt")
    r = subprocess.run([a.openconche, "--headless", "--assets", a.assets, "--userdir", os.path.join(a.workdir, "user"), "--script",
                        script, "--log-unimplemented", unimpl], cwd=a.workdir, capture_output=True, text=True, timeout=600)
    out = r.stdout + r.stderr
    open(os.path.join(a.workdir, name + ".out.txt"), "w").write(out)
    if r.returncode != 0:
        errors.append("%s: exit %d, tail %r" % (name, r.returncode, out[-600:]))
    for bad in ("[lua-error]", "[engine-error]", "Main died", "FAIL"):
        if bad in out:
            errors.append("%s: output contains %s" % (name, bad))
    if expect_exit and expect_exit not in out:
        errors.append("%s: %r missing from the output" % (name, expect_exit))
    try:
        for line in open(unimpl):
            stub = line.split("\t")[0].strip()
            if stub and stub not in ALLOWED and not stub.startswith(ALLOWED_PREFIX):
                errors.append("%s: unexpected stub hit: %s" % (name, line.strip()))
    except OSError:
        errors.append("%s: no unimplemented log" % name)
    return out


def state(out):
    d = {}
    for line in out.splitlines():
        if line.startswith("SAVESTATE\t") and "=" in line:
            k, v = line[len("SAVESTATE\t"):].split("=", 1)
            if k != "lastSave":
                d[k] = v
    return d


if a.scenario == "factory":
    out = run("factory", FACTORY)
    if "ok expect-window ui/chocolate_factory.lua" not in out:
        errors.append("the chocolate minigame was never confirmed open")
else:
    out = run("play", PLAY, "[app] exit: kQuitNow")
    if "[travel] arrived" not in out:
        errors.append("no [travel] arrived")
    if "ok expect-window ui/ui_shop.lua" not in out:
        errors.append("the shop never opened")
    before = state(out)
    if len(before) < 100:
        errors.append("the pre-quit save state has only %d entries" % len(before))
    save = os.path.join(a.workdir, "user", "profiles", "Tester", "save.txt")
    try:
        text = open(save, encoding="utf-8", errors="replace").read()
        if not (text.startswith("return {") and text.endswith("}")):
            errors.append("save.txt is not a Lua table chunk (%r ... %r)" % (text[:20], text[-20:]))
    except OSError:
        errors.append("no %s" % save)
    pre = re.search(r"^PRE\t(.*)$", out, re.M)
    out2 = run("resume", RESUME, "[app] exit after")
    post = re.search(r"^POST\t(.*)$", out2, re.M)
    if not pre or not post or pre.group(1) != post.group(1):
        errors.append("resumed state %r differs from the saved %r" % (post and post.group(1), pre and pre.group(1)))
    after = state(out2)
    diff = sorted(k for k in set(before) | set(after) if before.get(k) != after.get(k))
    if not after or diff:
        errors.append("the reloaded save differs in %d entries: %s" % (
            len(diff), ", ".join("%s: %s -> %s" % (k, before.get(k), after.get(k)) for k in diff[:8])))
    else:
        print("reloaded save identical (%d entries, lastSave ignored); state %s" % (len(after), pre.group(1)))

if errors:
    print("FAIL:\n  " + "\n  ".join(errors))
    sys.exit(1)
print("ok: vanilla " + a.scenario)
