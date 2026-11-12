"""The Great Chocolate Chase (docs/design/greatcc.md slice 10): the avatar and the zones on the real scripts and art.

Runs `--headless --userdir <scratch> --seed 1` once and asserts: exit 0; no [lua-error] /
[engine-error] / [native-error] / [content]; the [unimplemented] and [unimplemented-type] sets are empty; the only textures the scripts name that the install
does not ship are the shop's menu overlay, its full-screen backdrop and the trim of the yes/no dialog.

Level 1 (a new player through Story Mode): the avatar stands on station01's point (184, 371) facing front, with an empty queue.
The only stations that are mouse targets in level 1 are the ones with a machine, the press (station12) and the mixer (station14),
so the clicks are real mouse clicks on their art (the player switches Tutorials off in the Options screen first: the first customer's
tutorial would lock the level). A click on the press
starts a trip; the mixer and the press are clicked again while she is still on her way: the second and third wait in the queue in
order, and the trail lists the stations she reached in the order they were clicked. A click on the station she then stands at
arrives at once (she has not moved). Level 1 has no centre table (station16 shows only with the kDisplay1 upgrade), so every trip
is a straight line. She faces right while she walks across (the heading to the first station is about 27 degrees below the
horizontal), her scale follows her height, and she is green-dressed in the screenshots: standing and mid-walk screenshots are
checked for that, and for the standing spot being empty once she has left it.
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

MISSING = {"ui/button/hud_menu_overlay", "ui/fullscreendialog", "ui/story/btn_trim_left"}
TYPES = set()
CALLS = set()

BOOT = """expect-window scripts/mainmenu.lua 20000
wait 1000
click 333 447
wait 1500
type Zed
wait 100
click 310 368
wait 3500
click 625 488
wait 3000
click 450 565
wait 1500
click 362 395
wait 300
click 404 486
wait 1500
move 150 258
wait 700
click 150 258
"""
# Level 1 opens with its diary page. The level's clock starts when the dialog has bounced away (about 970 ms after the page is up); the
# waits that follow are what the old script's were, less the 1570 ms it took the old one to reach the start of the level and plus the
# 416 ms between the click on OK and that start.
DIARY = """expect-window scripts/plotwindow.lua 30000
wait 450
move 585 500
wait 50
move 585 500
wait 50
click 585 500
"""


def run(tag, script_text, shots=()):
    user = os.path.join(a.workdir, "user_" + tag)
    log = os.path.join(a.workdir, tag + ".log")
    script = os.path.join(a.workdir, tag + ".script")
    paths = {s: os.path.join(a.workdir, "%s_%s.png" % (tag, s)) for s in shots}
    for name, path in paths.items():
        script_text = script_text.replace("@%s@" % name, path)
    with open(script, "w") as f:
        f.write(script_text)
    p = subprocess.run([a.openconche, "--headless", "--assets", a.assets, "--userdir", user, "--seed", "1", "--script", script,
                        "--log", log], capture_output=True, text=True, encoding="utf-8",
                       errors="replace", timeout=420)
    out = (open(log, encoding="utf-8", errors="replace").read() if os.path.exists(log) else "") + "\n" + p.stdout + "\n" + p.stderr
    if p.returncode != 0:
        errors.append("%s: exit status %d" % (tag, p.returncode))
    for t in ("[lua-error]", "[engine-error]", "[native-error]", "[content]", "[script] FAIL", "[expect-lua]"):
        if t in out:
            errors.append("%s: %s in the output: %s" % (tag, t, next(l for l in out.split("\n") if t in l)))
    seen = set(re.findall(r"^(?:\[\+\d+ms\] )?\[unimplemented\] (\S+)", out, re.M))
    if seen != CALLS:
        errors.append("%s: [unimplemented] set differs: extra %s, missing %s" % (tag, sorted(seen - CALLS), sorted(CALLS - seen)))
    types = set(re.findall(r"^(?:\[\+\d+ms\] )?\[unimplemented-type\] (\S+)", out, re.M))
    if types != TYPES:
        errors.append("%s: [unimplemented-type] set differs: extra %s, missing %s" % (tag, sorted(types - TYPES), sorted(TYPES - types)))
    gone = set(re.findall(r"^(?:\[\+\d+ms\] )?\[gfx\] texture '([^']+)' not found", out, re.M))
    if not gone <= MISSING:
        errors.append("%s: textures the install does not ship beyond the known ones: %s" % (tag, sorted(gone - MISSING)))
    dumps = [d for d in re.split(r"^(?=\[windows\] )", p.stdout, flags=re.M) if d.startswith("[windows] ")]
    return out, dumps, paths


def player(d):
    m = re.search(r'^\s*ShopWindow "sprite_window" .* player=([-\d.]+),([-\d.]+) face=(\d) idle=(\S+) moving=(\d) queue=(\d+) '
                  r'dest=(\S+) trail=(\S+)(?: hands=.*)?$', d, re.M)
    if not m:
        return None
    return {"x": float(m.group(1)), "y": float(m.group(2)), "face": int(m.group(3)), "idle": m.group(4), "moving": m.group(5) == "1",
            "queue": int(m.group(6)), "dest": m.group(7), "trail": [] if m.group(8) == "-" else m.group(8).split(",")}


def png(path):
    """(width, height, channels, rows of bytes) of an 8-bit RGB or RGBA PNG, or None."""
    if not os.path.exists(path):
        errors.append("no screenshot %s" % os.path.basename(path))
        return None
    raw = open(path, "rb").read()
    if raw[:8] != b"\x89PNG\r\n\x1a\n":
        errors.append("%s is not a PNG" % os.path.basename(path))
        return None
    pos, idat, w, h, ct = 8, b"", 0, 0, 0
    while pos < len(raw):
        n, kind = struct.unpack(">I4s", raw[pos:pos + 8])
        body = raw[pos + 8:pos + 8 + n]
        if kind == b"IHDR":
            w, h, depth, ct = struct.unpack(">IIBB", body[:10])
        elif kind == b"IDAT":
            idat += body
        pos += 12 + n
    if (w, h) != (800, 600):
        errors.append("%s is %dx%d, not 800x600" % (os.path.basename(path), w, h))
        return None
    ch = {2: 3, 6: 4}.get(ct)
    if ch is None:
        errors.append("%s has colour type %d" % (os.path.basename(path), ct))
        return None
    data = zlib.decompress(idat)
    stride = w * ch
    rows, prev = [], bytearray(stride)
    for y in range(h):
        f = data[y * (stride + 1)]
        cur = bytearray(data[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            left = cur[i - ch] if i >= ch else 0
            up = prev[i]
            ul = prev[i - ch] if i >= ch else 0
            if f == 1:
                cur[i] = (cur[i] + left) & 255
            elif f == 2:
                cur[i] = (cur[i] + up) & 255
            elif f == 3:
                cur[i] = (cur[i] + ((left + up) >> 1)) & 255
            elif f == 4:
                pa, pb, pc = abs(up - ul), abs(left - ul), abs(left + up - 2 * ul)
                pr = left if pa <= pb and pa <= pc else (up if pb <= pc else ul)
                cur[i] = (cur[i] + pr) & 255
        rows.append(cur)
        prev = cur
    return w, h, ch, rows


def px(img, x, y):
    return tuple(img[3][y][x * img[2]:x * img[2] + 3])


def region_diff(a_img, b_img, x0, y0, x1, y1):
    """Number of pixels in the box that differ by more than 24 in some channel."""
    n = 0
    for y in range(y0, y1):
        for x in range(x0, x1):
            if max(abs(p - q) for p, q in zip(px(a_img, x, y), px(b_img, x, y))) > 24:
                n += 1
    return n




def expect(cond, msg):
    if not cond:
        errors.append(msg)


def green(img, x0, y0, x1, y1):
    """Pixels in the box that are the avatar's green dress."""
    n = 0
    for y in range(y0, y1, 2):
        for x in range(x0, x1, 2):
            r, g, b = px(img, x, y)
            if g > r + 25 and g > b + 15:
                n += 1
    return n


