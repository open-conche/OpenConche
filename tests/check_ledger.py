"""The ledger check, played headless against the real Reforged game.

The new-player flow of check_new_user.py (Story Mode, a name, the default difficulty) is followed by the opening quest
dialogs (clicking each one's button) until the port view is bare. Then:

  1. The ledger sits in the port modal and shows the money ($20,000), the date, the rank and the score per week. Only
     the Zurich factory gauge is shown; the map button is hidden and the port button shown. A screenshot has the money
     text drawn in the badge.
  2. A purchase at the Main Street Market (opened with `zur_market:OnClick()`, a $7000 buy of sugar) counts the money
     down over 20 steps of 50 ms: $20,000 -> $13,000 in a second, never overshooting.
  3. Port -> map -> port keeps one ledger window (the same id) and moves it into each screen's modal; the map and port
     buttons swap.
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

# The new-player flow, then the opening dialogs: "Understood", "I Know How to Play", then Ok/Thank You buttons.
INTRO = ("expect-window ui/mainmenu.lua 60000\nwait 1300\nclick 310 345\nexpect-window ui/ui_entername.lua 5000\nwait 600\n"
         "type Tester\nkey Return\nexpect-window ui/ui_difficulty.lua 3000\nwait 800\nclick 328 400\nwait 5000\n"
         "click 400 346\nwait 1500\nclick 503 346\nwait 1500\n" + "click 400 346\nwait 1500\n" * 4)
MAP_BUTTON = "click 115 435\n"  # the round Map / Port button on the badge
MARKET = "lua gCurrentBuilding=nil; zur_market:OnClick()\nwait 2500\nclick 614 191\nwait 1500\nclick 578 280\nwait 300\n"
BUY = "click 474 336\n"


def run(name, text):
    work = os.path.join(a.workdir, name)
    os.makedirs(work)
    script = os.path.join(work, "t.script")
    with open(script, "w") as f:
        f.write(text.replace("@W@", work))
    r = subprocess.run([a.openconche, "--headless", "--assets", a.assets, "--userdir", os.path.join(work, "user"), "--script",
                        script, "--dump-windows"], cwd=work, capture_output=True, text=True, timeout=300)
    if r.returncode != 0 or "[lua-error]" in r.stdout or "FAIL" in r.stdout:
        errors.append("%s: exit %d, tail %r" % (name, r.returncode, r.stdout[-600:]))
    return r.stdout, work


def tree(out):
    """The window dump as [(indent, line)] after the '[windows]' header."""
    lines = out.splitlines()
    for i, l in enumerate(lines):
        if l.startswith("[windows]"):
            return [(len(x) - len(x.lstrip()), x.strip()) for x in lines[i + 1:] if x.startswith(" ")]
    return []


def find(nodes, prefix):
    return [(i, ind, l) for i, (ind, l) in enumerate(nodes) if l.startswith(prefix)]


def parent_of(nodes, i):
    for j in range(i - 1, -1, -1):
        if nodes[j][0] < nodes[i][0]:
            return nodes[j][1]
    return ""


def label(nodes, name):
    for _, l in nodes:
        if l.startswith('Text "%s" ' % name):
            m = re.search(r' label="([^"]*)"', l)
            return m.group(1) if m else None
    return None


def hidden(nodes, prefix):
    hit = find(nodes, prefix)
    return None if not hit else " hidden" in hit[0][2].split(" label=")[0].split(" contents=")[0]


def money(nodes):
    m = re.fullmatch(r"\$([\d,]+)", label(nodes, "money") or "")
    return int(m.group(1).replace(",", "")) if m else None


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


# ---- 1. the ledger in the port ----
out, work = run("port", INTRO + "screenshot @W@/port.png\nquit\n")
nodes = tree(out)
ledgers = find(nodes, 'LedgerWindow "ledger"')
port_id = None
if len(ledgers) != 1:
    errors.append("port: expected one LedgerWindow, found %d" % len(ledgers))
else:
    i, _, line = ledgers[0]
    port_id = re.search(r"id=(\d+)", line).group(1)
    if 'script="ui/portview.lua"' not in parent_of(nodes, i):
        errors.append("port: the ledger's parent is not the port modal: %s" % parent_of(nodes, i))
if money(nodes) != 20000:
    errors.append("port: money label is %r, want $20,000" % label(nodes, "money"))
if not re.fullmatch(r"[A-Z][a-z]+ \d+, 19\d\d", label(nodes, "day") or ""):
    errors.append("port: date label is %r" % label(nodes, "day"))
if not label(nodes, "rank"):
    errors.append("port: no rank label")
if not (label(nodes, "score") or "").endswith("/week"):
    errors.append("port: score label is %r" % label(nodes, "score"))
if hidden(nodes, 'DookieDropper "zur_factory"') is not False:
    errors.append("port: the Zurich gauge should be shown")
for f in ("cap", "tok", "san", "tor", "wel"):
    if hidden(nodes, 'DookieDropper "%s_factory"' % f) is not True:
        errors.append("port: the %s gauge should be hidden" % f)
if hidden(nodes, 'Button "map_port"') is not True or hidden(nodes, 'Button "port_map"') is not False:
    errors.append("port: map_port should be hidden and port_map shown")
# The Zurich gauge's dump fields: its place in the panel, the popup it opens, the starting product and an idle factory.
gauge = find(nodes, 'DookieDropper "zur_factory"')
if gauge:
    line = gauge[0][2]
    for needle in ("rect=229,150,79,132", 'contents="zur_factory:LedgerRolloverPopup()"', 'product="b01"', "production=0", "stalled"):
        if needle not in line:
            errors.append("port: the zur_factory gauge has no '%s': %s" % (needle, line))
# The quest indicator: the flash ends on the complete picture.
quest = find(nodes, 'Bitmap "ledger_questgoals"')
if len(quest) != 1 or 'image="image/badge_button_indicator_complete"' not in quest[0][2]:
    errors.append("port: the quest indicator image is %r" % [q[2] for q in quest])
try:
    w, h, bpp, rows = read_png(os.path.join(work, "port.png"))
    bright = sum(1 for y in range(474, 504) for x in range(46, 196) if min(rows[y][x * bpp:x * bpp + 3]) > 180)
    if bright < 20:
        errors.append("port screenshot: the money text is not drawn in the badge (%d bright pixels)" % bright)
except OSError:
    errors.append("port: no screenshot")

# ---- 2. a purchase counts the money down over about a second ----
seen = []
for t in (0, 500, 1500):
    out, _ = run("buy%d" % t, INTRO + MARKET + BUY + "wait %d\nquit\n" % t)
    seen.append(money(tree(out)))
if None in seen:
    errors.append("buy: unreadable money labels %r" % seen)
else:
    # $7000 (1000 sacks at $7): 20 steps of $350 from $20,000, ending at $13,000.
    if not (seen[0] >= seen[1] >= seen[2]) or seen[2] != 13000:
        errors.append("buy: money labels over time should fall to $13,000, got %r" % seen)
    if not (13000 < seen[1] < 20000):
        errors.append("buy: money at 500 ms should be mid-count, got %r" % seen)
    for v in seen:
        if (20000 - v) % 350:
            errors.append("buy: %d is not a whole number of 350 steps below $20,000" % v)

# ---- 3. port -> map -> port keeps the ledger ----
out, _ = run("map", INTRO + MAP_BUTTON + "expect-window ui/mapview.lua 3000\nwait 1500\nquit\n")
nodes = tree(out)
ledgers = find(nodes, 'LedgerWindow "ledger"')
if len(ledgers) != 1 or re.search(r"id=(\d+)", ledgers[0][2]).group(1) != port_id:
    errors.append("map: the ledger should be the same window (id %s): %s" % (port_id, [l[2] for l in ledgers]))
elif 'script="ui/mapview.lua"' not in parent_of(nodes, ledgers[0][0]):
    errors.append("map: the ledger is not in the map modal")
if hidden(nodes, 'Button "port_map"') is not True or hidden(nodes, 'Button "map_port"') is not False:
    errors.append("map: port_map should be hidden and map_port shown")

out, work = run("back", INTRO + MAP_BUTTON + "expect-window ui/mapview.lua 3000\nwait 1500\n" + MAP_BUTTON +
                "expect-window ui/portview.lua 3000\nwait 1500\nscreenshot @W@/back.png\nquit\n")
nodes = tree(out)
ledgers = find(nodes, 'LedgerWindow "ledger"')
if len(ledgers) != 1 or re.search(r"id=(\d+)", ledgers[0][2]).group(1) != port_id:
    errors.append("back: the ledger should be the same window (id %s): %s" % (port_id, [l[2] for l in ledgers]))
elif 'script="ui/portview.lua"' not in parent_of(nodes, ledgers[0][0]):
    errors.append("back: the ledger is not in the port modal")
if money(nodes) != 20000 or hidden(nodes, 'Button "map_port"') is not True:
    errors.append("back: money %r, map_port hidden %r" % (label(nodes, "money"), hidden(nodes, 'Button "map_port"')))

if errors:
    print("FAIL:\n  " + "\n  ".join(errors))
    sys.exit(1)
print("ok: the ledger shows money, date and rank in the port, counts money down over 20 steps, and survives port -> map -> port")
