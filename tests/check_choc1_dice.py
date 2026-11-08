"""Chocolatier 1 dice (docs/re/choc-dice-game.md section 1): a new user plays through the tutor to San Francisco, raises
the travel building's encounter odds, opens the map and travels to Merida; the traveller encounter meets Aces McGuire the gambler
(sim/character.lua's PlayDice, data/travelers.lua) and the DiceGame dialog offers the wager. Before the roll only the intro shows;
Roll spins the four dice and StopDie is clicked mid-roll, which jumps the clock (the next die stops one tick after the press and
the outcome tick lands at elapsed 5044 instead of the natural 5022); the money moves by exactly the wager, and Leave closes the
dialog so the trip finishes in Merida. The 62 ms cadence and the natural leg times (die4 at 2790, the end at 5022) are pinned by
the unit tests (test_choc1_dice.cpp); this end-to-end run pins the StopDie press consequence, the outcome flow, the money and the
rendered die art.

The rubber bands of the walk-through (facts of the shipped Lua, not of the native):
* LCharacter:DoAction speaks the quest tutorial (qi_novicenudge) while a quest is active and a name-gated hint every 12 weeks,
  so gSim.quest is cleared and nextHintTime pushed out before travelling; otherwise the gambler talks instead of playing.
* LBuilding:ChooseCharacter picks self.character[bsutil.random(1, n)], so the travel building's character list is pinned to the
  gambler and encounterOdds to 100.
* The wager comes from bsutil.random over the player's money, so the test wraps the global RollDice to capture what the shipped
  Roll the Dice closure passes to the native, and compares the ledger before and after.

What the asserts pin (docs/re/choc-dice-game.md 1.3, 1.5, 1.7): the four outcome texts and stop_label are hidden at Init; the
spin is a 62 ms tick with die4 stopping first at 2790 ms and the contest ending at 5022 ms; the roll sound plays at RollDice and
stops when the last die stops (the log's stop-file line); stop and stop_label are disabled at the outcome and leave re-enabled;
exactly one outcome text shows and the intro hides (all three branches hide it); win pays +wager ('dice_logwin'), lose charges
-wager ('dice_logloss') and a tie moves nothing.

Asserts: exit 0, no [lua-error] / [engine-error] / [native-error] / [native-warning], the [game] line says choc1, only StartMovie
unimplemented, only the Logo placeholder, 800x600 screenshots that are not black and whose four die rectangles show the die art
(one pip before the roll; after it the rendered pip count follows each face, image/die<face+1>)."""
import argparse, os, re, shutil, struct, subprocess, sys, zlib

ap = argparse.ArgumentParser()
ap.add_argument("--openconche", required=True)
ap.add_argument("--assets", required=True)
ap.add_argument("--workdir", required=True)
a = ap.parse_args()
shutil.rmtree(a.workdir, ignore_errors=True)  # a fresh user dir
os.makedirs(a.workdir)
user = os.path.join(a.workdir, "user")
errors = []

ALLOWED_UNIMPLEMENTED = {"StartMovie"}
ALLOWED_PLACEHOLDERS = set()  # Logo is real since the seventh session (choc1-logo-and-hiscore.md)


def run(tag, text):
    script = os.path.join(a.workdir, tag + ".script")
    with open(script, "w") as f:
        f.write(text)
    unimpl = os.path.join(a.workdir, tag + "-unimplemented.txt")
    log = os.path.join(a.workdir, tag + "-log.txt")
    r = subprocess.run([a.openconche, "--headless", "--assets", a.assets, "--userdir", user, "--script", script, "--log-unimplemented", unimpl,
                       "--log", log], cwd=a.workdir, capture_output=True, text=True, timeout=240)
    out = r.stdout
    with open(os.path.join(a.workdir, tag + "-out.txt"), "w") as f:
        f.write(out)
    if r.returncode != 0:
        errors.append("%s: exit %d, tail %r" % (tag, r.returncode, out[-600:]))
    for bad in ("[lua-error]", "[engine-error]", "[native-error]", "[native-warning]", "FAIL"):
        if bad in out:
            errors.append("%s: output contains %s" % (tag, bad))
    if "[game] choc1 " not in out:
        errors.append("%s: no '[game] choc1' line (game detection)" % tag)
    for m in re.finditer(r"^\[unimplemented-type\] (\S+)", out, flags=re.M):
        if m.group(1) not in ALLOWED_PLACEHOLDERS:
            errors.append("%s: unexpected placeholder window type %s" % (tag, m.group(1)))
    if os.path.exists(unimpl):
        for line in open(unimpl):
            name = line.split("\t")[0].strip()
            if name and name not in ALLOWED_UNIMPLEMENTED:
                errors.append("%s: unexpected unimplemented native %s" % (tag, name))
    else:
        errors.append("%s: no --log-unimplemented file" % tag)
    return out, (open(log).read() if os.path.exists(log) else "")


