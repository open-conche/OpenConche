"""Chocolatier 1, the finale (docs/re/choc1-finale.md): a new user plays through the tutor to the San Francisco port, then the shipped
Lua's own ending path runs: Simulator.AwardMedal(gSim, table.getn(Simulator.MedalData)) - the LAST medal, "master" - snapshots gSim.mcWeeks
(from time) and gSim.mcMoney (from money), and DisplayDialog{'ui/reward_finale.lua'} builds the ending screen on the shipped Lua: the
mainmenu Bitmap, the Finale window (the native: 60 chocolate-raining particles on the 10 ms task), the medal bitmap centred
(kCenter, kCenter-20), the three texts (medal_master_finale message with weeks/money/score, the medal_master title, award_master), the
close button, PlaySound('mcfanfare') and BackgroundMusic.

What the asserts pin: the modal stack is portview + reward_finale; the Finale window is real (no placeholder) and its 10 ms clock runs (the
tick count advances between dumps ~1 s apart); the texts resolved through the string table (Memorial Service Award title, the
International Society of Confectioners, the weeks/money/score sentence with Dollars-formatted money); the medal bitmap shows
image/medal_master; mcfanfare (win_game_fanfare) fired; the Close button dismisses the dialog back to the port; the rain is visible in
pixels (two screenshots 1 s apart differ far more than a static screen could); MCW prints the snapshotted mcWeeks/mcMoney.

Asserts: exit 0, no [lua-error] / [engine-error] / [native-error] / [native-warning], the [game] line says choc1, only StartMovie
unimplemented, no placeholder window types at all, non-black 800x600 screenshots that differ as the rain moves."
"""
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
ALLOWED_PLACEHOLDERS = set()  # Logo, HiscoreWindow, FactoryGame, DiceGame, MapScreen and now Finale are all real


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
            elif f == 4:
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                pr = a if pa <= pb and pa <= pc else b if pb <= pc else c
                line[i] = (line[i] + pr) & 255
        rows.append(bytes(line))
        prev = line
    return w, h, rows, bpp


def png_not_black(path):
    w, h, rows, bpp = png_pixels(path)
    return sum(1 for r in rows for b in r if b) > (w * h * bpp) // 100


def differing_pixels(path1, path2):
    w1, h1, rows1, bpp1 = png_pixels(path1)
    w2, h2, rows2, bpp2 = png_pixels(path2)
    assert (w1, h1, bpp1) == (w2, h2, bpp2), "the screenshots differ in size"
    return sum(1 for y in range(h1) if rows1[y] != rows2[y])


shots = {k: os.path.join(a.workdir, k + ".png") for k in ("finale1", "finale2")}
SHOT = lambda k: "screenshot-logical %s\n" % shots[k]
MENU_READY = "expect-window ui/mainmenu.lua 20000\nwait 1500\n"

steps = [MENU_READY,
         "click 400 298\nexpect-window ui/entername.lua 5000\nwait 500\ntype Tester\nkey Return\n",
         "expect-window ui/chartalk.lua 8000\nwait 1000\nclick 225 315\nwait 800\nclick 470 315\n",           # No Thanks, OK: the logo builder
         "expect-window ui/logobuilder.lua 2000\nwait 800\nclick 625 355\n",                                 # Done: the recipe book
         "expect-window ui/dialog.lua 2000\nwait 800\nclick 598 350\n",                                        # the award
         "expect-window ui/reward.lua 2000\nwait 800\nclick 545 492\nexpect-window ui/portview.lua 5000\nwait 1500\n",
         # The ending path's own Lua: the LAST MedalData row ("master") snapshots mcWeeks/mcMoney and opens ui/reward_finale.lua.
         "lua Simulator.AwardMedal(gSim, table.getn(Simulator.MedalData))\n",
         "expect-window ui/reward_finale.lua 5000\nwait 1200\ndump\n" + SHOT("finale1"),
         "wait 1000\ndump\n" + SHOT("finale2"),
         "lua print('MCW', gSim.mcWeeks, gSim.mcMoney, gSim.medals[table.getn(Simulator.MedalData)])\n",
         "click 738 538\nwait 1500\nexpect-lua 5000 gSim.mcWeeks and gSim.mcMoney\n",                          # the close button dismisses
         "dump\n",
         "quit\n"]
