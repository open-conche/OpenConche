"""Chocolatier 2 enhancements: the mouse wheel on the ledger and the number pad (docs/design/enhancements.md section 5).

One fresh Story game reaches the San Francisco port (the tutor dialogs popped as in check_choc2_ledger.py); the script gives the
player money and 40 messages, then turns the wheel (`wheel X Y N`, N signed, positive up) in each place:

- ledger messages tab: the pointer over the book. Up presses scroll_up (seven lines a notch, down to line 1), down scroll_down (back
  up to the last page, where the game hides the arrow and nothing more happens);
- ledger inventory tab (three of every raw ingredient, more than one page): down moves inventoryTop by a page, up returns to 1, and up
  again stays at 1;
- the buy pad (Ferry Market, sugar at $30): up adds notches to the field and the subtotal follows, down subtracts, never below 0;
- the sell pad (Shop, ten of a product): a wheel up past the stock stops at the stock (the pad's own clamp).

It runs twice: with `--config enhancements=1` every step above changes the value, and with the default (automated runs are off)
the same wheel turns change nothing. Asserts: exit 0, no [lua-error] / [engine-error] / [native-error] / [native-warning], every
`expect-*` line ok, the `[enhancements]` line of each run, and (enhancements on) screenshots that are 800x600 and not black."""
import argparse, os, re, shutil, struct, subprocess, sys, zlib

ap = argparse.ArgumentParser()
ap.add_argument("--openconche", required=True)
ap.add_argument("--assets", required=True)
ap.add_argument("--workdir", required=True)
a = ap.parse_args()
shutil.rmtree(a.workdir, ignore_errors=True)
os.makedirs(a.workdir)
errors = []

POP = "lua DoPopModal('sf_tutor'); DoPopModal('simplelogo'); DoPopModal('ui/recipebook.lua'); DoPopModal('ui/medal_award.lua')\nwait 800\n"


