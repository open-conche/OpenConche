"""Chocolatier 2: the world map opens from the port's Travel button, a one-week trip animates and lands in the next port.

Docs: docs/design/choc2-port-and-ledger.md 2.5, 2.6 and section 4. A new user plays the intro into ui/portview.lua (San Francisco),
the tutorial dialogs are popped (as check_choc2_ledger.py does), and then:

1. The ledger's Travel button (to_map) opens ui/map.lua: the dump has the Map (ports, routes and the current port read from the
   style keys), a MapTravel, and a Sprite per port icon; the ledger's Travel button is hidden and its Port button shown
   (SetLedgerMode("map")). Screenshot.
2. Hovering New York opens the travel tag popup ("A 1-week journey", the price). Screenshot.
3. Clicking it starts the trip. At rank 0 the tutorial's traveller dialog appears first (a chartalk); its OK lets the Lua go on
   with InitiateTravel / TravelOneWeek. Two dumps and screenshots a few hundred ms apart show the train moving (the vehicle
   Sprite's position differs, the trail dots grow, MapTravel says travelling=1).
0. (before 2) Escape on the map opens ui/pausedialog.lua (a framed dialog, screenshot); the test closes it again. The visited port
   (San Francisco) shows through the colour picture (the Map's dump line says "colormask"; screenshot).
4. About two seconds later the trip lands: ui/portview.lua is the modal again, with New York's name in the banner. Screenshot.

Asserts: exit 0, no [lua-error] / [engine-error] / [native-error] / [native-warning], no placeholder for Map, MapTravel, Sprite or
AnimationWindow, only allow-listed natives unimplemented, the screenshots are 800x600, not black, and the trip screenshots differ."""
import argparse, os, re, shutil, struct, subprocess, sys, zlib

ap = argparse.ArgumentParser()
ap.add_argument("--openconche", required=True)
ap.add_argument("--assets", required=True)
ap.add_argument("--workdir", required=True)
a = ap.parse_args()
shutil.rmtree(a.workdir, ignore_errors=True)
os.makedirs(a.workdir)
user = os.path.join(a.workdir, "user")
errors = []

# Audio waits for its slice; bsgDate is the badge's date line; the logo editor, medal and factory status are later slices the tutorial
# dialogs reach.
ALLOWED_UNIMPLEMENTED = {"StartMovie", "SetMusicState", "SetAmbient", "bsgDate", "bsgSin", "bsgCos"}
ALLOWED_PLACEHOLDER_TYPES = {"LogoEditor", "FactoryStatus"}
REAL_TYPES = ("Map", "MapTravel", "Sprite", "AnimationWindow", "Ledger", "DynamicWindow", "Port", "Character")


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


def png_size(path):
    with open(path, "rb") as f:
        return struct.unpack(">II", f.read(24)[16:24])


def dumps(out):
    return [b for b in re.split(r"(?=^\[windows\] modal depth)", out, flags=re.M) if b.startswith("[windows]")]


def line(block, kind, name=None):
    pat = r'^\s*%s "%s" .*$' % (kind, name) if name is not None else r'^\s*%s .*$' % kind
    m = re.search(pat, block, flags=re.M)
    return m.group(0) if m else None


def modal_script(block):
    m = re.search(r'^  Modal "[^"]*" .*? script="([^"]*)"', block, flags=re.M)
    return m.group(1) if m else None


def vehicle_pos(block):
    m = re.search(r"sprite vehicle layer=10 pos=([\d.-]+),([\d.-]+)", block)
    return (float(m.group(1)), float(m.group(2))) if m else None


