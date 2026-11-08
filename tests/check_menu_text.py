"""Window-system text slice check: the Reforged main menu shows its labels, button captions and copyright line.

Runs `expect-window ui/mainmenu.lua`, waits, takes screenshot-logical and screenshot at scale 1 and 2, and
  * checks --dump-windows for the label strings (the "#"-literal markup strings, the string-table copyright and the
    captions, which resolve through strings.xml),
  * counts near-white text pixels in the logical screen regions where the reference puts each caption and the
    copyright line, and none in a control region that holds no text.
"""
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
ap.add_argument("--workdir", required=True)
a = ap.parse_args()
os.makedirs(a.workdir, exist_ok=True)
errors = []

# Logical regions (x, y, w, h) of the 800x600 screen, from reference/menu-1280x720.png (screen = 160 + 1.2x, 1.2y).
TEXT_REGIONS = {
    "copyright": (230, 582, 340, 16),
    "story_mode": (240, 385, 140, 22),
    "help": (180, 489, 90, 22),
    "community": (330, 509, 140, 22),
    "options": (480, 489, 90, 22),
    "save_quit": (600, 451, 130, 22),
}
CONTROL_REGION = (20, 250, 150, 60)
DUMP_PATTERNS = [
    r'Text "copyright" id=\d+ rect=0,580,800,20 label="Copyright [^"]*"',  # the publisher's line; shape only
    r'label="Copyright [^"]*PlayFirst',
    r'label="<outline color=\'000000\' size=2>Story Mode"',
    r'label="<outline color=\'000000\' size=2>Help"',
    r'label="<outline color=\'000000\' size=2>Community"',
    r'label="<outline color=\'000000\' size=2>Options"',
    r'label="<outline color=\'000000\' size=2>Save & Quit"',
]


def white_pixels(img, region, k):
    w, h, bpp, rows = img
    x0, y0, rw, rh = region
    n = 0
    for y in range(y0 * k, (y0 + rh) * k):
        row = rows[y]
        for x in range(x0 * k, (x0 + rw) * k):
            p = row[x * bpp:x * bpp + 3]
            if p[0] > 200 and p[1] > 200 and p[2] > 200:
                n += 1
    return n


for scale in (1, 2):
    work = os.path.join(a.workdir, "scale%d" % scale)
    os.makedirs(work, exist_ok=True)
    script = os.path.join(work, "menu.script")
    logical, shot = os.path.join(work, "logical.png"), os.path.join(work, "shot.png")
    for f in (logical, shot):
        if os.path.exists(f):
            os.remove(f)
    with open(script, "w") as f:
        f.write("expect-window ui/mainmenu.lua 60000\nwait 1500\nscreenshot-logical %s\nscreenshot %s\nquit\n" % (logical, shot))
    r = subprocess.run([a.openconche, "--headless", "--scale", str(scale), "--assets", a.assets,
                        "--userdir", os.path.join(work, "user"), "--script", script, "--dump-windows"],
                       cwd=work, capture_output=True, text=True, timeout=120)
    if r.returncode != 0:
        errors.append("scale %d: exit %d, tail %r" % (scale, r.returncode, r.stdout[-400:]))
        continue
    if "[lua-error]" in r.stdout:
        errors.append("scale %d: [lua-error] in the log" % scale)
    if scale == 1:
        for pat in DUMP_PATTERNS:
            if not re.search(pat, r.stdout):
                errors.append("dump lacks %s:\n%s" % (pat, r.stdout[-1800:]))
    lg, sh = read_png(logical), read_png(shot)
    if (lg[0], lg[1]) != (800, 600) or (sh[0], sh[1]) != (800 * scale, 600 * scale):
        errors.append("scale %d: unexpected screenshot sizes" % scale)
        continue
    for name, region in TEXT_REGIONS.items():
        for label, img, k in (("logical", lg, 1), ("screenshot", sh, scale)):
            n = white_pixels(img, region, k)
            need = 40 * k * k
            if n < need:
                errors.append("scale %d %s: only %d near-white pixels in the %s region (need %d)" % (scale, label, n, name, need))
    for label, img, k in (("logical", lg, 1), ("screenshot", sh, scale)):
        n = white_pixels(img, CONTROL_REGION, k)
        if n > 0:
            errors.append("scale %d %s: %d near-white pixels in the control region" % (scale, label, n))

if errors:
    print("FAIL:\n  " + "\n  ".join(errors))
    sys.exit(1)
print("ok: menu labels, captions and copyright are drawn at scale 1 and 2")
