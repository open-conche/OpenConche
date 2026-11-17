"""Chocolatier 1 enhancements (docs/design/enhancements.md sections 5 and 6, lua/enhance_choc1.lua): a new user plays through the
tutor to the live San Francisco port, then the mouse wheel and the trade screen's OK button are exercised with
`--config enhancements=1`; a second run without the switch checks that the original behaviour is untouched.

Run 1 (enhancements on):
* ledger: with 12 extra messages the wheel over the ledger scrolls the message list by one line per notch and stops at the
  game's own bounds (the up arrow gone at 1, the down arrow gone at the end); the inventory tab scrolls by a page.
* trade screen, buying (Ferry Market): the wheel over an item's field or price tag sets that item's pending quantity (clamped
  to 9999, 0 restores the idle label), the receipt shows it, nothing is bought (money and inventory unchanged); over the
  unnamed sack icon it does nothing.
* the three closes: nothing pending (closes as before); affordable (the game's own purchase runs, the dialog closes);
  not affordable (the game's own notice opens, the dialog stays, the quantities are kept, money unchanged).
* trade screen, selling (the shop): the wheel is limited to the inventory, the sale happens on close, Sell All still sells
  everything.
* the wheel on a row whose number pad is active leaves the pad typing into the row.
Run 2 (enhancements off, the suite's default): the wheel is dropped and the OK button discards a typed quantity.

Asserts: exit 0, no [lua-error] / [engine-error] / [native-error] / [native-warning] / FAIL, the [enhancements] line, only
StartMovie unimplemented, a screenshot of the trade screen with a wheel-entered quantity that is not black."""
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
shots = {k: os.path.join(a.workdir, k + ".png") for k in ("ledger", "receipt", "notice", "sell")}


def run(tag, text, extra):
    script = os.path.join(a.workdir, tag + ".script")
    with open(script, "w") as f:
        f.write(text)
    unimpl = os.path.join(a.workdir, tag + "-unimplemented.txt")
    r = subprocess.run([a.openconche, "--headless", "--assets", a.assets, "--userdir", os.path.join(user, tag), "--script", script,
                        "--log-unimplemented", unimpl] + extra, cwd=a.workdir, capture_output=True, text=True, timeout=300)
    out = r.stdout
    with open(os.path.join(a.workdir, tag + "-out.txt"), "w") as f:
        f.write(out)
    if r.returncode != 0:
        errors.append("%s: exit %d, tail %r" % (tag, r.returncode, out[-800:]))
    for bad in ("[lua-error]", "[engine-error]", "[native-error]", "[native-warning]", "FAIL"):
        if bad in out:
            errors.append("%s: output contains %s" % (tag, bad))
    if "[game] choc1 " not in out:
        errors.append("%s: no '[game] choc1' line" % tag)
    if os.path.exists(unimpl):
        for line in open(unimpl):
            name = line.split("\t")[0].strip()
            if name and name != "StartMovie":
                errors.append("%s: unexpected unimplemented native %s" % (tag, name))
    return out


def modal_stacks(out):
    """The modal script paths of every window dump, in order."""
    res = []
    for block in re.split(r"(?=^\[windows\] modal depth)", out, flags=re.M):
        if block.startswith("[windows] modal depth"):
            res.append(re.findall(r'^  Modal "[^"]*" .*? script="([^"]*)"', block, flags=re.M))
    return res


def states(out):
    """STATE lines: tag -> list of the tab-separated fields after the tag."""
    res = {}
    for m in re.finditer(r"^STATE\t(\S+)\t(.*)$", out, flags=re.M):
        res[m.group(1)] = m.group(2).split("\t")
    return res


def png_not_black(path):
    data = open(path, "rb").read()
    w, h, depth, ctype = struct.unpack(">IIBB", data[16:26])
    pos, idat = 8, b""
    while pos < len(data):
        n, typ = struct.unpack(">I4s", data[pos:pos + 8])
        if typ == b"IDAT":
            idat += data[pos + 8:pos + 8 + n]
        pos += 12 + n
    raw = zlib.decompress(idat)
    return (w, h) == (800, 600) and sum(1 for b in raw if b) > len(raw) // 100


