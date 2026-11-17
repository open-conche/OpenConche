"""Chocolatier 3 enhancements: the mouse wheel in the quest log and the cash register (docs/design/enhancements.md section 5).

Runs against the vanilla v1.0 game or the Reforged mod (--dataset), which share one overlay (module id choc3). A new Story game reaches the
Zurich port; the script makes 14 more quests active and unblocks the market, gives $3000 and ten cases of chocolate, then turns the
wheel (`wheel X Y N`, N signed, positive up) in each place:

- quest log: down scrolls the list (the window dump's `quest1` title follows), never past the game's own last position (the down arrow
  is gone and more notches change nothing); up returns to the top and stops there;
- buy register (Zurich market, sugar): up adds to the count, the game's own clamp stops it at what the money buys (count * price <= money <
  (count + 1) * price) and the subtotal follows; down subtracts one per notch, not below 0 (the subtotal then reads empty);
- sell register (Zurich shop): up stops at the stock (10).

Every dialog is closed with Escape. It runs twice per dataset: `--config enhancements=1` (every change above happens) and the default, which
for an automated run is off (the same wheel turns change nothing). Asserts: exit 0, no [lua-error] / [engine-error] / [native-error] /
[native-warning], every `expect-*` line ok, the `[enhancements]` line, and (on) screenshots that are 800x600 and not black."""
import argparse, os, re, shutil, struct, subprocess, sys, zlib

ap = argparse.ArgumentParser()
ap.add_argument("--openconche", required=True)
ap.add_argument("--assets", required=True)
ap.add_argument("--workdir", required=True)
ap.add_argument("--dataset", choices=("vanilla", "reforged"), required=True)
a = ap.parse_args()
shutil.rmtree(a.workdir, ignore_errors=True)
os.makedirs(a.workdir)
errors = []
reforged = a.dataset == "reforged"

if reforged:
    INTRO = ("expect-window ui/mainmenu.lua 60000\nwait 1300\nclick 310 345\nexpect-window ui/ui_entername.lua 5000\nwait 600\ntype Tester\n"
             "key Return\nexpect-window ui/ui_difficulty.lua 3000\nwait 800\nclick 328 400\nwait 5000\nclick 400 346\nwait 1500\n"
             "click 503 346\nwait 1500\n" + "click 400 346\nwait 1500\n" * 4 + "expect-window ui/portview.lua 3000\n")
    SUGAR, PRODUCT = "click 614 191\n", "click 490 200\n"
else:
    INTRO = ("expect-window ui/mainmenu.lua 60000\nwait 1300\nclick 400 345\nexpect-window ui/ui_entername.lua 5000\nwait 600\ntype Tester\n"
             "key Return\nexpect-window ui/portview.lua 5000\nwait 3000\n" + "click 400 346\nwait 1500\n" * 3)
    SUGAR, PRODUCT = "click 490 203\n", "click 490 200\n"

SETUP = ("lua n = 0; for name, q in pairs(_AllQuests) do if q.visible and not q.delivery and n < 14 and not Player.questsActive[name] then "
         "Player.questsActive[name] = Player.time + n; n = n + 1 end end; Player.buildingsBlocked = {}; Player.money = 3000; "
         "Player.products.b01 = 10\n"
         "lua function PadPrice() local _, _, p = string.find(GetLabel('subtotal'), 'x %$(%d+) ='); return tonumber(p) end\n")
QUEST_BUTTON = "click 193 382\nexpect-window ui/ui_questlog.lua 5000\nwait 1500\n"


def script(on, shots):
    def ex(expr_on, expr_off):
        return "expect-lua 100 " + (expr_on if on else expr_off) + "\n"

    def wheel(n):
        return "wheel %s %d\nwait 300\n" % (WHERE, n)

    s = INTRO + SETUP
    # Quest log.
    WHERE = "400 250"
    s += QUEST_BUTTON + "dump\n"                                   # dump 1: the top
    s += wheel(-1) + "dump\n"                                      # dump 2
    s += "screenshot-logical %s\n" % shots["quests"]
    s += wheel(-30) + "dump\n"                                     # dump 3: the bottom
    s += wheel(-5) + "dump\n"                                      # dump 4: still the bottom
    s += wheel(40) + "dump\n"                                      # dump 5: the top again
    s += wheel(3) + "dump\n"                                       # dump 6: and no further
    s += "key Escape\nwait 1500\n"
    # Buy register.
    WHERE = "500 200"
    s += "click 534 230\nwait 2500\nclick 400 346\nexpect-window ui/ui_market.lua 5000\nwait 1500\n" + SUGAR + "expect-window ui/ui_buysell.lua 5000\nwait 800\n"
    s += wheel(1) + "lua print('PRICEPROBE', PadPrice())\n"
    s += wheel(2) + ex("GetLabel('count') == '3'", "GetLabel('count') == '0'")
    s += wheel(-1) + ex("GetLabel('count') == '2'", "GetLabel('count') == '0'")
    s += "screenshot-logical %s\n" % shots["buy"]
    s += wheel(-10) + ex("GetLabel('count') == '0' and GetLabel('subtotal') == ''", "GetLabel('count') == '0'")
    s += wheel(1000)
    s += ex("tonumber(GetLabel('count')) * PadPrice() <= Player.money and (tonumber(GetLabel('count')) + 1) * PadPrice() > Player.money",
            "GetLabel('count') == '0'")
    s += "key Escape\nwait 1500\nkey Escape\nwait 1500\n"
    # Sell register.
    s += "click 107 215\nwait 2500\nclick 400 346\nexpect-window ui/ui_shop.lua 5000\nwait 1500\n" + PRODUCT + "expect-window ui/ui_buysell.lua 5000\nwait 800\n"
    s += wheel(4) + ex("GetLabel('count') == '4'", "GetLabel('count') == '0'")
    s += wheel(100) + ex("GetLabel('count') == '10'", "GetLabel('count') == '0'")
    s += "screenshot-logical %s\n" % shots["sell"]
    s += wheel(-100) + ex("GetLabel('count') == '0'", "GetLabel('count') == '0'")
    s += "quit\n"
    return s


