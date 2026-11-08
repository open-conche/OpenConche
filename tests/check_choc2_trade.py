"""Chocolatier 2 trade slice: buying at the Ferry Market and selling at San Francisco Chocolates, end to end.

Docs: docs/re/choc2-logo-and-trade.md part B. One fresh Story game (the tutor dialogs closed as in check_choc2_story.py) reaches the
live San Francisco port. The tutorial leaves the player with $0 and rank 0 (no haggling), so the script sets what the later game would
have: `gSim:AdjustMoney(5000)`, `gSim.rank = 1`, `gSim.weeks = 1` (the keeper's once-a-week mood bump is keyed on the week) and one
HaggleBonus (a quest variable that forces the "down" roll, so the haggle result is not luck).

The port is built with `LQuest._Variables.bcsign = 6`, so sim/port.lua's Fireworks closure runs: StartFireworks adds the five
TPirework sprites at the layout slot between the planes and the ships (the [port] log line, five `sprite firework` lines in the
first dump at consecutive layers, their bursts and sounds live on the 40 ms task; the trade test's market dump happens seconds in).

Market: click Sugar; the quantity dialog (ui/buysell.lua) opens with the keypad. Two keypad clicks type 12 (the pad calls the receipt
callback itself); a keyboard digit then types 125 and Backspace removes it again: those two go through the DynamicWindow
`ontext` hook (the dump shows its run count, 1 after the digit), and the subtotal label follows both ("125 x $30 = $3,750"). Buy: money -360, sugar
+12, a "buy" sound, the keeper's mood 50 -> 60 (once per week). Haggle (rank 1): the first line; the result dialog; the market's
prices drop (and turn green); the keeper's mood -4; the "haggle_price_down" sound. Closing the market after the rank change shows
the Novice trophy (ui/medal_award.lua), which is closed. Shop: one chocolate is put in the stock, the platter is clicked, 3 are sold:
money + 3 x price, stock 7, a "sell" sound; a second HaggleBonus then raises the shop's prices.

Asserts: exit 0, no [lua-error] / [engine-error] / [native-error] / [native-warning], every `expect-*` line ok, only the allow-listed
natives unimplemented, the dump values above, the audio `fired` counts, and screenshots that are 800x600, not black and mutually
different. The pictures are meant to be looked at too."""
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

# StartMovie is Chocolatier 3's intro movie (an empty name, skipped); the music natives wait for their slice; bsgDate is the badge's.
# The Novice trophy dialog (shown because this test raises the rank by hand) has a Sprite for the cup's glint: a later slice.
ALLOWED_PLACEHOLDER_TYPES = {"Sprite"}
ALLOWED_UNIMPLEMENTED = {"StartMovie", "SetMusicState", "SetAmbient", "bsgDate"}
shots = {k: os.path.join(a.workdir, k + ".png") for k in (
    "buy_open", "buy_typed", "market_bought", "haggle_ask", "haggle_answer", "market_haggled", "medal", "shop", "sell_dialog",
    "shop_sold", "shop_haggled")}

script = """expect-window ui/mainmenu.lua 20000
wait 1500
click 400 345
expect-window ui/entername.lua 5000
wait 500
type Tester
key Return
lua LQuest._Variables.bcsign = 6
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
lua gSim:AdjustMoney(5000); gSim.rank = 1; gSim.weeks = 1; LQuest._Variables.HaggleBonus = 1; p0 = LItem:ByName('sugar').price; inv0 = LItem:ByName('sugar').inventory
wait 1500
expect-lua 100 gSim.money == 5000
click 575 194
expect-window ui/marketui.lua 5000
wait 800
dump
click 320 185
expect-window ui/buysell.lua 5000
wait 600
expect-label subtotal
screenshot-logical {buy_open}
click 461 190
wait 200
click 489 190
wait 300
expect-label subtotal 12 x $30 = $360
type 5
wait 300
expect-label subtotal 125 x $30 = $3,750
dump
screenshot-logical {buy_typed}
key BackSpace
wait 300
expect-label subtotal 12 x $30 = $360
click 420 286
wait 1500
expect-lua 100 LItem:ByName('sugar').inventory == inv0 + 12
expect-lua 100 gSim.money == 5000 - 12 * p0
screenshot-logical {market_bought}
dump
click 630 229
expect-window ui/haggle.lua 5000
wait 800
screenshot-logical {haggle_ask}
click 400 300
wait 2000
screenshot-logical {haggle_answer}
click 500 398
wait 1500
expect-lua 100 LItem:ByName('sugar').price < p0
screenshot-logical {market_haggled}
dump
click 660 268
expect-window ui/medal_award.lua 5000
wait 500
screenshot-logical {medal}
click 475 305
wait 1500
expect-window ui/portview.lua 5000
lua for _, i in pairs(LItem._ByName) do if i.recipe and not prod then prod = i end end; prod.inventory = 10; gSim:InventoryChanged(); LQuest._Variables.HaggleBonus = 1
wait 500
click 646 275
expect-window ui/shopui.lua 5000
wait 800
lua m0 = gSim.money; q0 = prod.price
screenshot-logical {shop}
dump
click 403 150
expect-window ui/buysell.lua 5000
wait 600
click 517 190
wait 300
screenshot-logical {sell_dialog}
click 420 286
wait 1500
expect-lua 100 prod.inventory == 7
expect-lua 100 gSim.money == m0 + 3 * q0
screenshot-logical {shop_sold}
dump
click 675 189
expect-window ui/haggle.lua 5000
wait 800
click 400 300
wait 2000
click 500 398
wait 1500
expect-lua 100 prod.price > q0
screenshot-logical {shop_haggled}
dump
quit
""".format(**shots)

