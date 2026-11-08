"""Play-testing: the Reforged dev toolbar (`--cheatmode`), driven headless through its pages with real clicks.

After the new-player intro (Zurich port view) the toolbar along the top is used like a tester would:
  Money      +$1,000,000 (expect-lua: the money rose by exactly that)
  Recipes    Unlock All;  Ingredients  Unlock All;  Saves  (SAVE GAME opens the name entry; cancelled);  Inventory, Vars, Q:<quest>
  Utils      Progress +1 week (Player.time rises), +1/4 week, Reload Current Port, Product Creator (ui/ui_kitchen.lua), Crates
             (ui/ui_crates.lua), Slot Machine (ui/ui_slotselect.lua), Tip Manager, Debug Console (dev/dev_console.lua)
  Ports      select Las Vegas, Teleport Here (Player.portName == "lasvegas"), then Havana ("havana"); Unlock ALL
  Buildings  [o] Own on Cape Town Manufacturing (IsOwned), then click its name: remote visit (the Felix welcome dialog)
The run must not log [lua-error] or "Main died", every expect must hold, and the only stubs may be sound, movie and button sounds.
Not driven: Restart Game and the Curve Editor (it needs SetAnimSprite/UpdateAnimTime, logged stubs: dev-only and harmless).
dev/dev_products.lua is missing in Reforged, but no toolbar button opens it (devProducts is defined and never used), so nothing fails.
Screenshots are left in the work folder to be looked at. Needs the Reforged assets."""
import argparse, os, re, shutil, subprocess, sys

