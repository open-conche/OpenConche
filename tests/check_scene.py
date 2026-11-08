"""Runs openconche headless on a script and checks the screenshot and the logical coordinates it received."""
import argparse, math, os, re, struct, subprocess, sys, zlib


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
    """SDL3's LETTERBOX rule: full width or height, the other side floored to whole pixels and centred. Returns the
    horizontal output scale (w / 800) and the rectangle; the vertical scale is h / 600."""
    if abs(800 / 600 - W / H) < 0.0001:
        x, y, w, h = 0, 0, W, H
    elif 800 / 600 > W / H:
        w, h = W, math.floor(600 * W / 800)
        x, y = 0, (H - h) / 2
    else:
        w, h = math.floor(800 * H / 600), H
        x, y = (W - w) / 2, 0
    return w / 800, x, y, w, h


SQUARES = [((10, 10), (220, 40, 40)), ((740, 10), (40, 200, 60)),
           ((10, 540), (50, 90, 230)), ((740, 540), (240, 220, 40))]
BG, BLACK = (24, 24, 40), (0, 0, 0)


def click_lines(W, H):
    """click-window commands at positions predicted from the letterbox rectangle of a WxH output: the centre and two
    other logical points (in area), then two bar clicks (outside)."""
    s, x0, y0, bw, bh = letterbox(W, H)
    lines = ["click-window %.4f %.4f" % (x0 + lx * s, y0 + ly * bh / 600) for lx, ly in [(400, 300), (100, 50), (700, 500)]]
    return lines + ["click-window 3 3", "click-window %d %d" % (W - 3, H - 3)]


