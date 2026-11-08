"""Chocolatier 2 logo editor and the .LOGO port sign (docs/re/choc2-logo-and-trade.md part A).

Run 1: a new user plays Story Mode to the San Francisco tutor and answers "I Know How To Play", which opens ui/logoedit_basic.lua.
The dump shows a LogoEditor with the 350x200 preview at (5,85) and the default table of sim.lua:125-129 (bg4, logo10, the player's
name); screenshot. The company name is changed (typed, OK), the layout button A moves the text left and the graphic right,
Randomize picks a new background, graphic and font (the sliders follow) and Done stores the table in gSim.logo: a `lua` dump shows
the new name and the randomised look, the same as the editor held. The tutor's OK dialogs end; Story start already owns the San Francisco
factory (sim.lua MarkOwned), so the rebuilt port view has a `.LOGO` sprite with a rendered texture (dump and screenshot). Hovering it opens the
"Click to change your corporate sign" popup (screenshot) and clicking it reopens the Basic editor on the saved table, and Done
returns to the port view; the second visit presses Randomize before Done and the sign area of the port screenshot changes.

Run 2: the same path, then "Expert Mode" opens ui/logoedit.lua (the full editor): bold and right justify are switched on and the
text position slider is dragged; "Basic Mode" stores the table, which now has tfb=1, to=6 and a moved tx.

Asserts: exit 0, no [lua-error] / [engine-error] / [native-error] / [native-warning], no placeholder for LogoEditor, only
allow-listed natives unimplemented, 800x600 non-black screenshots that differ."""
import argparse, os, re, shutil, struct, subprocess, sys, zlib

ap = argparse.ArgumentParser()
ap.add_argument("--openconche", required=True)
ap.add_argument("--assets", required=True)
ap.add_argument("--workdir", required=True)
a = ap.parse_args()
shutil.rmtree(a.workdir, ignore_errors=True)
os.makedirs(a.workdir)
errors = []

ALLOWED_UNIMPLEMENTED = {"StartMovie", "LoadSound", "SetMusicState", "SetAmbient", "bsgDate", "bsgCos", "bsgSin", "PlaySound"}


def run(tag, text):
    script = os.path.join(a.workdir, tag + ".script")
    with open(script, "w") as f:
        f.write(text)
    unimpl = os.path.join(a.workdir, tag + "-unimplemented.txt")
    user = os.path.join(a.workdir, tag + "-user")
    r = subprocess.run([a.openconche, "--headless", "--assets", a.assets, "--userdir", user, "--script", script, "--log-unimplemented",
                        unimpl], cwd=a.workdir, capture_output=True, text=True, timeout=240)
    out = r.stdout
    if r.returncode != 0:
        errors.append("%s: exit %d, tail %r" % (tag, r.returncode, out[-600:]))
    for bad in ("[lua-error]", "[engine-error]", "[native-error]", "[native-warning]", "FAIL"):
        if bad in out:
            errors.append("%s: output contains %s" % (tag, bad))
    if "[unimplemented-type] LogoEditor" in out:
        errors.append("%s: LogoEditor is a placeholder" % tag)
    if os.path.exists(unimpl):
        for line in open(unimpl):
            name = line.split("\t")[0]
            if name not in ALLOWED_UNIMPLEMENTED:
                errors.append("%s: unexpected unimplemented native %s" % (tag, name))
    return out


def dumps(out):
    res = []
    for block in re.split(r"(?=^\[windows\] modal depth)", out, flags=re.M):
        m = re.match(r"\[windows\] modal depth (\d+)", block)
        if m:
            res.append((re.findall(r'^  Modal "[^"]*" .*? script="([^"]*)"', block, flags=re.M), block))
    return res


