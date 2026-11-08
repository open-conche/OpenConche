"""Chocolatier 1 Story Mode: a new user plays through the tutor to the live San Francisco port, hovers and clicks buildings, buys at the
market, switches ledger tabs; a second run on the same user folder lists the user in Change Player and resumes Story.

Docs: docs/re/choc1-fit.md, docs/re/choc1-rollover-and-quirks.md.

Run 1 (empty user dir): Story Mode -> ui/entername.lua -> the name "Tester" -> the Lua creates the user and the game and enters San
Francisco: ui/portview.lua is the base modal with the ledger over it and the tutor's ui/chartalk.lua on top (the shipped Lua never
loads ui/intro.lua on this path; it is checked on its own at the end). The tutor is walked through (No Thanks, OK, the company-logo
builder's Done, the recipe book, the award) to the live port. A hover over the Cliff Chalet opens the Rollover popup with its name
(instant: no fade; screenshot), moving away closes it, a click opens its dialog through the Rollover click path (DispatchRolloverClick
-> OnPopupClick, click_2 sound). The Ferry Market's buy/sell dialog: typing a quantity updates the receipt (ontext DynamicWindow),
the number pad's Buy and the OK button return to the port with the money and the messages changed. UtilSetLedgerTab reaches the
ledgers of the dialog and of the port underneath at once (open question 8). The Factory tab switches the ledger's body.

Then the factory mini-game (docs/re/choc1-factory.md): with one batch of ingredients (the Ferry Market sells no cacao, so
it is added through gSim:AdjustInventory), Brickhouse Square's status dialog opens, its minigame button opens the named `minigame`
modal with the FactoryGame (state 0, a 182-entry queue, the keeper's start dialog), Start runs the countdown (state 1, digit 2 at
880 ms), a shot between two trays is the first waste and pauses the game with the character's warning (state 3, the paused frame,
the Resume label, the game sound paused), the frame's resume button, a Lua TogglePause() and the HUD pause button resume and pause, Escape pauses the resumed run (the exe
loop's key-27 check) and the HUD button resumes it again,
the harness's autoshoot plays the 60 s (the speed rises, trays are collected, the counter counts), the run ends in state 5 with a
verdict and the end dialog, OK returns the product count as the weekly rate (status dialog, gSim.minigames, one sim week), and
leaving restores the port ambient. Screenshots of the status dialog, the start dialog, the countdown, the warning, mid-run, the end
and the status dialog after OK are written and looked at by eye (they are only checked for size and not being black here).

Run 2 (same user dir): Change Player lists Tester (ChoosePlayer), Story resumes the saved game on ui/map.lua (MapScreen is a
placeholder), and hovering a port icon opens the travel tag (absolute placement).

Asserts: exit 0, no [lua-error] / [engine-error] / [native-error] / [native-warning], the [game] line says choc1, only StartMovie
unimplemented, only the allow-listed placeholder types per run, non-black 800x600 screenshots."""
import argparse, os, re, shutil, struct, subprocess, sys, zlib

ap = argparse.ArgumentParser()
ap.add_argument("--openconche", required=True)
ap.add_argument("--assets", required=True)
ap.add_argument("--workdir", required=True)
a = ap.parse_args()
shutil.rmtree(a.workdir, ignore_errors=True)  # a fresh user dir: no users on the first run
os.makedirs(a.workdir)
user = os.path.join(a.workdir, "user")
errors = []

# StartMovie is the SDK's intro movie (an empty name, skipped). Everything else this path touches is real.
ALLOWED_UNIMPLEMENTED = {"StartMovie"}
# The factory mini-game's gun cursor and arrow are set for real through the app's cursor service; no prefix allowance.
ALLOWED_UNIMPLEMENTED_PREFIXES = ()


def run(tag, text, placeholders):
    script = os.path.join(a.workdir, tag + ".script")
    with open(script, "w") as f:
        f.write(text)
    unimpl = os.path.join(a.workdir, tag + "-unimplemented.txt")
    log = os.path.join(a.workdir, tag + "-log.txt")
    r = subprocess.run([a.openconche, "--headless", "--assets", a.assets, "--userdir", user, "--script", script, "--log-unimplemented", unimpl,
                        "--log", log], cwd=a.workdir, capture_output=True, text=True, timeout=240)
    out = r.stdout
    with open(os.path.join(a.workdir, tag + "-out.txt"), "w") as f:
        f.write(out)
    if r.returncode != 0:
        errors.append("%s: exit %d, tail %r" % (tag, r.returncode, out[-600:]))
    for bad in ("[lua-error]", "[engine-error]", "[native-error]", "[native-warning]", "FAIL"):
        if bad in out:
            errors.append("%s: output contains %s" % (tag, bad))
    if "[game] choc1 " not in out:
        errors.append("%s: no '[game] choc1' line (game detection)" % tag)
    for m in re.finditer(r"^\[unimplemented-type\] (\S+)", out, flags=re.M):
        if m.group(1) not in placeholders:
            errors.append("%s: unexpected placeholder window type %s" % (tag, m.group(1)))
    if os.path.exists(unimpl):
        for line in open(unimpl):
            name = line.split("\t")[0].strip()
            if name and name not in ALLOWED_UNIMPLEMENTED and not name.startswith(ALLOWED_UNIMPLEMENTED_PREFIXES):
                errors.append("%s: unexpected unimplemented native %s" % (tag, name))
    else:
        errors.append("%s: no --log-unimplemented file" % tag)
    return out, (open(log).read() if os.path.exists(log) else "")


