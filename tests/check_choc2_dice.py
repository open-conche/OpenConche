"""Chocolatier 2 dice game (docs/design/choc2-minigames.md 2.5 and 6): the real Lua entry, a bet, two rolls, the result.

A fresh Story game (the tutor dialogs closed as in check_choc2_trade.py) reaches the live San Francisco port. The script gives it what
the entry gate wants, `gSim:AdjustMoney(100000); gSim.rank = 4` (money > 5000 and rank >= 4 means no waiting between gambles), then does
what a den character does: `PlayDice()` returns the character action, which is called with a Character (`ny_gambler01`). The Lua opens
ui/chartalk.lua ("gamble_dice_yn"); Yes opens ui/gamble_dice.lua. With $100,000 the ante is $1,000 and each arrow raises the bet by
$1,900 (the Lua's own limits), so two raises make the bet $4,800.

Roll 1: "Roll the Dice" pays the ante at once (money -1000), `RollFirst()` spins dice 0 and 1 (screenshot at 1 s: two faces cycle, the
others stay `?`), and the Lua's `Pause(50)` loop ends when `gDiceRollCount` reaches 2 (3534 ms of the modal clock; the dump shows
elapsed=3534). The two stopped dice show their faces, the player's label shows their total. Raise twice (the arrows enable). Roll 2:
`RollSecond()` spins the other six; one click on "Stop a Die" stops the next die on the next tick (the dump of the roll in progress shows
three faces); `gDiceRollCount` reaches 8; the Lua pays and says who won. Asserts: the dumped faces add up to the dump's totals and to the
Lua's own `PlayerTotal()`/`CharacterTotal()`, the money delta is +4800, -4800 or -1000 (tie) by those totals (checked in Lua and again
here from the dump), the result sound (dice_win, dice_lose, none for a tie) and the roll sound twice. Seeded: the same seed twice gives the
same faces and money, another seed other faces. No [lua-error], and the unimplemented natives are allow-listed.

The tie path (money -ante, no sound) is not exercised: no seed here ties, and its money is the Lua's.

The pictures are meant to be looked at too: they are 800x600, not black and mutually different."""
import argparse, os, re, shutil, struct, subprocess, sys, zlib

ap = argparse.ArgumentParser()
ap.add_argument("--openconche", required=True)
ap.add_argument("--assets", required=True)
ap.add_argument("--workdir", required=True)
a = ap.parse_args()
shutil.rmtree(a.workdir, ignore_errors=True)
os.makedirs(a.workdir)
errors = []

# StartMovie is Chocolatier 3's intro movie (an empty name, skipped); bsgDate is the badge's.
ALLOWED_UNIMPLEMENTED = {"StartMovie", "bsgDate"}
SEEDS = (7, 7, 12)  # the first two must agree
SHOTS = ("open", "spin", "first", "raised", "second", "result")
BET = 4800  # $1,000 + 2 x $1,900 at $100,000
ANTE = 1000

SCRIPT = """expect-window ui/mainmenu.lua 20000
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
lua gSim:AdjustMoney(100000); gSim.rank = 4; m0 = gSim.money
wait 500
lua gAct = PlayDice(); gR = gAct(LCharacter:ByName('ny_gambler01'))
expect-window ny_gambler01 5000
wait 800
click 209 305
expect-window ui/gamble_dice.lua 5000
wait 800
screenshot-logical {open}
dump
click 304 420
wait 1000
screenshot-logical {spin}
dump
expect-lua 5000 gDiceRollCount == 2
wait 300
expect-lua 100 gSim.money == m0 - {ante}
screenshot-logical {first}
dump
click 688 337
wait 200
click 688 337
wait 300
screenshot-logical {raised}
click 304 420
wait 1500
click 304 420
wait 100
dump
screenshot-logical {second}
expect-lua 8000 gDiceRollCount == 8
wait 500
expect-lua 100 gSim.money - m0 == ((PlayerTotal() > CharacterTotal()) and {bet} or ((PlayerTotal() < CharacterTotal()) and -{bet} or -{ante}))
lua print('DELTA', gSim.money - m0, PlayerTotal(), CharacterTotal())
screenshot-logical {result}
dump
click 599 420
wait 1000
expect-window ui/portview.lua 5000
dump
quit
"""


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


