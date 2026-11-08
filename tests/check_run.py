"""Play-testing: `--cheatmode --run tools/playtest/<snippet>.lua`, played headless against the real Reforged game.

Run 1 starts a new Story game (new player, the opening dialogs) and saves. Run 2 resumes it with --run <snippet>: Story Mode opens
the map, the first time the map (or port view) is shown the file runs (a `[run]` log line), and the snippet's window must appear
with no Lua error and Main alive. F9 is then pressed once: the file must run a second time. Cases (--case, repeatable):

  casino    ui/ui_slotselect.lua over Las Vegas (Player.portName == "lasvegas")
  factory   ui/ui_factory.lua in Zurich
  designer  ui/ui_kitchen.lua
  rich      money over $10m, every port available, no dialog
  freeplay  the Free Play session on the map (Player.mode == "free")
Screenshots are left in the work folder to be looked at. Needs the Reforged assets."""
import argparse, os, re, shutil, subprocess, sys

ap = argparse.ArgumentParser()
ap.add_argument("--openconche", required=True)
ap.add_argument("--assets", required=True)
ap.add_argument("--workdir", required=True)
ap.add_argument("--playtest", required=True, help="the tools/playtest folder")
ap.add_argument("--case", action="append", default=[])
a = ap.parse_args()
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
lua Player:SaveGame()
quit
"""

# case: (snippet, window expected, Lua condition expected, whether the snippet is expected to work on a second run)
CASES = {
    "casino": ("casino.lua", "ui/ui_slotselect.lua", 'Player.portName == "lasvegas"'),
    "factory": ("factory.lua", "ui/ui_factory.lua", 'Player.portName == "zurich" and zur_factory:IsOwned()'),
    "designer": ("designer.lua", "ui/ui_kitchen.lua", "Player.money > 100000"),
    "rich": ("rich.lua", "ui/mapview.lua", "Player.money > 10000000 and lasvegas:IsAvailable() and tokyo:IsAvailable()"),
    "freeplay": ("freeplay.lua", "ui/mapview.lua", 'Player.mode == "free"'),
}


def run(args, script, out):
    with open(out + ".script", "w") as f:
        f.write(script)
    r = subprocess.run([a.openconche, "--headless", "--assets", a.assets, "--cheatmode", "--script", out + ".script",
                        "--log-unimplemented", out + ".unimpl"] + args, cwd=a.workdir, capture_output=True, text=True, timeout=600)
    with open(out + ".txt", "w") as f:
        f.write(r.stdout + r.stderr)
    return r


for case in a.case or sorted(CASES):
    snippet, window, cond = CASES[case]
    work = os.path.join(a.workdir, case)
    os.makedirs(work)
    user = os.path.join(work, "user")
    r = run(["--userdir", user], NEW_GAME, os.path.join(work, "run1"))
    if r.returncode != 0 or "[lua-error]" in r.stdout:
        errors.append("%s: run 1 failed, exit %d: %r" % (case, r.returncode, r.stdout[-400:]))
        continue
    dialog = window not in ("ui/mapview.lua",)  # the snippet leaves a dialog open over the view
    check = "expect-window %s 5000\nwait 1500\nexpect-lua 2000 %s\n" % (window, cond)
    script = ("expect-window ui/mainmenu.lua 60000\nwait 1300\nclick 310 345\n" + check +
              "lua print('MARK1')\ndump\nscreenshot %s/%s.png\n" % (work, case))
    if dialog:  # F9 with a dialog open must be refused (it would replace the dialog); closing it lets F9 through
        script += "key F9\nwait 800\nlua print('MARK2')\ndump\nkey Escape\nwait 1000\n"
    script += "key F9\nwait 1500\n" + check + "lua print('MARK3')\ndump\nscreenshot %s/%s_f9.png\nquit\n" % (work, case)
    r = run(["--userdir", user, "--run", os.path.join(a.playtest, snippet)], script, os.path.join(work, "run2"))
    out = r.stdout + r.stderr
    path = os.path.join(a.playtest, snippet)
    runs = out.count("[run] " + path)
    if r.returncode != 0 or "FAIL" in out:
        errors.append("%s: exit %d, %s" % (case, r.returncode, [l for l in out.splitlines() if "FAIL" in l][:3]))
    if "[lua-error]" in out or "Main died" in out:
        errors.append("%s: Lua error: %s" % (case, [l for l in out.splitlines() if "lua-error" in l or "Main died" in l][:3]))
    skipped = out.count("[run] skipped: a dialog is open")
    if skipped != (1 if dialog else 0):
        errors.append("%s: F9 with a dialog open was skipped %d times" % (case, skipped))
    # No ghost view: exactly one port/map view in the modal chain at each dump.
    for mark in ("MARK1", "MARK2", "MARK3"):
        i = out.find(mark + "\n")
        if i < 0:
            if mark != "MARK2" or dialog:
                errors.append("%s: no %s dump" % (case, mark))
            continue
        j = out.find("MARK", i + 5)
        sect = out[i:j if j > 0 else len(out)]
        views = len(re.findall(r'Modal "[^"]*" id=\d+ [^\n]*script="ui/(?:portview|mapview)\.lua"', sect))
        if views != 1:
            errors.append("%s: %s shows %d port/map views in the modal chain, expected 1" % (case, mark, views))
    if runs != 2:
        errors.append("%s: the snippet ran %d times, expected 2 (the first view and F9)" % (case, runs))
    print("%s: ok=%s runs=%d" % (case, not any(e.startswith(case + ":") for e in errors), runs))

if errors:
    print("FAIL")
    for e in errors:
        print(" ", e)
    sys.exit(1)
print("run ok")
