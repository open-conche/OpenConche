"""The crates bonus game check, played headless against the real Reforged game.

The new-player flow of check_new_user.py and the opening dialogs, then the crates dialog is opened the way
dev/dev_utils.lua does it: `lua DisplayDialog {"ui/ui_crates.lua"}`. In the game it is reached from a casino character's
action (sim/character_actions.lua PlayCrates, and ui/ui_slotselect.lua), which needs a later port, so the Lua command
stands in for that.

  1. Before the bet: the dialog is up, the CratesGame window is hidden and unprepared, the bet label is shown.
  2. Start (bet taken), then crate 1 and crate 4 clicked: only those two are open, the winnings label shows their sum
     and their times are the 50 ms ticks since each was opened.
  4. The play leg again with --fullscreen (2560x1440 emulated): the same clicks in logical coordinates open the same crates.
  3. Done reveals the rest: exactly three crates have value 0, the 21 values are the factor table for the bet (rounded
     down to hundreds), whammies revealed are 'blank'. A screenshot has the small red values and the black ones.
"""
import argparse, os, re, shutil, struct, subprocess, sys, zlib

ap = argparse.ArgumentParser()
ap.add_argument("--openconche", required=True)
ap.add_argument("--assets", required=True)
ap.add_argument("--workdir", required=True)
a = ap.parse_args()
shutil.rmtree(a.workdir, ignore_errors=True)
os.makedirs(a.workdir)
errors = []

INTRO = ("expect-window ui/mainmenu.lua 60000\nwait 1300\nclick 310 345\nexpect-window ui/ui_entername.lua 5000\nwait 600\n"
         "type Tester\nkey Return\nexpect-window ui/ui_difficulty.lua 3000\nwait 800\nclick 328 400\nwait 5000\n"
         "click 400 346\nwait 1500\nclick 503 346\nwait 1500\n" + "click 400 346\nwait 1500\n" * 4)
OPEN = 'lua DisplayDialog {"ui/ui_crates.lua"}\nexpect-window ui/ui_crates.lua 3000\nwait 2000\n'
# Screen positions: the dialog sits at (54, 44), so the Start button is at (449, 344), crate 1's centre at (305, 163),
# crate 4's at (485, 163) and the Done button at (532, 344).
START = "click 449 344\nwait 1000\n"
FACTORS = [0.435, 0.435, 0.435, 0.4, 0.35, 0.35, 0.35, 0.35, 0.25, 0.25, 0.25, 0.25, 0.24, 0.24, 0.24, 0.15, 0.1, 0.05, 0, 0, 0]


def run(name, text, extra=()):
    work = os.path.join(a.workdir, name)
    os.makedirs(work)
    script = os.path.join(work, "t.script")
    with open(script, "w") as f:
        f.write(text.replace("@W@", work))
    r = subprocess.run([a.openconche, "--headless", "--assets", a.assets, *extra, "--userdir", os.path.join(work, "user"), "--script",
                        script, "--dump-windows"], cwd=work, capture_output=True, text=True, timeout=300)
    if r.returncode != 0 or "[lua-error]" in r.stdout or "FAIL" in r.stdout:
        errors.append("%s: exit %d, tail %r" % (name, r.returncode, r.stdout[-600:]))
    return r.stdout, work


def dump_line(out, prefix):
    for l in out.splitlines():
        if l.strip().startswith(prefix):
            return l.strip()
    return ""


def label(out, name):
    m = re.search(r' label="([^"]*)"', dump_line(out, 'Text "%s" ' % name))
    return m.group(1) if m else None


def crates(out):
    """(the dump line, {index: (state, value, time)}) from the CratesGame dump line."""
    line = dump_line(out, 'CratesGame "crates"')
    res = {}
    for m in re.finditer(r"c(\d+)=(\w+)(?:/(\d+)/(\d+))?", line):
        res[int(m.group(1))] = (m.group(2), int(m.group(3) or 0), int(m.group(4) or 0))
    return line, res


def read_png(path):
    data = open(path, "rb").read()
    pos, idat = 8, b""
    while pos < len(data):
        n, typ = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        pos += 12 + n
        if typ == b"IHDR":
            w, h, depth, ctype = struct.unpack(">IIBB", body[:10])
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


# ---- 1. before the bet ----
out, work = run("before", INTRO + OPEN + "quit\n")
line, c = crates(out)
if not line:
    errors.append("before: no CratesGame window in the dump")
elif " hidden" not in line.split(" c1=")[0] or "prepared" in line or len(c) != 21 or any(v[0] != "waiting" for v in c.values()):
    errors.append("before: the crates should be hidden, unprepared and waiting: %s" % line[:200])