# station points of Iquitos (zones/zones.xml): the stations she is sent to
S01, S12, S14 = (184, 371), (476, 520), (234, 491)
PRESS, MIXER = "click 500 480", "click 150 440"  # on the machines' art (the tank of the mixer, the weight of the press)

text = BOOT + DIARY + """wait 5350
dump
screenshot @stand@
move 500 480
wait 50
""" + PRESS + """
wait 50
move 150 440
wait 50
""" + MIXER + """
wait 50
move 500 480
wait 50
""" + PRESS + """
wait 50
dump
wait 450
dump
screenshot @mid@
wait 300
dump
wait 2200
dump
wait 6000
dump
screenshot @end@
""" + PRESS + """
wait 100
dump
wait 5000
dump
"""
out, d, paths = run("walk", text, shots=("stand", "mid", "end"))
if len(d) != 8:
    errors.append("walk: expected 8 window dumps, got %d" % len(d))
else:
    p = [player(x) for x in d]
    expect(all(p), "walk: no player in a dump: %s" % [bool(x) for x in p])
    if all(p):
        expect(abs(p[0]["x"] - S01[0]) < 0.5 and abs(p[0]["y"] - S01[1]) < 0.5, "walk: she does not stand on station01's point: %s" % p[0])
        expect(p[0]["face"] == 2 and not p[0]["moving"] and p[0]["queue"] == 0 and p[0]["dest"] == "station01" and p[0]["trail"] == [],
               "walk: the avatar at the start: %s" % p[0])
        # three clicks on the machines: one trip, two queued
        expect(p[1]["dest"] == "station12" and p[1]["queue"] == 2, "walk: the first trip and the queue: %s" % p[1])
        expect(p[2]["moving"] and p[2]["dest"] == "station12" and p[2]["queue"] == 2, "walk: the trip and the queue a little later: %s" % p[2])
        expect(S01[0] < p[2]["x"] < S12[0] and S01[1] < p[2]["y"] < S12[1], "walk: not between station01 and station12: %s" % p[2])
        expect(p[2]["face"] == 1, "walk: she does not face right on the way to station12: %s" % p[2])
        expect(p[3]["x"] > p[2]["x"] or p[3]["dest"] != "station12", "walk: she did not get on: %s -> %s" % (p[2], p[3]))
        expect(p[4]["queue"] <= 1 and p[4]["trail"][:1] == ["station12"], "walk: the queue is not running: %s" % p[4])
        # the end: every station reached in the order clicked, queue empty
        expect(p[5]["trail"] == ["station12", "station14", "station12"] and p[5]["queue"] == 0 and not p[5]["moving"],
               "walk: the trail after the queue ran: %s" % p[5])
        expect(abs(p[5]["x"] - S12[0]) < 0.5 and abs(p[5]["y"] - S12[1]) < 0.5, "walk: she did not stop on station12's point: %s" % p[5])
        # a click on the station she stands at: the arrival is at once, she has not moved
        expect(p[6]["trail"] == ["station12", "station14", "station12", "station12"] and not p[6]["moving"] and p[6]["queue"] == 0
               and abs(p[6]["x"] - S12[0]) < 0.5,
               "walk: a click on the station she stands at did not arrive in place: %s" % p[6])
        # the arrival turned her to the point's facing and half a second later she faces front again
        expect(p[7]["face"] == 2 and not p[7]["moving"], "walk: she does not face front after standing a while: %s" % p[7])
    imgs = {k: png(v) for k, v in paths.items()}
    if all(imgs.values()):
        # standing: the green dress at her spot (184, 371), the feet at the point, the body above it
        expect(green(imgs["stand"], 140, 280, 240, 392) > 250, "walk: no green dress at the start point (%d)" % green(imgs["stand"], 140, 280, 240, 392))
        # in motion she is somewhere along the line to station12, and the start spot is empty
        expect(green(imgs["mid"], 140, 280, 240, 392) < 110, "walk: the start spot is not empty once she left (the counter's own green trim counts about 75) (%d)" % green(imgs["mid"], 140, 280, 240, 392))
        expect(green(imgs["mid"], 220, 380, 480, 530) > 250, "walk: no green dress along the walk (%d)" % green(imgs["mid"], 220, 380, 480, 530))
        expect(region_diff(imgs["stand"], imgs["mid"], 140, 200, 240, 392) > 800, "walk: the standing and walking screenshots do not differ at the start spot")
        # at the end she stands at station12's point (476, 520), at the press
        expect(green(imgs["end"], 420, 400, 540, 525) > 250, "walk: no green dress at station12 (%d)" % green(imgs["end"], 420, 400, 540, 525))
        # the HUD and the background are unchanged by her: the register strip is still cream
        c = px(imgs["stand"], 720, 15)
        expect(c[0] > 200 and c[1] > 190 and c[2] > 150, "walk: the register strip is not cream: %s" % (c,))

for e in errors:
    print("FAIL:", e)
print("greatcc_walk: %s" % ("FAILED" if errors else "ok"))
sys.exit(1 if errors else 0)
