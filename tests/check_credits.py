"""CreditsWindow and the `.lua` require fallback, played headless against the real game data.

  --case credits   Main menu -> Options -> Credits. Two logical screenshots well past the intro pause differ in the credits
                   rectangle (the text scrolled), both show dark text in the left and the right half of it, and neither
                   run logs a Lua error. Escape closes the dialog (back to the Options modal).
  --case history   Reforged only: port -> Catalogue badge -> History tab. The tab's `require` of an extensionless name
                   resolves through the `.lua` fallback (its log line is present) and there is no [lua-error].
--options "x,y" is the centre of the main menu's Options button (550,455 on both the Reforged and the vanilla menu)."""
import argparse, os, re, shutil, struct, subprocess, sys, zlib

ap = argparse.ArgumentParser()
ap.add_argument("--openconche", required=True)
ap.add_argument("--assets", required=True)
ap.add_argument("--workdir", required=True)
ap.add_argument("--case", required=True, choices=["credits", "history"])
ap.add_argument("--twocol", action="store_true",
                help="the credits.txt has '|' lines (vanilla): look for rows with text in both column bands and none between")
ap.add_argument("--options", default="550,455")
a = ap.parse_args()
a.openconche, a.assets, a.workdir = (os.path.abspath(p) for p in (a.openconche, a.assets, a.workdir))
shutil.rmtree(a.workdir, ignore_errors=True)
os.makedirs(a.workdir)
errors = []
START = "expect-window ui/mainmenu.lua 60000\nwait 1300\n"


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


def run(name, text):
    work = os.path.join(a.workdir, name)
    os.makedirs(work, exist_ok=True)
    script = os.path.join(work, "t.script")
    with open(script, "w") as f:
        f.write(text.replace("@W@", work))
    r = subprocess.run([a.openconche, "--headless", "--scale", "1", "--assets", a.assets, "--userdir",
                        os.path.join(work, "user"), "--script", script, "--dump-windows"],
                       cwd=work, capture_output=True, text=True, timeout=240)
    tag = name
    if r.returncode != 0:
        errors.append("%s: exit %d, tail %r" % (tag, r.returncode, r.stdout[-600:]))
    for l in r.stdout.splitlines():
        if "[lua-error]" in l or "FAIL" in l:
            errors.append("%s: %s" % (tag, l))
    return r, work


def windows(dump):
    """(type, name, abs x, abs y, w, h, line) for every dumped window of the last dump (rects are parent-relative)."""
    out, stack = [], []
    last = dump.rfind("[windows]")
    for line in dump[last:].splitlines():
        m = re.match(r'^( +)(\w+) "([^"]*)" id=\d+ rect=(-?\d+),(-?\d+),(\d+),(\d+)', line)
        if not m:
            continue
        indent = len(m.group(1))
        x, y, w, h = map(int, m.groups()[3:])
        while stack and stack[-1][0] >= indent:
            stack.pop()
        ox, oy = (stack[-1][1], stack[-1][2]) if stack else (0, 0)
        stack.append((indent, ox + x, oy + y))
        out.append((m.group(2), m.group(3), ox + x, oy + y, w, h, line))
    return out


def depth(dump):
    m = re.findall(r"modal depth (\d+)", dump)
    return int(m[-1]) if m else -1


def ink(img, x0, y0, x1, y1):
    """Number of dark pixels (text on the parchment) in the box."""
    w, h, bpp, rows = img
    n = 0
    for y in range(max(0, y0), min(h, y1)):
        row = rows[y]
        for x in range(max(0, x0), min(w, x1)):
            if row[x * bpp] + row[x * bpp + 1] + row[x * bpp + 2] < 3 * 90:
                n += 1
    return n


def two_column_rows(img, x, y, w, h):
    """Longest run of rows with ink in the left column band and the right column band but none in the middle gap band
    (columns are 224 wide from x+10 and x+234; centred text leaves the gap between them clear on two-column lines)."""
    best = run = 0
    for yy in range(y, y + h):
        left = ink(img, x + 10, yy, x + 234 - 8, yy + 1)
        right = ink(img, x + 234 + 8, yy, x + 458, yy + 1)
        mid = ink(img, x + 234 - 8, yy, x + 234 + 8, yy + 1)
        if (left or right or mid) and left and right and not mid:
            run += 1
        elif left or right or mid:
            best = max(best, run)
            run = run if (not mid and (left or right)) else 0  # tall glyph rows may touch only one band
        else:
            best = max(best, run)
            run = 0
    return max(best, run)


