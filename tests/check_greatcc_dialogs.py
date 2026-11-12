"""The Great Chocolate Chase (docs/design/greatcc.md slice 2): dialog motion and transitions on the real scripts.

Runs `--headless --userdir <scratch> --seed 1`: from the main menu the Quit button opens scripts/yesno.lua, whose tint fades in
and whose RBounceWindow slides down from above; Cancel closes it again through `PostMessage(kTintOff); DoTransition(kBounceBack);
CloseWindow()`. A click made while the exit transition runs is swallowed (input is blocked while an overlay is current or
queued). Asserts: exit 0; no [lua-error] / [engine-error] / [native-error]; the [unimplemented] and [unimplemented-type] sets
equal the allow-lists below; the dump mid-slide shows the dialog partway (0 < t < 1, tint ramping in), at rest it sits on its
target (t=1.00), mid-exit the tint ramps out and the window is leaving, and at the end only the main menu is left (the click
during the exit did not reopen the dialog); the three screenshots (mid-slide, at rest, mid-exit) differ from each other, and
the final one equals the main menu's."""
import argparse, os, re, shutil, subprocess, sys

ap = argparse.ArgumentParser()
ap.add_argument("--openconche", required=True)
ap.add_argument("--assets", required=True)
ap.add_argument("--workdir", required=True)
a = ap.parse_args()
shutil.rmtree(a.workdir, ignore_errors=True)
os.makedirs(a.workdir)
errors = []

# Natives the menu and yesno.lua reach that no slice has built yet: none. The window types
# (TMainMenu, RShrinkText, KSplit9Image) are real since slice 3, the TSettings natives since slice 4.
ALLOWED_UNIMPLEMENTED = set()
ALLOWED_TYPES = set()

SHOT = lambda n: os.path.join(a.workdir, n + ".png")
script = os.path.join(a.workdir, "dialogs.script")
with open(script, "w") as f:
    f.write("""expect-window scripts/mainmenu.lua 20000
wait 1500
screenshot-logical %(menu)s
click 678 471
wait 90
dump
screenshot-logical %(mid)s
wait 900
dump
screenshot-logical %(rest)s
click 489 348
wait 100
screenshot-logical %(exit)s
dump
click 678 471
wait 1000
dump
screenshot-logical %(after)s
""" % {k: SHOT(k) for k in ("menu", "mid", "rest", "exit", "after")})
log = os.path.join(a.workdir, "run.log")
p = subprocess.run([a.openconche, "--headless", "--assets", a.assets, "--userdir", os.path.join(a.workdir, "user"), "--seed", "1",
                    "--script", script, "--log", log], capture_output=True, text=True, encoding="utf-8", errors="replace",
                   timeout=120)
out = (open(log, encoding="utf-8", errors="replace").read() if os.path.exists(log) else "") + "\n" + p.stdout + "\n" + p.stderr
if p.returncode != 0:
    errors.append("exit status %d" % p.returncode)
for tag in ("[lua-error]", "[engine-error]", "[native-error]"):
    if tag in out:
        errors.append("%s in the output: %s" % (tag, next(l for l in out.split("\n") if tag in l)))
seen = set(re.findall(r"^(?:\[\+\d+ms\] )?\[unimplemented\] (\S+)", out, re.M))
if seen != ALLOWED_UNIMPLEMENTED:
    errors.append("[unimplemented] set differs: extra %s, missing %s" % (sorted(seen - ALLOWED_UNIMPLEMENTED),
                                                                         sorted(ALLOWED_UNIMPLEMENTED - seen)))
types = set(re.findall(r"^(?:\[\+\d+ms\] )?\[unimplemented-type\] (\S+)", out, re.M))
if types != ALLOWED_TYPES:
    errors.append("[unimplemented-type] set differs: extra %s, missing %s" % (sorted(types - ALLOWED_TYPES),
                                                                              sorted(ALLOWED_TYPES - types)))

# The dumps come from the program's stdout, one block per `dump` command, each starting at its "[windows]" header.
dumps = [d for d in re.split(r"^(?=\[windows\] )", p.stdout, flags=re.M) if d.startswith("[windows] ")]
if len(dumps) != 4:
    errors.append("expected 4 window dumps, got %d" % len(dumps))