def editor_line(block):
    m = re.search(r'^\s*LogoEditor "" id=\d+ rect=(\S+) preview=(\d+),(\d+) b=(\S+) l=(\S+) lf=(\d+) lx=(\d+) tx=(\d+) to=(\d+) '
                  r'tfo=(\d+) tfb=(\d+) t="(.*)"( nopreview)?$', block, flags=re.M)
    if not m:
        return None
    return dict(rect=m.group(1), px=int(m.group(2)), py=int(m.group(3)), b=m.group(4), l=m.group(5), lf=int(m.group(6)),
                lx=int(m.group(7)), tx=int(m.group(8)), to=int(m.group(9)), tfo=int(m.group(10)), tfb=int(m.group(11)),
                t=m.group(12), nopreview=bool(m.group(13)))


def logo_prints(out):
    """Each `lua print("LOGO", ...)` line as a dict."""
    res = []
    for m in re.finditer(r"^LOGO\t(.*)$", out, flags=re.M):
        res.append(dict(kv.split("=", 1) for kv in m.group(1).split("\t")))
    return res


def png_size(path):
    with open(path, "rb") as f:
        d = f.read(24)
    return struct.unpack(">II", d[16:24])


def png_not_black(path):
    data = open(path, "rb").read()
    pos, idat = 8, b""
    while pos < len(data):
        n, typ = struct.unpack(">I4s", data[pos:pos + 8])
        if typ == b"IDAT":
            idat += data[pos + 8:pos + 8 + n]
        pos += 12 + n
    raw = zlib.decompress(idat)
    return sum(1 for b in raw if b) > len(raw) // 100


def shot(tag):
    return os.path.join(a.workdir, tag + ".png")


PRINT = ('lua print("LOGO\\tt=" .. gSim.logo.t .. "\\tb=" .. gSim.logo.b .. "\\tl=" .. gSim.logo.l .. "\\tlf=" .. gSim.logo.lf .. '
         '"\\tlx=" .. gSim.logo.lx .. "\\ttx=" .. gSim.logo.tx .. "\\tto=" .. gSim.logo.to .. "\\ttfb=" .. (gSim.logo.tfb or 0))\n')

# The tutor path: a new user, Story Mode, the intro, then the tutor's "I Know How To Play" and two OKs reach the Basic logo editor.
TO_EDITOR = ("expect-window ui/mainmenu.lua 20000\nwait 1500\nclick 400 345\nexpect-window ui/entername.lua 5000\nwait 500\ntype Tester\n"
             "key Return\nwait 2000\nclick 400 300\nexpect-window ui/portview.lua 5000\nwait 1500\n"
             "click 389 305\nwait 1500\nclick 299 305\nwait 1500\nclick 299 305\nwait 1500\n"
             "expect-window ui/logoedit_basic.lua 5000\nwait 500\n")

# ---- run 1: the Basic editor, the sign, the click on it ----
script = (TO_EDITOR + "dump\nscreenshot-logical %s\n" % shot("basic0") +
          "click 330 84\ntype  Choc\nclick 617 84\nwait 300\ndump\n"       # the name field, then its OK
          "click 470 333\nwait 300\ndump\n"                                  # layout A: text left, graphic right
          "click 400 388\nwait 300\ndump\nscreenshot-logical %s\n" % shot("basic1") +  # Randomize
          "click 580 388\nwait 1500\n" + PRINT +                             # Done
          "click 299 305\nwait 1500\nclick 299 305\nwait 1500\n"             # the tutor's OKs
          # Story start already owns the San Francisco factory (sim.lua MarkOwned), so the sign shows once the port is rebuilt.
          # SwapToModal stands in for the next port build (DoPlayerLogo does the same after the editor).
          "lua SwapToModal('ui/portview.lua')\nwait 1500\ndump\n"
          "screenshot-logical %s\n" % shot("sign") +
          "move 195 234\nwait 500\ndump\nscreenshot-logical %s\n" % shot("hover") +
          "click 195 234\nexpect-window ui/logoedit_basic.lua 5000\nwait 500\ndump\nscreenshot-logical %s\n" % shot("again") +
          "click 400 388\nwait 300\nclick 580 388\nwait 2500\ndump\nscreenshot-logical %s\nquit\n" % shot("sign2"))  # Randomize, Done
out = run("run1", script)
d = dumps(out)
if len(d) != 8:
    errors.append("run 1: expected 8 window dumps, got %d" % len(d))