def dumps(out):
    """Each window dump as (modal depth, [modal script paths], text)."""
    res = []
    for block in re.split(r"(?=^\[windows\] modal depth)", out, flags=re.M):
        m = re.match(r"\[windows\] modal depth (\d+)", block)
        if m:
            res.append((int(m.group(1)), re.findall(r'^  Modal "[^"]*" .*? script="([^"]*)"', block, flags=re.M), block))
    return res


def line_of(block, kind, name):
    m = re.search(r"^\s*%s \"%s\" .*$" % (kind, re.escape(name)), block, flags=re.M)
    return m.group(0) if m else None


def fired(block, sound):
    m = re.search(r"^audio fired .*?\b%s=(\d+)" % re.escape(sound), block, flags=re.M)
    return int(m.group(1)) if m else 0


def png_pixels(path):
    """(width, height, rows of RGBA byte strings, bytes per pixel) of an 8-bit RGB/RGBA PNG, unfiltered."""
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
            elif f == 4:  # Paeth: the distances pick which neighbour (left, above, above-left) predicts the byte
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                pr = a if pa <= pb and pa <= pc else b if pb <= pc else c
                line[i] = (line[i] + pr) & 255
        rows.append(bytes(line))
        prev = line
    return w, h, rows, bpp


def pip_pixels(path, x, y):
    """Dark (pip) pixels of the 50x50 die rectangle whose top-left screen corner is (x, y)."""
    w, h, rows, bpp = png_pixels(path)
    dark = 0
    for yy in range(y, y + 50):
        row = rows[yy]
        for xx in range(x, x + 50):
            r, g, b = row[xx * bpp:xx * bpp + 3]
            al = row[xx * bpp + 3] if bpp == 4 else 255
            if al > 128 and r + g + b < 330:
                dark += 1
    return dark


def png_not_black(path):
    w, h, rows, bpp = png_pixels(path)
    return sum(1 for r in rows for b in r if b) > (w * h * bpp) // 100


shots = {k: os.path.join(a.workdir, k + ".png") for k in ("preroll", "after")}
SHOT = lambda k: "screenshot-logical %s\n" % shots[k]
MENU_READY = "expect-window ui/mainmenu.lua 20000\nwait 1500\n"

