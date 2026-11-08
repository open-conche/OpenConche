"""Runs openconche headless with and without --test-text and checks that the text appears only where it should.

The frames with and without the text scene are compared pixel by pixel: every differing pixel must lie inside the
expected text boxes, and each box must hold enough of them (the paragraph must also have wrapped onto several lines).
"""
import argparse, os, shutil, struct, subprocess, sys, zlib


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



def letterbox(W, H):
    s = min(W / 800, H / 600)
    return s, (W - 800 * s) / 2, (H - 600 * s) / 2


TITLE = (80, 20, 640, 120)
PARA = (100, 370, 600, 150)
LABEL = (260, 160, 280, 60)


def run(a, name, text):
    wd = os.path.join(a.workdir, name)
    os.makedirs(wd, exist_ok=True)
    open(os.path.join(wd, "t.script"), "w").write("wait 30\nscreenshot scene.png\nscreenshot-logical logical.png\nquit\n")
    cmd = [a.openconche, "--headless", "--userdir", os.path.join(a.workdir, "user"), "--script", "t.script", "--assets", a.assets]
    if a.mode == "windowed":
        cmd += ["--scale", a.scale]
        W, H = 800 * int(a.scale), 600 * int(a.scale)
    else:
        cmd += ["--fullscreen", "--display-size", a.display_size]
        W, H = (int(v) for v in a.display_size.split("x"))
    if text:
        cmd.append("--test-text")
    r = subprocess.run(cmd, cwd=wd, capture_output=True, text=True, timeout=120)
    print(r.stdout[-600:], r.stderr[-600:])
    if r.returncode != 0:
        raise SystemExit("openconche failed (%d)" % r.returncode)
    return wd, W, H