SETUP = """expect-window ui/mainmenu.lua 20000
wait 1500
click 400 298
expect-window ui/entername.lua 5000
wait 500
type Tester
key Return
expect-window ui/chartalk.lua 8000
wait 1000
click 225 315
wait 800
click 470 315
expect-window ui/logobuilder.lua 5000
wait 800
click 625 355
expect-window ui/dialog.lua 5000
wait 800
click 598 350
expect-window ui/reward.lua 5000
wait 800
click 545 492
wait 3000
click 620 120
expect-window ui/chartalk.lua 5000
wait 500
click 225 315
wait 800
"""
UP, DOWN = "control/scrollup_up", "control/scrolldown_up"
OPEN_MARKET = "click 200 330\nexpect-window ui/buysell.lua 5000\nwait 800\n"
OPEN_SHOP = "click 510 240\nexpect-window ui/buysell.lua 5000\nwait 800\n"
YES = "click 710 354\nwait 1200\n"
# Row centres on the trade screen: sugar's field and price tag (sack layout), the shop's single platter row.
SUGAR, SUGAR_TAG, SUGAR_ICON = (287, 288), (263, 171), (281, 235)
MILK = (387, 288)
BASEBARS, BASEBARS_TAG = (364, 298), (364, 265)


def wheel(pos, n):
    """`wheel` takes at most 1000 notches a line."""
    s = ""
    while n:
        k = max(-1000, min(1000, n))
        s += "wheel %d %d %d\n" % (pos[0], pos[1], k)
        n -= k
    return s + "wait 100\n"


def state(tag, expr):
    return "lua print('STATE', %r, %s)\n" % (tag, expr)


def expect(expr):
    return "expect-lua 2000 %s\n" % expr


LEDGER = (500, 470)
MONEY = "gSim.money"
LABELS = "GetLabel('sugar'), GetLabel('milk')"