# The dice dialog: the Window at (101,10), the DiceGame filling it; die1 (447,223) die2 (392,223) die3 (447,165) die4 (392,165)
# inside it, the Roll the Dice button over the stop button at (322,273) and Leave at (62,273).
steps = [MENU_READY,
         "click 400 298\nexpect-window ui/entername.lua 5000\nwait 500\ntype Tester\nkey Return\n",
         "expect-window ui/chartalk.lua 8000\nwait 1000\nclick 225 315\nwait 800\nclick 470 315\n",           # No Thanks, OK: the logo builder
         "expect-window ui/logobuilder.lua 2000\nwait 800\nclick 625 355\n",                                 # Done: the recipe book
         "expect-window ui/dialog.lua 2000\nwait 800\nclick 598 350\n",                                        # the award
         "expect-window ui/reward.lua 2000\nwait 800\nclick 545 492\nexpect-window ui/portview.lua 5000\nwait 1500\n",
         # The wager needs money (and the 20 % cap shapes the stake): a known large balance. The shipped Lua's quest gates are
         # cleared so the gambler plays instead of talking (LCharacter:DoAction).
         "lua gSim:AdjustMoney(100000)\n"
         "lua gSim.quest = nil; gSim.nextHintTime = 999999; LQuest._Variable.QuestCounter = 99\n"
         "lua LBuilding:ByName('travel').encounterOdds = 100; LBuilding:ByName('travel').character = { LCharacter:ByName('gambler') }\n"
         "lua gM0 = gSim.money\n",
         # The wrapper captures the exact wager text the Roll the Dice closure passes to the native.
         "lua gWager = nil; gOrigRoll = RollDice; RollDice = function(b) gWager = b return gOrigRoll(b) end\n",
         # The map on top of the port, then Merida's icon: the encounter meets the gambler on the shipped TravelTick path.
         "lua table.insert(gCommandQueue, function() DoModal('ui/map.lua') end)\nwait 1500\n",
         "move 164 187\nwait 100\nmove 165 188\nwait 300\n",
         "click 165 188\nwait 3000\ndump\n" + SHOT("preroll"),
         "lua gM1 = gSim.money\n",                                                                          # the ledger before the bet
         "click 455 315\nwait 700\ndump\n",                                                                    # D2: Roll
         "click 455 315\nwait 400\ndump\n",                                                                    # D3: StopDie pressed mid-roll
         "wait 5600\ndump\n" + SHOT("after"),                                                                 # D4: the outcome
         "lua print('WAGER', gWager, gM1, gSim.money)\n",
         "click 195 315\nwait 2500\nexpect-lua 4000 gSim.port.name == 'merida' and tonumber(gWager) > 0\n",        # Leave: the trip finishes
         "quit\n"]
out, log = run("dice", "".join(steps))
d = dumps(out)
if len(d) != 4:
    errors.append("expected 4 window dumps, got %d" % len(d))
