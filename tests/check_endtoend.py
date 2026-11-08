"""The end-to-end run, played headless against the real Reforged game.

  run 1 (play)    New Story game (name, default difficulty, quest dialogs); a $7000 sugar purchase at the Zurich market;
                  the map; travel to Cape Town; ten sacks of sugar at the Cape Town market; five cases of chocolate sold at
                  the Cape Town shop (the cases are granted with a lua line, as no factory exists yet); the ledger's menu
                  button (the pause dialog saves on open); Main Menu; the quit button; "Yes" (AskQuit saves and posts
                  kQuitNow). The run must end by itself long before the script's final 10 minute wait.
  save file       profiles/<user>/save.txt exists, starts with "return {" and ends with "}" (it is loaded as Lua in run 2).
  run 2 (resume)  Same userdir: "Welcome back, Tester!", Story Mode resumes on the map; the save table decoded by the game
                  (loadstring of LoadGameString(), flattened to path=value lines) equals the one captured before the quit
                  (Player run through the game's own BuildSaveGameString()), ignoring only the volatile lastSave; money,
                  time, subticks, port and every inventory count equal the literals captured from run 1.
  run 3 (free)    Same userdir: Free Play from the menu opens the map in mode "free" with the Free Play start money of exactly
                  20000, time 1, Zurich. The session is then changed (SubtractMoney, a trip to Cape Town through SetPort) and
                  the app quits (the exit saves into the Free Play sidecar).
  run 4 (free 2)  Free Play again: the sidecar is read back (mode free, the money/time/port of run 3, not the Story ones).
  run 5 (story 2) Story Mode again: the Story save still resumes with run 1's values (not Free Play's).
Each run's --log-unimplemented file may only name sound, movie and button-sound stubs. Run 1 must end with
"[app] exit: kQuitNow" (the Yes button of AskQuit), the others with "[app] exit: Main returned" or the script's quit.
The sell step checks that the money gained is exactly 5 times the price shown in the shop (the b01_price label).

Shortcuts taken with the `lua` script command (everything else is real clicks and typing):
  - `capetown:Unlock()`: Cape Town is locked at the start; unlocking it stands in for the quest/tutorial progress. The trip
    itself is clicked on the map.
  - `_AllProducts["b01"]:AdjustInventory(20)`: 20 cases of chocolate are granted, as no factory exists yet to make them.
  - `lua function Snap/Flat ...`, `print(...)` and `expect-lua` only read state (Snap, Flat, price and money prints).
  - Run 3 changes the Free Play session with `Player:SubtractMoney` and `Player:SetPort` (a flight to Cape Town without
    playing the map) so that run 4 can tell the sidecar from a fresh Free Play game.
The market and shop are opened by clicking the buildings' sprites (positions from the --dump-windows sprite lines of
building:zur_market, building:cap_market, building:cap_shop), the same way a player does.
Run at each of --displays (scale1,fullscreen); screenshots are left in the work folder to be looked at."""
import argparse, os, re, shutil, subprocess, sys

ap = argparse.ArgumentParser()
ap.add_argument("--openconche", required=True)
ap.add_argument("--assets", required=True)
ap.add_argument("--workdir", required=True)
ap.add_argument("--displays", default="scale1")
a = ap.parse_args()
os.makedirs(a.workdir, exist_ok=True)
errors = []

DISPLAYS = {"scale1": ("--scale", "1"), "scale2": ("--scale", "2"),
            "fullscreen": ("--fullscreen", "--display-size", "2560x1440")}
# The only stubs the run may hit: sound events, the intro movie (movies are not played) and the button click sounds.
ALLOWED_STUBS = {"SoundEvent": "sound events (market buy, shop sell, music, money sound)",
                 "ButtonSound": "button click sounds",
                 "StartMovie": "the intro movie of the first story launch"}

SNAP = ('lua function Snap() local t = {} for k, v in pairs(Player.ingredients) do table.insert(t, k .. "=" .. v) end '
        'for k, v in pairs(Player.products) do table.insert(t, "p:" .. k .. "=" .. v) end table.sort(t) '
        'return table.concat(t, ",") end')

# Flattens a table to "SAVESTATE<TAB>path=value" lines (functions skipped, strings quoted).
FLAT = ('lua function Flat(t, p) for k, v in pairs(t) do local key = p .. tostring(k) if type(v) == "table" then Flat(v, key .. ".") '
        'elseif type(v) == "string" then print("SAVESTATE", key .. "=" .. string.format("%q", v)) '
        'elseif type(v) ~= "function" then print("SAVESTATE", key .. "=" .. tostring(v)) end end end\n')