def run(tag, seed):
    d = os.path.join(a.workdir, tag)
    os.makedirs(d)
    shots = {k: os.path.join(d, k + ".png") for k in SHOTS}
    path = os.path.join(d, "dice.script")
    open(path, "w").write(SCRIPT.format(bet=BET, ante=ANTE, **shots))
    unimpl = os.path.join(d, "unimplemented.txt")
    r = subprocess.run([a.openconche, "--headless", "--assets", a.assets, "--userdir", os.path.join(d, "user"), "--seed", str(seed), "--log",
                        os.path.join(d, "engine.log"), "--script", path, "--log-unimplemented", unimpl],
                       cwd=d, capture_output=True, text=True, timeout=300)
    out = r.stdout
    open(os.path.join(d, "out.txt"), "w").write(out)
    if r.returncode != 0:
        errors.append("%s: exit %d, tail %r" % (tag, r.returncode, out[-600:]))
    for bad in ("[lua-error]", "[engine-error]", "[native-error]", "[native-warning]", "FAIL"):
        if bad in out:
            errors.append("%s: output contains %s" % (tag, bad))
    for m in re.finditer(r"^\[unimplemented-type\] (\S+)", out, flags=re.M):
        errors.append("%s: placeholder window type %s" % (tag, m.group(1)))
    if os.path.exists(unimpl):
        for line in open(unimpl):
            if line.split("\t")[0] not in ALLOWED_UNIMPLEMENTED:
                errors.append("%s: unexpected unimplemented native %s" % (tag, line.split("\t")[0]))
    if len(re.findall(r"^\[script\] ok expect-", out, flags=re.M)) < 11:
        errors.append("%s: too few expect-* lines passed" % tag)
    dumps = [x for x in re.split(r"(?=^\[windows\] modal depth)", out, flags=re.M) if x.startswith("[windows]")]
    if len(dumps) != 6:
        errors.append("%s: expected 6 window dumps, got %d" % (tag, len(dumps)))
        return None
    info = []
    for dump in dumps[:5]:
        m = re.search(r'DiceGame "" id=\d+ rect=\S+ dice=([-\d,]+) rolls=(\d+) rolling=(\d) elapsed=(\d+) player=(\d+) character=(\d+)', dump)
        info.append(m and dict(faces=[None if f == "-" else int(f) for f in m.group(1).split(",")], rolls=int(m.group(2)),
                               rolling=int(m.group(3)), elapsed=int(m.group(4)), player=int(m.group(5)), character=int(m.group(6))))
    # dumps: 0 open, 1 spin, 2 first roll done, 3 mid second roll, 4 result, 5 port again
    open_, spin, first, second, result = info[0], info[1], info[2], info[3], info[4]
    if not all((open_, spin, first, second, result)):
        errors.append("%s: a dump has no DiceGame line" % tag)
        return None
    if open_["faces"] != [None] * 8 or open_["rolls"] != 0:
        errors.append("%s: the opening dump is not eight unknown dice: %r" % (tag, open_))
    if not (spin["rolling"] == 1 and 900 <= spin["elapsed"] <= 1100 and spin["faces"] == [None] * 8):
        errors.append("%s: the spinning dump is wrong: %r" % (tag, spin))
    if not (first["rolling"] == 0 and first["elapsed"] == 3534 and all(f is not None for f in first["faces"][:2])
            and first["faces"][2:] == [None] * 6 and first["player"] == sum(first["faces"][:2]) and first["character"] == 0):
        errors.append("%s: the first-roll dump is wrong: %r" % (tag, first))
    stopped = [f for f in second["faces"] if f is not None]
    if not (second["rolling"] == 1 and second["rolls"] == 2 and second["faces"][:2] == first["faces"][:2] and len(stopped) == 3
            and second["faces"][2] is not None):
        errors.append("%s: the mid-second-roll dump (one Stop click) should show three stopped dice in order: %r" % (tag, second))
    f = result["faces"]
    if not (result["rolling"] == 0 and None not in f and f[:2] == first["faces"][:2] and result["rolls"] == 2):
        errors.append("%s: the result dump is not eight stopped dice: %r" % (tag, result))
        return None
    if result["player"] != sum(f[:4]) or result["character"] != sum(f[4:]):
        errors.append("%s: the totals do not add up: %r" % (tag, result))
    m = re.search(r"^DELTA\t(-?\d+)\t(\d+)\t(\d+)", out, flags=re.M)
    if not m:
        errors.append("%s: no DELTA line" % tag)
        return None
    delta, pt, ct = int(m.group(1)), int(m.group(2)), int(m.group(3))
    want = BET if pt > ct else (-BET if pt < ct else -ANTE)
    if (pt, ct) != (result["player"], result["character"]) or delta != want:
        errors.append("%s: delta %d with totals %d/%d, wanted %d with %r" % (tag, delta, pt, ct, want, (result["player"], result["character"])))
    # The result text: 'instructions' tells who won and the amount.
    final = dumps[4]
    text = re.search(r'Text "instructions" .*label="([^"]*)"', final)
    text = text.group(1) if text else ""
    amount = "$4,800" if pt != ct else "$1,000"
    if amount not in text or (pt < ct and "I win" not in text) or (pt == ct and "keep" not in text):
        errors.append("%s: the result text %r does not match the outcome" % (tag, text))
    audio = re.findall(r"^audio fired (.*)$", out, flags=re.M)
    fired = dict(kv.split("=") for kv in audio[-1].split()) if audio else {}
    if fired.get("sfx/roll_dice.ogg") != "2":
        errors.append("%s: roll_dice fired %s times, wanted 2" % (tag, fired.get("sfx/roll_dice.ogg")))
    for snd, wanted in (("sfx/dice_win.ogg", pt > ct), ("sfx/dice_lose.ogg", pt < ct)):
        if (fired.get(snd) == "1") != wanted or (not wanted and snd in fired):
            errors.append("%s: %s fired %s, outcome %d/%d" % (tag, snd, fired.get(snd), pt, ct))
    if "ui/portview.lua" not in dumps[5]:
        errors.append("%s: Done did not return to the port view" % tag)
    blobs = {}
    for k, p in shots.items():
        try:
            data = open(p, "rb").read()
            if struct.unpack(">II", data[16:24]) != (800, 600) or not png_not_black(p):
                errors.append("%s: the %s screenshot is wrong" % (tag, k))
            blobs[k] = data
        except OSError as e:
            errors.append("%s: no %s screenshot: %s" % (tag, k, e))
    if len(set(blobs.values())) != len(blobs):
        errors.append("%s: two of the screenshots are equal" % tag)
    return dict(faces=f, delta=delta, pt=pt, ct=ct)


runs = [run("run%d_seed%d" % (i, s), s) for i, s in enumerate(SEEDS)]
if all(runs):
    if runs[0] != runs[1]:
        errors.append("the same seed gave different games: %r vs %r" % (runs[0], runs[1]))
    if runs[0]["faces"] == runs[2]["faces"]:
        errors.append("two seeds gave the same eight faces: %r" % (runs[0]["faces"],))
    print("outcomes", [(r["pt"], r["ct"], r["delta"]) for r in runs])

if errors:
    print("FAIL\n" + "\n".join(errors))
    sys.exit(1)
print("Chocolatier 2 dice OK")