def script(on, shots):
    def ex(expr_on, expr_off):
        return "expect-lua 100 " + (expr_on if on else expr_off) + "\n"

    s = ("expect-window ui/mainmenu.lua 20000\nwait 1500\nclick 400 345\nexpect-window ui/entername.lua 5000\nwait 500\ntype Tester\n"
         "key Return\nwait 2000\nwait 10000\nclick 400 300\nexpect-window ui/portview.lua 5000\nwait 1500\n"
         + POP * 14 + "expect-dump 5000 modal depth 2\nwait 2500\n")
    s += "lua gSim:AdjustMoney(5000); gSim.rank = 1; gSim.weeks = 1\n"
    s += "lua for i = 1, 40 do gSim:Message('Test message number ' .. i) end; gSim:FlushMessages()\nwait 1000\n"
    # The messages tab is rebuilt by leaving and coming back; it opens on the last page.
    s += "click 705 410\nwait 500\nclick 340 410\nwait 800\n"
    s += "expect-lua 100 gSim.messageTop == 24\n"
    s += "wheel 500 480 1\nwait 300\n" + ex("gSim.messageTop == 17", "gSim.messageTop == 24")
    s += "wheel 500 480 -1\nwait 300\n" + "expect-lua 100 gSim.messageTop == 24\n"
    s += "wheel 500 480 2\nwait 300\n" + ex("gSim.messageTop == 10", "gSim.messageTop == 24")
    s += "screenshot-logical {messages}\n"
    s += "wheel 500 480 9\nwait 300\n" + ex("gSim.messageTop == 1", "gSim.messageTop == 24")
    s += "wheel 500 480 1\nwait 300\n" + ex("gSim.messageTop == 1", "gSim.messageTop == 24")
    s += "wheel 500 480 -1\nwait 300\n" + ex("gSim.messageTop == 8", "gSim.messageTop == 24")
    s += "wheel 500 480 -9\nwait 300\n" + "expect-lua 100 gSim.messageTop == 24\n"
    # The factories tab has no arrows: nothing moves.
    s += "click 470 410\nwait 500\nwheel 500 480 1\nwait 300\nclick 340 410\nwait 500\nexpect-lua 100 gSim.messageTop == 24\n"
    # Inventory tab with more than a page.
    s += ("lua for _, i in pairs(LItem._ByName) do if not i.recipe then i.inventory = 3 end end; gSim:InventoryChanged()\n"
          "click 705 410\nwait 600\nexpect-lua 100 gSim.inventoryTop == 1\n")
    s += "wheel 500 480 -1\nwait 300\n" + ex("gSim.inventoryTop > 1", "gSim.inventoryTop == 1")
    s += "screenshot-logical {inventory}\n"
    s += "wheel 500 480 1\nwait 300\nexpect-lua 100 gSim.inventoryTop == 1\n"
    s += "wheel 500 480 1\nwait 300\nexpect-lua 100 gSim.inventoryTop == 1\n"
    # The buy pad.
    s += ("click 575 194\nexpect-window ui/marketui.lua 5000\nwait 800\nclick 320 185\nexpect-window ui/buysell.lua 5000\nwait 600\n"
          "expect-lua 100 gNumberPadTarget == 'sugar'\n")
    s += "wheel 400 300 3\nwait 200\n" + ex("GetLabel('sugar') == '3'", "GetLabel('sugar') ~= '3'")
    if on:
        s += "expect-label subtotal 3 x $30 = $90\n"
    s += "wheel 400 300 -1\nwait 200\n" + ex("GetLabel('sugar') == '2'", "GetLabel('sugar') ~= '2'")
    if on:
        s += "expect-label subtotal 2 x $30 = $60\n"
    s += "screenshot-logical {buy_pad}\n"
    s += "wheel 400 300 -10\nwait 200\n" + ex("GetLabel('sugar') == '0'", "GetLabel('sugar') ~= '0'")
    s += "wheel 400 300 50\nwait 200\n" + ex("GetLabel('sugar') == '50'", "GetLabel('sugar') ~= '50'")
    s += "key Escape\nwait 600\nclick 660 268\nwait 1500\n"
    # The sell pad.
    s += ("lua for _, i in pairs(LItem._ByName) do if i.recipe and not prod then prod = i end end; prod.inventory = 10; "
          "gSim:InventoryChanged()\nwait 500\n"
          "click 646 275\nexpect-window ui/chartalk.lua 5000\nwait 800\nclick 300 303\nexpect-window ui/shopui.lua 5000\nwait 800\nclick 403 150\nexpect-window ui/buysell.lua 5000\nwait 600\n")
    s += "wheel 400 300 4\nwait 200\n" + ex("GetLabel(gNumberPadTarget) == '4'", "GetLabel(gNumberPadTarget) ~= '4'")
    s += "wheel 400 300 50\nwait 200\n" + ex("GetLabel(gNumberPadTarget) == '10'", "GetLabel(gNumberPadTarget) ~= '10'")
    s += "screenshot-logical {sell_pad}\n"
    s += "wheel 400 300 -50\nwait 200\n" + ex("GetLabel(gNumberPadTarget) == '0'", "GetLabel(gNumberPadTarget) ~= '0'")
    s += "quit\n"
    return s.format(**shots)


def run(tag, on):
    work = os.path.join(a.workdir, tag)
    os.makedirs(work)
    path = os.path.join(work, "enhance.script")
    shots = {k: os.path.join(work, k + ".png") for k in ("messages", "inventory", "buy_pad", "sell_pad")}
    open(path, "w").write(script(on, shots))
    cmd = [a.openconche, "--headless", "--assets", a.assets, "--userdir", os.path.join(work, "user"), "--log", os.path.join(work, "engine.log"),
           "--script", path]
    if on:
        cmd += ["--config", "enhancements=1"]
    r = subprocess.run(cmd, cwd=work, capture_output=True, text=True, timeout=300)
    out = r.stdout
    open(os.path.join(work, "out.txt"), "w").write(out)
    if r.returncode != 0:
        errors.append("%s: exit %d, tail %r" % (tag, r.returncode, out[-600:]))
    for bad in ("[lua-error]", "[engine-error]", "[native-error]", "[native-warning]", "FAIL"):
        if bad in out:
            errors.append("%s: output contains %s" % (tag, bad))
    log = open(os.path.join(work, "engine.log"), errors="replace").read()
    want = "[enhancements] on (choc2)" if on else "[enhancements] off"
    if want not in log:
        errors.append("%s: no '%s' line" % (tag, want))
    if len(re.findall(r"^\[script\] ok expect-", out, flags=re.M)) < 20:
        errors.append("%s: too few expect-* lines passed" % tag)
    if on and not re.search(r"\[input\] wheel \d+ \d+ notches", log):
        errors.append("%s: no wheel input in the log" % tag)
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
print("Chocolatier 2 enhancements OK")