else:
    before, mid, pressed, after = d
    # Before the roll: the dice dialog is on the map on the port, the four outcome texts are hidden, the intro and both
    # buttons show (Roll covers Stop, which Init leaves shown).
    if before[1][-2:] != ["ui/map.lua", "ui/dialog.lua"] or before[1][0] != "ui/portview.lua":
        errors.append("the dice dialog is not on the map on the port: %r" % (before[1],))
    dl = line_of(before[2], "DiceGame", "")
    if not re.search(r"dice=0,0,0,0 rolling=1 elapsed=0 outcome=none bet=-", dl or ""):
        errors.append("the DiceGame does not rest before the roll (four zeroed faces, no bet): %r" % (dl,))
    for hidden in ("win_message", "lose_message", "draw_message", "stop_label"):
        if " hidden" not in (line_of(before[2], "Text", hidden) or ""):
            errors.append("%s is not hidden before the roll" % hidden)
    intro = line_of(before[2], "Text", "intro") or ""
    if " hidden" in intro or "wager" not in intro:
        errors.append("the intro question is not the only text before the roll: %r" % (intro,))
    if " hidden" in (line_of(before[2], "Button", "stop") or ""):
        errors.append("Init wrongly disabled the stop button (the roll button covers it)")
    for b in ("roll", "leave"):
        if " hidden" in (line_of(before[2], "Button", b) or ""):
            errors.append("%s is hidden before the roll" % b)
    # Mid-roll: the closure disabled roll and leave, enabled stop and stop_label, and the 62 ms clock runs the spin.
    md = line_of(mid[2], "DiceGame", "") or ""
    m = re.search(r"rolling=1 elapsed=(\d+) outcome=none bet=(\d+)", md)
    if not m:
        errors.append("mid-roll the dice are not spinning: %r" % (md,))
    else:
        if not 0 < int(m.group(1)) < 2790:
            errors.append("mid-roll the clock is not between the RollDice and the first die stop: elapsed=%s" % m.group(1))
    for kind, b, hidden in (("Button", "roll", True), ("Text", "roll_label", True), ("Button", "leave", True),
                            ("Text", "leave_label", True), ("Button", "stop", False), ("Text", "stop_label", False)):
        l = line_of(mid[2], kind, b) or ""
        if (" hidden" in l) != hidden:
            errors.append("mid-roll %s should be %s: %r" % (b, "hidden" if hidden else "shown", l))
    if fired(mid[2], "audio/sfx/dice_game_roll.ogg") != 1:
        errors.append("the roll sound did not play exactly once by the mid-roll dump")
    # The StopDie press (clicking the same spot now hits the stop button): one press while every die still spins jumps
    # elapsed to 2688, so its next tick (2750) stops die4 and the whole remaining cadre re-grids to 2750 + 62k: die3
    # stops at 3556, die2 at 4300 and the contest ends at elapsed 5044 - one tick later than the natural 5022 of the
    # unit-tested cadence. The pressed dump lands well before the natural die4 stop (a value of 2750 or more here can
    # only come from the jump, the natural clock would still be under 2046), and die4 froze at its final face.
    pd = line_of(pressed[2], "DiceGame", "") or ""
    pm = re.search(r"rolling=1 elapsed=(\d+) outcome=none", pd)
    if not pm:
        errors.append("the stopped dump has no spinning DiceGame: %r" % (pd,))
    else:
        if not 2750 <= int(pm.group(1)) < 5000:
            errors.append("StopDie did not jump the clock to the 2688 grid (elapsed=%s, expected 2750..4999)" % pm.group(1))
        for what in ("stop", "stop_label"):
            if " hidden" in (line_of(pressed[2], "Button" if what == "stop" else "Text", what) or ""):
                errors.append("%s should still be shown while a die is spinning after the press" % what)
    # After the roll: the contest has ended, one outcome text shows, intro and the stop controls hide, the outcome
    # sound fired and the ledger moved by exactly the captured wager.
    m = re.search(r"dice=(\d),(\d),(\d),(\d) rolling=0 elapsed=(\d+) outcome=(win|lose|tie|none) bet=(-?\d+)", after[2])
    if not m:
        errors.append("the outcome dump has no DiceGame line: %r" % (line_of(after[2], "DiceGame", ""),))
    else:
        faces = [int(x) for x in m.groups()[:4]]
        ms, outcome, bet = int(m.group(5)), m.group(6), int(m.group(7))
        if outcome == "none":
            errors.append("the contest did not end (%r)" % outcome)
        elif ms != 5044:
            errors.append("the outcome tick is elapsed=%d, expected the 5044 of the press-re-gridded run (5022 unpressed)" % ms)
        pmx = re.search(r"dice=\d,\d,\d,(\d)", pd)
        if not pmx or pmx.group(1) != m.group(4):
            errors.append("die4 did not freeze at its final face on the press's next tick (%r vs the outcome's %s)"
                          % (pd, m.group(4)))
        if outcome != "none":
            shown = {n: " hidden" not in (line_of(after[2], "Text", n) or "") for n in ("win_message", "lose_message", "draw_message")}
            # The sim's verdict key is "tie" (the dice_game_tie sound); the Lua window's child is "draw_message".
            want_key = "draw" if outcome == "tie" else outcome
            if sum(shown.values()) != 1 or not shown["%s_message" % want_key]:
                errors.append("exactly the %s_message must show: %r" % (outcome, shown))
            for what in ("intro", "stop_label"):
                if " hidden" not in (line_of(after[2], "Text", what) or ""):
                    errors.append("the %s should be disabled at the outcome" % what)
            if " hidden" not in (line_of(after[2], "Button", "stop") or ""):
                errors.append("the stop button should be disabled at the outcome")
            if " hidden" in (line_of(after[2], "Button", "leave") or ""):
                errors.append("the leave button should be re-enabled at the outcome")
            player, character = faces[0] + faces[1], faces[2] + faces[3]
            if (outcome == "win") != (character < player) or (outcome == "lose") != (player < character):
                errors.append("the outcome %r does not follow from the faces %r (player %d, character %d)" % (outcome, faces, player, character))
            if fired(after[2], "audio/sfx/dice_game_%s.ogg" % outcome) != 1:
                errors.append("the dice_game_%s.ogg sound did not play exactly once" % outcome)
        if bet <= 0:
            errors.append("the bet text is missing at the outcome: %r" % (bet,))
        w = re.search(r"^WAGER\s+(\d+)\s+(\d+)\s+(\d+)\s*$", out, flags=re.M)
        if not w:
            errors.append("the WAGER line is missing from the output")
        else:
            wager, m0, money = (int(x) for x in w.groups())
            if wager != bet:
                errors.append("the captured wager %d differs from the dump's bet %d" % (wager, bet))
            want = wager if outcome == "win" else -wager if outcome == "lose" else 0
            if money - m0 != want:
                errors.append("the ledger moved by %+d, expected %+d (outcome %s)" % (money - m0, want, outcome))
            # The intro named the same wager as the RollDice call (Dollars formats with commas).
            if ("$%s?" % "{:,}".format(wager)) not in (intro or ""):
                errors.append("the intro's wager does not match the RollDice bet: %r" % (intro,))