path = os.path.join(a.workdir, "trade.script")
with open(path, "w") as f:
    f.write(script)
unimpl = os.path.join(a.workdir, "unimplemented.txt")
r = subprocess.run([a.openconche, "--headless", "--assets", a.assets, "--userdir", user, "--log", os.path.join(a.workdir, "engine.log"), "--script", path, "--log-unimplemented", unimpl],
                   cwd=a.workdir, capture_output=True, text=True, timeout=300)
out = r.stdout
open(os.path.join(a.workdir, "out.txt"), "w").write(out)
if r.returncode != 0:
    errors.append("exit %d, tail %r" % (r.returncode, out[-600:]))
for bad in ("[lua-error]", "[engine-error]", "[native-error]", "[native-warning]", "FAIL"):
    if bad in out:
        errors.append("output contains %s" % bad)
if "[game] choc2 " not in out:
    errors.append("no '[game] choc2' line")
for m in re.finditer(r"^\[unimplemented-type\] (\S+)", out, flags=re.M):
    if m.group(1) not in ALLOWED_PLACEHOLDER_TYPES:
        errors.append("placeholder window type %s" % m.group(1))
if os.path.exists(unimpl):
    for line in open(unimpl):
        name = line.split("\t")[0]
        if name not in ALLOWED_UNIMPLEMENTED:
            errors.append("unexpected unimplemented native %s" % name)
try:
    log = open(os.path.join(a.workdir, "engine.log"), errors="replace").read()
except OSError:
    log = ""
    errors.append("no engine.log")
for m in re.finditer(r"\[audio\] (missing file|rejected) sfx/(buy|sell|haggle_price_\w+)", log):
    errors.append("a trade sound did not resolve: %s" % m.group(0))
oks = len(re.findall(r"^\[script\] ok expect-", out, flags=re.M))
if oks < 23:
    errors.append("only %d expect-* lines passed" % oks)
if "[choc2] DynamicWindow:" in out:
    errors.append("a DynamicWindow key is still reported unsupported")

dumps = re.split(r"(?=^\[windows\] modal depth)", out, flags=re.M)
dumps = [d for d in dumps if d.startswith("[windows]")]
# market, buy typed, market bought, market haggled, shop, shop sold, shop haggled
if len(dumps) != 7:
    errors.append("expected 7 window dumps, got %d" % len(dumps))
else:
    market, typed, bought, haggled, shop, sold, shop_h = dumps
    if not re.search(r'Character ".*" id=\d+ rect=\S+ character=sf_marketkeep mood=50 ', market):
        errors.append("the market keeper does not start at mood 50")
    fw = re.findall(r"sprite firework layer=(\d+)", market)
    if len(fw) != 5:
        errors.append("the port should show five firework sprites, saw %d" % len(fw))
    elif sorted(int(x) for x in fw) != [int(fw[0]) + i for i in range(5)]:
        errors.append("the firework sprites are not at five consecutive layout slots: %r" % (fw,))
    m = re.search(r'DynamicWindow "" id=\d+ rect=0,0,1,1 .*ontext runs=(\d+)', typed)
    if not m or int(m.group(1)) != 1:
        errors.append("the ontext DynamicWindow did not run once for the keyboard digit (pad clicks do not post):%r" % (m.group(0) if m else None))
    if not re.search(r'Text "subtotal" .*label="125 x \$30 = \$3,750"', typed):
        errors.append("the typed dump has no 125 subtotal")
    if not re.search(r'character=sf_marketkeep mood=60 ', bought):
        errors.append("a purchase did not lift the keeper's mood to 60")
    if not re.search(r'character=sf_marketkeep mood=56 ', haggled):
        errors.append("haggling did not cost the keeper 4 mood (56)")
    if not re.search(r"""Text "sugar_price" .*label="<font color='4aa008'>\$27""", haggled):
        errors.append("the market's sugar price did not change after the haggle")
    if "ui/shopui.lua" not in shop:
        errors.append("the shop dump is not ui/shopui.lua")
if not re.search(r"^\[port\] StartFireworks: five fireworks between layout items ", out, flags=re.M):
    errors.append("the port's StartFireworks did not run its five fireworks")
audio = re.findall(r"^audio fired (.*)$", out, flags=re.M)
if not audio:
    errors.append("no audio dump")
else:
    fired = dict(kv.split("=") for kv in audio[-1].split())
    for name, n in (("sfx/buy.ogg", "1"), ("sfx/sell.ogg", "1"), ("sfx/haggle_price_down.ogg", "2")):
        if fired.get(name) != n:
            errors.append("audio fired %s=%s, wanted %s" % (name, fired.get(name), n))
    # A firework burst reached the sound: one of sfx/firework1..5.ogg (the six rolls have no shipped sixth file).
    if not any(int(fired.get("sfx/firework%d.ogg" % i, "0")) > 0 for i in range(1, 6)):
        errors.append("no firework burst was heard by the last audio dump")


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

if errors:
    print("FAIL\n" + "\n".join(errors))
    sys.exit(1)
print("Chocolatier 2 trade OK")