s = SETUP
# ---- ledger: messages ----
s += "lua for i = 1, 12 do PlayerMessage('Test message ' .. i) end UpdateDynamicWindow('ledgertabs')\nwait 300\n"
s += wheel(LEDGER, 100)
s += expect("gCurrentLedgerTop == 1 and not EnhanceHasButtonGraphic('%s') and EnhanceHasButtonGraphic('%s')" % (UP, DOWN))
s += wheel(LEDGER, -2)
s += expect("gCurrentLedgerTop == 3 and EnhanceHasButtonGraphic('%s')" % UP)
s += wheel(LEDGER, -1000)
s += expect("gCurrentLedgerTop > 3 and not EnhanceHasButtonGraphic('%s') and EnhanceHasButtonGraphic('%s')" % (DOWN, UP))
s += "lua gTopWas = gCurrentLedgerTop\n" + wheel(LEDGER, 1) + expect("gCurrentLedgerTop == gTopWas - 1 and EnhanceHasButtonGraphic('%s')" % DOWN)
s += "screenshot-logical %s\n" % shots["ledger"]
# ---- ledger: inventory (one page per notch) ----
s += "lua for name, _ in pairs(LItem._ByName) do gSim:AdjustInventory(name, 2) end\n"
s += "click 665 570\nwait 500\n"
s += expect("gCurrentLedgerTop == 1 and not EnhanceHasButtonGraphic('%s') and EnhanceHasButtonGraphic('%s')" % (UP, DOWN))
s += wheel(LEDGER, 1) + expect("gCurrentLedgerTop == 1")  # up at the first page: nothing
s += wheel(LEDGER, -1) + expect("gCurrentLedgerTop == 2 and EnhanceHasButtonGraphic('%s')" % UP)
s += wheel(LEDGER, 1) + expect("gCurrentLedgerTop == 1")
s += "lua gSim.inventory = {}\nclick 330 570\nwait 500\n"  # back to Messages, inventory emptied
# ---- buying: the wheel is only pending ----
s += OPEN_MARKET
s += state("m0", "%s, %s, %s" % (LABELS, MONEY, "gSim.inventory.sugar"))
s += wheel(SUGAR, 3) + state("w3", "%s, %s, %s" % (LABELS, MONEY, "gSim.inventory.sugar"))
s += "dump\n"
s += "screenshot-logical %s\n" % shots["receipt"]
s += wheel(SUGAR_TAG, 2) + state("tag", LABELS)
s += wheel(SUGAR_ICON, 4) + state("icon", LABELS)  # the sack icon has no name: nothing
s += wheel(SUGAR, -1000) + state("zero", "%s, %s" % (LABELS, MONEY))
s += wheel(SUGAR, -1000) + state("zero2", LABELS)
s += wheel(MILK, 1) + wheel(SUGAR, 1) + state("one", "%s, %s" % (LABELS, MONEY))
# the pad: activate sugar, type 7, wheel on milk and on sugar, type on
s += "click 280 288\nwait 300\ntype 7\nwait 200\n" + state("pad0", "%s, gNumberPadTarget" % LABELS)
s += wheel(MILK, 4) + state("pad1", "%s, gNumberPadTarget" % LABELS)
s += wheel(SUGAR, -1000) + state("pad2", "'[' .. GetLabel('sugar') .. ']', gNumberPadTarget")
s += "type 5\nwait 200\n" + state("pad3", "%s, gNumberPadTarget" % LABELS)
s += wheel(SUGAR, 3) + "type 2\nwait 200\n" + state("pad4", LABELS)
# ---- the closes: affordable (sugar 82, milk 5, at $3 and $8) ----
s += "dump\n" + YES + "dump\n"
s += state("bought", "%s, %s, gSim.inventory.sugar, gSim.inventory.milk" % (MONEY, "gSim.messages[table.getn(gSim.messages)]"))
# ---- not affordable: the notice, the dialog stays, the quantities are kept ----
s += OPEN_MARKET
s += state("m1", MONEY)
s += wheel(SUGAR, 9999) + state("big", "%s, %s" % (LABELS, MONEY))
s += YES + "dump\n"
s += "screenshot-logical %s\n" % shots["notice"]
s += state("refused", "%s, gSim.inventory.sugar" % MONEY)
s += "click 400 347\nwait 800\ndump\n"  # the notice's OK
s += state("kept", "%s, %s, gSim.inventory.sugar" % (LABELS, MONEY))
# ---- nothing pending: clear the quantity, the same button closes ----
s += wheel(SUGAR, -9999) + state("cleared", LABELS)
s += YES + "dump\n" + state("closed", "%s, gSim.inventory.sugar" % MONEY)
# ---- selling ----
s += "lua gSim:AdjustInventory('basebars', 5)\n"
s += OPEN_SHOP
s += state("s0", "GetLabel('basebars'), %s, gSim.inventory.basebars" % MONEY)
s += wheel(BASEBARS, 3) + state("s3", "GetLabel('basebars'), %s, gSim.inventory.basebars" % MONEY)
s += "dump\nscreenshot-logical %s\n" % shots["sell"]
s += wheel(BASEBARS_TAG, 100) + state("s5", "GetLabel('basebars'), %s, gSim.inventory.basebars" % MONEY)
s += YES + "dump\n"
s += state("sold", "%s, gSim.inventory.basebars" % MONEY)
# Sell All is not touched: it sells the rest and closes.
s += "lua gSim:AdjustInventory('basebars', 4)\n"
s += OPEN_SHOP
s += wheel(BASEBARS, 1) + state("a1", "GetLabel('basebars'), gSim.inventory.basebars")
s += "click 680 277\nwait 1500\ndump\n"
s += state("all", "%s, gSim.inventory.basebars" % MONEY)
s += "quit\n"

out1 = run("on", s, ["--config", "enhancements=1"])
if "[enhancements] on (choc1)" not in out1:
    errors.append("run 1: the overlay did not load")
st = states(out1)
mod = modal_stacks(out1)


def want(tag, fields):
    if st.get(tag) != fields:
        errors.append("run 1: %s is %r, expected %r" % (tag, st.get(tag), fields))


want("m0", ["Buy", "Buy", "3500", "nil"])
want("w3", ["3", "Buy", "3500", "nil"])
want("tag", ["5", "Buy"])
want("icon", ["5", "Buy"])
want("zero", ["Buy", "Buy", "3500"])
want("zero2", ["Buy", "Buy"])
want("one", ["1", "1", "3500"])
want("pad0", ["7", "1", "sugar"])
want("pad1", ["7", "5", "sugar"])
want("pad2", ["[]", "sugar"])
want("pad3", ["5", "5", "sugar"])
want("pad4", ["82", "5"])
# sugar 82 x $3 + milk 5 x $8 = $286
b = st.get("bought")
if not b or b[0] != str(3500 - 82 * 3 - 5 * 8) or b[2] != "82" or b[3] != "5":
    errors.append("run 1: after the affordable close: %r" % (b,))