ap = argparse.ArgumentParser()
ap.add_argument("--openconche", required=True)
ap.add_argument("--assets", required=True)
ap.add_argument("--workdir", required=True)
a = ap.parse_args()
shutil.rmtree(a.workdir, ignore_errors=True)
os.makedirs(a.workdir)
W = a.workdir
errors = []

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
wait 4000
click 401 346
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
expect-window dev_menu 3000
"""

# Toolbar buttons (x, y=8). Dialog rows are 14 px apart from y=20 (CLOSE); the Utils rows start at x=690.
BAR = {"quest": 50, "inventory": 185, "ingredients": 260, "recipes": 330, "ports": 380, "buildings": 430, "vars": 480,
       "saves": 530, "utils": 690, "money": 750}


def bar(name):
    return "click %d 8\nwait 600\n" % BAR[name]


def utils(row, then="", name=None):
    s = bar("utils") + "click 700 %d\nwait 1500\n" % (20 + 14 * row)
    if name:
        s += "expect-window %s 3000\nscreenshot %s/%s.png\n" % (name, W, then or name.split("/")[-1])
    return s + "key Escape\nwait 600\n"


S = INTRO
S += "lua gM0 = Player.money\n" + bar("money") + "click 740 91\nwait 600\nexpect-lua 2000 Player.money == gM0 + 1000000\n"
S += ("lua function AllRecipes() for _, p in pairs(_AllProducts) do if p.category and p.category.name ~= 'user' and not p:IsKnown() then "
      "return false end end return true end\n"
      "lua function AllIngredients() for _, i in pairs(_AllIngredients) do if not i:IsAvailable() then return false end end return true end\n"
      "lua gRecipes0 = AllRecipes()\nlua gIng0 = AllIngredients()\nlua print('BEFORE', gRecipes0, gIng0)\n")
S += bar("recipes") + "click 40 49\nwait 600\nexpect-lua 2000 AllRecipes()\nscreenshot %s/recipes.png\nkey Escape\nwait 400\n" % W
S += bar("ingredients") + "click 260 49\nwait 600\nexpect-lua 2000 AllIngredients()\nscreenshot %s/ingredients.png\nkey Escape\nwait 400\n" % W
S += bar("saves") + "click 520 35\nwait 800\nexpect-window ui/ui_entername.lua 3000\nkey Escape\nwait 400\nkey Escape\nwait 400\n"
S += bar("inventory") + "screenshot %s/inventory.png\nkey Escape\nwait 400\n" % W
S += bar("vars") + "screenshot %s/vars.png\nkey Escape\nwait 400\n" % W
S += bar("quest") + "screenshot %s/quests.png\nkey Escape\nwait 400\n" % W
S += "lua gT0 = Player.time\n" + utils(1) + "expect-lua 2000 Player.time == gT0 + 1\n"
S += "lua gT1, gS1 = Player.time, Player.subticks\n" + utils(2) + "expect-lua 2000 Player.time ~= gT1 or Player.subticks ~= gS1\n"
S += bar("utils") + "click 700 62\nwait 3000\n"  # Reload Current Port closes the menu itself
S += utils(5, "kitchen", "ui/ui_kitchen.lua")
S += utils(6, "crates", "ui/ui_crates.lua")
S += utils(10, "slotselect", "ui/ui_slotselect.lua")
S += utils(11, "tips", "dev/dev_tips.lua")
S += bar("utils") + "click 700 189\nwait 1500\nexpect-window dev/dev_console.lua 3000\nscreenshot %s/console.png\nkey Escape\nwait 600\nkey Escape\nwait 600\n" % W  # the console opens over the Utils menu
# Ports: the grid lists the 20 ports alphabetically from y=45 in 14 px rows: Las Vegas is the 11th row (y=185), Havana the 9th (157).
S += bar("ports") + "click 190 185\nwait 600\nclick 30 45\nwait 2500\nexpect-lua 3000 Player.portName == \"lasvegas\"\nscreenshot %s/lasvegas.png\n" % W
S += bar("ports") + "click 190 157\nwait 600\nclick 30 45\nwait 2500\nexpect-lua 3000 Player.portName == \"havana\"\nscreenshot %s/havana.png\n" % W
S += bar("ports") + "click 30 117\nwait 600\nscreenshot %s/unlockall.png\nkey Escape\nwait 600\n" % W
S += "expect-lua 1000 tokyo:IsAvailable() and lasvegas:IsAvailable()\n"
S += bar("buildings") + "click 162 385\nwait 600\nexpect-lua 2000 cap_factory:IsOwned()\nscreenshot %s/buildings.png\n" % W
S += "click 60 385\nwait 3000\nexpect-lua 3000 Player.portName == \"capetown\"\nscreenshot %s/remote.png\nquit\n" % W

script = os.path.join(W, "t.script")
with open(script, "w") as f:
    f.write(S)
unimpl = os.path.join(W, "unimplemented.txt")
r = subprocess.run([a.openconche, "--headless", "--assets", a.assets, "--cheatmode", "--userdir", os.path.join(W, "user"), "--script", script,
                    "--dump-windows", "--log-unimplemented", unimpl], cwd=W, capture_output=True, text=True, timeout=900)
out = r.stdout + r.stderr
with open(os.path.join(W, "out.txt"), "w") as f:
    f.write(out)
if r.returncode != 0 or "FAIL" in out:
    errors.append("exit %d: %s" % (r.returncode, [l for l in out.splitlines() if "FAIL" in l][:3]))
if "BEFORE\tfalse\tfalse" not in out:  # otherwise the Unlock All checks would prove nothing
    errors.append("recipes and ingredients were not locked before Unlock All")
bad = [l for l in out.splitlines() if "[lua-error]" in l or "Main died" in l]
if bad:
    errors.append("Lua errors: %s" % bad[:3])
try:
    names = [l.split("\t")[0] for l in open(unimpl)]
except OSError:
    names = []
extra = sorted(set(names) - {"SoundEvent", "ButtonSound", "StartMovie"})
if extra:
    errors.append("unexpected stubs: %s" % extra)
for p in ("lasvegas", "havana", "unlockall", "kitchen", "crates", "slotselect", "console", "remote"):
    if not os.path.exists(os.path.join(W, p + ".png")):
        errors.append("missing screenshot " + p)

if errors:
    print("FAIL")
    for e in errors:
        print(" ", e)
    sys.exit(1)
print("devmenu ok")
