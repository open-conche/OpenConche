"""Game-window slices 4 to 7 check, on the real Reforged data (headless, simulated clock):

  Zurich: the first quest dialog shows its character (CharWindow, neutral face) and `lua SetMood("main_alex", 90)`
  cross-fades to the pleased face in 4 ticks of 50 ms (the dump's character line). The dialog is then popped and the port
  rebuilt (SwapToModal), so its intro starts afresh:
    * at 0.5 s the scene is dark (the buildings' labels are up); at 3 s it is lit and the labels are gone,
    * cars move: two screenshots 2 s apart differ on the street,
    * moving over zur_market gives the highlight sprite and the label comes back; clicking it opens a building dialog.
  Bali has animated waves: two dumps 600 ms apart show the wave sprite on different frames.
  Zoom-in: Building:TransitionIn (path to 150,50 over 150 ms, `zoomin` over 100 ms) on a Bitmap "background" over the Zurich
  port: 50 ms in, the background is smaller than its texture and anchored top-left; after, it is full size at 150,50.
  (No shipped building dialog has a window named "background", so the scenario builds one; the Lua helper is the real one.)"""
import argparse, os, re, shutil, struct, subprocess, sys, zlib

ap = argparse.ArgumentParser()
ap.add_argument("--openconche", required=True)
ap.add_argument("--assets", required=True)
ap.add_argument("--workdir", required=True)
a = ap.parse_args()
shutil.rmtree(a.workdir, ignore_errors=True)
os.makedirs(a.workdir)
errors = []


def read_png(path):
    data = open(path, "rb").read()
    pos, idat, w = 8, b"", None
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
                pr = x if pa <= pb and pa <= pc else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 255
        rows.append(line)
        prev = line
    return w, h, bpp, rows


def px(img, x, y):
    w, h, bpp, rows = img
    return tuple(rows[y][x * bpp:x * bpp + 3])


def mean_luma(img, box):
    x0, y0, x1, y1 = box
    t = n = 0
    for y in range(y0, y1, 3):
        for x in range(x0, x1, 3):
            r, g, b = px(img, x, y)
            t += r + g + b
            n += 3
    return t / n


def diff_count(i1, i2, box):
    x0, y0, x1, y1 = box
    n = 0
    for y in range(y0, y1):
        for x in range(x0, x1):
            if sum(abs(p - q) for p, q in zip(px(i1, x, y), px(i2, x, y))) > 40:
                n += 1
    return n


def run(name, text):
    path = os.path.join(a.workdir, name + ".script")
    with open(path, "w") as f:
        f.write(text)
    r = subprocess.run([a.openconche, "--headless", "--assets", a.assets, "--userdir", os.path.join(a.workdir, "user_" + name),
                        "--script", path], cwd=a.workdir, capture_output=True, text=True, timeout=240)
    if r.returncode != 0 or "[lua-error]" in r.stdout or "FAIL" in r.stdout or "[sprite-lua]" in r.stdout:
        errors.append("%s: exit %d, tail %r" % (name, r.returncode, r.stdout[-700:]))
    return r.stdout


def shot(n):
    return os.path.join(a.workdir, n)


START = """expect-window ui/mainmenu.lua 60000
wait 1300
click 310 345
expect-window ui/ui_entername.lua 5000
wait 600
type Tester
key Return
expect-window ui/ui_difficulty.lua 3000
wait 800
click 328 400
expect-window ui/ui_quest_offer.lua 5000
wait 1500
"""

# ---- Zurich, the quest dialog's character and SetMood ----
out = run("zurich", START + "screenshot " + shot("quest.png") + """
dump
lua SetMood("main_alex", 90)
wait 17
dump
wait 100
dump
wait 300
dump
lua DoPopModal('ui/ui_quest_offer.lua')
wait 300
lua SwapToModal("ui/portview.lua")
wait 500
screenshot """ + shot("port05.png") + """
wait 2500
screenshot """ + shot("port3.png") + """
wait 2000
screenshot """ + shot("port5.png") + """
move 534 230
wait 100
expect-sprite highlight:zur_market 1 2000
expect-sprite highlight:zur_bank 0 100
wait 300
screenshot """ + shot("hover.png") + """
click 534 230
expect-window ui/ui_quest_offer.lua 5000
wait 800
quit
""")
chars = re.findall(r'CharWindow "main_alex" .*? character=main_alex frame=(\d+) target=(\d+) timer=(\d+)', out)
if len(chars) < 4:
    errors.append("the quest dialog's CharWindow is not in the dumps (%d found)" % len(chars))
else:
    first = [tuple(map(int, c)) for c in chars[:4]]
    if first[0] != (2, 2, 0):
        errors.append("the character should start on the neutral face, got %r" % (first[0],))
    if first[1][:2] != (2, 3):
        errors.append("SetMood should start a fade to the pleased face, got %r" % (first[1],))
    if not (first[2][0] == 2 and first[2][1] == 3 and 50 <= first[2][2] <= 150):
        errors.append("mid-fade state wrong: %r" % (first[2],))
    if first[3][:2] != (3, 3):
        errors.append("the fade should have ended on frame 3, got %r" % (first[3],))
    if len(set(c[0] for c in chars[:4])) < 2:
        errors.append("the shown frame never changed")
if not os.path.exists(shot("hover.png")):
    errors.append("no screenshots were taken")