else:
    opened, named, layout, rnd, signed, hover, again, final = d
    e0 = editor_line(opened[1])
    if not e0 or e0["nopreview"]:
        errors.append("run 1: no LogoEditor with a preview in the Basic editor: %r" % e0)
    elif (e0["px"], e0["py"], e0["rect"], e0["b"], e0["l"], e0["t"]) != (5, 85, "0,0,700,365", "bg4", "logo/logo10", "Tester"):
        errors.append("run 1: the editor did not open on the default table: %r" % e0)
    if "ui/logoedit_basic.lua" not in opened[0]:
        errors.append("run 1: the editor modal is not ui/logoedit_basic.lua: %r" % (opened[0],))
    en = editor_line(named[1])
    if not en or en["t"] != "TesterChoc":
        errors.append("run 1: SetLogoText did not change the name: %r" % en)
    el = editor_line(layout[1])
    if not el or (el["tx"], el["lx"]) != (0, 1000):
        errors.append("run 1: layout A did not put the text left and the graphic right: %r" % el)
    er = editor_line(rnd[1])
    if not er or (er["b"], er["l"], er["lf"]) == ("bg4", "logo/logo10", 0):
        errors.append("run 1: Randomize did not change the background, graphic or font: %r" % er)
    elif er["t"] != "TesterChoc" or er["tx"] not in (0, 500, 1000) or er["lx"] != 1000 - er["tx"]:
        errors.append("run 1: Randomize touched the name or put the text and graphic on the same side: %r" % er)
    if "ui/portview.lua" not in signed[0] or "building:.LOGO " not in signed[1]:
        errors.append("run 1: the port view has no .LOGO sprite after the factory is owned")
    m = re.search(r"sprite building:\.LOGO layer=\d+ pos=195\.0,234\.0 scale=0\.3 .*tex=\$logo-sign:350x200", signed[1])
    if not m:
        errors.append("run 1: the .LOGO sprite is not the 350x200 rendered texture at 195,234 scale 0.3")
    if "hover=.LOGO" not in hover[1]:
        errors.append("run 1: hovering the sign did not target it")
    ea = editor_line(again[1])
    if not ea or ea["t"] != "TesterChoc" or (ea["b"], ea["l"]) != (er and er["b"], er and er["l"]):
        errors.append("run 1: clicking the sign did not reopen the editor on the saved table: %r" % ea)
    if "ui/portview.lua" not in final[0] or "ui/logoedit_basic.lua" in final[0]:
        errors.append("run 1: Done did not return to the port view")
prints = logo_prints(out)
if len(prints) != 1:
    errors.append("run 1: expected one gSim.logo print, got %d" % len(prints))
else:
    p = prints[0]
    if not d or len(d) != 8 or not er or p["t"] != "TesterChoc" or (p["b"], p["l"], int(p["lf"]), int(p["lx"]), int(p["tx"])) != (
            er["b"], er["l"], er["lf"], er["lx"], er["tx"]):
        errors.append("run 1: gSim.logo does not hold the edit: %r" % p)
    if (p["b"], p["l"], p["lf"]) == ("bg4", "logo/logo10", "0"):
        errors.append("run 1: gSim.logo still has the default look: %r" % p)