else:
    mid, rest, leaving, final = dumps

    def bounce(d):
        m = re.search(r'RBounceWindow "" id=\d+ rect=(-?\d+),(-?\d+),\d+,\d+ .*target=(-?\d+),(-?\d+) len=200ms t=([\d.]+)', d)
        return None if not m else tuple(float(v) for v in m.groups())

    def tint(d):
        m = re.search(r'VTintWindow "" id=\d+ rect=0,0,800,600 color=0.00,0.00,0.00,0.78 fade=1 time=200 (in|out)', d)
        return m.group(1) if m else None

    if 'Modal "ok"' not in mid or 'script="scripts/yesno.lua"' not in mid:
        errors.append("Quit did not open scripts/yesno.lua")
    b = bounce(mid)
    if not b or not (0 < b[4] < 1) or b[1] >= b[3]:
        errors.append("mid-slide dump: the dialog is not partway to its target: %s" % (b,))
    if tint(mid) != "in":
        errors.append("mid-slide dump: the tint is not fading in")
    b = bounce(rest)
    if not b or b[4] != 1.0 or (b[0], b[1]) != (b[2], b[3]):
        errors.append("resting dump: the dialog is not on its target: %s" % (b,))
    b = bounce(leaving)
    if not b or not (0 < b[4] < 1) or (b[0], b[1]) == (b[2], b[3]):
        errors.append("exit dump: the dialog is not on its way out: %s" % (b,))
    if tint(leaving) != "out":
        errors.append("exit dump: the tint is not fading out")
    if 'Modal "ok"' in final:
        errors.append("the dialog is still up at the end (or the click during the exit reopened it)")
    if 'Modal "mainmenu_dialog"' not in final:
        errors.append("the main menu is not the top modal at the end")

shots = {}
for k in ("menu", "mid", "rest", "exit", "after"):
    path = SHOT(k)
    if not os.path.exists(path):
        errors.append("no screenshot %s" % k)
    else:
        shots[k] = open(path, "rb").read()
if len(shots) == 5:
    for x, y in (("mid", "rest"), ("rest", "exit"), ("mid", "exit"), ("menu", "mid"), ("menu", "rest")):
        if shots[x] == shots[y]:
            errors.append("screenshots %s and %s are identical" % (x, y))
    if shots["after"] != shots["menu"]:
        errors.append("after the dialog closes the screen is not the main menu again")

# The window close request (the profile's closeRequest, as App runs it on a Quit event): the first opens the quit dialog on
# Main's coroutine and Cancel closes it with Main still alive; with the dialog up, a second request quits.
CLOSE = 'if AreQuitting() then PostMessage(CreateNamedMessage(kQuitNow, "quit")) else table.insert(gCommandQueue, assert(loadfile("scripts/quitverify.lua"))) end'
script2 = os.path.join(a.workdir, "close.script")
with open(script2, "w") as f:
    f.write("""expect-window scripts/mainmenu.lua 20000
wait 1500
lua %(c)s
expect-window scripts/yesno.lua 3000
expect-lua 2000 AreQuitting() == true
wait 900
click 489 348
expect-lua 3000 AreQuitting() == false
wait 600
lua %(c)s
expect-window scripts/yesno.lua 3000
wait 900
lua %(c)s
wait 60000
""" % {"c": CLOSE})
log2 = os.path.join(a.workdir, "close.log")
p2 = subprocess.run([a.openconche, "--headless", "--assets", a.assets, "--userdir", os.path.join(a.workdir, "user2"), "--seed", "1",
                     "--script", script2, "--log", log2], capture_output=True, text=True, encoding="utf-8", errors="replace",
                    timeout=120)
out2 = (open(log2, encoding="utf-8", errors="replace").read() if os.path.exists(log2) else "") + "\n" + p2.stdout + "\n" + p2.stderr
if p2.returncode != 0:
    errors.append("close: exit status %d" % p2.returncode)
for tag in ("[lua-error]", "[engine-error]", "[native-error]", "Main died"):
    if tag in out2:
        errors.append("close: %s in the output: %s" % (tag, next(l for l in out2.split("\n") if tag in l)))
if "[app] exit: kQuitNow" not in out2:
    errors.append("close: the second close request with the quit dialog up did not quit")
if len(re.findall(r"\[script\] ok expect-", out2)) < 5:
    errors.append("close: an expectation did not pass (the quit dialog did not open, close or reopen)")
for e in errors:
    print("FAIL:", e)
print("greatcc_dialogs: %s" % ("FAILED" if errors else "ok"))
sys.exit(1 if errors else 0)
