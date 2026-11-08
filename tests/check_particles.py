"""Particles slice (docs/re/particles.md): the Awards fireworks and the slot machine's payout sparkle, headless against the real
Reforged game.

  1. Awards: `lua Player.medals.medal_07 = true` then `DisplayDialog{"ui/ui_medals.lua"}` (what the Awards button runs). The same run
     without the medal is the baseline. With the medal, StartFireworks makes the FireworksWindow create 9 fireworks; a few hundred
     ms after the dialog is up the dump shows bursting fireworks with particles, and the screenshot differs from the baseline in the
     burst area (x 50..299, y 100..299 plus the spread of the sparks) while the baseline matches itself. Without the medal the
     FireworksWindow has no fireworks.
  2. Slots: a winning pull (seed 10; StopSlots shortens the spin) of ui_slotmachine.lua. While the sparkle runs the screenshot differs from the dialog without
     it around (185, 244) and the dump shows live sparkle particles. (That onpayout waits for gSparkleTime = payout*0.5+1 s is
     unit-tested in test_stubs.cpp and shown by check_slotmachine.py, whose second pull only works once onpayout has run.)
"""
import argparse, math, os, re, shutil, struct, subprocess, sys, zlib

ap = argparse.ArgumentParser()
ap.add_argument("--openconche", required=True)
ap.add_argument("--assets", required=True)
ap.add_argument("--workdir", required=True)
a = ap.parse_args()
shutil.rmtree(a.workdir, ignore_errors=True)
os.makedirs(a.workdir)
errors = []


def err(m):
    errors.append(m)


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


def run(name, script, seed=None):
    work = os.path.join(a.workdir, name)
    os.makedirs(work)
    path = os.path.join(work, "t.script")
    with open(path, "w") as f:
        f.write(script.replace("@W@", work))
    r = subprocess.run([a.openconche, "--headless", "--assets", a.assets, "--userdir", os.path.join(work, "user"), "--script", path,
                        "--dump-windows"] + (["--seed", str(seed)] if seed is not None else []), cwd=work, capture_output=True, text=True, timeout=300)
    out = r.stdout
    with open(os.path.join(work, "out.txt"), "w") as f:
        f.write(out)
    if r.returncode != 0 or "[lua-error]" in out or "FAIL" in out:
        err("%s: exit %d, tail %r" % (name, r.returncode, out[-600:]))
    return work, out


def fireworks_lines(out):
    return [l.strip() for l in out.splitlines() if l.strip().startswith("FireworksWindow")]


# ---- 1. Awards ----
AWARDS = INTRO + "@PLANT@lua DisplayDialog{\"ui/ui_medals.lua\"}\nexpect-window ui/ui_medals.lua 3000\nwait 2500\n"
SHOTS = "".join("screenshot @W@/s%d.png\ndump\nwait 300\n" % i for i in range(5)) + "quit\n"
# The baseline holds the same medal but StartFireworks is replaced by a no-op, so the dialog itself is identical.
QUIET = "lua Player.medals.medal_07 = true\nlua StartFireworks = function() end\n"
base_dir, base_out = run("awards_base", AWARDS.replace("@PLANT@", QUIET) + SHOTS)
base2_dir, _ = run("awards_base2", AWARDS.replace("@PLANT@", QUIET) + SHOTS)
nomedal_dir, nomedal_out = run("awards_nomedal", AWARDS.replace("@PLANT@", "") + "dump\nquit\n")
fw_dir, fw_out = run("awards_fireworks", AWARDS.replace("@PLANT@", "lua Player.medals.medal_07 = true\n") + SHOTS)

for l in fireworks_lines(nomedal_out) + fireworks_lines(base_out):
    if "fireworks=0" not in l:
        err("without StartFireworks the FireworksWindow has fireworks: " + l)
if not fireworks_lines(nomedal_out):
    err("no FireworksWindow in the baseline dump (the Awards dialog should hold one)")
lines = fireworks_lines(fw_out)
if len(lines) < 5:
    err("expected 5 FireworksWindow dumps, got %d" % len(lines))
else:
    counts = []
    for l in lines:
        m = re.search(r"fireworks=(\d+) bursting=(\d+) particles=(\d+)", l)
        if not m or int(m.group(1)) != 9:
            err("fireworks != 9: " + l)
        else:
            counts.append((int(m.group(2)), int(m.group(3))))
    print("awards (bursting, particles) per shot:", counts)
    if not any(c[0] > 0 and c[1] > 0 for c in counts):
        err("no firework was ever bursting with particles: %r" % counts)
# Burst area: the bursts land in x 50..299, y 100..299; sparks spread about +-100 px sideways and fall up to ~110 px.
BOX = (0, 60, 420, 440)
diffs = []
for i in range(5):
    p = lambda d, i=i: os.path.join(d, "s%d.png" % i)
    if not (os.path.exists(p(base_dir)) and os.path.exists(p(fw_dir))):
        err("missing screenshot %d" % i)
        continue
    noise = diff_pixels(p(base_dir), p(base2_dir), BOX)
    d = diff_pixels(p(base_dir), p(fw_dir), BOX)
    print("awards shot %d: %d differing pixels vs baseline (baseline vs itself: %d)" % (i, d, noise))
    if noise > 0:
        err("shot %d: the baseline differs from itself (%d px), so the comparison is meaningless" % (i, noise))
    diffs.append(d)