def credits():
    opt = tuple(map(int, a.options.split(",")))
    open_opts = START + "click %d %d\nexpect-window ui/ui_options.lua 5000\nwait 1200\n" % opt
    r, _ = run("find", open_opts + "dump\nquit\n")
    btn = [w for w in windows(r.stdout) if w[0] == "Button" and w[1] == "credits"]
    if not btn:
        errors.append("the Options dialog has no credits button")
        return
    cx, cy = btn[0][2] + btn[0][4] // 2, btn[0][3] + btn[0][5] // 2
    open_credits = open_opts + "click %d %d\nexpect-window ui/ui_credits.lua 5000\nwait 1200\n" % (cx, cy)
    # The first draw latches the start; the intro pause is 3 s. One shot before it ends, one 8 s into the scroll.
    r, work = run("scroll", open_credits + "screenshot-logical @W@/s1.png\nwait 2000\nscreenshot-logical @W@/s2.png\n"
                  "wait 6000\nscreenshot-logical @W@/s3.png\ndump\nquit\n")
    cw = [w for w in windows(r.stdout) if w[0] == "CreditsWindow"]
    if not cw:
        errors.append("no CreditsWindow in the dump after opening Credits")
        return
    _, _, x, y, w, h, line = cw[0]
    if "lines=none" in line:
        errors.append("the credits window found no credits file: " + line)
    if "[credits] cannot open" in r.stdout:
        errors.append("credits.txt could not be opened")
    s1, s2, s3 = (read_png(os.path.join(work, n)) for n in ("s1.png", "s2.png", "s3.png"))
    left1, right1 = ink(s1, x, y, x + w // 2, y + h), ink(s1, x + w // 2, y, x + w, y + h)
    print("credits rect %d,%d,%d,%d; dark pixels first shot left/right %d/%d" % (x, y, w, h, left1, right1))
    if left1 < 200 or right1 < 200:
        errors.append("the first shot has no text in both halves of the credits rectangle (%d, %d)" % (left1, right1))
    for tag, img in (("s2", s2), ("s3", s3)):
        if ink(img, x, y, x + w // 2, y + h) < 200 and ink(img, x + w // 2, y, x + w, y + h) < 200:
            errors.append("%s shows no text" % tag)
    if a.twocol:
        runs = [two_column_rows(img, x, y, w, h) for img in (s1, s2, s3)]
        print("rows with text in both column bands and a clear gap, per shot: %s" % runs)
        if max(runs) < 6:
            errors.append("no two-column rows (left and right bands inked, middle gap clear) in any shot: %s" % runs)
    diff13 = sum(1 for yy in range(y, y + h) if s1[3][yy] != s3[3][yy])
    print("rows differing between the first and the last shot: %d of %d" % (diff13, h))
    if diff13 < h // 4:
        errors.append("the credits did not scroll between the shots (%d differing rows)" % diff13)
    # The end of the file is reachable: H is the whole text height (the original's half would stop short). With the 21
    # padding lines (347 / 16) after the last credits line, that line's top is at H - 336 - one line, so a shot with
    # s = H - 336 - 150 has it about 150 px down the window, with only blank padding below it. The dump gives H and
    # `time`; the first draw is about 1.2 s before the shot's reference point.
    m = re.search(r"time=(\d+) intropause=(\d+) scrollheight=(\d+)", line)
    if not m:
        errors.append("the credits dump line has no time/scrollheight: " + line)
    else:
        tm, ip, hh = map(int, m.groups())
        t_target = ip + (hh - 336 - 150) * tm / float(hh)
        r, work = run("end", open_credits + "wait %d\nscreenshot-logical @W@/end.png\nquit\n" % max(0, int(t_target - 1200)))
        img = read_png(os.path.join(work, "end.png"))
        rows = [yy for yy in range(y, y + h) if ink(img, x, yy, x + w, yy + 1)]
        print("end shot: ink rows %s..%s of the window (%d)" % (rows[0] - y if rows else None, rows[-1] - y if rows else None, h))
        if not rows or not (60 <= rows[-1] - y <= 260):
            errors.append("the last credits line is not near 150 px down in the end shot (ink bottom %s): the scroll "
                          "does not reach the end of the file" % (rows[-1] - y if rows else None))
    # Escape closes it: back to Options (modal depth 3), and nothing else is lost.
    r, _ = run("escape", open_credits + "key Escape\nwait 1500\ndump\nquit\n")
    if depth(r.stdout) != 3:
        errors.append("Escape did not close Credits (modal depth %d, 3 is the Options dialog)" % depth(r.stdout))
    r, _ = run("ok", open_credits + "dump\nquit\n")
    ok = [w for w in windows(r.stdout) if w[0] == "Button" and w[1] == "ok"]
    if not ok:
        errors.append("Credits has no ok button")
    else:
        bx, by = ok[-1][2] + ok[-1][4] // 2, ok[-1][3] + ok[-1][5] // 2
        r, _ = run("ok", open_credits + "click %d %d\nwait 1500\ndump\nquit\n" % (bx, by))
        if depth(r.stdout) != 3:
            errors.append("the Ok button did not close Credits (modal depth %d)" % depth(r.stdout))


def history():
    text = START + """click 310 345
expect-window ui/ui_entername.lua 5000
wait 600
type Tester
key Return
expect-window ui/ui_difficulty.lua 3000
wait 800
click 328 400
expect-window ui/ui_quest_offer.lua 5000
wait 1500
lua DoPopModal('ui/ui_quest_offer.lua')
wait 300
lua SwapToModal("ui/portview.lua")
wait 3000
click 136 573
expect-window ui/ui_catalogue.lua 5000
wait 1500
click 95 72
wait 1500
screenshot-logical @W@/history.png
dump
quit
"""
    r, work = run("history", text)
    if "[script] require 'ui/catalogue_history_documents' resolved as 'ui/catalogue_history_documents.lua'" not in r.stdout:
        errors.append("the .lua fallback log line for the History tab is missing")
    if "could not load package" in r.stdout:
        errors.append("a require failed: " + [l for l in r.stdout.splitlines() if "could not load" in l][0])


{"credits": credits, "history": history}[a.case]()
if errors:
    print("FAIL:\n  " + "\n  ".join(errors))
    sys.exit(1)
print("ok: %s" % a.case)