def titles_and_arrows(block):
    """The ten quest titles (None when the row is hidden) and whether scrollUp / scrollDown are shown, from one window dump."""
    lines = block.splitlines()
    arrows = {}
    rows = {}
    for i, l in enumerate(lines):
        m = re.match(r'\s*Button "(scrollUp|scrollDown)" .*', l)
        if m:
            arrows[m.group(1)] = " hidden " not in l
        m = re.match(r'\s*Button "quest(\d+)" .*', l)
        if m:
            title = None
            for t in lines[i + 1:i + 6]:
                mt = re.match(r'\s*Text "label\d" .*?label="([^"]*)"', t)
                if mt and " hidden " not in t:
                    title = mt.group(1)
            rows[int(m.group(1))] = None if " hidden " in l else title
    return [rows.get(n) for n in range(1, 11)], arrows.get("scrollUp"), arrows.get("scrollDown")


def run(tag, on):
    work = os.path.join(a.workdir, tag)
    os.makedirs(work)
    shots = {k: os.path.join(work, k + ".png") for k in ("quests", "buy", "sell")}
    path = os.path.join(work, "enhance.script")
    open(path, "w").write(script(on, shots))
    cmd = [a.openconche, "--headless", "--assets", a.assets, "--userdir", os.path.join(work, "user"), "--log", os.path.join(work, "engine.log"),
           "--script", path]
    if on:
        cmd += ["--config", "enhancements=1"]
    r = subprocess.run(cmd, cwd=work, capture_output=True, text=True, timeout=400)
    out = r.stdout
    open(os.path.join(work, "out.txt"), "w").write(out)
    if r.returncode != 0:
        errors.append("%s: exit %d, tail %r" % (tag, r.returncode, out[-600:]))
    for bad in ("[lua-error]", "[engine-error]", "[native-error]", "[native-warning]", "FAIL"):
        if bad in out:
            errors.append("%s: output contains %s" % (tag, bad))
    log = open(os.path.join(work, "engine.log"), errors="replace").read()
    want = "[enhancements] on (choc3)" if on else "[enhancements] off"
    if want not in log:
        errors.append("%s: no '%s' line" % (tag, want))
    if len(re.findall(r"^\[script\] ok expect-", out, flags=re.M)) < 10:
        errors.append("%s: too few expect-* lines passed" % tag)
    if on and not re.search(r"\[input\] wheel \d+ \d+ notches", log):
        errors.append("%s: no wheel input in the log" % tag)
    dumps = [d for d in re.split(r"(?=^\[windows\] modal depth)", out, flags=re.M) if d.startswith("[windows]")]
    if len(dumps) != 6:
        errors.append("%s: expected 6 window dumps, got %d" % (tag, len(dumps)))
        return shots
    d = [titles_and_arrows(x) for x in dumps]
    top, down1, bottom, bottom2, top2, top3 = d
    if not (top[0][0] and top[0][1] and top[1] is False and top[2] is True):
        errors.append("%s: the quest log does not open at the top with only the down arrow: %r" % (tag, top))
    if on:
        if down1[0][:9] != top[0][1:10] or down1[1] is not True:
            errors.append("%s: one notch down did not move the list by one row: %r" % (tag, down1))
        if bottom[2] is not False or bottom[1] is not True or None in bottom[0]:
            errors.append("%s: thirty notches down did not stop at the game's last position: %r" % (tag, bottom))
        if bottom2 != bottom:
            errors.append("%s: the list over-scrolled at the bottom: %r" % (tag, bottom2))
        for name, x in (("up to the top", top2), ("past the top", top3)):
            if x != top:
                errors.append("%s: the list is not at the top after %s: %r" % (tag, name, x))
    else:
        for name, x in (("down", down1), ("bottom", bottom), ("bottom2", bottom2), ("top2", top2), ("top3", top3)):
            if x != top:
                errors.append("%s: the quest log moved on a wheel turn with enhancements off (%s)" % (tag, name))
    return shots


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


shots = run("on", True)
for k, p in shots.items():
    try:
        d = open(p, "rb").read()
        if struct.unpack(">II", d[16:24]) != (800, 600) or not png_not_black(p):
            errors.append("the %s screenshot is wrong" % k)
    except OSError as e:
        errors.append("no %s screenshot: %s" % (k, e))
run("off", False)

if errors:
    print("FAIL\n" + "\n".join(errors))
    sys.exit(1)
print("Chocolatier 3 enhancements OK (%s)" % a.dataset)
