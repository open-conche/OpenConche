"""Chocolatier 2 quantity dialog keys: Enter buys, Escape cancels (ui/buysell.lua, docs/re/choc2-logo-and-trade.md 2.4).

buysell.lua marks its Cancel button `default=true` and leaves OK without a flag, so the original-style Enter rule would press Cancel.
The engine presses the OK sibling on Enter (WindowManager::legacyCancelDefault). Reaches the San Francisco market as
check_choc2_trade.py does, types 12 on the keyboard and presses Return: sugar +12, money -12 x price. Then types 3 and presses
Escape: the dialog is gone and nothing changed."""
import argparse, os, re, shutil, subprocess, sys

ap = argparse.ArgumentParser()
ap.add_argument("--openconche", required=True)
ap.add_argument("--assets", required=True)
ap.add_argument("--workdir", required=True)
a = ap.parse_args()
shutil.rmtree(a.workdir, ignore_errors=True)
os.makedirs(a.workdir)
user = os.path.join(a.workdir, "user")
errors = []

script = """expect-window ui/mainmenu.lua 20000
wait 1500
click 400 345
expect-window ui/entername.lua 5000
wait 500
type Tester
key Return
wait 2000
click 400 300
expect-window ui/portview.lua 5000
wait 1500
click 208 304
wait 700
click 300 303
wait 700
click 300 303
wait 1500
expect-window ui/portview.lua 5000
wait 300
lua gSim:AdjustMoney(5000); gSim.rank = 0; gSim.weeks = 1; p0 = LItem:ByName('sugar').price; inv0 = LItem:ByName('sugar').inventory
wait 1500
expect-lua 100 gSim.money == 5000
click 575 194
expect-window ui/marketui.lua 5000
wait 800
click 320 185
expect-window ui/buysell.lua 5000
wait 600
type 12
wait 300
expect-label subtotal 12 x $30 = $360
key Return
wait 1500
expect-window ui/marketui.lua 5000
expect-lua 100 LItem:ByName('sugar').inventory == inv0 + 12
expect-lua 100 gSim.money == 5000 - 12 * p0
click 320 185
expect-window ui/buysell.lua 5000
wait 600
type 3
wait 300
expect-label subtotal 3 x $30 = $90
key Escape
wait 1000
expect-window ui/marketui.lua 5000
expect-lua 100 LItem:ByName('sugar').inventory == inv0 + 12
expect-lua 100 gSim.money == 5000 - 12 * p0
"""
path = os.path.join(a.workdir, "enter.script")
with open(path, "w") as f:
    f.write(script)
r = subprocess.run([a.openconche, "--headless", "--assets", a.assets, "--userdir", user, "--script", path],
                   cwd=a.workdir, capture_output=True, text=True, timeout=300)
out = r.stdout
open(os.path.join(a.workdir, "out.txt"), "w").write(out)
if r.returncode != 0:
    errors.append("exit %d, tail %r" % (r.returncode, out[-600:]))
for bad in ("[lua-error]", "[engine-error]", "[native-error]", "[native-warning]", "FAIL"):
    if bad in out:
        errors.append("output contains %s" % bad)
if "[game] choc2 " not in out:
    errors.append("no '[game] choc2' line")
oks = len(re.findall(r"^\[script\] ok expect-", out, flags=re.M))
if oks < 14:
    errors.append("only %d expect-* lines passed" % oks)

if errors:
    print("FAIL\n" + "\n".join(errors))
    sys.exit(1)
print("Chocolatier 2 Enter/Escape OK")
