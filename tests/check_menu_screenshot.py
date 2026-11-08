"""Window-system slices 2 to 4 check: the Reforged main menu draws its background and bitmaps headless.

Runs `expect-window ui/mainmenu.lua`, waits 1300 ms of simulated time (the menu fades in over 1 s), takes screenshot-logical and screenshot, at
scale 1 and scale 2, and probes pixels:
  * flat background regions against reference/menu-native-crop.png (a full-resolution crop of the real menu; logical
    point (x, y) is at ((x - 200) * 2.4, (y - 317) * 2.4) in it),
  * the logo and the corners against reference/menu-1280x720.png (the 800x600 screen scaled by 1.2 and centred, so
    logical (x, y) is at (160 + 1.2x, 1.2y)),
  * the Free Play silhouette, which shows because the profile folder is empty, against its known grey.
It also checks that the dump shows resolved rectangles in parent space."""
import argparse, os, re, struct, subprocess, sys, zlib


def read_png(path):
    data = open(path, "rb").read()
    assert data[:8] == b"\x89PNG\r\n\x1a\n", "not a PNG"
    pos, idat, w = 8, b"", None
    while pos < len(data):
        n, typ = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        pos += 12 + n
        if typ == b"IHDR":
            w, h, depth, ctype = struct.unpack(">IIBB", body[:10])
            assert depth == 8 and ctype in (2, 6), "unsupported PNG format"
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
            a = line[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if f == 1: line[i] = (line[i] + a) & 255
            elif f == 2: line[i] = (line[i] + b) & 255
            elif f == 3: line[i] = (line[i] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pr = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 255
        rows.append(line)
        prev = line
    return w, h, bpp, rows



ap = argparse.ArgumentParser()
ap.add_argument("--openconche", required=True)
ap.add_argument("--assets", required=True)
ap.add_argument("--reference", required=True)
ap.add_argument("--workdir", required=True)
a = ap.parse_args()
os.makedirs(a.workdir, exist_ok=True)
errors = []


def px(img, x, y):
    w, h, bpp, rows = img
    x, y = int(x), int(y)
    return tuple(rows[y][x * bpp:x * bpp + 3])


def close(p, q, tol):
    return all(abs(i - j) <= tol for i, j in zip(p, q))


crop = read_png(os.path.join(a.reference, "menu-native-crop.png"))
full = read_png(os.path.join(a.reference, "menu-1280x720.png"))

BACKGROUND_PROBES = [(250, 330), (400, 380), (320, 500), (300, 420)]
LOGO_PROBES = [(300, 60), (120, 70), (660, 100), (60, 40), (250, 185), (500, 170), (740, 230), (40, 230)]
# The enabled buttons now draw their art (Story, Help, Community, Options; Free Play and Change Player are
# silhouettes without a profile). Points on the art above the label lines, against the real menu.
BUTTON_PROBES = [(310, 345), (296, 335), (324, 335), (310, 320), (250, 455), (264, 445), (250, 430), (400, 475),
                 (386, 465), (414, 465), (400, 450), (550, 455), (536, 445), (564, 445), (550, 430)]
SILHOUETTE = (480, 350, (33, 33, 33))  # inside the Free Play silhouette

for scale in (1, 2):
    work = os.path.join(a.workdir, "scale%d" % scale)
    os.makedirs(work, exist_ok=True)
    script = os.path.join(work, "menu.script")
    logical, shot = os.path.join(work, "logical.png"), os.path.join(work, "shot.png")
    for f in (logical, shot):
        if os.path.exists(f):
            os.remove(f)
    with open(script, "w") as f:
        f.write("expect-window ui/mainmenu.lua 60000\nwait 1300\nscreenshot-logical %s\nscreenshot %s\nquit\n" % (logical, shot))
    r = subprocess.run([a.openconche, "--headless", "--scale", str(scale), "--assets", a.assets,
                        "--userdir", os.path.join(work, "user"), "--script", script, "--dump-windows"],
                       cwd=work, capture_output=True, text=True, timeout=120)
    if r.returncode != 0:
        errors.append("scale %d: exit %d, tail %r" % (scale, r.returncode, r.stdout[-400:]))
        continue
    if "[lua-error]" in r.stdout:
        errors.append("scale %d: [lua-error] in the log" % scale)
    if scale == 1:
        for pat in (r'Bitmap "main_background" id=\d+ rect=0,0,800,600 ',
                    r'Bitmap "game_logo" id=\d+ rect=0,0,800,281 ',
                    r'Bitmap "free_play_disable" id=\d+ rect=415,270,150,150 ',
                    r'Bitmap "choose_player_disable" id=\d+ rect=72,372,90,90 ',
                    r'Window "buttons" id=\d+ rect=0,0,800,600'):
            if not re.search(pat, r.stdout):
                errors.append("dump lacks %s:\n%s" % (pat, r.stdout[-1500:]))
    lg = read_png(logical)
    sh = read_png(shot)
    if (lg[0], lg[1]) != (800, 600):
        errors.append("logical screenshot is %dx%d" % (lg[0], lg[1]))
    if (sh[0], sh[1]) != (800 * scale, 600 * scale):
        errors.append("screenshot is %dx%d at scale %d" % (sh[0], sh[1], scale))
        continue
    for (lx, ly) in BACKGROUND_PROBES:
        want = px(crop, (lx - 200 + 0.5) * 2.4, (ly - 317 + 0.5) * 2.4)
        for name, img, k in (("logical", lg, 1), ("screenshot", sh, scale)):
            got = px(img, lx * k + k // 2, ly * k + k // 2)
            if not close(got, want, 14):
                errors.append("scale %d %s (%d,%d): got %s, the real menu has %s" % (scale, name, lx, ly, got, want))
    for (lx, ly) in LOGO_PROBES:
        want = px(full, 160 + 1.2 * lx + 0.6, 1.2 * ly + 0.6)
        got = px(lg, lx, ly)
        if not close(got, want, 24):
            errors.append("scale %d logical logo probe (%d,%d): got %s, the real menu has %s" % (scale, lx, ly, got, want))
    for (lx, ly) in BUTTON_PROBES:
        want = px(full, 160 + 1.2 * lx + 0.6, 1.2 * ly + 0.6)
        got = px(lg, lx, ly)
        if not close(got, want, 16):
            errors.append("scale %d logical button probe (%d,%d): got %s, the real menu has %s" % (scale, lx, ly, got, want))
    sx, sy, want = SILHOUETTE
    if not close(px(lg, sx, sy), want, 12):
        errors.append("scale %d: the Free Play silhouette probe is %s, expected about %s" % (scale, px(lg, sx, sy), want))

if errors:
    print("FAIL:\n  " + "\n  ".join(errors))
    sys.exit(1)
print("ok: menu background, logo, button art and silhouettes match the reference at scale 1 and 2")