INTRO = """expect-window ui/mainmenu.lua 60000
wait 1300
click 310 345
expect-window ui/ui_entername.lua 5000
wait 600
type Tester
key Return
expect-window ui/ui_difficulty.lua 3000
wait 800
click 328 400
wait 5000
click 400 346
wait 1500
click 503 346
wait 1500
click 400 346
wait 1500
click 400 346
wait 1500
click 400 346
wait 1500
click 400 346
wait 1500
expect-window ui/portview.lua 3000
screenshot @W@/1_port.png
"""

PLAY = INTRO + SNAP + "\n" + FLAT + """
expect-lua 1000 Player.money == 20000 and Player.time == 1 and Player.portName == "zurich" and Player.mode == "story"
click 534 230
wait 2500
screenshot @W@/2_zurich_market.png
click 614 191
wait 1500
click 578 280
wait 300
screenshot @W@/3_zurich_register.png
click 474 336
wait 2500
expect-lua 1000 Player.ingredients.sugar == 1000 and Player.money == 13000
click 578 345
wait 2500
screenshot @W@/4_zurich_after.png
expect-lua 1000 Player.subticks == 1 and Player.time == 1
lua capetown:Unlock(); T0 = Player.time; M0 = Player.money; F = Player.portsCost["capetown"]; W = Player:GetPort():GetRoute(capetown).time
click 115 435
expect-window ui/mapview.lua 3000
wait 500
screenshot @W@/5_map.png
move 426 270
wait 400
click 426 270
expect-lua 2000 gTravelActive
wait 1200
screenshot @W@/6_travel.png
expect-lua 8000 Player.portName == "capetown" and not gTravelActive
expect-window ui/portview.lua 3000
wait 1000
expect-lua 1000 F > 0 and Player.time == T0 + W and Player.money == M0 - F and Player.subticks == 0
screenshot @W@/7_capetown.png
click 540 306
wait 2500
screenshot @W@/8_cape_market.png
lua M1 = Player.money
click 614 191
wait 1500
click 342 222
wait 200
click 483 262
wait 200
screenshot @W@/9_cape_register.png
click 474 336
wait 2500
expect-lua 1000 Player.ingredients.sugar == 1010 and Player.money < M1 and (M1 - Player.money) > 0 and Mod(M1 - Player.money, 10) == 0
click 578 345
wait 2500
expect-lua 1000 Player.subticks == 1
lua _AllProducts["b01"]:AdjustInventory(20)
click 326 290
wait 2500
click 400 342
wait 2500
screenshot @W@/10_shop.png
lua print("PRICE", GetLabel("b01_price"))
click 545 200
wait 1500
click 498 222
wait 300
screenshot @W@/11_sell_register.png
lua M2 = Player.money
click 474 336
wait 2500
expect-lua 1000 Player.products.b01 == 15 and Player.money > M2
lua print("SOLD", M2, Player.money)
screenshot @W@/12_sold.png
click 578 345
wait 2500
expect-lua 1000 Player.subticks == 2 and Player.time == T0 + W
lua print("SNAP", Player.money, Player.time, Player.subticks, Player.portName, Snap())
lua Flat(loadstring(Player:BuildSaveGameString())(), "")
click 50 565
expect-window ui/ui_pause.lua 3000
wait 1500
screenshot @W@/13_pause.png
click 552 335
expect-window ui/mainmenu.lua 5000
wait 1500
screenshot @W@/14_menu.png
click 683 417
wait 1000
screenshot @W@/15_quit_confirm.png
click 313 396
wait 600000
"""


def region_count(path, x0, y0, x1, y1):
    """Number of cream-ish pixels (the keeper's headscarf) in a box of an 8-bit RGB/RGBA PNG, or None if unreadable."""
    import struct, zlib
    try:
        data = open(path, "rb").read()
    except OSError:
        return None
    pos, idat = 8, b""
    while pos < len(data):
        n, typ = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        pos += 12 + n
        if typ == b"IHDR":
            w, h, _, ctype = struct.unpack(">IIBB", body[:10])
            bpp = 3 if ctype == 2 else 4
        elif typ == b"IDAT":
            idat += body
    raw = zlib.decompress(idat)
    stride, prev, rows = w * bpp, bytearray(w * bpp), []
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
    n = 0
    for y in range(y0, y1):
        for x in range(x0, x1):
            r, g, b = rows[y][x * bpp:x * bpp + 3]
            if r > 130 and g > 120 and b > 80 and abs(r - g) < 35 and 25 < r - b < 110:
                n += 1
    return n


