"""The Great Chocolate Chase (docs/design/greatcc.md slice 4): settings, users and progress on the real scripts.

Run 1 (`--headless --userdir <scratch> --seed 1`): two players are made through the real natives (the `lua` script command), a
saved value and the current level are set for each, and switching back and forth shows every player keeping their own state
(a value, the level, the Options Tutorials flag), while the game mode, which is a global preference, follows neither. A player
is renamed and a third is created and deleted again. Then the Options screen's Tutorials checkbox is switched off for the
first player. Asserts the files: users.txt lists Ann and Benny with Ann current, each player's greatcc-progress.txt holds their
own `value.num_bars` and `level` keys (and only Ann's has `tutorials.show=0`), prefs.txt holds `gamemode=2`, and no per-user
prefs file carries a gamemode.
Run 2 (a new engine on the same folder): the state is still there (Ann current, her value, level, mode 2, Tutorials off in the
Options checkbox), and switching to Benny shows his.
Both runs: exit 0, no [lua-error] / [engine-error] / [native-error] / [content] / [lua-arg], and the [unimplemented] and
[unimplemented-type] sets equal the allow-lists below."""
import argparse, os, re, shutil, subprocess, sys

ap = argparse.ArgumentParser()
ap.add_argument("--openconche", required=True)
ap.add_argument("--assets", required=True)
ap.add_argument("--workdir", required=True)
a = ap.parse_args()
shutil.rmtree(a.workdir, ignore_errors=True)
os.makedirs(a.workdir)
errors = []

# The TGameState telemetry store is not built; nothing else the menu and Options reach is a stub any more.
ALLOWED_UNIMPLEMENTED = set()
ALLOWED_TYPES = set()

USER = os.path.join(a.workdir, "user")


def run(tag, script_text):
    log = os.path.join(a.workdir, tag + ".log")
    script = os.path.join(a.workdir, tag + ".script")
    with open(script, "w") as f:
        f.write(script_text)
    p = subprocess.run([a.openconche, "--headless", "--assets", a.assets, "--userdir", USER, "--seed", "1", "--script", script,
                        "--log", log], capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=180)
    out = (open(log, encoding="utf-8", errors="replace").read() if os.path.exists(log) else "") + "\n" + p.stdout + "\n" + p.stderr
    if p.returncode != 0:
        errors.append("%s: exit status %d" % (tag, p.returncode))
    for t in ("[lua-error]", "[engine-error]", "[native-error]", "[content]", "[lua-arg]", "[script] FAIL"):
        if t in out:
            errors.append("%s: %s in the output: %s" % (tag, t, next(l for l in out.split("\n") if t in l)))
    seen = set(re.findall(r"^(?:\[\+\d+ms\] )?\[unimplemented\] (\S+)", out, re.M))
    if seen != ALLOWED_UNIMPLEMENTED:
        errors.append("%s: [unimplemented] set differs: extra %s, missing %s" % (
            tag, sorted(seen - ALLOWED_UNIMPLEMENTED), sorted(ALLOWED_UNIMPLEMENTED - seen)))
    types = set(re.findall(r"^(?:\[\+\d+ms\] )?\[unimplemented-type\] (\S+)", out, re.M))
    if types != ALLOWED_TYPES:
        errors.append("%s: [unimplemented-type] set differs: extra %s, missing %s" % (
            tag, sorted(types - ALLOWED_TYPES), sorted(ALLOWED_TYPES - types)))
    gone = set(re.findall(r"^(?:\[\+\d+ms\] )?\[gfx\] texture '([^']+)' not found", out, re.M))
    if gone:
        errors.append("%s: textures not found: %s" % (tag, sorted(gone)))
    dumps = [d for d in re.split(r"^(?=\[windows\] )", p.stdout, flags=re.M) if d.startswith("[windows] ")]
    return out, dumps


def read(rel):
    path = os.path.join(USER, rel)
    return open(path, encoding="utf-8").read() if os.path.exists(path) else None


def lines(text):
    return (text or "").split("\n")


