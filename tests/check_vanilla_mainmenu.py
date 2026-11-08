"""Vanilla (assets.pfp) boot: the game's own bytecode Main reaches ui/mainmenu.lua headless, with no Lua error.

Checks exit 0, no [lua-error] / [engine-error], the ZXML tables loaded (ingredient/product/port counts non-zero), the
menu's Start Game / Help / Options / High Scores / Save & Quit labels, that only the allow-listed natives were logged as
unimplemented, and that a logical screenshot of the menu was written (an 800x600 PNG)."""
import argparse, os, re, shutil, struct, subprocess, sys

ap = argparse.ArgumentParser()
ap.add_argument("--openconche", required=True)
ap.add_argument("--assets", required=True)
ap.add_argument("--workdir", required=True)
a = ap.parse_args()
shutil.rmtree(a.workdir, ignore_errors=True)  # a fresh user dir, so Start Game asks for a name
os.makedirs(a.workdir)
shot = os.path.join(a.workdir, "vanilla_mainmenu.png")
shot2 = os.path.join(a.workdir, "vanilla_newgame.png")
unimpl = os.path.join(a.workdir, "unimplemented.txt")
script = os.path.join(a.workdir, "t.script")
with open(script, "w") as f:
    f.write("expect-window ui/mainmenu.lua 60000\nwait 1300\ndump\nscreenshot-logical %s\n"
            "click 400 345\nexpect-window ui/ui_entername.lua 5000\nwait 600\ntype Tester\nkey Return\n"
            "expect-window ui/portview.lua 5000\nwait 3000\nscreenshot-logical %s\nquit\n" % (shot, shot2))
r = subprocess.run([a.openconche, "--headless", "--assets", a.assets, "--userdir", os.path.join(a.workdir, "user"), "--script", script,
                    "--dump-windows", "--log-unimplemented", unimpl], cwd=a.workdir, capture_output=True, text=True, timeout=120)
out = r.stdout
errors = []
if r.returncode != 0:
    errors.append("exit %d, tail %r" % (r.returncode, out[-500:]))
for bad in ("[lua-error]", "[engine-error]", "FAIL", "ZXML"):
    if bad in out:
        errors.append("output contains %s" % bad)
for pat in (r"LoadIngredients: ([1-9]\d*) ", r"LoadProducts: ([1-9]\d*) ", r"LoadPortFileList: ([1-9]\d*) "):
    if not re.search(pat, out):
        errors.append("missing %s" % pat)
for name in ("ok expect-window ui/mainmenu.lua", "ok expect-window ui/ui_entername.lua", "ok expect-window ui/portview.lua"):
    if name not in out:
        errors.append("%s missing" % name)
for label in ("Start Game", "Help", "Options", "High Scores", "Save & Quit"):  # the menu's button captions in the dump
    if label not in out:
        errors.append("menu label %r not in the window dump" % label)
if "Baumeister Confections is in shambles" not in out:
    errors.append("the new-game intro dialog text is not on screen")
ALLOWED = {"SoundEvent", "StartMovie", "ButtonSound"}
if os.path.exists(unimpl):
    for l in open(unimpl):
        name = l.split("\t")[0]
        if name not in ALLOWED:
            errors.append("unexpected unimplemented native %s" % name)
for p in (shot, shot2):
    try:
        with open(p, "rb") as f:
            d = f.read(24)
        w, h = struct.unpack(">II", d[16:24])
        if (w, h) != (800, 600):
            errors.append("%s is %dx%d" % (p, w, h))
    except OSError as e:
        errors.append("no screenshot: %s" % e)
if errors:
    print("FAIL\n" + "\n".join(errors))
    sys.exit(1)
print("vanilla main menu OK")