def run(tag, display, userdir, text, name, expect_exit=None):
    work = os.path.join(a.workdir, tag)
    os.makedirs(work, exist_ok=True)
    script = os.path.join(work, name + ".script")
    with open(script, "w") as f:
        f.write(text.replace("@W@", work))
    unimpl = os.path.join(work, name + ".unimpl.txt")
    r = subprocess.run([a.openconche, "--headless", *DISPLAYS[display], "--assets", a.assets, "--userdir", userdir,
                        "--script", script, "--log-unimplemented", unimpl], cwd=work, capture_output=True, text=True,
                       timeout=900)
    open(os.path.join(work, name + ".out.txt"), "w").write(r.stdout + r.stderr)
    where = "%s/%s" % (tag, name)
    if expect_exit and expect_exit not in r.stdout:
        errors.append("%s: expected %r in the output" % (where, expect_exit))
    if r.returncode != 0 or "[lua-error]" in r.stdout or "FAIL" in r.stdout:
        errors.append("%s: exit %d, tail %r" % (where, r.returncode, r.stdout[-700:]))
    try:
        for line in open(unimpl):
            stub = line.split("\t")[0].strip()
            if stub and stub not in ALLOWED_STUBS:
                errors.append("%s: unexpected stub hit: %s" % (where, line.strip()))
    except OSError:
        errors.append("%s: no unimplemented log" % where)
    return r


def state(out):
    """The SAVESTATE lines of an output as a dict path -> value, without the volatile lastSave."""
    d = {}
    for line in out.splitlines():
        if line.startswith("SAVESTATE\t") and "=" in line:
            k, v = line[len("SAVESTATE\t"):].split("=", 1)
            if k != "lastSave":
                d[k] = v
    return d


def resume_script(button, expectations, extra=""):
    return ("expect-window ui/mainmenu.lua 60000\nwait 1300\nscreenshot @W@/%s_menu.png\n" % button[0] + SNAP + "\n" + FLAT + extra +
            "lua print('WELCOME', GetLabel('welcome'))\n"
            "click %s\nexpect-window ui/mapview.lua 15000\nwait 1500\nscreenshot @W@/%s_map.png\n" % (button[1], button[0]) +
            expectations + "quit\n")


def story_checks(money, time, sub, port, inv):
    return ('expect-lua 1000 Player.mode == "story" and Player.name == "Tester"\n'
            "expect-lua 1000 Player.money == %s and Player.time == %s and Player.subticks == %s\n"
            'expect-lua 1000 Player.portName == "%s"\n'
            'expect-lua 1000 Snap() == "%s"\n' % (money, time, sub, port, inv))


STORY = ("story", "310 345")
FREE = ("free", "490 345")

