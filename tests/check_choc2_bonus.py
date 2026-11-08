"""Chocolatier 2 crates bonus game (docs/design/choc2-minigames.md 2.6 and 6): the real Lua entry, a bet, crates opened until the
game ends on a win and on a whammy, with two seeds each.

A fresh Story game (the tutor dialogs closed as in check_choc2_dice.py) reaches the live San Francisco port. The script gives it what the
entry gate wants, `gSim:AdjustMoney(100000); gSim.rank = 4`, then does what a den character does: `PlayBonus()` returns the character
action, which is called with a Character (`ny_gambler01`). The Lua opens ui/chartalk.lua ("gamble_crate_yn"); Yes opens
ui/gamble_bonus.lua, which builds the BonusGame window with its 25 select_N buttons (5 columns, 60 px apart).

The BonusGame is a CratesGame on CratesSpec::bonus(): Start pays the bet (`gSim:AdjustMoney(-bet)`) and PrepareGame(bet) hides the prizes in
the 25 crates. The prize table is a GUESS, so the check pins only what the engine decided: the 25 revealed values are
exactly the spec's values for the bet (4 whammies, the rest from the scaled table), and "I've put more than $X in these crates" is the
Lua's rounding of their sum.
  win run:     crates 1..5 hold prizes; "Done" (the Lua's Finish) reveals the rest, "Close" pays. Money moves by -bet + winnings
               (the Lua's own rule: a tie pays nothing back), where the winnings are the revealed values of the five crates the player opened.
  whammy run:  crates 1..4 hold prizes and crate 5 a whammy: the game ends by itself, the rest is revealed, a sixth click does nothing, and
               "Close" leaves the money at -bet.
Asserts: no [lua-error] etc., the unimplemented natives allow-listed, the dump values, the money delta from the dump, the sounds
(crate_click 25, crate_with_money once per opened prize, crate_no_money 4), the same seed twice gives the same game, and screenshots that are
800x600, not black and mutually different. The pictures are meant to be looked at too."""
import argparse, os, re, shutil, struct, subprocess, sys, zlib

ap = argparse.ArgumentParser()
ap.add_argument("--openconche", required=True)
ap.add_argument("--assets", required=True)
ap.add_argument("--workdir", required=True)
a = ap.parse_args()
shutil.rmtree(a.workdir, ignore_errors=True)
os.makedirs(a.workdir)
errors = []

# StartMovie is Chocolatier 3's intro movie (an empty name, skipped); bsgDate is the badge's; StartSparkle is the prize burst.
ALLOWED_UNIMPLEMENTED = {"StartMovie", "bsgDate", "StartSparkle"}
# (kind, seed): win seeds 1 and 3 open prizes in crates 1..5; whammy seeds 2 and 4 open prizes in 1..4 and a whammy in 5. The first run
# is repeated to check determinism.
RUNS = [("win", 1), ("win", 1), ("win", 3), ("whammy", 2), ("whammy", 4)]
FACTORS = [0.435] * 4 + [0.4] + [0.35] * 5 + [0.25] * 5 + [0.24] * 3 + [0.15, 0.1, 0.05] + [0.0] * 4  # CratesSpec::bonus(), a guess
COLS = 5
FIRST_X, FIRST_Y = 185, 193  # centre of select_1: frame (100,50) + button (60,120) + half of 50x47
STEP = 60
START = (599, 400)  # Start / Done / Close: frame + (414,330 or 370) + half of 170x40
DONE = (599, 440)