def dumps(out):
    """Each window dump as (modal depth, [modal script paths], text)."""
    res = []
    for block in re.split(r"(?=^\[windows\] modal depth)", out, flags=re.M):
        m = re.match(r"\[windows\] modal depth (\d+)", block)
        if m:
            res.append((int(m.group(1)), re.findall(r'^  Modal "[^"]*" .*? script="([^"]*)"', block, flags=re.M), block))
    return res


def png_pixels(path):
    """(width, height, rows of RGB(A) byte strings, bytes per pixel) of an 8-bit RGB/RGBA PNG, unfiltered."""
    data = open(path, "rb").read()
    w, h, depth, ctype = struct.unpack(">IIBB", data[16:26])
    assert depth == 8 and ctype in (2, 6), "unsupported PNG"
    bpp = 3 if ctype == 2 else 4
    pos, idat = 8, b""
    while pos < len(data):
        n, typ = struct.unpack(">I4s", data[pos:pos + 8])
        if typ == b"IDAT":
            idat += data[pos + 8:pos + 8 + n]
        pos += 12 + n
    raw = zlib.decompress(idat)
    rows, prev, stride = [], bytearray(w * bpp), w * bpp
    for y in range(h):
        f, line = raw[y * (stride + 1)], bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if f == 1: line[i] = (line[i] + a) & 255
            elif f == 2: line[i] = (line[i] + b) & 255
            elif f == 3: line[i] = (line[i] + (a + b) // 2) & 255
            elif f == 4:  # Paeth: the distances pick which neighbour (left, above, above-left) predicts the byte
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(bytes(line))
        prev = line
    return w, h, rows, bpp


def png_not_black(path):
    w, h, rows, bpp = png_pixels(path)
    return sum(1 for r in rows for b in r if b) > (w * h * bpp) // 100


def white_share(path, x0, y0, x1, y1):
    """Share of the pixels in the box that are (nearly) white."""
    w, h, rows, bpp = png_pixels(path)
    n = hit = 0
    for y in range(y0, y1):
        for x in range(x0, x1):
            px = rows[y][x * bpp:x * bpp + 3]
            n += 1
            hit += min(px) >= 240
    return hit / n


def line_of(block, kind, name):
    m = re.search(r'^\s*%s "%s" .*$' % (kind, re.escape(name)), block, flags=re.M)
    return m.group(0) if m else None


def fired(block, sound):
    m = re.search(r"^audio fired .*?\b%s=(\d+)" % re.escape(sound), block, flags=re.M)
    return int(m.group(1)) if m else 0


shots = {k: os.path.join(a.workdir, k + ".png") for k in ("tutor", "port", "hover", "buysell", "receipt", "tab", "intro", "port2", "chooser", "map",
                                                          "maptag", "trip", "merida", "fstatus", "fstart", "fcount", "fmiss", "frun", "fend", "fafter")}
MENU_READY = "expect-window ui/mainmenu.lua 20000\nwait 1500\n"  # the menu's Update has run by then
SHOT = lambda k: "screenshot-logical %s\n" % shots[k]

# ---- run 1: a new user, Story Mode, the tutor, the live port ----
steps = [MENU_READY,
         "click 400 298\nexpect-window ui/entername.lua 5000\nwait 500\ntype Tester\nkey Return\n",
         "expect-window ui/chartalk.lua 8000\nwait 1000\ndump\n" + SHOT("tutor"),                  # D1: the tutor's first page
         "click 225 315\nwait 800\nclick 470 315\nexpect-window ui/logobuilder.lua 5000\nwait 800\n",  # No Thanks, OK
         "click 625 355\nexpect-window ui/dialog.lua 5000\nwait 800\n",                             # the logo builder's Done: the recipe book
         "click 598 350\nexpect-window ui/reward.lua 5000\nwait 800\n",                              # close it: the award
         "click 545 492\nwait 3000\ndump\n" + SHOT("port"),                                          # D2: the live port
         "move 620 120\nwait 300\ndump\n" + SHOT("hover"),                                           # D3: the Cliff Chalet's popup
         "move 700 300\nwait 300\ndump\n",                                                           # D4: it closed
         "move 20 110\nwait 300\ndump\n",                                                            # D4b: the Brickhouse Square factory at x=-3; (20,110) is under the steamship and sf_clouds windows (they pass the hover on) but left of the PlayerLogo button (x >= 44)
         "move 700 300\nwait 6000\ndump\n" + SHOT("port2"),                                          # D4c: the sprites have moved
         "click 620 120\nexpect-window ui/chartalk.lua 5000\nwait 500\ndump\n",                      # D5: the building's dialog
         "click 225 315\nwait 800\n",                                                                # No Thanks: back to the port
         "click 200 330\nexpect-window ui/buysell.lua 5000\nwait 800\ndump\n" + SHOT("buysell"),     # D6: the Ferry Market
         "lua UtilSetLedgerTab('inventory')\nwait 300\ndump\n",                                      # D7: both ledgers' tabs
         "click 280 288\nwait 300\ntype 3\nwait 500\ndump\n" + SHOT("receipt"),                      # D8: the receipt follows the quantity
         "click 350 283\nwait 500\nclick 710 350\nwait 4500\ndump\n",                                # D9: bought, back at the port
         "click 498 580\nwait 500\ndump\n" + SHOT("tab"),                                            # D10: the Factory tab
         "lua UtilSetLedgerTab('inventory')\nwait 300\ndump\n",                                      # D11: the Inventory tab
         # ---- the factory mini-game (docs/re/choc1-factory.md) ----
         # The Ferry Market has no cacao, and Basic Chocolate Bars need one sugar (bought above) and one cacao per batch: the
         # real flow would travel to another port, so cacao is added with the sim's own call.
         "lua gSim:AdjustInventory('cacao', 3)\nclick 20 110\nexpect-dump 5000 Factory Setup\nwait 800\ndump\n" + SHOT("fstatus"),     # F1: Brickhouse Square's status dialog
         "click 506 288\nexpect-dump 5000 FactoryGame \"factory\"\nwait 800\ndump\n" + SHOT("fstart"),                     # F2: the start dialog, state 0
         "click 390 328\nwait 900\ndump\n" + SHOT("fcount"),                                                            # F3: the countdown, state 1
         # The gun is first loaded at elapsed ~1140 ms with the wheel at about -0.75 rad: a shot at the gap between two of the seven
         # trays (aim angle -0.30 rad, 250 px out) leaves the playfield. It is the first waste of the game and the first game is for
         # basebars, so the game pauses and the character's warning opens.
         "expect-dump 20000 gun=loaded\nclick 539 224\nwait 1000\ndump\n" + SHOT("fmiss"),                             # F4: misses=1, state 3, the warning
         "click 475 315\nwait 800\ndump\n",                                                                          # F5: warning closed, still paused
         "click 321 325\nwait 300\ndump\n",                                                                          # F6: the frame's resume button
         "lua TogglePause()\nwait 300\ndump\n",                                                                      # F7: the native pauses
         "click 701 438\nwait 300\ndump\n",                                                                          # F8: the HUD pause button resumes
         "key Escape\nwait 300\ndump\n",                                                                                  # F8b: Escape pauses the run (the loop's key-27 check)
         "click 701 438\nwait 300\ndump\n",                                                                              # F8c: the HUD pause button resumes after Escape
         "focus lost\nwait 300\ndump\nfocus gained\nwait 300\ndump\n",                                                  # F9, F10: losing focus pauses, regaining does not resume
         "click 701 438\nwait 300\ndump\n",                                                                          # F11: resumed
         "autoshoot 70000\nwait 15000\ndump\n" + SHOT("frun"),                                                       # F12: playing by the harness aim
         "expect-dump 70000 state=5\nwait 600\ndump\n" + SHOT("fend"),                                              # F13: the run is over, the verdict
         "click 380 328\nwait 1500\ndump\n" + SHOT("fafter") +
         "expect-lua 1000 (function() local r = LFactory:ByPort('sanfrancisco').rate for _, m in ipairs(gSim.messages) do if m == 'Adjusted the San Francisco factory to make ' .. r .. ' cases of Basic Chocolate Bars per week' then return r > 0 end end end)()\n"
         "lua print('FACTORYRATE', LFactory:ByPort('sanfrancisco').rate, gSim.minigames, gSim.time)\n",                                                       # F14: OK: the status dialog shows the rate
         "click 708 342\nwait 1500\ndump\n",                                                                         # F15: leave: the port
         # ui/intro.lua is not loaded by anything on this path; it still has to build and its button has to enter the port.
         "lua table.insert(gCommandQueue, function() DoModal('ui/intro.lua') end)\nwait 500\ndump\n" + SHOT("intro"),  # D12
         "click 400 565\nwait 1500\ndump\n",                                                         # D13: Start Game
         "quit\n"]
out1, log1 = run("run1", "".join(steps), set())  # Logo, HiscoreWindow, FactoryGame, DiceGame and MapScreen are all real
d = dumps(out1)
if len(d) != 32:
    errors.append("run 1: expected 32 window dumps, got %d" % len(d))
else:
    (tutor, port, hover, away, fhover, port2d, click, market, tabs1, receipt, bought, factory, inventory,
     fstatus, fstart, fcount, fmiss, fwarned, fresumed, fpaused2, fresumed2, fesc, fescres, flost, fgained, fresumed3, frun, fend, fafter, fport, intro, started) = d
    if tutor[1] != ["ui/portview.lua", "ui/chartalk.lua"] or "Greetings, Tester" not in tutor[2]:
        errors.append("run 1: the tutor is not on top of the port view: %r" % (tutor[1],))
    if port[1] != ["ui/portview.lua"]:
        errors.append("run 1: the port view is not the only modal: %r" % (port[1],))
    if not re.search(r'Logo "" id=\d+ rect=44,72,106,60 bg=\S+ graphic=\S+ scale=0\.30\d+ ', port[2]):
        errors.append("run 1: the factory's player-sign Logo window is missing from the port: %r" %
                      re.findall(r'Logo .*', port[2]))
    for name in ("sfmarket", "sfsaloon", "sftutorplace", "sfshop", "sffactory"):
        if not line_of(port[2], "Rollover", name):
            errors.append("run 1: the port has no Rollover %s" % name)
    for b in ("messages", "factories", "inventory"):
        if not line_of(port[2], "Button", b):
            errors.append("run 1: the ledger has no %s tab" % b)
    if not line_of(port[2], "Text", "money") or 'label="$3,500"' not in line_of(port[2], "Text", "money"):
        errors.append("run 1: the money text is not $3,500: %r" % line_of(port[2], "Text", "money"))
    if 'DynamicWindow "ledgertabs"' not in port[2]:
        errors.append("run 1: no ledgertabs DynamicWindow")
    if "RolloverPopup" in port[2]:
        errors.append("run 1: a popup is open before the first hover")
    if "RolloverPopup" not in hover[2] or 'label="The Cliff Chalet"' not in hover[2]:
        errors.append("run 1: hovering sftutorplace did not open a popup with its name")
    fl = line_of(port[2], "Rollover", "sffactory") or ""
    if " rect=-3,53,198,143 " not in fl:
        errors.append("run 1: the factory Rollover is not at -3,53,198,143: %r" % fl)
    for name, rect in (("sfmarket", "0,42,475,322"), ("sfsaloon", "359,150,131,133"), ("sfshop", "480,205,"), ("sftutorplace", "567,75,")):
        if " rect=" + rect not in (line_of(port[2], "Rollover", name) or ""):
            errors.append("run 1: Rollover %s is not at %s" % (name, rect))
    if "RolloverPopup" not in fhover[2] or 'label="Brickhouse Square"' not in fhover[2]:
        errors.append("run 1: hovering the factory at x=-3 did not open the Brickhouse Square popup")
    # The path sprites move: positions in the dumps differ, and the steamship (a path from x=1200) is not drawn at 0,0.
    def anim_pos(block, name):
        m = re.search(r'SimpleAnimation "%s" .*? pos=(-?\d+),(-?\d+)' % name, block)
        return m.groups() if m else None
    # A still is centred on its point: sf_clouds (800x128 at animy 64) has its top edge at 0; the far clouds set flip=false.
    cl = re.search(r'SimpleAnimation "[^"]*" [^\n]*source="ports/sf_clouds"[^\n]*', port[2])
    if not cl or " top=0" not in cl.group(0):
        errors.append("run 1: the sf_clouds still is not centred on its point (top should be 0): %r" % (cl.group(0) if cl else None))
    far = re.search(r'SimpleAnimation "[^"]*" [^\n]*source="ports/clouds_far"[^\n]*', port[2])
    if not far or " mirror=0" not in far.group(0):
        errors.append("run 1: the far clouds (flip=false) are mirrored: %r" % (far.group(0) if far else None))
    # flip defaults to true: a sprite moving right is mirrored (the buggy's art faces left), moving left is not.
    hb = [anim_pos(b, "horsebuggy_far") for b in (port[2], port2d[2])]
    mir = [re.search(r'SimpleAnimation "horsebuggy_far" [^\n]* mirror=(\d)', b) for b in (port[2], port2d[2])]
    if hb[0] and hb[1] and all(mir):
        dx = int(hb[1][0]) - int(hb[0][0])
        if dx > 0 and mir[1].group(1) != "1" or dx < 0 and mir[1].group(1) != "0":
            errors.append("run 1: the far horse buggy moved %+d px but mirror=%s" % (dx, mir[1].group(1)))
    s1, s2 = anim_pos(port[2], "steamship_close"), anim_pos(port2d[2], "steamship_close")
    if not s1 or not s2 or s1 == s2 or s1 == ("0", "0") or s2 == ("0", "0"):
        errors.append("run 1: the steamship does not move along its path: %r then %r" % (s1, s2))
    if "RolloverPopup" in away[2]:
        errors.append("run 1: the popup stayed after the pointer left its hot rectangle")
    if click[1] != ["ui/portview.lua", "ui/chartalk.lua"] or "Felix" not in click[2]:
        errors.append("run 1: clicking the Cliff Chalet did not open its dialog: %r" % (click[1],))
    if fired(click[2], "audio/sfx/click_2.ogg") <= fired(away[2], "audio/sfx/click_2.ogg"):
        errors.append("run 1: the Rollover's clicksound (click_2) did not play on the click")
    if market[1] != ["ui/portview.lua", "ui/buysell.lua"] or 'label="Ferry Market"' not in market[2]:
        errors.append("run 1: the Ferry Market dialog is not open: %r" % (market[1],))
    if 'DynamicWindow "receipt"' not in market[2] or " ontext" not in line_of(market[2], "DynamicWindow", "receipt"):
        errors.append("run 1: the receipt is not an ontext DynamicWindow")
    if "BuySellActivator('sugar'" not in market[2]:
        errors.append("run 1: the dialog's item Rollovers are missing")
    # Open question 8: UtilSetLedgerTab from a script reaches the dialog's ledger and the port's under it.
    on = re.findall(r'Button "inventory" [^\n]*\bstate=\d+ layer=\d+ on\b', tabs1[2])
    if len(on) != 2:
        errors.append("run 1: UtilSetLedgerTab('inventory') set %d inventory buttons on, expected the dialog's and the port's" % len(on))
    if 'label="x 3: $9"' not in receipt[2] and "x 3: $9" not in receipt[2]:
        errors.append("run 1: the receipt does not show 3 sugars for $9")
    if "x 3" in market[2]:
        errors.append("run 1: the receipt already had a quantity before typing")
    if bought[1] != ["ui/portview.lua"] or 'label="$3,491"' not in (line_of(bought[2], "Text", "money") or ""):
        errors.append("run 1: after buying 3 sugar the money is not $3,491 on the port: %r" % line_of(bought[2], "Text", "money"))
    if "Bought 3 sacks of Sugar" not in bought[2]:
        errors.append("run 1: the message log does not report the purchase")
    fl = line_of(factory[2], "Button", "factories") or ""
    if " on " not in fl + " " or " off" not in (line_of(factory[2], "Button", "messages") or ""):
        errors.append("run 1: the Factory tab is not switched on: %r" % fl)
    if 'Text "messageout"' not in bought[2] or 'Text "messageout"' in factory[2]:
        errors.append("run 1: the Factory tab did not replace the Messages body")
    il = line_of(inventory[2], "Button", "inventory") or ""
    if " on" not in il or " off" not in (line_of(inventory[2], "Button", "factories") or ""):
        errors.append("run 1: UtilSetLedgerTab('inventory') did not switch the tab: %r" % il)
    # ---- the factory mini-game ----
    def fg(block):
        m = re.search(r'FactoryGame "factory" [^\n]*', block)
        if not m:
            return {}
        line = m.group(0)
        kv = dict(re.findall(r"\b(\w+)=(\S+)", line.split(" trays=")[0]))
        kv["line"] = line
        return kv
    def audio_pause(block):
        m = re.search(r"^audio levels .*?\bpause=(\S+)", block, flags=re.M)
        return m.group(1) if m else None
    if fstatus[1] != ["ui/portview.lua", "ui/dialog.lua"] or 'label="Basic Chocolate Bars"' not in fstatus[2]:
        errors.append("run 1: clicking Brickhouse Square did not open the status dialog for Basic Chocolate Bars: %r" % (fstatus[1],))
    if 'Text "" ' not in fstatus[2] or ' label="0" size=70' not in fstatus[2]:
        errors.append("run 1: the status dialog does not show the rate 0 (nothing made yet)")
    if fstart[1][-1:] != ["ui/dialog.lua"] or 'Modal "minigame"' not in fstart[2]:
        errors.append("run 1: the minigame button did not open the named minigame modal: %r" % (fstart[1],))
    g0 = fg(fstart[2])
    if g0.get("state") != "0" or g0.get("gun") != "empty" or g0.get("queue") != "182" or g0.get("template") != "sugar,cacao":
        errors.append("run 1: the FactoryGame is not idle with a 182-entry queue and the sugar,cacao template: %r" % g0.get("line"))
    if len(re.findall(r"\b\d@\d+,\d+:0/2", g0.get("line", ""))) != 7:
        errors.append("run 1: the FactoryGame does not have seven empty trays of two slots")
    if "Aim and click to fire ingredients" not in fstart[2] or 'Button "" ' not in fstart[2]:
        errors.append("run 1: the keeper's start dialog text is missing")
    if "FactoryGame" in re.findall(r"^\[unimplemented-type\] (\S+)", out1, flags=re.M):
        errors.append("run 1: FactoryGame is a placeholder")
    gc = fg(fcount[2])
    if gc.get("state") != "1" or 'Text "countdown" ' not in fcount[2] or 'label="2"' not in line_of(fcount[2], "Text", "countdown"):
        errors.append("run 1: the countdown is not at state 1 showing 2 (elapsed 880 ms): %r" % (gc.get("line"),))
    if not (860 <= int(gc.get("elapsed", "0")) <= 940) or float(gc.get("angle", "0")) >= 0:
        errors.append("run 1: the countdown did not turn the wheel (angle should be negative): %r" % (gc.get("line"),))
    if fired(fcount[2], "audio/sfx/countdown_tick.ogg") != 1:
        errors.append("run 1: the countdown tick did not play exactly once by 880 ms")
    if 'Frame "paused" ' not in fstart[2] or " hidden" not in line_of(fstart[2], "Frame", "paused"):
        errors.append("run 1: the paused frame is not hidden at the start")
    # The first waste: misses=1, state 3 (paused), the warning (a chartalk modal) over the minigame, the frame and label say paused.
    gm = fg(fmiss[2])
    if gm.get("state") != "3" or gm.get("misses") != "1" or fired(fmiss[2], "audio/sfx/ingredient_failure_01.ogg") != 1:
        errors.append("run 1: the shot at the gap between trays was not a miss that paused the game: %r" % (gm.get("line"),))
    if fmiss[1][-2:] != ["ui/dialog.lua", "ui/chartalk.lua"] or "Wasted ingredient" not in fmiss[2]:
        errors.append("run 1: the first waste did not open the character's warning over the minigame: %r" % (fmiss[1],))
    if 'label="Resume"' not in line_of(fmiss[2], "Text", "pause_label") or " hidden" in line_of(fmiss[2], "Frame", "paused"):
        errors.append("run 1: while paused the label should read Resume and the paused frame show")
    if audio_pause(fmiss[2]) != "game":
        errors.append("run 1: pausing the factory did not pause the game sound: %r" % audio_pause(fmiss[2]))
    if abs(float(gm.get("speed", "0")) - 1.0) > 1e-6:
        errors.append("run 1: a waste at start speed must not change the speed: %r" % gm.get("speed"))
    gw = fg(fwarned[2])
    if fwarned[1][-1:] != ["ui/dialog.lua"] or gw.get("state") != "3" or gw.get("elapsed") != gm.get("elapsed"):
        errors.append("run 1: closing the warning should leave the game paused and frozen: %r" % (gw.get("line"),))
    gr = fg(fresumed[2])
    if gr.get("state") != "2" or audio_pause(fresumed[2]) != "none" or " hidden" not in line_of(fresumed[2], "Frame", "paused") \
            or 'label="Pause"' not in line_of(fresumed[2], "Text", "pause_label"):
        errors.append("run 1: the paused frame's resume button did not resume: %r" % (gr.get("line"),))
    gp = fg(fpaused2[2])
    if gp.get("state") != "3" or int(gp.get("elapsed", "0")) < int(gr.get("elapsed", "0")) or audio_pause(fpaused2[2]) != "game":
        errors.append("run 1: TogglePause() from Lua did not pause: %r" % (gp.get("line"),))
    gr2 = fg(fresumed2[2])
    if gr2.get("state") != "2" or int(gr2.get("elapsed", "0")) < int(gp.get("elapsed", "0")):
        errors.append("run 1: the HUD pause button did not resume: %r" % (gr2.get("line"),))
    # The app losing the foreground pauses a running game (exe 0040afc0); regaining it does not resume.
    # Escape pauses a running game (the exe main loop's key-27 check, choc1-natives.md section 8) - the same pause
    # helper as the close and deactivate messages, so an already-paused game is skipped and the HUD button resumes.
    gesc, gescr = fg(fesc[2]), fg(fescres[2])
    if gesc.get("state") != "3" or 'label="Resume"' not in (line_of(fesc[2], "Text", "pause_label") or "") \
            or " hidden" in (line_of(fesc[2], "Frame", "paused") or "") or audio_pause(fesc[2]) != "game":
        errors.append("run 1: Escape did not pause the running factory game: %r" % (gesc.get("line"),))
    if gescr.get("state") != "2" or int(gescr.get("elapsed", "0")) < int(gesc.get("elapsed", "0")):
        errors.append("run 1: the HUD pause button did not resume after Escape: %r" % (gescr.get("line"),))
    gl, gg, gr3 = fg(flost[2]), fg(fgained[2]), fg(fresumed3[2])
    if gl.get("state") != "3" or gg.get("state") != "3" or gg.get("elapsed") != gl.get("elapsed"):
        errors.append("run 1: losing the foreground did not pause the game, or regaining it resumed: %r then %r" % (gl.get("state"), gg.get("state")))
    if gr3.get("state") != "2":
        errors.append("run 1: the HUD pause button did not resume after the focus pause: %r" % (gr3.get("line"),))
    # Mid-run: the harness aim fills trays (the speed has gone up), none of the trays is a power-up.
    gu = fg(frun[2])
    if gu.get("state") != "2" or int(gu.get("elapsed", "0")) < 12000 or int(gu.get("count", "0")) < 1 or float(gu.get("speed", "1")) <= 1.0:
        errors.append("run 1: mid-run the game is not producing: %r" % (gu.get("line"),))
    if 'Text "timer" ' not in frun[2] or not re.search(r'Text "timer" [^\n]*label="0:[0-4]\d"', frun[2]):
        errors.append("run 1: the timer text does not count down: %r" % line_of(frun[2], "Text", "timer"))
    if not re.search(r'Text "counter" [^\n]*label="%s"' % gu.get("count", "x"), frun[2]):
        errors.append("run 1: the counter text is not the product count %s" % gu.get("count"))
    # The end: state 5 and a verdict, the keeper's end dialog with that verdict's text.
    ge = fg(fend[2])
    if ge.get("state") != "5" or not ge.get("verdict", "").startswith("production_") or ge.get("verdict") == "production_stopped":
        errors.append("run 1: the run did not end normally in state 5 with a verdict: %r" % (ge.get("line"),))
    if int(ge.get("count", "0")) < 5:
        errors.append("run 1: the harness aim made only %s cases in 60 s" % ge.get("count"))
    if fired(fend[2], "audio/sfx/factory_shut_down.ogg") != 1 or fired(fend[2], "audio/sfx/mini_game_production_over.ogg") != 1 \
            or fired(fend[2], "audio/sfx/warning_bell.ogg") != 1 or fired(fend[2], "audio/sfx/minigame_starting_bell.ogg") != 1:
        errors.append("run 1: the end-of-run sounds did not each play once")
    if "Try Again" not in fend[2] or " hidden" in (line_of(fend[2], "Window", "keeper") or "") or "Aim and click" in fend[2]:
        errors.append("run 1: the end dialog is not shown (Try Again) or still shows the start text")
    if audio_pause(fend[2]) != "none":
        errors.append("run 1: the game sound is still paused at the end")
    if not re.search(r"^audio music audio/music/minigame\.ogg", fend[2], flags=re.M):
        errors.append("run 1: the minigame tune is not playing during the run")
    # OK returns the product count: the rate in the status dialog, the message, the sim.
    gf = int(ge.get("count", "0"))
    if fafter[1] != ["ui/portview.lua", "ui/dialog.lua"] or 'label="%d" size=70' % gf not in fafter[2]:
        errors.append("run 1: after OK the status dialog does not show the rate %d: modals %r" % (gf, fafter[1]))
    rate = re.search(r"^FACTORYRATE\s+(\d+)\s+(\d+)\s+(\d+)", out1, flags=re.M)
    if not rate or int(rate.group(1)) != gf or rate.group(2) != "1":
        errors.append("run 1: the factory rate / minigames count after OK are %r, expected %d and 1" % (rate.groups() if rate else None, gf))
    if not rate or int(rate.group(3)) != 7:
        errors.append("run 1: the sim week after the mini-game is %r, expected 7 (gSim:Tick ran once)" % (rate.group(3) if rate else None))
    if fport[1] != ["ui/portview.lua"]:
        errors.append("run 1: leaving the factory did not return to the port view: %r" % (fport[1],))
    if not re.search(r"^audio env audio/sfx/port_loop\.ogg", fport[2], flags=re.M):
        errors.append("run 1: the port ambient (port_loop) was not restored after the mini-game")
    if not re.search(r"^audio env none", fresumed2[2], flags=re.M):
        errors.append("run 1: the ambient was not cleared by the start button")
    if intro[1][-1] != "ui/intro.lua" or "Welcome, Chocolatier!" not in intro[2]:
        errors.append("run 1: ui/intro.lua did not build: %r" % (intro[1],))
    if "Start Game" not in intro[2]:
        errors.append("run 1: the intro has no button")
    if "ui/chartalk.lua" not in started[1] or "ui/portview.lua" not in started[1]:
        errors.append("run 1: the intro's button did not enter the port and select the tutor's place: %r" % (started[1],))
for k in ("tutor", "port", "port2", "hover", "buysell", "receipt", "tab", "intro", "fstatus", "fstart", "fcount", "fmiss", "frun", "fend", "fafter"):
    try:
        w, h, _, _ = png_pixels(shots[k])
        if (w, h) != (800, 600):
            errors.append("%s screenshot is %dx%d" % (k, w, h))
        elif not png_not_black(shots[k]):
            errors.append("the %s screenshot is (nearly) all black" % k)
    except (OSError, AssertionError) as e:
        errors.append("no %s screenshot: %s" % (k, e))
try:
    # The popup is a white box with the building's name at the pointer + 20: white there, not before the hover.
    if white_share(shots["hover"], 645, 124, 705, 140) < 0.5 or white_share(shots["port"], 645, 124, 705, 140) > 0.2:
        errors.append("the hover screenshot has no white popup box right of the pointer")
except (OSError, AssertionError) as e:
    errors.append("hover pixel check: %s" % e)
try:
    # The factory status dialog's Bitmap honours its `mask` (red-channel JPEG mask): the picture's corner is transparent, so the port
    # shows through (not the picture's white).
    w_, h_, rows, bpp = png_pixels(shots["fstatus"])
    fs = re.search(r'^ *Bitmap "" id=\d+ rect=(\d+),(\d+),\d+,\d+ image="image/factory_status"', fstatus[2], flags=re.M)
    fx, fy = int(fs.group(1)), int(fs.group(2))  # (the picture sits at the dialog's origin, which is the screen's)
    for dx, dy in ((2, 3), (3, 6), (2, 20)):
        px = rows[fy + dy][(fx + dx) * bpp:(fx + dx) * bpp + 3]
        if min(px) >= 235:
            errors.append("the factory status dialog's corner (%d,%d) is white %r: its mask is not honoured" % (fx + dx, fy + dy, tuple(px)))
except (OSError, AssertionError, AttributeError, IndexError, NameError) as e:
    errors.append("status corner check: %r" % (e,))
try:
    # The pause button's "Pause" (docs/re/choc1-text-flags.md): one line of height 28 and its ink at button y 37..51, x 2..56 (the exe keeps
    # the overflowing last letter on the line; without it the word breaks into "Paus" / "e" and clips).
    blk = port[2]
    m = re.search(r'^ *Text "" id=\d+ rect=0,30,56,30 label="Pause" size=28 flags=5 lines=(\d+) lh=(\d+)', blk, flags=re.M)
    if not m or (m.group(1), m.group(2)) != ("1", "28"):
        errors.append("the pause button's Text is not one line of height 28: %r" % (m.group(0) if m else None))
    led = re.search(r'^ *DynamicWindow "" id=\d+ rect=(\d+),(\d+),\d+,\d+ contents="ui/ledger.lua"', blk, flags=re.M)
    bx, by = int(led.group(1)), int(led.group(2)) + 122  # the Button is at ledger.lua x=0, y=122
    w_, h_, rows, bpp = png_pixels(shots["port"])
    ink = [(x, y) for y in range(by + 30, by + 57) for x in range(bx, bx + 59)
           if rows[y][x * bpp] < 150 and rows[y][x * bpp] > rows[y][x * bpp + 2] + 25 and rows[y][x * bpp + 1] < rows[y][x * bpp]]  # the brown ink inside the lilac disc
    ys = [y - by for _, y in ink]
    xs = [x - bx for x, _ in ink]
    if not ink or not (36 <= min(ys) <= 38 and 50 <= max(ys) <= 52 and 1 <= min(xs) <= 4 and 54 <= max(xs) <= 57):
        errors.append("the pause label's ink is not at y 37..51, x 2..56: %r" % (((min(xs), max(xs), min(ys), max(ys)) if ink else None),))
except (OSError, AssertionError, AttributeError, IndexError, NameError) as e:
    errors.append("pause label check: %r" % (e,))
try:
    # The sky and water band (x 0..800, y 100..420) differs between two port screenshots 6 s apart: the clouds, boats and waves move.
    _, _, ra, bpp = png_pixels(shots["port"])
    _, _, rb, _ = png_pixels(shots["port2"])
    diff = sum(1 for y in range(100, 420) for x in range(0, 800, 2) if ra[y][x * bpp:x * bpp + 3] != rb[y][x * bpp:x * bpp + 3])
    if diff < 200:
        errors.append("the port screenshots 6 s apart barely differ (%d sampled pixels): the path sprites are not moving" % diff)
except (OSError, AssertionError) as e:
    errors.append("port movement pixel check: %s" % e)
for want in ("[save] saved empire", "[audio] file audio/sfx/click_2.ogg"):
    if want not in log1:
        errors.append("run 1: the log lacks %r" % want)
users = os.path.join(user, "users.txt")
if not os.path.exists(users) or "Tester" not in open(users).read():
    errors.append("run 1: users.txt does not list Tester")

# ---- run 2: the same user dir ----
steps = [MENU_READY + "dump\n",
         "click 117 395\nexpect-window ui/chooseplayer.lua 3000\nwait 500\ndump\n" + SHOT("chooser"),  # D1, D2: the chooser
         "click 367 503\nwait 500\ndump\n",                                                            # D3: OK closes it
         "click 400 298\nexpect-window ui/map.lua 8000\nwait 1500\ndump\n" + SHOT("map"),              # D4: Story resumes on the map
         "move 215 213\nwait 300\ndump\n" + SHOT("maptag"),                                            # D5: a port icon's travel tag
         # D6: the Merida icon's tag (a train, 1 week); D7: clicking the tag starts the trip (the Rollover click path ->
         # LPort:OnPopupClick -> InitiateTravel -> AnimateTravel); D8: hovering during the trip opens nothing; D9: the
         # arrival is the Merida port view. The user has rank 1, so a traveller encounter (20 % per week) is switched off for determinism.
         "lua LBuilding:ByName('travel').encounterOdds = 0\nlua gT0 = gSim.time\nlua gM0 = gSim.money\nmove 164 187\nwait 100\nmove 165 188\nwait 300\ndump\n",
         "click 165 188\nwait 1200\nlua gP = gTravelPrice\ndump\n" + SHOT("trip"),
         "move 164 187\nwait 100\nmove 165 188\nwait 300\ndump\n",
         "expect-window ui/portview.lua 5000\nwait 1000\ndump\n" + SHOT("merida"),
         "expect-lua 1000 gSim.port.name == 'merida' and gSim.time == gT0 + 1 and gM0 - gSim.money == gP and gP > 0\n"
         "expect-lua 1000 gTravelPrice == nil\n"
         "quit\n"]
out2, log2 = run("run2", "".join(steps), set())
d = dumps(out2)
if len(d) != 9:
    errors.append("run 2: expected 9 window dumps, got %d" % len(d))
else:
    menu, choose, closed, resumed, tag, mtag, trip, trip2, arrived = d
    if "Welcome to Chocolatier, Tester." not in menu[2]:
        errors.append("run 2: the welcome label does not name the user")
    if choose[1][-1:] != ["ui/chooseplayer.lua"] or "ChoosePlayer " not in choose[2]:
        errors.append("run 2: Change Player did not open the chooser: %r" % (choose[1],))
    if 'label="Tester"' not in (line_of(choose[2], "Text", "label0") or "") or "add another player" not in (line_of(choose[2], "Text", "label1") or ""):
        errors.append("run 2: the chooser does not list Tester and the new-player entry")
    if " on" not in (line_of(choose[2], "Button", "player0") or "") or " hidden" not in (line_of(choose[2], "Button", "player2") or ""):
        errors.append("run 2: the chooser's radios are wrong (Tester on, slot 2 hidden)")
    if choose[0] != 3 or closed[0] != 2:
        errors.append("run 2: the chooser did not open and close (depth %d then %d)" % (choose[0], closed[0]))
    if "ui/map.lua" not in resumed[1] or "MapScreen" not in resumed[2] or 'label="$3,491"' not in (line_of(resumed[2], "Text", "money") or ""):
        errors.append("run 2: Story did not resume the saved game on the map: %r" % (resumed[1],))
    if "RolloverPopup" not in tag[2] or "3-week journey" not in tag[2] or "image/traveltag" not in tag[2]:
        errors.append("run 2: hovering a port icon did not open the travel tag")
    ms = re.search(r'MapScreen "" [^\n]*trip=active end="merida" leg=1/1 elapsed=(\d+) dots=(\d+) vehicle=train', trip[2])
    if "RolloverPopup" not in mtag[2] or "A 1-week journey." not in mtag[2]:
        errors.append("run 2: hovering the Merida icon did not open its 1-week tag")
    if not ms:
        errors.append("run 2: no active MapScreen trip to Merida 1.2 s after the click: %r" % line_of(trip[2], "MapScreen", ""))
    elif not 1000 <= int(ms.group(1)) <= 1400 or int(ms.group(2)) != int(ms.group(1)) // 200:
        errors.append("run 2: the trip clock or the dot count is off: elapsed %s, dots %s" % ms.groups())
    if "ui/map.lua" not in trip[1][-1:] or 'label="February 18, 1880"' not in trip[2]:
        errors.append("run 2: the map is not still showing the departure date during the trip")
    if "RolloverPopup" in trip2[2] or "trip=active" not in trip2[2]:
        errors.append("run 2: a Rollover popup opened (or the trip ended) during the trip: the map-trip gate is not held")
    if arrived[1] != ["ui/portview.lua"] or 'label="February 25, 1880"' not in arrived[2] or "MapScreen" in arrived[2]:
        errors.append("run 2: the trip did not end on the Merida port view one week later: %r" % (arrived[1],))
    if "Arrived in Merida" not in arrived[2]:
        errors.append("run 2: the message log does not say Arrived in Merida")
    if fired(arrived[2], "audio/sfx/train.ogg") < 1:
        errors.append("run 2: the train sound (PlaySound('train')) did not play")
for k in ("chooser", "map", "maptag", "trip", "merida"):
    try:
        if png_pixels(shots[k])[:2] != (800, 600) or not png_not_black(shots[k]):
            errors.append("the %s screenshot is wrong" % k)
    except (OSError, AssertionError) as e:
        errors.append("no %s screenshot: %s" % (k, e))

if errors:
    print("FAIL choc1_story:\n  " + "\n  ".join(errors))
    sys.exit(1)
print("choc1_story ok")