out, log = run("finale", "".join(steps))
d = dumps(out)
if len(d) != 3:
    errors.append("expected 3 window dumps, got %d" % len(d))
else:
    first, second, closed = d
    if first[1][-1:] != ["ui/reward_finale.lua"] or first[1][0] != "ui/portview.lua":
        errors.append("the finale dialog is not over the port: %r" % (first[1],))
    if line_of(first[2], "Finale", "") is None:
        errors.append("no Finale window in the dump (was it created?)")
    # The 10 ms clock: two dumps ~1 s apart, the tick count moved on (the rain animates).
    def tick_of(block):
        m = re.search(r"^\s*Finale \".*? tick=(\d+) respawns=(\d+) pos0=(-?[\d.]+),(-?[\d.]+)", block, flags=re.M)
        return (int(m.group(1)), int(m.group(2))) if m else None
    t1, t2 = tick_of(first[2]), tick_of(second[2])
    if not t1:
        errors.append("the Finale dump line has no tick/respawns detail: %r" % (line_of(first[2], "Finale", ""),))
    elif t1[0] < 20:
        errors.append("the Finale clock barely ran (tick=%d after a 1.2 s wait)" % t1[0])
    else:
        if not t2 or t2[0] <= t1[0]:
            errors.append("the rain does not animate between dumps (tick %r -> %r)" % (t1, t2))
    # The texts resolved through the string table (the three labels of ui/reward_finale.lua).
    block = first[2]
    for needle in ("Memorial Service Award", "International Society of Confectioners", "This represents earnings of"):
        if needle not in block:
            errors.append("the finale dialog is missing the %r text" % needle)
    if not re.search(r"Marten Baumeister", block):
        errors.append("the medal_master title text is missing")
    if "$" not in block:
        errors.append("no Dollars-formatted money in the finale dialog")
    if "image/medal_master" not in block:
        errors.append("the master medal bitmap is missing")
    if fired(first[2], "audio/sfx/win_game_fanfare.ogg") + fired(second[2], "audio/sfx/win_game_fanfare.ogg") < 1:
        errors.append("mcfanfare (win_game_fanfare) did not fire")
    # The snapshot the AwardMedal path stored, and the close returning to the port.
    m = re.search(r"^MCW\t(\d+)\t(\d+)\t(true|nil)", out, flags=re.M)
    if not m:
        errors.append("no MCW line printed")
    elif int(m.group(1)) <= 0 or int(m.group(2)) <= 0 or m.group(3) != "true":
        errors.append("the MCW line does not show the snapshot (%r)" % (m.group(0),))
    if closed[1][-1:] == ["ui/reward_finale.lua"]:
        errors.append("the close button did not dismiss the finale dialog")
    if closed[1][-1:] != ["ui/portview.lua"]:
        errors.append("the port is not back on top after Close: %r" % (closed[1],))

# The rain is visible: the two shots differ, and neither is black.
for k in ("finale1", "finale2"):
    if not os.path.exists(shots[k]):
        errors.append("no %s screenshot" % k)
if not errors:
    if not png_not_black(shots["finale1"]):
        errors.append("finale1.png is black")
    if not png_not_black(shots["finale2"]):
        errors.append("finale2.png is black")
    diff = differing_pixels(shots["finale1"], shots["finale2"])
    if diff < 10:
        errors.append("the two finale screenshots are nearly identical (%d rows differ; the rain should move)" % diff)

if errors:
    print("FAIL: %d errors" % len(errors))
    for e in errors:
        print("  " + e)
    sys.exit(1)
print("finale OK: ticks %r -> %r, rain moved %s pixel rows" % (t1, t2, differing_pixels(shots["finale1"], shots["finale2"])))