def crate_xy(i):
    return FIRST_X + STEP * ((i - 1) % COLS), FIRST_Y + STEP * ((i - 1) // COLS)


def expected_values(bet):
    out = []
    for f in FACTORS:
        x = int(bet * f + 0.5) // 100 * 100
        if x < 100 and f > 0:
            x = 100
        out.append(x)
    return out


def dollars(v):
    return "${:,}".format(v)


HEAD = """expect-window ui/mainmenu.lua 20000
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
lua gAct = PlayBonus(); gR = gAct(LCharacter:ByName('ny_gambler01'))
expect-window ny_gambler01 5000
wait 800
click 209 305
expect-window ui/gamble_bonus.lua 5000
wait 800
screenshot-logical {open}
dump
click {sx} {sy}
wait 800
screenshot-logical {started}
dump
"""


def script(kind, shots):
    s = HEAD.format(sx=START[0], sy=START[1], **shots)
    nclick = 5 if kind == "win" else 6
    for i in range(1, nclick + 1):
        x, y = crate_xy(i)
        if kind == "whammy" and i == 6:
            s += "dump\n"  # the ended game, before the sixth click
        s += "click %d %d\nwait 1500\n" % (x, y)
        if i == 3:
            s += "screenshot-logical %s\n" % shots["mid"]
    s += "screenshot-logical %s\ndump\n" % shots["end"]  # win: five opened, game running; whammy: ended, all revealed
    if kind == "win":
        s += "click %d %d\nwait 1000\nscreenshot-logical %s\ndump\n" % (DONE[0], DONE[1], shots["done"])
    s += "click %d %d\nwait 1000\ndump\n" % (DONE[0], DONE[1])  # Close
    s += "lua print('DELTA', gSim.money - m0)\nquit\n"
    return s


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


def crates_of(dump):
    m = re.search(r'BonusGame "" id=\d+ rect=\S+ (.*)', dump)
    if not m:
        return None
    return {int(c): (st, int(v or 0), int(t or 0)) for c, st, v, t in re.findall(r"c(\d+)=(\w+)(?:/(\d+)/(\d+))?", m.group(1))}


def label(dump, name):
    m = re.search(r'Text "%s" id=\d+ rect=\S+ label="([^"]*)"' % name, dump)
    return m.group(1) if m else None


def run(tag, kind, seed):
    d = os.path.join(a.workdir, tag)
    os.makedirs(d)
    names = ["open", "started", "mid", "end"] + (["done"] if kind == "win" else [])
    shots = {k: os.path.join(d, k + ".png") for k in names}
    path = os.path.join(d, "bonus.script")
    open(path, "w").write(script(kind, shots))
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
    if len(re.findall(r"^\[script\] ok expect-", out, flags=re.M)) < 6:
        errors.append("%s: too few expect-* lines passed" % tag)
    dumps = [x for x in re.split(r"(?=^\[windows\] modal depth)", out, flags=re.M) if x.startswith("[windows]")]
    want = 5  # open, started, (pre-click-6,) end, (done,) closed
    if len(dumps) != want:
        errors.append("%s: expected %d window dumps, got %d" % (tag, want, len(dumps)))
        return None
    opened, started = dumps[0], dumps[1]
    pre6 = dumps[2] if kind == "whammy" else None
    end = dumps[3] if kind == "whammy" else dumps[2]
    closed = dumps[-1]
    m = re.search(r"^DELTA\t(-?\d+)", out, flags=re.M)
    if not m:
        errors.append("%s: no DELTA line" % tag)
        return None
    delta = int(m.group(1))

    c0 = crates_of(opened)
    if not c0 or len(c0) != 25 or any(v[0] != "waiting" for v in c0.values()):
        errors.append("%s: the opening dump is not 25 waiting crates: %r" % (tag, c0))
        return None
    bet = int((label(opened, "bet") or "$0").replace("$", "").replace(",", ""))
    if bet < 100:
        errors.append("%s: no bet in the opening dump" % tag)
        return None
    cs = crates_of(started)
    if " prepared" not in started.split("BonusGame", 1)[1].split("\n", 1)[0] or any(v[0] != "waiting" for v in cs.values()):
        errors.append("%s: after Start the crates should be prepared and all waiting" % tag)
    ce = crates_of(end)
    full = crates_of(dumps[3]) if kind == "win" else ce  # the dump where every crate is revealed
    values = [full[i][1] for i in range(1, 26)]
    expect = expected_values(bet)
    if sorted(values) != sorted(expect):
        errors.append("%s: the 25 values %r are not the spec's for a bet of %d: %r" % (tag, sorted(values), bet, sorted(expect)))
    if values.count(0) != 4:
        errors.append("%s: %d whammies, wanted 4" % (tag, values.count(0)))
    total = sum(values)
    shown = label(started, "instructions") or ""
    floor = total // 10000 * 10000 if total > 100000 else (total // 1000 * 1000 if total > 10000 else total // 100 * 100)
    if dollars(floor) not in shown:
        errors.append("%s: the Lua says %r, but the crates hold %d (%s)" % (tag, shown, total, dollars(floor)))
    # The opening money: the Lua deducted the bet at Start (the end dump is after it; the delta below checks the whole game).
    if kind == "win":
        if any(values[i] == 0 for i in range(5)):
            errors.append("%s: seed %d no longer gives prizes in crates 1..5: %r" % (tag, seed, values[:6]))
            return None
        winnings = sum(ce[i][1] for i in range(1, 6))
        if winnings != sum(values[:5]):
            errors.append("%s: the opened crates changed value at the reveal" % tag)
        if any(ce[i][0] != "opening" for i in range(1, 6)) or any(ce[i][0] != "waiting" for i in range(6, 26)):
            errors.append("%s: before Done five crates should be open and twenty waiting: %r" % (tag, ce))
        if (label(end, "winnings") or "") != dollars(winnings):
            errors.append("%s: 'Click Done to keep' shows %r, the opened crates add up to %s" % (tag, label(end, "winnings"), dollars(winnings)))
        done = dumps[3]
        cd = crates_of(done)
        if any(cd[i][0] == "waiting" for i in range(1, 26)) or [cd[i][1] for i in range(1, 26)] != values:
            errors.append("%s: Done should reveal all 25 crates with the same values" % tag)
        if (label(done, "winnings") or "") != dollars(winnings):
            errors.append("%s: after Done the Lua shows %r, wanted %s" % (tag, label(done, "winnings"), dollars(winnings)))
        want_delta = -bet + (winnings if winnings != bet else 0)
        opened_prizes = 5
    else:
        if values[4] != 0 or any(values[i] == 0 for i in range(4)):
            errors.append("%s: seed %d no longer gives prizes in 1..4 and a whammy in 5: %r" % (tag, seed, values[:6]))
            return None
        if any(ce[i][0] == "waiting" for i in range(1, 26)):
            errors.append("%s: a whammy should end the game and reveal every crate: %r" % (tag, ce))
        c6 = crates_of(pre6)
        if c6[6][:2] != ce[6][:2]:
            errors.append("%s: the sixth click changed crate 6: %r -> %r" % (tag, c6[6], ce[6]))
        lost = label(end, "instructions") or ""
        if lost != label(pre6, "instructions") or "whammy" not in lost:
            errors.append("%s: the instructions after the sixth click are not the whammy text: %r" % (tag, lost))
        want_delta = -bet
        opened_prizes = 4
    if delta != want_delta:
        errors.append("%s: money moved %d, wanted %d (bet %d, values %r)" % (tag, delta, want_delta, bet, values))
    if "ui/portview.lua" not in closed or "BonusGame" in closed:
        errors.append("%s: Close did not return to the port view" % tag)

    audio = re.findall(r"^audio fired (.*)$", out, flags=re.M)
    fired = dict(kv.split("=") for kv in audio[-1].split()) if audio else {}
    for snd, n in (("sfx/crate_click.ogg", 25), ("sfx/crate_with_money.ogg", opened_prizes), ("sfx/crate_no_money.ogg", 4)):
        if fired.get(snd) != str(n):
            errors.append("%s: %s fired %s times, wanted %d" % (tag, snd, fired.get(snd), n))

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
    return dict(values=values, delta=delta, bet=bet)


results = [run("run%d_%s_seed%d" % (i, k, s), k, s) for i, (k, s) in enumerate(RUNS)]
if all(results):
    if results[0] != results[1]:
        errors.append("the same seed gave different games: %r vs %r" % (results[0], results[1]))
    if results[0]["values"] == results[2]["values"] or results[3]["values"] == results[4]["values"]:
        errors.append("two seeds gave the same 25 values")
    print("outcomes", [(k, s, r["delta"]) for (k, s), r in zip(RUNS, results)])

if errors:
    print("FAIL\n" + "\n".join(errors))
    sys.exit(1)
print("choc2_bonus ok")