def decode(path):
    """(width, height, rows of RGB(A) bytes) of an 8-bit non-interlaced PNG, filters undone."""
    data = open(path, "rb").read()
    w, h, depth, ctype = struct.unpack(">IIBB", data[16:26])
    ch = {2: 3, 6: 4}[ctype]
    pos, idat = 8, b""
    while pos < len(data):
        n, typ = struct.unpack(">I4s", data[pos:pos + 8])
        if typ == b"IDAT":
            idat += data[pos + 8:pos + 8 + n]
        pos += 12 + n
    raw = zlib.decompress(idat)
    stride, rows, prev = w * ch, [], bytearray(w * ch)
    for y in range(h):
        f = raw[y * (stride + 1)]
        cur = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a_ = cur[i - ch] if i >= ch else 0
            b_ = prev[i]
            c_ = prev[i - ch] if i >= ch else 0
            if f == 1: cur[i] = (cur[i] + a_) & 255
            elif f == 2: cur[i] = (cur[i] + b_) & 255
            elif f == 3: cur[i] = (cur[i] + (a_ + b_) // 2) & 255
            elif f == 4:
                p_ = a_ + b_ - c_
                pa, pb, pc = abs(p_ - a_), abs(p_ - b_), abs(p_ - c_)
                cur[i] = (cur[i] + (a_ if pa <= pb and pa <= pc else b_ if pb <= pc else c_)) & 255
        rows.append(cur)
        prev = cur
    return w, h, ch, rows


def crop(path, x0, y0, x1, y1):
    w, h, ch, rows = decode(path)
    return b"".join(bytes(rows[y][x0 * ch:x1 * ch]) for y in range(y0, y1))


for k in ("basic0", "basic1", "sign", "sign2", "hover", "again"):
    try:
        if png_size(shot(k)) != (800, 600) or not png_not_black(shot(k)):
            errors.append("the %s screenshot is wrong" % k)
    except OSError as ex:
        errors.append("no %s screenshot: %s" % (k, ex))
try:
    if open(shot("basic0"), "rb").read() == open(shot("basic1"), "rb").read():
        errors.append("Randomize did not change the screen")
    if crop(shot("sign"), 135, 200, 255, 255) == crop(shot("sign2"), 135, 200, 255, 255):
        errors.append("the sign area is the same after the second Randomize")
    if open(shot("sign"), "rb").read() == open(shot("hover"), "rb").read():
        errors.append("hovering the sign did not change the screen")
except OSError:
    pass

# ---- run 2: Expert Mode ----
script = (TO_EDITOR + "click 222 388\nexpect-window ui/logoedit.lua 5000\nwait 500\ndump\nscreenshot-logical %s\n" % shot("expert0") +
          "click 648 191\nclick 671 219\nwait 200\n"                      # Bold, Right
          "down 606 432\nmove 540 432\nup 540 432\nwait 300\ndump\nscreenshot-logical %s\n" % shot("expert1") +  # textx slider
          "click 490 523\nwait 1500\nexpect-window ui/logoedit_basic.lua 5000\nwait 500\ndump\n" + PRINT + "quit\n")
out = run("run2", script)
d = dumps(out)
if len(d) != 3:
    errors.append("run 2: expected 3 window dumps, got %d" % len(d))
else:
    x0, x1, basic = d
    e0, e1 = editor_line(x0[1]), editor_line(x1[1])
    if "ui/logoedit.lua" not in x0[0] or not e0 or (e0["px"], e0["py"], e0["rect"]) != (5, 295, "0,0,700,500"):
        errors.append("run 2: Expert Mode did not open ui/logoedit.lua with the preview at (5,295): %r %r" % (x0[0], e0))
    if not e1 or e1["tfb"] != 1 or e1["to"] != 6 or e1["tx"] >= 500:
        errors.append("run 2: bold, right justify and the position slider did not reach the editor: %r" % e1)
    elif not re.search(r'^\s*Button "bold" .* on command$', x1[1], flags=re.M):
        errors.append("run 2: the Bold toggle does not show on after its press")
    if "ui/logoedit_basic.lua" not in basic[0]:
        errors.append("run 2: Basic Mode did not return to ui/logoedit_basic.lua: %r" % (basic[0],))
prints = logo_prints(out)
if len(prints) != 1:
    errors.append("run 2: expected one gSim.logo print, got %d" % len(prints))
elif prints[0]["tfb"] != "1" or prints[0]["to"] != "6" or int(prints[0]["tx"]) >= 500:
    errors.append("run 2: Basic Mode did not store the expert edits in gSim.logo: %r" % prints[0])
for k in ("expert0", "expert1"):
    try:
        if png_size(shot(k)) != (800, 600) or not png_not_black(shot(k)):
            errors.append("the %s screenshot is wrong" % k)
    except OSError as ex:
        errors.append("no %s screenshot: %s" % (k, ex))

if errors:
    print("FAIL\n" + "\n".join(errors))
    sys.exit(1)
print("Chocolatier 2 logo editor OK")