# The sparks are 1.6..6 px quads at alpha <= 0.8, so a single burst is a few hundred pixels; the early shots have the most.
if len(diffs) == 5 and (max(diffs) < 200 or sum(1 for d in diffs if d > 100) < 2):
    err("the sparks are not visible in the screenshots: %r pixels differ from the baseline" % diffs)

# ---- 2. Slots ----
SLOTS = INTRO + ("lua DisplayDialog{\"ui/ui_slotmachine.lua\", bet=100}\nexpect-window ui/ui_slotmachine.lua 3000\nwait 1500\n"
                 "screenshot @W@/before.png\n")
PULLS = ""
for n in range(4):  # seed 10 wins pulls 1 and 4 and loses 2 and 3; each pull waits out the longest sparkle
    # StopSlots() cuts the first running wheel to 3 steps (three steps take 4..7 ticks with the carried phase, so it stops 200..350 ms later), one wheel per call that matters, so four
    # calls 400 ms apart stop the wheels in order and the last one about 1.5 s after the pull; the sparkle is 400..550 ms old at shot a. (The real spin lasts
    # 1.8..5.6 s, which would make the screenshot times random.)
    PULLS += "click 585 140\nwait 100\n" + "lua StopSlots()\nwait 400\n" * 4 + "wait 350\nscreenshot @W@/p%d_a.png\ndump\nwait 800\nscreenshot @W@/p%d_b.png\ndump\nwait 5500\ndump\n" % (n, n)
work, out = run("slots", SLOTS + PULLS + "quit\n", seed=10)
slot = [l.strip() for l in out.splitlines() if l.strip().startswith("SlotMachine")]
oks = [l.strip() for l in out.splitlines() if l.strip().startswith('Button "ok"')]
want = 4 * 3
if len(slot) < want or len(oks) < want:
    err("slots: expected %d dumps, got %d/%d" % (want, len(slot), len(oks)))
else:
    won = 0
    wins, losses = [], []
    for n in range(4):
        a_, b_, c_ = (slot[n * 3 + k] for k in range(3))
        prev = int(re.search(r"sparkle=(\d+)", slot[n * 3 - 1]).group(1)) if n else 0
        payout = float(re.search(r"payout=([-0-9.e]+)", c_).group(1))
        sparkle = [int(re.search(r"sparkle=(\d+)", s).group(1)) for s in (a_, b_, c_)]
        print("slots pull %d: payout %g, sparkle particles %s" % (n + 1, payout, sparkle))
        if payout > 0:
            won += 1
            if sparkle[0] == 0 or sparkle[1] == 0:
                err("pull %d (payout %g): no sparkle particles while the phase runs: %r" % (n + 1, payout, sparkle))
            wins.append(n)
        else:
            losses.append(n)
            # Leftovers of an earlier win stay frozen in the list (nothing clears them), but a loss adds none.
            if sparkle[0] > prev or sparkle[1] > prev or sparkle[2] > prev:
                err("pull %d: a payout of 0 made sparkle particles: %r (before: %d)" % (n + 1, sparkle, prev))
    if not won or not losses:
        err("slots: seed 10 needs at least one winning and one losing pull, got wins %r losses %r" % (wins, losses))
    else:
        shot = lambda n, s: os.path.join(work, "p%d_%s.png" % (n, s))
        # The coins sit in the payout tray at screen x 345..420, y 320..400 at first (origin 185,244 maps to about x=400) and
        # then fall out of it, over the dialog's lower frame (y 450..570). The text line at y 435..455 differs between a win
        # and a loss, so the boxes avoid it. The loss shot is taken at the same 400 ms.
        TRAY = (340, 310, 440, 420)
        FALL = (340, 460, 440, 590)
        for n in wins:
            tray = diff_pixels(shot(losses[0], "a"), shot(n, "a"), TRAY)
            fall = diff_pixels(shot(losses[0], "b"), shot(n, "b"), FALL)
            moved = diff_pixels(shot(n, "a"), shot(n, "b"), TRAY)
            print("slots win pull %d: coins in the tray %d px, fallen %d px, tray emptied %d px" % (n + 1, tray, fall, moved))
            if tray < 300:
                err("pull %d: no coins at the payout tray at 400 ms (%d px differ from a loss)" % (n + 1, tray))
            if fall < 300:
                err("pull %d: no coins below the tray 1.2 s after the pull (%d px differ from a loss)" % (n + 1, fall))
            if moved < 300:
                err("pull %d: the coins did not move between 400 ms and 1.2 s (%d px)" % (n + 1, moved))

if errors:
    print("FAIL")
    for e in errors:
        print(" ", e)
    sys.exit(1)
print("particles ok")