else:
    p05, p3, p5, hover = (read_png(shot(n)) for n in ("port05.png", "port3.png", "port5.png", "hover.png"))
    sky = (100, 380, 700, 430)  # the mountain strip, scenery that the intro tint darkens
    l05, l3 = mean_luma(p05, (0, 0, 800, 120)), mean_luma(p3, (0, 0, 800, 120))
    if not l3 > l05 * 1.5:
        errors.append("the intro should brighten the scene: 0.5 s %.0f, 3 s %.0f" % (l05, l3))
    # A label's beige fill (255,215,171) is at the market's label at 0.5 s and gone at 3 s.
    def beige(img):
        n = 0
        for y in range(268, 286):
            for x in range(470, 600):
                r, g, b = px(img, x, y)
                if abs(r - 255) < 12 and abs(g - 215) < 14 and abs(b - 171) < 18:
                    n += 1
        return n
    if beige(p05) < 100:
        errors.append("the market label should show at 0.5 s (beige pixels %d)" % beige(p05))
    if beige(p3) > 5:
        errors.append("the labels should have faded by 3 s (beige pixels %d)" % beige(p3))
    if beige(hover) < 100:
        errors.append("hovering the market should show its label (beige pixels %d)" % beige(hover))
    # The highlight glow is drawn around the building: above its label, hover and no hover differ by pixels.
    glow = diff_count(p5, hover, (440, 100, 640, 266))
    if glow < 100:
        errors.append("hovering the market should draw its highlight (changed pixels above the label %d)" % glow)
    moved = diff_count(p3, p5, (0, 300, 400, 600))
    if moved < 300:
        errors.append("the cars should have moved between the 3 s and 5 s screenshots (changed pixels %d)" % moved)
if "ok expect-sprite highlight:zur_market" not in out:
    errors.append("no highlight sprite for zur_market on hover")

# ---- Bali: the waves animate ----
out = run("bali", START + """lua DoPopModal('ui/ui_quest_offer.lua')
wait 300
lua Player.portName = "bali"
lua SwapToModal("ui/portview.lua")
wait 300
dump
wait 700
dump
quit
""")
frames = re.findall(r'sprite scenery:wave\d?\S* layer=\d+ pos=\S+ scale=\S+ frame=(\d+)/(\d+)', out)
if len(frames) < 2:
    errors.append("no wave sprites in the Bali dumps")
else:
    half = len(frames) // 2
    if frames[:half] == frames[half:]:
        errors.append("the wave frames did not advance between the dumps: %r" % frames[:half])

# ---- zoomin: Building:TransitionIn on a Bitmap over the port ----
out = run("zoom", START + """lua DoPopModal('ui/ui_quest_offer.lua')
wait 300
lua SwapToModal("ui/portview.lua")
wait 3500
lua PushModal('zoomtest')
lua GenericCreate({ typename = 'Window', name = 'contents', x = 0, y = 0, w = 800, h = 600 }); PopWindow()
lua GenericCreate({ typename = 'Bitmap', name = 'background', image = 'image/popup_back_dialog', x = 10, y = 20 }); PopWindow()
wait 100
screenshot """ + shot("zoom_before.png") + """
lua Building.TransitionIn({x = 10, y = 20}, 150, 50)()
wait 50
dump
screenshot """ + shot("zoom_mid.png") + """
wait 250
dump
screenshot """ + shot("zoom_after.png") + """
quit
""")
bgs = re.findall(r'Bitmap "background" id=\d+ rect=(-?\d+),(-?\d+),(\d+),(\d+)', out)
if len(bgs) < 2:
    errors.append("zoom: the dumps have no background bitmap (%d)" % len(bgs))
else:
    mid, end = tuple(map(int, bgs[0])), tuple(map(int, bgs[1]))
    if end != (150, 50, 601, 317):
        errors.append("zoom: the background ends at %r, expected 150,50,601,317" % (end,))
    if not (0 < mid[2] < end[2] and 0 < mid[3] < end[3]):
        errors.append("zoom: 50 ms in, the background is %dx%d, expected smaller than %dx%d" % (mid[2], mid[3], end[2], end[3]))
    if "[unimplemented] Transition zoomin" in out:
        errors.append("zoom: the zoomin transition is still logged as unimplemented")
if os.path.exists(shot("zoom_after.png")):
    zb, zm, za = (read_png(shot(n)) for n in ("zoom_before.png", "zoom_mid.png", "zoom_after.png"))
    area = (0, 20, 800, 400)
    if diff_count(zm, zb, area) < 2000 or diff_count(zm, za, area) < 2000:
        errors.append("zoom: the mid-zoom frame should differ from both the before and the after frame in the dialog area (%d, %d)"
                      % (diff_count(zm, zb, area), diff_count(zm, za, area)))
    # A point inside the final dialog (150..751, 50..367) that the smaller mid-zoom dialog has not reached.
    if px(za, 650, 250) == px(zm, 650, 250):
        errors.append("zoom: the mid frame already covers the right part of the final dialog")
else:
    errors.append("zoom: no screenshots were taken")

if errors:
    print("FAIL:\n  " + "\n  ".join(errors))
    sys.exit(1)
print("ok: Zurich intro, hover, click, cars, the quest character and SetMood; Bali waves advance; the zoomin transition grows a bitmap")