bet = None
m = re.fullmatch(r"\$([\d,]+)", label(out, "bet") or "")
if m:
    bet = int(m.group(1).replace(",", ""))
else:
    errors.append("before: the bet label is %r" % label(out, "bet"))

# ---- 2. start and open two crates ----
out, work = run("play", INTRO + OPEN + START + "click 305 163\nwait 300\nscreenshot @W@/open300.png\n"
                "wait 1200\nclick 485 163\nwait 1000\nscreenshot @W@/two.png\nquit\n")
line, c = crates(out)
if len(c) != 21 or "prepared" not in line:
    errors.append("play: the crates are not prepared: %s" % line[:200])
else:
    opened = [i for i, v in c.items() if v[0] != "waiting"]
    if sorted(opened) != [1, 4]:
        errors.append("play: crates 1 and 4 should be the only open ones, got %r" % sorted(opened))
    elif c[1][0] == "opening" and c[4][0] == "opening":
        got = c[1][1] + c[4][1]
        won = re.sub(r"[^\d]", "", label(out, "winnings") or "")
        if got == 0 or str(got) != won:
            errors.append("play: winnings label %r should be %d" % (label(out, "winnings"), got))
        # Crate 1 opened about 2500 ms before the dump (300 + 1200 + 1000), crate 4 about 1000 ms before.
        if not (2300 <= c[1][2] <= 2700 and 800 <= c[4][2] <= 1200):
            errors.append("play: opening times %r %r are not about 2500 and 1000 ms" % (c[1][2], c[4][2]))
    elif 0 not in (c[1][1], c[4][1]):
        errors.append("play: a crate that is not opening should be a whammy: %r" % (c,))

# ---- 3. done reveals the rest ----
out, work = run("reveal", INTRO + OPEN + START + "click 305 163\nwait 600\nclick 485 163\nwait 600\nclick 532 344\nwait 800\n"
                "screenshot @W@/reveal.png\nquit\n")
line, c = crates(out)
values = [c[i][1] for i in sorted(c)]
if len(c) != 21 or any(v[0] == "waiting" for v in c.values()):
    errors.append("reveal: every crate should be open: %s" % line[:300])
else:
    zeros = [i for i in c if c[i][1] == 0]
    if len(zeros) != 3:
        errors.append("reveal: expected three whammies, got %d" % len(zeros))
    if bet:
        want = sorted(max(100, int(bet * f + 0.5) // 100 * 100) if f > 0 else 0 for f in FACTORS)
        if sorted(values) != want:
            errors.append("reveal: the values %r are not the factor table for a $%d bet %r" % (sorted(values), bet, want))
    for i, (st, v, t) in c.items():
        if v == 0 and i not in (1, 4) and st != "blank":
            errors.append("reveal: whammy crate %d should be blank, is %s" % (i, st))
    try:
        w, h, bpp, rows = read_png(os.path.join(work, "reveal.png"))
        # The crate area is x 270..690, y 140..300 on screen. Revealed values are red-brown (0.8, 0.2, 0); the player's
        # two are black; the whammy devils are red too.
        red = dark = 0
        for y in range(140, 300):
            for x in range(270, 690):
                r_, g_, b_ = rows[y][x * bpp:x * bpp + 3]
                if r_ > 170 and g_ < 90 and b_ < 40:
                    red += 1
                if r_ < 40 and g_ < 40 and b_ < 40:
                    dark += 1
        if red < 150:
            errors.append("reveal screenshot: too few red value pixels (%d)" % red)
        if dark < 30:
            errors.append("reveal screenshot: too few black value pixels (%d)" % dark)
    except OSError:
        errors.append("reveal: no screenshot")

# ---- 4. fullscreen: the same two clicks (logical coordinates) open the same two crates in the letterboxed output ----
out, work = run("fullscreen", INTRO + OPEN + START + "click 305 163\nwait 300\nclick 485 163\nwait 800\nscreenshot @W@/full.png\nquit\n",
                ("--fullscreen", "--display-size", "2560x1440"))
line, c = crates(out)
if len(c) != 21 or sorted(i for i, v in c.items() if v[0] != "waiting") != [1, 4]:
    errors.append("fullscreen: crates 1 and 4 should be the only open ones: %s" % line[:200])
try:
    w, h, bpp, rows = read_png(os.path.join(work, "full.png"))
    if (w, h) != (2560, 1440):
        errors.append("fullscreen: screenshot is %dx%d" % (w, h))
except OSError:
    errors.append("fullscreen: no screenshot")

if errors:
    print("FAIL")
    for e in errors:
        print(" ", e)
    sys.exit(1)
print("crates ok")
