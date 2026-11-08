"""The slot machine check (docs/design/factories.md section 10): the slot machine's wheels spin (docs/re/slot-machine.md).
Headless against the real Reforged game. After the new-player intro of check_crates.py, `lua DisplayDialog{"ui/ui_slotmachine.lua",
bet=100}` stands in for the casino (ui_slotselect.lua), the lever is pulled four times, and for each pull:

  1. Before the first: `ok` reads Cancel and the SlotMachine has not spun.
  2. After pull n: spins=n, `ok` reads Ok, the instructions label holds the result text ("Nothing. ..." for a loss), and the
     money changed by exactly Floor(bet*payout+0.9) - bet for the dumped payout. A change on pull 2 or later also proves the
     earlier onpayout ran (PullLever returns early until PaidOut has run).
  3. The Lua raised no error and StartSlots is not in the unimplemented log.
Three runs: the default seed and seed 10 (both win all four pulls), which must win at least once, and seed 3, which must lose
at least once (pull 3), so the loss label and money checks run. Screenshots before the first pull, one second into its spin and
after it; in the reel window (x 285..520, y 175..255) the mid-spin shot must differ from both.
"""
import argparse, math, os, re, shutil, struct, subprocess, sys, zlib

ap = argparse.ArgumentParser()
ap.add_argument("--openconche", required=True)
ap.add_argument("--assets", required=True)
ap.add_argument("--workdir", required=True)
a = ap.parse_args()
shutil.rmtree(a.workdir, ignore_errors=True)
errors = []


def read_png(path):
    data = open(path, "rb").read()
    pos, idat, w = 8, b"", None
    while pos < len(data):
        n, typ = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        pos += 12 + n
        if typ == b"IHDR":
            w, h, depth, ctype = struct.unpack(">IIBB", body[:10])
            assert depth == 8 and ctype in (2, 6)
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
                pr = x if pa <= pb and pa <= pc else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 255
        rows.append(line)
        prev = line
    return w, h, bpp, rows


def diff_pixels(p, q, box):
    """Number of pixels in box=(x0,y0,x1,y1) (logical 800x600 coordinates) whose colour differs by more than 24 in any channel."""
    w, h, bpp, ra = read_png(p)
    w2, h2, bpp2, rb = read_png(q)
    assert (w, h, bpp) == (w2, h2, bpp2)
    sx, sy = w / 800.0, h / 600.0
    n = 0
    for y in range(int(box[1] * sy), int(box[3] * sy)):
        for x in range(int(box[0] * sx), int(box[2] * sx)):
            i = x * bpp
            if any(abs(ra[y][i + k] - rb[y][i + k]) > 24 for k in range(3)):
                n += 1
    return n


INTRO = ("expect-window ui/mainmenu.lua 60000\nwait 1300\nclick 310 345\nexpect-window ui/ui_entername.lua 5000\nwait 600\n"
         "type Tester\nkey Return\nexpect-window ui/ui_difficulty.lua 3000\nwait 800\nclick 328 400\nwait 5000\n"
         "click 400 346\nwait 1500\nclick 503 346\nwait 1500\n" + "click 400 346\nwait 1500\n" * 4)
# Each pull waits 12 s of simulated time: the wheels spin 1.8 to 5.6 s, then the payout sparkle runs gSparkleTime = payout*0.5+1 s
# (5 s for the 8x jackpot) before onpayout.
# The dialog's lever ball is at (585, 140) on screen (see the screenshot); the handle button is masked, so the click must hit it.
PULLS = 4
SCRIPT = INTRO + ("lua gBefore = Player.money\nlua DisplayDialog{\"ui/ui_slotmachine.lua\", bet=100}\n"
                  "expect-window ui/ui_slotmachine.lua 3000\nwait 1500\nscreenshot @W@/before.png\ndump\n")
for i in range(PULLS):
    SCRIPT += "lua print('MONEY', Player.money)\nclick 585 140\nwait 1000\n"
    if i == 0:
        SCRIPT += "screenshot @W@/spinning.png\n"
    SCRIPT += "wait 11000\nlua print('MONEY', Player.money)\ndump\n"
    if i == 0:
        SCRIPT += "screenshot @W@/after.png\n"
SCRIPT += "quit\n"