def check_letterboxed(px, W, H, errors, what=""):
    """The test scene on a WxH output: squares, centre and background at their predicted places, black bars."""
    def check(x, y, c, name):
        if px(x, y) != c:
            errors.append("%s%s: pixel (%d,%d) is %s, expected %s" % (what, name, x, y, px(x, y), c))
    s, x0, y0, bw, bh = letterbox(W, H)
    ix0, iy0 = round(x0), round(y0)
    sy = bh / 600
    for (lx, ly), c in SQUARES:
        check(round(ix0 + (lx + 25) * s), round(iy0 + (ly + 25) * sy), c, "square")
    check(round(ix0 + 400 * s), round(iy0 + 300 * sy), (200, 120, 40), "centre")
    check(round(ix0 + 100 * s), round(iy0 + 100 * sy), BG, "background")
    for y in (0, 1, H // 4, H // 2, H - 2, H - 1):
        for x in range(0, ix0 - 1, max(1, ix0 // 3 or 1)):
            check(x, y, BLACK, "left bar")
        for x in range(ix0 + round(bw) + 1, W, max(1, (W - ix0 - round(bw)) // 3 or 1)):
            check(x, y, BLACK, "right bar")
    for x in (0, W // 2, W - 1):
        for y in range(0, iy0 - 1, max(1, iy0 // 3 or 1)):
            check(x, y, BLACK, "top bar")
        for y in range(iy0 + round(bh) + 1, H, max(1, (H - iy0 - round(bh)) // 3 or 1)):
            check(x, y, BLACK, "bottom bar")
    if ix0 > 0 and x0 == ix0:  # a half-pixel offset has no exact edge to probe
        check(10, H // 2, BLACK, "bar x=10")
        check(W - 10, H // 2, BLACK, "bar x=W-10")
        check(ix0 - 1, H // 2, BLACK, "bar edge")
        check(ix0 + round(bw), H // 2, BLACK, "bar edge")
        check(ix0 + round(5 * s), H // 2, BG, "scene edge")
        check(ix0 + round(bw) - round(5 * s), H // 2, BG, "scene edge")
    if ix0 == 320:
        check(319, 720, BLACK, "bar edge")
        check(320, 720, BG, "scene edge")
        check(2239, 720, BG, "scene edge")
        check(2240, 720, BLACK, "bar edge")


def load_png(workdir, name, size, errors):
    p = os.path.join(workdir, name)
    if not os.path.exists(p):
        errors.append("no %s written" % name)
        return None
    w, h, bpp, rows = read_png(p)
    if (w, h) != size:
        errors.append("%s is %dx%d, expected %dx%d" % (name, w, h, size[0], size[1]))
        return None
    return lambda x, y: tuple(rows[y][x * bpp:x * bpp + 3])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--openconche", required=True)
    ap.add_argument("--mode", choices=["windowed", "fullscreen", "resize"], required=True)
    ap.add_argument("--scale", default="1")
    ap.add_argument("--display-size", default="2560x1440")
    ap.add_argument("--script")
    ap.add_argument("--gfx", action="store_true", help="also probe the textured/blended/clipped test scene")
    ap.add_argument("--assets")
    ap.add_argument("--workdir", required=True)
    a = ap.parse_args()
    os.makedirs(a.workdir, exist_ok=True)
    for f in ("scene.png", "logical.png", "scene2x.png", "back.png"):
        if os.path.exists(os.path.join(a.workdir, f)):
            os.remove(os.path.join(a.workdir, f))
    errors = []
    if a.mode == "resize":
        # A resizable window: dragged to 1600x1000 (bars left and right), then 1600x1200 (4:3, no bars), then back.
        W, H = 1600, 1000
        cmd = [a.openconche, "--headless", "--userdir", os.path.join(a.workdir, "user"), "--scale", "1", "--no-assets",
               "--script", "resize.script"]
        # The click queued right before a resize is still mapped with the old size (1600x1000); the ones after each
        # resize with the new one (1600x1200 is exactly 2x, 800x600 is 1x).
        s, x0, y0, bw, bh = letterbox(W, H)
        lines = (["wait 50", "click 400 300", "resize %d %d" % (W, H)] + click_lines(W, H) +
                 ["screenshot scene.png", "click-window %.4f %.4f" % (x0 + 700 * s, y0 + 500 * bh / 600),
                  "resize 1600 1200", "click-window 200 100", "screenshot scene2x.png", "resize 800 600",
                  "click-window 400 300", "screenshot back.png"])
        open(os.path.join(a.workdir, "resize.script"), "w").write("\n".join(lines) + "\n")
    elif a.mode == "windowed":
        cmd = [a.openconche, "--headless", "--userdir", os.path.join(a.workdir, "user"), "--scale", a.scale, "--script", a.script]
        if not a.assets: cmd.append("--no-assets")
        if a.gfx:
            cmd += ["--test-scene", "--assets", a.assets]
        W, H = 800 * int(a.scale), 600 * int(a.scale)
        inputs = []
    else:
        W, H = (int(v) for v in a.display_size.split("x"))
        cmd = [a.openconche, "--headless", "--userdir", os.path.join(a.workdir, "user"), "--fullscreen", "--display-size", a.display_size, "--script", "fs.script"]
        if not a.assets: cmd.append("--no-assets")
        if a.gfx:
            cmd += ["--test-scene", "--assets", a.assets]
        # Window coordinates computed here from the known letterbox rectangle, not by the engine.
        lines = ["wait 50", "click 400 300"] + click_lines(W, H) + ["screenshot scene.png", "screenshot-logical logical.png"]
        open(os.path.join(a.workdir, "fs.script"), "w").write("\n".join(lines) + "\n")
    # [input] lines go to the log file only, and mouse-move only with --log-input.
    logpath = os.path.join(a.workdir, "scene_input.log")
    cmd += ["--log", logpath, "--log-input"]
    r = subprocess.run(cmd, cwd=a.workdir, capture_output=True, text=True, timeout=60)
    logtext = open(logpath, encoding="utf-8").read() if os.path.exists(logpath) else ""
    if "[input]" in r.stdout:
        errors.append("[input] lines reached stdout")
    print(r.stdout, r.stderr)
    if r.returncode != 0:
        errors.append("exit code %d" % r.returncode)

    def load(name, size):
        return load_png(a.workdir, name, size, errors)

    px = load("scene.png", (W, H))
    squares = SQUARES

    def check(px, x, y, c, what):
        if px(x, y) != c:
            errors.append("%s: pixel (%d,%d) is %s, expected %s" % (what, x, y, px(x, y), c))

    s, x0, y0, bw, bh = letterbox(W, H)
    ix0, iy0 = round(x0), round(y0)
    if px:
        # Scene geometry (and bars) predicted from the letterbox rectangle.
        check_letterboxed(px, W, H, errors)
        if a.mode == "windowed" and a.scale == "1":
            for (x, y), c in {(1, 300): (255, 255, 255), (798, 300): (255, 255, 255), (400, 1): (255, 255, 255),
                              (9, 9): BG, (10, 10): (220, 40, 40), (59, 59): (220, 40, 40), (60, 60): BG}.items():
                check(px, x, y, c, "scale 1")
        if a.mode == "windowed":
            k = int(a.scale)
            # edges of the red square sit at doubled positions
            check(px, 10 * k - 1, 10 * k, BG, "square edge")
            check(px, 10 * k, 10 * k, (220, 40, 40), "square edge")
            check(px, 60 * k - 1, 60 * k - 1, (220, 40, 40), "square edge")
            check(px, 60 * k, 60 * k, BG, "square edge")
    if a.gfx and px:
        k = s  # output pixels per logical pixel (the letterbox scale in fullscreen)
        lp = load("logical.png", (800, 600))

        def blend(bg, c, al):
            return tuple(round(b * (1 - al) + v * al) for b, v in zip(bg, c))

        def near(img, scale, lx, ly, want, tol, what):
            ox, oy = (ix0, iy0) if img is px else (0, 0)
            got = img(int(ox + (lx + 0.5) * scale), int(oy + (ly + 0.5) * scale))
            if any(abs(g - w) > tol for g, w in zip(got, want)):
                errors.append("%s: logical (%d,%d) is %s, expected %s +-%d" % (what, lx, ly, got, want, tol))

        al = (36 * 4 + 3) / 255
        probes = [
            (136, 404, blend(BG, (255, 0, 0), al), 4, "checker alpha red"),
            (112, 404, blend(BG, (0, 0, 255), (12 * 4 + 3) / 255), 4, "checker alpha blue"),
            (127, 484, blend(BG, (255, 0, 0), al), 4, "flipped checker"),
            (216, 416, (220, 40, 40), 14, "jpg quadrant"), (248, 416, (40, 200, 60), 14, "jpg quadrant"),
            (216, 448, (50, 90, 230), 14, "jpg quadrant"), (248, 448, (240, 220, 40), 14, "jpg quadrant"),
            (320, 420, (255, 128, 0), 3, "tint"),
            (370, 420, blend(BG, (255, 255, 255), 0.5), 3, "half alpha"),
            (320, 470, (124, 74, 40), 2, "additive"),
            (370, 470, (12, 12, 20), 2, "multiply"),
            (420, 470, (20, 10, 0), 2, "subtract (software renderer rejects custom modes: falls back to Normal)"),
            (470, 470, (124, 74, 40), 2, "additive geometry"),
            (520, 470, (124, 74, 40), 2, "additive texture"),
            (630, 430, (0, 255, 0), 1, "inside both clips"),
            (610, 430, (255, 0, 255), 1, "inside outer clip only"),
            (610, 410, BG, 1, "outside inner clip"),
            (645, 430, BG, 1, "right of clips"),
            (630, 445, BG, 1, "below clips"),
            (590, 430, BG, 1, "left of clips"),
        ]
        for lx, ly, want, tol, what in probes:
            near(px, k, lx, ly, want, tol, what)
            if lp:
                near(lp, 1, lx, ly, want, tol + (3 if k != 1 else 0), what + " (logical shot)")
        # clip edges are exact at integer scales
        for lx, ly, want, what in [] if a.mode != "windowed" else [(620, 420, (0, 255, 0), "clip corner in"), (639, 439, (0, 255, 0), "clip corner in"),
                                   (640, 439, BG, "clip right edge"), (639, 440, BG, "clip bottom edge"),
                                   (619, 439, (255, 0, 255), "outer clip inner edge"), (619, 440, BG, "outer clip bottom")]:
            near(px, k, lx, ly, want, 1, what)
    if a.mode in ("fullscreen", "resize"):
        if a.mode == "fullscreen":
            lp = load("logical.png", (800, 600))
            if lp:
                for (lx, ly), c in squares:
                    check(lp, lx + 25, ly + 25, c, "logical square")
                check(lp, 400, 300, (200, 120, 40), "logical centre")
                check(lp, 5, 5, BG, "logical corner")
        # Clicks: window coordinates were computed above; the engine must map them back.
        got = [tuple(map(float, m)) for m in re.findall(r"mouse-down (\S+) (\S+) button 1(?! \(outside\))", logtext)]
        want = [(400, 300), (400, 300), (100, 50), (700, 500)]
        if a.mode == "resize":
            want += [(700, 500), (100, 50), (400, 300)]  # before the second resize, after it, and back at 800x600
        if len(got) != len(want) or any(abs(g[0] - w[0]) > 0.05 or abs(g[1] - w[1]) > 0.05 for g, w in zip(got, want)):
            errors.append("in-area clicks logged %s, expected %s" % (got, want))
        outs = re.findall(r"mouse-down \S+ \S+ button 1 \(outside\)", logtext)
        if len(outs) != 2:
            errors.append("expected 2 outside clicks (window 3,3 and W-3,H-3), saw %d" % len(outs))
        if "[screenshot] scene.png %dx%d" % (W, H) not in r.stdout:
            errors.append("screenshot log lacks size")
    if a.mode == "resize":
        # Three sizes: the picture follows the window (never the other way round), bars appear and disappear.
        px2 = load("scene2x.png", (1600, 1200))
        if px2:
            check_letterboxed(px2, 1600, 1200, errors, "1600x1200: ")
            # the red square's edges at exactly doubled positions, no bars
            for (x, y), c in {(19, 20): BG, (20, 20): (220, 40, 40), (119, 119): (220, 40, 40), (120, 120): BG,
                              (2, 600): (255, 255, 255), (1596, 600): (255, 255, 255)}.items():
                check(px2, x, y, c, "1600x1200")
        pxb = load("back.png", (800, 600))
        if pxb:
            check_letterboxed(pxb, 800, 600, errors, "800x600: ")
            for (x, y), c in {(9, 9): BG, (10, 10): (220, 40, 40), (59, 59): (220, 40, 40), (60, 60): BG}.items():
                check(pxb, x, y, c, "back at 800x600")
        order = [r.stdout.find("[display] resized, output %s, scale %s" % o) for o in
                 (("1600x1000", "1.666"), ("1600x1200", "2.000"), ("800x600", "1.000"))]
        if any(i < 0 for i in order) or order != sorted(order):
            errors.append("[display] resized lines missing or out of order: %s" % order)
        if len(re.findall(r"\[display\] resized", r.stdout)) != 3:
            errors.append("expected exactly 3 [display] resized lines")
        if "[display] fullscreen" in r.stdout:
            errors.append("a resize reported fullscreen")
    elif a.mode == "windowed" and not a.gfx:
        for pat in (r"\[input\] mouse-down 400 300 button 1", r"\[input\] mouse-up 400 300 button 1",
                    r"\[input\] mouse-move 12\.5 30", r"\[display\] fullscreen on, output 2560x1440",
                    r"\[display\] fullscreen off, output %dx%d" % (W, H),
                    r"\[input\] key Escape"):
            if not re.search(pat, logtext):
                errors.append("log lacks /%s/" % pat)
        if "[input] key Return" in logtext or "[input] key F11" in logtext:
            errors.append("fullscreen hotkeys leaked to the app")
        on, off = logtext.find("fullscreen on"), logtext.find("fullscreen off")
        if not (0 <= on < off < logtext.find("key Escape")):
            errors.append("display changes not logged in event order with [input] lines")
    if errors:
        print("FAIL:\n  " + "\n  ".join(errors))
        return 1
    print("OK")
    return 0


sys.exit(main())