for display in a.displays.split(","):
    tag = "e2e_" + display
    shutil.rmtree(os.path.join(a.workdir, tag), ignore_errors=True)
    user = os.path.join(a.workdir, tag, "user")

    # ---- run 1: play, save, quit ----
    r = run(tag, display, user, PLAY, "1_play", "[app] exit: kQuitNow")
    m = re.search(r"^SNAP\t(\d+)\t(\d+)\t(\d+)\t(\w+)\t(.*)$", r.stdout, re.M)
    frames = re.search(r"\[app\] exit after (\d+) frames", r.stdout)
    if not frames or int(frames.group(1)) > 20000:
        errors.append("%s: the run did not end by itself after Yes (frames %s)" % (tag, frames and frames.group(1)))
    if not m:
        errors.append("%s: no SNAP line from run 1" % tag)
        continue
    money, time, sub, port, inv = m.group(1), m.group(2), m.group(3), m.group(4), m.group(5)
    print("%s: before quit money=%s time=%s subticks=%s port=%s inventory=%s" % (tag, money, time, sub, port, inv))
    if port != "capetown" or "sugar=1010" not in inv or "p:b01=15" not in inv or sub != "2":
        errors.append("%s: unexpected state before quit: %s" % (tag, m.group(0)))
    # The sell step: money gained == 5 x the price shown in the shop.
    pm = re.search(r"^PRICE\t(.*)$", r.stdout, re.M)
    sm = re.search(r"^SOLD\t(\d+)\t(\d+)$", r.stdout, re.M)
    digits = re.sub(r"<[^>]*>", "", pm.group(1)) if pm else ""
    digits = re.sub(r"[^\d]", "", digits)
    if not (pm and sm and digits):
        errors.append("%s: no PRICE/SOLD lines (%r %r)" % (tag, pm and pm.group(1), sm and sm.group(0)))
    elif int(sm.group(2)) - int(sm.group(1)) != 5 * int(digits):
        errors.append("%s: sold 5 cases for %d but the shop showed %s (%s)" % (
            tag, int(sm.group(2)) - int(sm.group(1)), digits, pm.group(1)))
    else:
        print("%s: sold 5 cases at $%s = $%d" % (tag, digits, 5 * int(digits)))
    # Character art wrapping: zur_marketkeep's body frame is taller than its image, and the overflow used to wrap round to
    # the top of the head, which then showed on the name plate. The plate (x 150..230, y 283..296) holds ~200 cream-ish
    # pixels in a good frame and ~660 with the hat drawn over it.
    if display == "scale1":
        shot = os.path.join(a.workdir, tag, "2_zurich_market.png")
        cream = region_count(shot, 150, 283, 230, 296)
        if cream is None or cream > 400:
            errors.append("%s: Verona's head wraps over the name plate (cream pixels %r)" % (tag, cream))
        else:
            print("%s: market keeper's name plate is clear (%d cream pixels)" % (tag, cream))
    before = state(r.stdout)
    if len(before) < 100:
        errors.append("%s: the pre-quit save state has only %d entries" % (tag, len(before)))

    # ---- the save file ----
    save = os.path.join(user, "profiles", "Tester", "save.txt")
    try:
        text = open(save, encoding="utf-8", errors="replace").read()
        if not (text.startswith("return {") and text.endswith("}")):
            errors.append("%s: save.txt is not a Lua table chunk (%r ... %r)" % (tag, text[:20], text[-20:]))
    except OSError:
        errors.append("%s: no %s" % (tag, save))

    # ---- run 2: resume Story; the whole decoded save equals the one before the quit ----
    resume = resume_script(STORY, story_checks(money, time, sub, port, inv), "lua Flat(loadstring(LoadGameString())(), '')\n")
    r = run(tag, display, user, resume, "2_resume", "[app] exit after")
    if "WELCOME" not in r.stdout:
        errors.append("%s: run 2 printed no welcome label" % tag)
    elif "Welcome back, Tester!" not in r.stdout.split("WELCOME")[1][:60]:
        errors.append("%s: run 2 welcome label is %r" % (tag, r.stdout.split("WELCOME")[1][:40]))
    after = state(r.stdout)
    diff = sorted(k for k in set(before) | set(after) if before.get(k) != after.get(k))
    if not after or diff:
        errors.append("%s: the reloaded save differs from the saved one in %d entries: %s" % (
            tag, len(diff), ", ".join("%s: %s -> %s" % (k, before.get(k), after.get(k)) for k in diff[:8])))
    else:
        print("%s: reloaded save identical (%d entries, lastSave ignored)" % (tag, len(after)))

    # ---- run 3: Free Play starts fresh (20000), then the session is changed and saved by the exit ----
    free = resume_script(FREE, 'expect-lua 1000 Player.mode == "free" and Player:IsFreePlay() and Player.portName == "zurich"\n'
                         "expect-lua 1000 Player.money == 20000 and Player.time == 1\n"
                         "lua capetown:Unlock(); Player:SubtractMoney(2223, true); Player:SetPort('capetown')\n"
                         "lua print('FREE', Player.money, Player.time, Player.mode, Player.portName)\n")
    r = run(tag, display, user, free, "3_free", "[app] exit after")
    m = re.search(r"^FREE\t(\d+)\t(\d+)\t(\w+)\t(\w+)", r.stdout, re.M)
    print("%s: free play money=%s time=%s mode=%s port=%s" % (tag, *(m.group(1, 2, 3, 4) if m else ("?",) * 4)))
    if not m or m.group(1) != "17777":
        errors.append("%s: free play session did not change to 17777: %r" % (tag, m and m.group(0)))
    fmoney, ftime, fport = (m.group(1), m.group(2), m.group(4)) if m else ("17777", "1", "capetown")
    if fport != "capetown":
        errors.append("%s: free play port is %s, not capetown" % (tag, fport))
    sidecars = [f for f in os.listdir(user) if f.startswith("reforged_freeplay_")]
    if not sidecars:
        errors.append("%s: no Free Play sidecar file in %s: %r" % (tag, user, os.listdir(user)))
    if not os.path.exists(save):
        errors.append("%s: save.txt vanished" % tag)

    # ---- run 4: Free Play resumes from the sidecar ----
    free2 = resume_script(FREE, 'expect-lua 1000 Player.mode == "free" and Player:IsFreePlay()\n'
                          "expect-lua 1000 Player.money == %s and Player.time == %s and Player.portName == '%s'\n"
                          % (fmoney, ftime, fport) + "expect-lua 1000 Player.money ~= %s\n" % money)
    run(tag, display, user, free2, "4_free_resume", "[app] exit after")

    # ---- run 5: the Story save still resumes with its own values ----
    story2 = resume_script(STORY, story_checks(money, time, sub, port, inv) + "expect-lua 1000 not Player:IsFreePlay()\n")
    run(tag, display, user, story2, "5_story_again", "[app] exit after")

if errors:
    print("FAIL:\n  " + "\n  ".join(errors))
    sys.exit(1)
print("ok: new game, buy, travel, buy and sell, pause-save, quit, resume with identical state, Free Play start and resume, Story resume")