shots = {k: os.path.join(a.workdir, k + ".png") for k in ("map", "pause", "hover", "encounter", "trip1", "trip2", "arrive")}
script_path = os.path.join(a.workdir, "run.script")
POP = "lua DoPopModal('sf_tutor'); DoPopModal('simplelogo'); DoPopModal('ui/recipebook.lua'); DoPopModal('ui/medal_award.lua')\nwait 800\n"
# Coordinates: the ledger's to_map button is 68x58 at (158, 26) inside the badge bitmap at (0, 72) of the ledger at y=290; New York's
# marker is at (205, 88) on the 800x600 map; the traveller's dialog has its OK at (300, 303).
script = ("expect-window ui/mainmenu.lua 20000\nwait 1500\nclick 400 345\nexpect-window ui/entername.lua 5000\nwait 500\ntype Tester\n"
          "key Return\nwait 2000\nwait 10000\nclick 400 300\nexpect-window ui/portview.lua 5000\nwait 1500\n"
          + POP * 14 + "expect-dump 5000 modal depth 2\nwait 2500\n"
          "dump\n"                                                                  # dump 1: San Francisco
          "click 192 417\nexpect-window ui/map.lua 5000\nwait 1500\ndump\nscreenshot-logical %s\n"      # dump 2: the map
          "key Escape\nexpect-window ui/pausedialog.lua 5000\nwait 600\nscreenshot-logical %s\n"
          "lua DoPopModal('ui/pausedialog.lua')\nwait 300\nexpect-window ui/map.lua 3000\n"
          "key Escape\nexpect-window ui/pausedialog.lua 5000\nwait 300\n"                         # a second Escape works too
          "lua DoPopModal('ui/pausedialog.lua')\nwait 300\nexpect-window ui/map.lua 3000\n"
          "move 205 88\nwait 500\ndump\nscreenshot-logical %s\n"                                         # dump 3: the New York popup
          "click 205 88\nexpect-window ui/chartalk.lua 5000\nwait 500\nscreenshot-logical %s\n"
          "click 300 303\nexpect-window ui/map.lua 5000\nwait 500\ndump\nscreenshot-logical %s\n"                                       # dump 4: trip, first frame
          "wait 600\ndump\nscreenshot-logical %s\n"                                                      # dump 5: trip, later
          "wait 1500\nexpect-window ui/portview.lua 8000\nwait 1500\ndump\nscreenshot-logical %s\nquit\n"  # dump 6: New York
          % (shots["map"], shots["pause"], shots["hover"], shots["encounter"], shots["trip1"], shots["trip2"], shots["arrive"]))
open(script_path, "w").write(script)
unimpl = os.path.join(a.workdir, "unimplemented.txt")
r = subprocess.run([a.openconche, "--headless", "--assets", a.assets, "--userdir", user, "--script", script_path, "--log-unimplemented", unimpl,
                   "--log", os.path.join(a.workdir, "log.txt")],
                   cwd=a.workdir, capture_output=True, text=True, timeout=240)
out = r.stdout
if r.returncode != 0:
    errors.append("exit %d, tail %r" % (r.returncode, out[-600:]))
for bad in ("[lua-error]", "[engine-error]", "[native-error]", "[native-warning]", "FAIL", "[choc2] MapTravel", "[choc2] InitiateTravel",
            "[choc2] TravelOneWeek"):
    if bad in out:
        errors.append("output contains %s" % bad)
if "[game] choc2 " not in out:
    errors.append("no '[game] choc2' line")
for m in re.finditer(r"^\[unimplemented-type\] (\S+)", out, flags=re.M):
    if m.group(1) in REAL_TYPES or m.group(1) not in ALLOWED_PLACEHOLDER_TYPES:
        errors.append("unexpected placeholder window type %s" % m.group(1))
if os.path.exists(unimpl):
    for l in open(unimpl):
        if l.split("\t")[0] not in ALLOWED_UNIMPLEMENTED:
            errors.append("unexpected unimplemented native %s" % l.split("\t")[0])

d = dumps(out)
if len(d) != 6:
    errors.append("expected 6 window dumps, got %d" % len(d))