elif "Bought" not in b[1]:
    errors.append("run 1: no purchase message: %r" % (b[1],))
mon = int(b[0]) if b else 0
want("m1", [str(mon)])
want("big", ["9999", "Buy", str(mon)])
want("refused", [str(mon), "82"])
want("kept", ["9999", "Buy", str(mon), "82"])
want("cleared", ["Buy", "Buy"])
want("closed", [str(mon), "82"])
want("s0", ["Sell", str(mon), "5"])
want("s3", ["3", str(mon), "5"])
want("s5", ["5", str(mon), "5"])
sold = st.get("sold")
if not sold or int(sold[0]) <= mon or sold[1] not in ("0", "nil"):
    errors.append("run 1: the sale on close did not happen: %r" % (sold,))
a1 = st.get("a1")
if a1 != ["1", "4"]:
    errors.append("run 1: before Sell All: %r" % (a1,))
allst = st.get("all")
if not allst or not sold or int(allst[0]) <= int(sold[0]) or allst[1] not in ("0", "nil"):
    errors.append("run 1: Sell All did not sell everything: %r" % (allst,))
# modal stacks of the dumps: the receipt dump, the pre-close dump, the post-close dump, the notice, the kept dialog, closed, sell...
PORT, TRADE, OKD = "ui/portview.lua", "ui/buysell.lua", "ui/okdialog.lua"
expected = [[PORT, TRADE],            # receipt
            [PORT, TRADE],            # before the affordable OK
            [PORT],                   # after: closed
            [PORT, TRADE, OKD],       # not affordable: the game's notice over the open trade screen
            [PORT, TRADE],            # notice dismissed: still open
            [PORT],                   # nothing pending: closed
            [PORT, TRADE],            # sell, 3 pending
            [PORT],                   # sold on close
            [PORT]]                   # Sell All closed
if mod != expected:
    errors.append("run 1: modal stacks %r, expected %r" % (mod, expected))
for k in ("ledger", "receipt", "notice", "sell"):
    if not os.path.exists(shots[k]) or not png_not_black(shots[k]):
        errors.append("run 1: screenshot %s is missing or black" % k)

# ---- run 2: the switch off ----
s2 = SETUP + OPEN_MARKET
s2 += wheel(SUGAR, 3) + state("off_wheel", "%s, %s" % (LABELS, MONEY))
s2 += "click 280 288\nwait 300\ntype 3\nwait 300\n" + state("off_typed", LABELS)
s2 += YES + "dump\n" + state("off_closed", "%s, gSim.inventory.sugar" % MONEY)
s2 += "quit\n"
out2 = run("off", s2, [])
if "[enhancements] off" not in out2:
    errors.append("run 2: [enhancements] off missing")
st = states(out2)
if st.get("off_wheel") != ["Buy", "Buy", "3500"]:
    errors.append("run 2: the wheel was not dropped: %r" % (st.get("off_wheel"),))
if st.get("off_typed") != ["3", "Buy"]:
    errors.append("run 2: typing did not set the quantity: %r" % (st.get("off_typed"),))
if st.get("off_closed") != ["3500", "nil"]:
    errors.append("run 2: the OK button did not discard the pending quantity: %r" % (st.get("off_closed"),))
if modal_stacks(out2) != [[PORT]]:
    errors.append("run 2: the OK button did not close the dialog: %r" % (modal_stacks(out2),))

# ---- typing: the field holds four digits with the switch on, three as the original with it off ----
TYPE5 = SETUP + OPEN_MARKET + "click 280 288\nwait 300\ntype 12345\nwait 300\n" + state("typed", "GetLabel('sugar')") + "quit\n"
st = states(run("type_on", TYPE5, ["--config", "enhancements=1"]))
if st.get("typed") != ["1234"]:
    errors.append("typing with the switch on: %r, expected 1234" % (st.get("typed"),))
st = states(run("type_off", TYPE5, []))
if st.get("typed") != ["123"]:
    errors.append("typing with the switch off: %r, expected 123" % (st.get("typed"),))

if errors:
    for e in errors:
        print("FAIL:", e)
    sys.exit(1)
print("ok")