# ------------------------------------------------------------------------------------------------------------------ run 1
RUN1 = """expect-window scripts/mainmenu.lua 20000
wait 500
expect-lua 500 GetNumUsers()==0 and GetCurrentUser()==-1 and GetCurrentGameMode()==0
lua CreateNewUser('Ann')
lua SetManagedValue('num_bars', 11, true)
lua SetCurrentLevel(4)
lua CreateNewUser('Ben')
expect-lua 500 GetNumUsers()==2 and GetCurrentUser()==1 and GetManagedInt('num_bars',0)==0 and GetCurrentLevel()==0
lua SetManagedValue('num_bars', 22, true)
lua SetCurrentLevel(9)
lua SetCurrentUser(0)
expect-lua 500 GetManagedInt('num_bars',0)==11 and GetCurrentLevel()==4
lua SetCurrentGameMode(2)
lua SetCurrentUser(1)
expect-lua 500 GetManagedInt('num_bars',0)==22 and GetCurrentLevel()==9 and GetCurrentGameMode()==2
lua ChangeCurrentUserName('Benny')
lua CreateNewUser('Cy')
expect-lua 500 GetNumUsers()==3 and GetManagedInt('num_bars',0)==0
lua DeleteUser(2)
expect-lua 500 GetNumUsers()==2 and GetCurrentUserName()=='Benny' and GetManagedInt('num_bars',0)==22
lua SetCurrentUser(0)
expect-lua 500 GetCurrentUserName()=='Ann' and GetManagedInt('num_bars',0)==11
wait 100
click 575 520
wait 1500
dump
click 362 395
wait 300
dump
wait 100
"""
out, dumps = run("run1", RUN1)
if len(dumps) != 2:
    errors.append("run1: expected 2 window dumps, got %d" % len(dumps))
else:
    if not re.search(r'Button "options_tutorials_checkbox" .* on$', dumps[0], re.M):
        errors.append("run1: Tutorials does not start on for Ann")
    if not re.search(r'Button "options_tutorials_checkbox" .* off', dumps[1], re.M):
        errors.append("run1: Tutorials did not switch off")

users = lines(read("users.txt"))
folders = [l.split("\t") for l in users if "\t" in l]
if [f[1] for f in folders] != ["Ann", "Benny"]:
    errors.append("run1: users.txt does not list Ann and Benny: %s" % users)
if "current 0" not in users:
    errors.append("run1: Ann is not the current user: %s" % users)
if len(folders) == 2:
    ann = read("profiles/%s/greatcc-progress.txt" % folders[0][0])
    ben = read("profiles/%s/greatcc-progress.txt" % folders[1][0])
    if ann is None or ben is None:
        errors.append("run1: a progress file is missing (Ann %s, Benny %s)" % (ann is not None, ben is not None))
    else:
        if lines(ann)[0] != "greatcc-progress 1":
            errors.append("run1: bad header in Ann's progress file")
        for who, text, bars, level in (("Ann", ann, "11", "4"), ("Benny", ben, "22", "9")):
            if "value.num_bars=%s" % bars not in lines(text):
                errors.append("run1: %s's file lacks value.num_bars=%s: %s" % (who, bars, lines(text)))
            if "value.current_level=%s" % level not in lines(text):
                errors.append("run1: %s's file lacks value.current_level=%s" % (who, level))
        if "tutorials.show=0" not in lines(ann):
            errors.append("run1: Ann's file lacks tutorials.show=0")
        if "tutorials.show=0" in lines(ben):
            errors.append("run1: Benny's file has Ann's Tutorials flag")
    for f in folders:
        prefs = read("profiles/%s/prefs.txt" % f[0]) or ""
        if "gamemode" in prefs:
            errors.append("run1: %s has a per-user gamemode pref" % f[1])
        if "greatcc-progress" in prefs:
            errors.append("run1: progress leaked into the prefs of %s" % f[1])
if "gamemode\t2" not in lines(read("prefs.txt")) and "gamemode=2" not in lines(read("prefs.txt")):
    errors.append("run1: the global prefs lack gamemode 2: %s" % lines(read("prefs.txt")))

# ------------------------------------------------------------------------------------------------------------------ run 2
RUN2 = """expect-window scripts/mainmenu.lua 20000
wait 500
expect-lua 500 GetNumUsers()==2 and GetCurrentUserName()=='Ann' and GetCurrentGameMode()==2
expect-lua 500 GetManagedInt('num_bars',0)==11 and GetCurrentLevel()==4 and GetManagedInt('current_worth',0)==0
lua SetCurrentUser(1)
expect-lua 500 GetCurrentUserName()=='Benny' and GetManagedInt('num_bars',0)==22 and GetCurrentLevel()==9
lua SetCurrentUser(0)
click 575 520
wait 1500
dump
"""
out, dumps = run("run2", RUN2)
if len(dumps) != 1 or not re.search(r'Button "options_tutorials_checkbox" .* off', dumps[0], re.M):
    errors.append("run2: Tutorials is not off after a restart")

for e in errors:
    print("FAIL:", e)
print("greatcc_users: %s" % ("FAILED" if errors else "ok"))
sys.exit(1 if errors else 0)