else:
    port, mapd, hover, trip1, trip2, arrive = d
    if modal_script(port) != "ui/portview.lua":
        errors.append("dump 1 is not the port view: %r" % modal_script(port))
    m = re.search(r'Text "" .* label="San Francisco" size=16', port)
    if not m:
        errors.append("dump 1: the banner does not say San Francisco")
    to_map = line(port, "Button", "to_map")
    if to_map is None or " hidden" in to_map:
        errors.append("dump 1: the Travel button is missing or hidden in the port view: %r" % to_map)

    if modal_script(mapd) != "ui/map.lua":
        errors.append("dump 2 is not the map: %r" % modal_script(mapd))
    mp = line(mapd, "Map")
    mm = re.search(r"ports=(\d+) routes=(\d+) current=(\w+)", mp or "")
    if not mm or int(mm.group(1)) < 20 or int(mm.group(2)) < 10 or mm.group(3) != "sanfrancisco":
        errors.append("dump 2: the Map did not read ports/routes/current from the style: %r" % mp)
    if "visited=1" not in (mp or "") or " colormask" not in (mp or ""):
        errors.append("dump 2: the Map did not build the colour mask for the one visited port: %r" % mp)
    mt = line(mapd, "MapTravel")
    if mt is None or "travelling=0" not in mt:
        errors.append("dump 2: no idle MapTravel: %r" % mt)
    n_icons = len(re.findall(r'^\s*Sprite ".*" id=\d+ rect=\S+ image="image/', mapd, flags=re.M))
    if n_icons < 12:  # one per port that is not hidden (16 at the start)
        errors.append("dump 2: only %d port icon Sprites" % n_icons)
    if "image/port_current" not in mapd:
        errors.append("dump 2: no current-port ring")
    if " hidden" not in (line(mapd, "Button", "to_map") or "") or " hidden" in (line(mapd, "Button", "to_port") or " hidden"):
        errors.append("dump 2: SetLedgerMode('map') did not swap the Travel and Port buttons")

    if "PopupWindow" not in hover or "1-week journey" not in hover:
        errors.append("dump 3: hovering New York opened no travel tag with its '1-week journey' text")

    for blk, tag in ((trip1, "dump 4"), (trip2, "dump 5")):
        mt = line(blk, "MapTravel")
        if modal_script(blk) != "ui/map.lua" or mt is None or "travelling=1" not in mt or "leg=train" not in mt:
            errors.append("%s: not travelling by train on the map: %r" % (tag, mt))
    p1, p2 = vehicle_pos(trip1), vehicle_pos(trip2)
    if p1 is None or p2 is None:
        errors.append("the trip dumps have no vehicle sprite: %r %r" % (p1, p2))
    elif p1 == p2 or p2[0] <= p1[0]:
        errors.append("the vehicle did not move east between the dumps: %r then %r" % (p1, p2))
    dots = [int(re.search(r"dots=(\d+)", line(b, "MapTravel") or "dots=-1").group(1)) for b in (trip1, trip2)]
    if not 0 < dots[0] < dots[1]:
        errors.append("the trail dots did not grow: %r" % dots)

    if modal_script(arrive) != "ui/portview.lua":
        errors.append("dump 6: the trip did not land in a port view: %r" % modal_script(arrive))
    if not re.search(r'Text "" .* label="New York" size=16', arrive):
        errors.append("dump 6: the port banner does not say New York")
    def money(block):
        m = re.search(r'Text "money" .* label="\$([\d,]+)"', block)
        return int(m.group(1).replace(",", "")) if m else None
    before, after = money(port), money(arrive)
    # The hover says the trip costs $477; the weekly Tick may add earnings, so allow a margin either way.
    if before is None or after is None or not 0 < before - after <= 700:
        errors.append("the fare was not charged: badge money %r before, %r after" % (before, after))
    if "MapTravel" in arrive:
        errors.append("dump 6: the map is still in the chain")

# Travel sounds (docs/re/choc2-map-binary.md 1.4): the train's ambient bed and its one-shot, once per trip week.
try:
    log = open(os.path.join(a.workdir, "log.txt")).read()
except OSError:
    log = ""
for want in ("[audio] ambient sfx/saloon_loop.ogg", "[audio] file sfx/train.ogg"):
    if want not in log:
        errors.append("no %r in the log" % want)
if log.count("[audio] file sfx/train.ogg") != 1:
    errors.append("the train one-shot played %d times for a one-week trip" % log.count("[audio] file sfx/train.ogg"))

for k, p in shots.items():
    try:
        if png_size(p) != (800, 600) or not png_not_black(p):
            errors.append("the %s screenshot is wrong" % k)
    except (OSError, zlib.error) as e:
        errors.append("no %s screenshot: %s" % (k, e))
try:
    if open(shots["trip1"], "rb").read() == open(shots["trip2"], "rb").read():
        errors.append("the two trip screenshots are equal: nothing moved")
    if open(shots["map"], "rb").read() == open(shots["arrive"], "rb").read():
        errors.append("the map and arrival screenshots are equal")
except OSError:
    pass

if errors:
    print("FAIL\n" + "\n".join(errors))
    sys.exit(1)
print("Chocolatier 2 travel OK")