def luma_rows(rows, bpp, x0, y0, w, h):
    """The region as rows of luminance values."""
    out = []
    for y in range(y0, y0 + h):
        row = rows[y]
        out.append([(299 * row[x * bpp] + 587 * row[x * bpp + 1] + 114 * row[x * bpp + 2]) // 1000 for x in range(x0, x0 + w)])
    return out


def stretch2x(lum, bilinear):
    """What a stretched 800x600 frame looks like at 2x: each source pixel becomes two columns, either repeated (nearest)
    or interpolated at the quarter-pixel centres a GPU uses (bilinear: 3/4 of a pixel and 1/4 of its neighbour)."""
    out = []
    for row in lum:
        n = len(row)
        line = []
        for k in range(n):
            if bilinear:
                l, r = row[max(0, k - 1)], row[min(n - 1, k + 1)]
                line += [(3 * row[k] + l) // 4, (3 * row[k] + r) // 4]
            else:
                line += [row[k], row[k]]
        out.append(line)
    return out


def steps(lum, threshold, inside_pairs_only=False):
    """Counts horizontal luminance steps larger than the threshold between a pixel and its right neighbour. With
    inside_pairs_only, only the steps between the two columns of a pair (x = 2k to 2k+1) count."""
    n = 0
    for row in lum:
        for x in range(len(row) - 1):
            if inside_pairs_only and x % 2:
                continue
            if abs(row[x + 1] - row[x]) > threshold:
                n += 1
    return n


def resize_main(a):
    """The window grows from 800x600 to 1600x1200 while the text scene is up: the text must be rasterised again at the
    new size (sharp), not the 800x600 picture stretched (nearest or bilinear) to 1600x1200."""
    wd = os.path.join(a.workdir, "resize")
    os.makedirs(wd, exist_ok=True)
    for f in ("one.png", "two.png"):
        if os.path.exists(os.path.join(wd, f)):
            os.remove(os.path.join(wd, f))
    open(os.path.join(wd, "t.script"), "w").write(
        "wait 30\nscreenshot one.png\nresize 1600 1200\nscreenshot two.png\nquit\n")
    cmd = [a.openconche, "--headless", "--userdir", os.path.join(a.workdir, "user"), "--script", "t.script",
           "--assets", a.assets, "--scale", "1", "--test-text"]
    r = subprocess.run(cmd, cwd=wd, capture_output=True, text=True, timeout=120)
    print(r.stdout[-600:], r.stderr[-600:])
    if r.returncode != 0:
        raise SystemExit("openconche failed (%d)" % r.returncode)
    errors = []
    shots = {}
    for name, size in (("one.png", (800, 600)), ("two.png", (1600, 1200))):
        w, h, bpp, rows = read_png(os.path.join(wd, name))
        if (w, h) != size:
            errors.append("%s is %dx%d, expected %dx%d" % (name, w, h, size[0], size[1]))
            continue
        k = size[0] // 800
        shots[name] = luma_rows(rows, bpp, PARA[0] * k, PARA[1] * k, PARA[2] * k, PARA[3] * k)
    if len(shots) == 2:
        one, two = shots["one.png"], shots["two.png"]
        # The discriminator: a "strong" step is more than 60% of the sharpest step in the 800x600 shot. Stretching that
        # shot to 2x cannot put a strong step inside a column pair: nearest repeats the column (step 0) and bilinear
        # leaves at most half the sharpest step there. Text rasterised at 2x has its edges on both column parities.
        sharpest = max(abs(r[x + 1] - r[x]) for r in one for x in range(len(r) - 1))
        strong = int(sharpest * 0.6)
        n1 = steps(one, strong)
        n2 = steps(two, strong)
        inside2 = steps(two, strong, True)
        fake = {name: steps(stretch2x(one, bilinear), strong, True) for name, bilinear in (("nearest", False), ("bilinear", True))}
        print("one.png: sharpest step %d, %d strong steps (> %d) in the paragraph box" % (sharpest, n1, strong))
        print("two.png: %d strong steps, %d of them inside column pairs; a stretched one.png would have %s" % (n2, inside2, fake))
        if sharpest < 100 or n1 < 200:
            errors.append("one.png: sharpest step %d, %d strong steps: the text is missing or faint" % (sharpest, n1))
        if any(v != 0 for v in fake.values()):
            errors.append("the metric is broken: a stretched one.png shows strong steps inside column pairs: %s" % fake)
        if inside2 < 0.25 * n1:
            errors.append("two.png: %d strong steps inside column pairs (one.png has %d strong steps): the 2x picture "
                          "looks like a stretched 800x600 frame, not text rasterised at 2x" % (inside2, n1))
        if n2 <= n1:
            errors.append("two.png has %d strong steps, not more than one.png's %d: no extra detail at 2x" % (n2, n1))
    if "[display] resized, output 1600x1200, scale 2.000" not in r.stdout:
        errors.append("no [display] resized line for 1600x1200")
    if errors:
        print("FAIL:\n  " + "\n  ".join(errors))
        return 1
    print("OK")
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--openconche", required=True)
    ap.add_argument("--mode", choices=["windowed", "fullscreen", "resize"], required=True)
    ap.add_argument("--scale", default="1")
    ap.add_argument("--display-size", default="2560x1440")
    ap.add_argument("--reforged", required=True, help="Reforged assets folder holding fonts/choco3.mvec and fertigo.mvec")
    ap.add_argument("--workdir", required=True)
    a = ap.parse_args()
    fonts = [os.path.join(a.reforged, "fonts", f) for f in ("choco3.mvec", "fertigo.mvec")]
    if not all(os.path.exists(f) for f in fonts):
        print("SKIP: Reforged fonts not found under", a.reforged)
        return 77
    a.assets = os.path.join(a.workdir, "assets")
    os.makedirs(os.path.join(a.assets, "fonts"), exist_ok=True)
    for f in fonts:
        shutil.copy(f, os.path.join(a.assets, "fonts"))
    if a.mode == "resize":
        return resize_main(a)
    base, W, H = run(a, "base", False)
    with_, _, _ = run(a, "text", True)
    s, x0, y0 = letterbox(W, H)
    errors = []
    for shot, size, (ox, oy, k) in (("scene.png", (W, H), (round(x0), round(y0), s)), ("logical.png", (800, 600), (0, 0, 1.0))):
        w1, h1, b1, r1 = read_png(os.path.join(base, shot))
        w2, h2, b2, r2 = read_png(os.path.join(with_, shot))
        if (w1, h1) != size or (w2, h2) != size:
            errors.append("%s has size %dx%d / %dx%d, expected %s" % (shot, w1, h1, w2, h2, size))
            continue
        margin = 4 * k
        boxes = {"title": TITLE, "para": PARA, "label": LABEL}
        counts = {n: 0 for n in boxes}
        bbox = {n: [1e9, 1e9, -1, -1] for n in boxes}
        bands = set()
        stray = 0
        first_stray = None
        for y in range(h1):
            ra, rb = r1[y], r2[y]
            if ra == rb:
                continue
            for x in range(w1):
                if ra[x * b1:x * b1 + 3] == rb[x * b2:x * b2 + 3]:
                    continue
                lx, ly = (x - ox) / k, (y - oy) / k
                hit = None
                for n, (bx, by, bw, bh) in boxes.items():
                    if bx - margin <= lx < bx + bw + margin and by - margin <= ly < by + bh + margin:
                        hit = n
                if hit is None:
                    stray += 1
                    first_stray = first_stray or (x, y)
                    continue
                counts[hit] += 1
                bb = bbox[hit]
                bb[0], bb[1], bb[2], bb[3] = min(bb[0], lx), min(bb[1], ly), max(bb[2], lx), max(bb[3], ly)
                if hit == "para":
                    bands.add(int((ly - PARA[1]) // 25))
        if stray:
            errors.append("%s: %d text pixels outside the boxes (first at %s)" % (shot, stray, first_stray))
        for n, minimum in (("title", 600), ("para", 1500), ("label", 2000)):
            if counts[n] < minimum * k * k:
                errors.append("%s: only %d pixels changed in the %s box, expected at least %d" % (shot, counts[n], n, minimum * k * k))
        bb = bbox["title"]
        if counts["title"]:
            if bb[2] - bb[0] < 300 or abs((bb[0] + bb[2]) / 2 - 400) > 30:
                errors.append("%s: title ink spans x %.0f..%.0f, expected a centred run wider than 300" % (shot, bb[0], bb[2]))
            if bb[1] < TITLE[1] or bb[3] > TITLE[1] + TITLE[3]:
                errors.append("%s: title ink y %.0f..%.0f leaves its box" % (shot, bb[1], bb[3]))
        if len(bands) < 3:
            errors.append("%s: paragraph covers %d vertical bands, expected it to wrap onto at least 3" % (shot, len(bands)))
        if shot == "scene.png" and a.mode == "windowed" and int(a.scale) == 2:
            # Text drawn at logical resolution and stretched 2x would repeat every pixel pair; at device resolution
            # the partly covered edge pixels of neighbouring columns almost never match.
            partial = pairs = 0
            for y in range(int((PARA[1] + 5) * k), int((PARA[1] + PARA[3] - 5) * k)):
                rb = r2[y]
                for x in range(int((PARA[0]) * k), int((PARA[0] + PARA[2]) * k) - 1):
                    v = rb[x * b2]
                    if 40 < v < 215:
                        partial += 1
                        pairs += rb[x * b2:x * b2 + 3] == rb[(x + 1) * b2:(x + 1) * b2 + 3]
            frac = pairs / max(1, partial)
            print("device-resolution check: %d partial pixels, %.1f%% equal to their right neighbour" % (partial, frac * 100))
            if partial < 2000 or frac > 0.2:
                errors.append("text does not look rasterised at device resolution (%.0f%% repeated partial pixels)" % (frac * 100))
        print(shot, counts, "bands", sorted(bands), "stray", stray)
    if errors:
        print("FAIL:\n  " + "\n  ".join(errors))
        return 1
    print("OK")
    return 0


sys.exit(main())