for k in ("preroll", "after"):
    try:
        w, h, _, _ = png_pixels(shots[k])
        if (w, h) != (800, 600):
            errors.append("the %s screenshot is %dx%d" % (k, w, h))
        elif not png_not_black(shots[k]):
            errors.append("the %s screenshot is (nearly) all black" % k)
    except (OSError, AssertionError) as e:
        errors.append("no %s screenshot: %s" % (k, e))
# The dice art on the screenshots: the die bitmaps sit at screen (548,233) (493,233) (548,175) (493,175). The preroll art is
# image/die1 for every die (Init re-images each counted die to picture slot 0), and the redraw's FACT is face ->
# image/die<face+1>. Each die's screen region over the dialog's dark backdrop differs from the preroll count by exactly
# the dark-pip pixels the shown die art adds over image/die1; measuring those deltas from the 50x50 art in the game's own assets
# (dark = alpha > 128 and R+G+B < 330, the criterion pip_pixels uses) gives, per face value (its die<f+1> art over die1):
#   face:      0    1    2    3    4    5
#   delta:     0   75  139  183  237  301    (gaps 75, 64, 44, 54, 64 - every window +/-25 is disjoint)
# so a delta window of the expected face only, +/-25 (the blended edges wobble by a dozen or so), cannot be satisfied
# by a neighbouring face's art - an off-by-one image/die<face> mapping lands in the face-1 window and fails.
ART_DELTA = (0, 75, 139, 183, 237, 301)
try:
    spots = [(x, y) for y in (233, 175) for x in (548, 493)]  # screen spots of die1..die4
    pips_before = [pip_pixels(shots["preroll"], x, y) for x, y in spots]
    if max(pips_before) - min(pips_before) > 10:
        errors.append("the preroll dice do not all show the one-pip die1 art: %r" % (pips_before,))
    if len(d) == 4:
        dm = re.search(r"dice=(\d),(\d),(\d),(\d) rolling=0", d[3][2])
        if not dm:
            errors.append("no faces to check the die art with")
        else:
            faces = [int(x) for x in dm.groups()]
            for i, (x, y) in enumerate(spots):
                delta = pip_pixels(shots["after"], x, y) - pips_before[i]
                want = ART_DELTA[faces[i]]
                if not want - 25 <= delta <= want + 25:
                    errors.append("after the roll die%d (face %d) does not show the die<face+1> art (dark delta %d, want %d +/-25)"
                                  % (i + 1, faces[i], delta, want))
except (OSError, AssertionError) as e:
    errors.append("visual check: %r" % (e,))
for want in ("[audio] stop file audio/sfx/dice_game_roll.ogg", "[save] saved empire"):
    if want not in log:
        errors.append("the log lacks %r" % want)

if errors:
    print("FAIL choc1_dice:\n  " + "\n  ".join(errors))
    sys.exit(1)
print("choc1_dice ok")
