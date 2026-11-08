"""The exit save must never write a bare Player over a healthy save.

  1. boot death   A healthy Tester profile is made by a real new game. Then the game boots from a scratch copy of the assets
                  with an error() injected before Player:LoadGame (ui/mainloop.lua), so Main dies at boot. The exit must log
                  "[save] exit save skipped" and save.txt must be byte-for-byte unchanged.
  2. normal run   A new game whose money is then changed and the app is left with the script's `quit`: the exit save runs and
                  save.txt holds the changed money (and a fresh game has knownRecipes b01..b12, categoryCount.bar = 12).
Needs the Reforged assets."""
import argparse, os, re, shutil, subprocess, sys

ap = argparse.ArgumentParser()
ap.add_argument("--openconche", required=True)
ap.add_argument("--assets", required=True)
ap.add_argument("--workdir", required=True)
a = ap.parse_args()
a.openconche, a.assets, a.workdir = (os.path.abspath(x) for x in (a.openconche, a.assets, a.workdir))
shutil.rmtree(a.workdir, ignore_errors=True)
os.makedirs(a.workdir)
errors = []

NEW_GAME = """expect-window ui/mainmenu.lua 60000
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
"""


def run(name, user, script, assets=None, extra=()):
    path = os.path.join(a.workdir, name + ".script")
    open(path, "w").write(script)
    r = subprocess.run([a.openconche, "--headless", "--assets", assets or a.assets, "--userdir", user, "--script", path] + list(extra),
                       cwd=a.workdir, capture_output=True, text=True, timeout=600)
    out = r.stdout + r.stderr
    open(os.path.join(a.workdir, name + ".txt"), "w").write(out)
    return out


def save_path(user):
    return os.path.join(user, "profiles", "Tester", "save.txt")


def read_save(user):
    return open(save_path(user), encoding="utf-8").read()


def recipes(s):
    m = re.search(r"knownRecipes=\{([^{}]*)\}", s)
    return re.findall(r"(\w+)=true", m.group(1)) if m else None


# ---- 2 first (it makes the healthy profile): a normal run still saves at exit ----
user = os.path.join(a.workdir, "user")
out = run("normal", user, NEW_GAME + "lua Player.money = 12345\nquit\n")
if "[lua-error]" in out or "exit save skipped" in out:
    errors.append("normal: unexpected error or skipped save: %r" % out[-400:])
try:
    s = read_save(user)
    if not re.search(r"\bmoney=12345,", s):
        errors.append("normal: the exit save did not write the changed money")
    if sorted(recipes(s) or []) != ["b%02d" % i for i in range(1, 13)]:
        errors.append("normal: a fresh game should know b01..b12, got %s" % recipes(s))
    if not re.search(r"categoryCount=\{[^{}]*\bbar=12\b", s):
        errors.append("normal: categoryCount.bar should be 12 in a fresh game")
except OSError as e:
    errors.append("normal: no save.txt: %s" % e)
    s = ""

# ---- 1. boot death leaves the healthy save alone ----
assets = os.path.join(a.workdir, "assets_boom")
shutil.copytree(a.assets, assets)
ml = os.path.join(assets, "ui", "mainloop.lua")
text = open(ml, encoding="utf-8").read()
old = '\t\tPlayer:LoadGame("story")\n'
if text.count(old) != 1:
    errors.append("boot death: cannot inject the error (mainloop.lua changed)")
else:
    open(ml, "w", encoding="utf-8").write(text.replace(old, '\t\terror("simulated boot-time Lua error")\n' + old))
    before = read_save(user)
    out = run("boom", user, "wait 2000\nquit\n", assets=assets)
    if "Main died" not in out:
        errors.append("boot death: Main did not die as injected: %r" % out[-400:])
    if "[save] exit save skipped" not in out:
        errors.append("boot death: no '[save] exit save skipped' line")
    if read_save(user) != before:
        errors.append("boot death: save.txt was overwritten")

for e in errors:
    print("FAIL", e)
print("ok" if not errors else "%d failure(s)" % len(errors))
sys.exit(1 if errors else 0)