def run(work, seed, expect_win, expect_loss=False):
    os.makedirs(work, exist_ok=True)
    def err(m):
        errors.append("[seed %s] %s" % (seed, m))
    script = os.path.join(work, "t.script")
    with open(script, "w") as f:
        f.write(SCRIPT.replace("@W@", work))
    unimpl = os.path.join(work, "unimplemented.txt")
    r = subprocess.run([a.openconche, "--headless", "--assets", a.assets, "--userdir", os.path.join(work, "user"), "--script", script,
                        "--dump-windows", "--log-unimplemented", unimpl] + (["--seed", str(seed)] if seed is not None else []), cwd=work, capture_output=True, text=True, timeout=300)
    out = r.stdout
    with open(os.path.join(work, "out.txt"), "w") as f:
        f.write(out)
    if r.returncode != 0 or "[lua-error]" in out or "FAIL" in out:
        err("exit %d, tail %r" % (r.returncode, out[-600:]))
    lines = [l.strip() for l in out.splitlines()]
    oks = [l for l in lines if l.startswith('Button "ok"')]
    spins = [l for l in lines if l.startswith("SlotMachine")]
    texts = [l for l in lines if l.startswith('Text "instructions"')]
    money = [float(l.split()[1]) for l in lines if l.startswith("MONEY")]
    label = lambda l: re.search(r'label="([^"]*)"', l).group(1) if re.search(r'label="([^"]*)"', l) else ""
    BET = 100
    if len(oks) < PULLS + 1 or len(spins) < PULLS + 1 or len(texts) < PULLS + 1 or len(money) != 2 * PULLS:
        err("expected %d dumps and %d money readings, got %d/%d/%d dumps, %d readings" %
                      (PULLS + 1, 2 * PULLS, len(oks), len(spins), len(texts), len(money)))
    else:
        if 'label="Cancel"' not in oks[0] or "spins=0" not in spins[0]:
            err("before the pull: %r %r" % (oks[0], spins[0]))
        wins = 0
        for i in range(PULLS):
            # Pull i+1. The dump has no enabled flag, so the Ok label, the result text and the money show the Lua flow ran to its
            # end: onstopped (result text, winnings) and onpayout (which lets the next pull through: PullLever returns early
            # until PaidOut has run, so a money change on pull 2 or later proves the earlier onpayout ran).
            m = re.search(r"payout=([-0-9.e]+)", spins[i + 1])
            if "spins=%d " % (i + 1) not in spins[i + 1] or not m:
                err("pull %d: %r" % (i + 1, spins[i + 1]))
                continue
            payout = float(m.group(1))
            # The net change is Floor(bet * payout + 0.9) - bet. Computing it from the dumped payout keeps the check independent
            # of which outcome the platform's random distribution gives for the seed.
            want = math.floor(BET * payout + 0.9) - BET
            got = money[2 * i + 1] - money[2 * i]
            if got != want:
                err("pull %d: payout %g, money changed by %g, expected %g" % (i + 1, payout, got, want))
            if 'label="Ok"' not in oks[i + 1]:
                err("pull %d: ok is not Ok: %r" % (i + 1, oks[i + 1]))
            result = label(texts[i + 1])
            if not result or result == label(texts[0]):
                err("pull %d: the instructions were not replaced by the result: %r" % (i + 1, texts[i + 1]))
            if payout == 0 and not result.startswith("Nothing."):
                err("pull %d: a loss should read 'Nothing. ...': %r" % (i + 1, result))
            if payout > 0:
                wins += 1
                if result.startswith("Nothing."):
                    err("pull %d: a win reads as a loss: %r" % (i + 1, result))
        print("pulls: %d, wins: %d" % (PULLS, wins))
        if expect_win and not wins:
            err("--expect-win: no pull won")
        if expect_loss and wins == PULLS:
            err("expected at least one lost pull, all %d won" % PULLS)
    for p in ("before.png", "spinning.png", "after.png"):
        if not os.path.exists(os.path.join(work, p)):
            err("missing screenshot " + p)
    try:
        shots = [open(os.path.join(work, p), "rb").read() for p in ("before.png", "spinning.png", "after.png")]
    except OSError:
        shots = None
    if shots:
        REEL = (285, 175, 520, 255)
        paths = [os.path.join(work, p) for p in ("before.png", "spinning.png", "after.png")]
        d0, d2 = diff_pixels(paths[1], paths[0], REEL), diff_pixels(paths[1], paths[2], REEL)
        print("reel window: spinning differs from before in %d px, from after in %d px" % (d0, d2))
        if d0 < 200 or d2 < 200:
            err("the reel window does not change while spinning (%d px vs before, %d px vs after)" % (d0, d2))
    try:
        names = [l.split("\t")[0] for l in open(unimpl)]
    except OSError:
        names = []
    if "StartSlots" in names:
        err("StartSlots is still logged as unimplemented")


# Seed 1 is the harness default. The expected money change is computed from the dumped payout, so the check does not
# depend on which pulls win. Rng derives its doubles from raw mt19937 words, so seeded outcomes are the same on every
# platform: seeds 1 and 10 win all four pulls (the Lua and the wheels consume the game stream), which covers the payout path; seed 3
# loses pull 3, which covers the loss path.
run(os.path.join(a.workdir, "seed1"), None, False)
run(os.path.join(a.workdir, "seed10"), 10, True)
run(os.path.join(a.workdir, "seed3"), 3, False, True)

if errors:
    print("FAIL")
    for e in errors:
        print(" ", e)
    sys.exit(1)
print("slotmachine ok")
