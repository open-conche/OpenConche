"""Transitions on the Reforged menu.

(a) The main menu fades in over about a second: a shot early in the fade is darker than the reference, a shot after it
    matches the real menu (the same probes as reforged_menu_screenshot).
(b) Story Mode opens ui_entername.lua, which starts at x=1000 and runs CenterFadeIn: after the fade the panel is at its
    centred position in the dump, shows its art, and an early shot is a partial blend.
(c) Save & Quit opens the yes/no dialog; "No" fades it closed and the menu remains; "Yes" makes the app quit with exit 0
    well before the script's own end."""
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
    return tuple(rows[int(y)][int(x) * bpp:int(x) * bpp + 3])


def close(p, q, tol):
    return all(abs(i - j) <= tol for i, j in zip(p, q))


def run(name, script_text):
    work = os.path.join(a.workdir, name)
    os.makedirs(work, exist_ok=True)
    script = os.path.join(work, "t.script")
    with open(script, "w") as f:
        f.write(script_text.replace("@W", work))
    r = subprocess.run([a.openconche, "--headless", "--scale", "1", "--assets", a.assets, "--userdir",
                        os.path.join(work, "user"), "--script", script, "--dump-windows"], cwd=work,
                       capture_output=True, text=True, timeout=120)
    if "[lua-error]" in r.stdout:
        errors.append("%s: [lua-error] in the log" % name)
    return r, work


crop = read_png(os.path.join(a.reference, "menu-native-crop.png"))
BACKGROUND_PROBES = [(250, 330), (400, 380), (320, 500), (300, 420)]
MENU = "expect-window ui/mainmenu.lua 60000\n"

# (a) the fade-in
r, work = run("fadein", MENU + "wait 200\nscreenshot-logical @W/early.png\nwait 1100\nscreenshot-logical @W/late.png\nquit\n")
if r.returncode != 0:
    errors.append("fadein: exit %d, tail %r" % (r.returncode, r.stdout[-500:]))
else:
    early, late = read_png(os.path.join(work, "early.png")), read_png(os.path.join(work, "late.png"))
    for (lx, ly) in BACKGROUND_PROBES:
        want = px(crop, (lx - 200 + 0.5) * 2.4, (ly - 317 + 0.5) * 2.4)
        if not close(px(late, lx, ly), want, 14):
            errors.append("fadein: after the fade (%d,%d) is %s, the real menu has %s" % (lx, ly, px(late, lx, ly), want))
        if sum(px(early, lx, ly)) >= sum(want) * 0.6:
            errors.append("fadein: 200 ms in, (%d,%d) is %s, not much darker than %s" % (lx, ly, px(early, lx, ly), want))

# (b) the name dialog: CenterFadeIn from x=1000
r, work = run("entername", MENU + "wait 1300\nclick 310 345\nexpect-window ui/ui_entername.lua 5000\nwait 30\n"
              "screenshot-logical @W/mid.png\nwait 600\nscreenshot-logical @W/end.png\nquit\n")
if r.returncode != 0:
    errors.append("entername: exit %d, tail %r" % (r.returncode, r.stdout[-500:]))
else:
    m = re.search(r'Bitmap "entername" id=\d+ rect=(-?\d+),(-?\d+),(\d+),(\d+)', r.stdout)
    if not m:
        errors.append("entername: the dump has no entername panel")
    else:
        x, y, w, h = map(int, m.groups())
        if (x, y) != ((800 - w) // 2, 141):
            errors.append("entername: the panel is at %d,%d, expected the centred %d,141" % (x, y, (800 - w) // 2))
    mid, end = read_png(os.path.join(work, "mid.png")), read_png(os.path.join(work, "end.png"))
    cream = px(end, 250, 200)  # inside the panel, left of the prompt text
    if not (cream[0] > 235 and cream[1] > 220 and cream[2] > 170):
        errors.append("entername: the panel's cream interior is %s after the fade" % (cream,))
    if close(px(mid, 250, 200), cream, 12):
        errors.append("entername: 30 ms in, the panel is already fully opaque (%s)" % (px(mid, 250, 200),))
    if close(px(end, 60, 300), px(end, 250, 200), 40):
        errors.append("entername: a point outside the panel looks like the panel")

# (c) the yes/no dialog through Save & Quit
QUIT = "click 683 417\nexpect-window ui/ui_generic_yn.lua 5000\nwait 300\n"
r, work = run("no", MENU + "wait 1300\n" + QUIT + "click 484 400\nwait 600\nquit\n")
if r.returncode != 0:
    errors.append("no: exit %d, tail %r" % (r.returncode, r.stdout[-500:]))
elif 'script="ui/ui_generic_yn.lua"' in r.stdout or 'script="ui/mainmenu.lua"' not in r.stdout:
    errors.append("no: the dialog did not close and leave the menu:\n%s" % r.stdout[-800:])
elif "[script] FAIL" in r.stdout:
    errors.append("no: a script step failed")

r, work = run("yes", MENU + "wait 1300\n" + QUIT + "click 314 400\nwait 5000\n")
m = re.search(r"\[app\] exit after (\d+) frames", r.stdout)
if r.returncode != 0 or not m:
    errors.append("yes: exit %d, tail %r" % (r.returncode, r.stdout[-500:]))
elif int(m.group(1)) * 1000 / 60.0 > 1300 + 300 + 1500 + 1000:
    errors.append("yes: the app took %s frames to quit; it should quit soon after the click" % m.group(1))

if errors:
    print("FAIL:\n  " + "\n  ".join(errors))
    sys.exit(1)
print("ok: menu fade-in, the name dialog's centred fade-in and the yes/no fade-close behave")
