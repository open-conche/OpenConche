"""The Great Chocolate Chase (docs/design/greatcc.md slice 5): audio on the real scripts, headless (the mixer renders into a
discard buffer; nothing is played).

Run 1 (`--headless --userdir <scratch> --seed 1`): the main menu starts audio/music/theme.ogg once (a looping music voice, logged
as `[audio] voice ...`); a player is made; opening Options and pressing a button logs the button's click file
(audio/sfx/buttonclick.ogg, which style.lua names); the Sound and Music sliders are dragged and the audio levels follow (the dump's
`audio levels` line); Mute All sets the mute flag and leaves the levels where they are, pressing it again clears it without
re-applying the stored levels; Close (OK) stores the levels as per-mille prefs; Mute All is pressed once more so the player is left muted.
Asserts the prefs file holds soundlevel/musiclevel near the dragged values and soundoff=1.
Run 2 (a new engine on the same folder): the first dump shows the same levels and mute for that player, and the menu starts the theme
again (a fresh engine has nothing playing).
Both runs: exit 0, no [lua-error] / [engine-error] / [native-error] / [content] / [lua-arg], and the [unimplemented] and
[unimplemented-type] sets equal the allow-lists below."""
import argparse, glob, os, re, shutil, subprocess, sys

ap = argparse.ArgumentParser()
ap.add_argument("--openconche", required=True)
ap.add_argument("--assets", required=True)
ap.add_argument("--workdir", required=True)
a = ap.parse_args()
shutil.rmtree(a.workdir, ignore_errors=True)
os.makedirs(a.workdir)
errors = []

# The TGameState telemetry store is not built; every sound native is real.
ALLOWED_UNIMPLEMENTED = set()
USER = os.path.join(a.workdir, "user")
LEVELS = re.compile(r"^audio levels sfx=([\d.]+) music=([\d.]+) env=[\d.]+ mute=(\d) pause=(\S+)", re.M)


def run(tag, script_text, allowed_types):
    log = os.path.join(a.workdir, tag + ".log")
    script = os.path.join(a.workdir, tag + ".script")
    with open(script, "w") as f:
        f.write(script_text)
    p = subprocess.run([a.openconche, "--headless", "--assets", a.assets, "--userdir", USER, "--seed", "1", "--script", script,
                        "--log", log], capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=180)
    logtext = open(log, encoding="utf-8", errors="replace").read() if os.path.exists(log) else ""
    out = logtext + "\n" + p.stdout + "\n" + p.stderr
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
    if types != allowed_types:
        errors.append("%s: [unimplemented-type] set differs: extra %s, missing %s" % (
            tag, sorted(types - allowed_types), sorted(allowed_types - types)))
    levels = [(float(m[0]), float(m[1]), m[2] == "1", m[3]) for m in LEVELS.findall(p.stdout)]
    return logtext, levels


def near(x, want, tol=0.04):
    return abs(x - want) <= tol


# ------------------------------------------------------------------------------------------------------------------ run 1
# Positions (logical 800x600): Options button (575, 520); Sound knob at 0.75 centre (509, 174), Music knob (509, 247); Mute All
# (362, 310); Close (404, 486). The sliders are 301 px of travel from x = 257.
RUN1 = """expect-window scripts/mainmenu.lua 20000
wait 800
lua CreateNewUser('Ann')
wait 100
dump
click 575 520
wait 1500
dump
move 509 174
wait 50
down 509 174
move 460 174
move 404 174
up 404 174
wait 100
move 509 247
wait 50
down 509 247
move 420 247
move 359 247
up 359 247
wait 100
dump
click 362 310
wait 100
dump
click 362 310
wait 100
dump
click 404 486
wait 1500
dump
click 575 520
wait 1500
click 362 310
wait 100
dump
click 404 486
wait 1500
dump
wait 100
"""
log1, lv1 = run("run1", RUN1, set())
if lv1[:1] and not (near(lv1[0][0], 0.75, 0.001) and near(lv1[0][1], 0.75, 0.001) and not lv1[0][2]):
    errors.append("run1: the first player does not start at 750/750 unmuted: %s" % (lv1[0],))
if len(lv1) != 8:
    errors.append("run1: expected 8 dumps with an audio line, got %d" % len(lv1))
else:
    if not (near(lv1[2][0], 0.41, 0.03) and near(lv1[2][1], 0.29, 0.03) and not lv1[2][2]):
        errors.append("run1: the sliders did not set sound 0.41 / music 0.29: %s" % (lv1[2],))
    if not (lv1[3][2] and near(lv1[3][0], lv1[2][0], 0.001) and near(lv1[3][1], lv1[2][1], 0.001)):
        errors.append("run1: mute did not set the mute flag and keep the levels: %s" % (lv1[3],))
    if not (not lv1[4][2] and near(lv1[4][0], lv1[2][0], 0.001) and near(lv1[4][1], lv1[2][1], 0.001)):
        errors.append("run1: unmute did not clear the flag and keep the levels: %s" % (lv1[4],))
    if not lv1[7][2]:
        errors.append("run1: the player was not left muted: %s" % (lv1[7],))
themes = re.findall(r"^(?:\[\+\d+ms\] )?\[audio\] voice audio/music/theme\.ogg group=music loop=1$", log1, re.M)
if len(themes) != 1:
    errors.append("run1: expected the theme to start once, saw %d" % len(themes))
clicks = re.findall(r"^(?:\[\+\d+ms\] )?\[audio\] file audio/sfx/buttonclick\.ogg$", log1, re.M)
if len(clicks) < 4:
    errors.append("run1: expected button clicks to log audio/sfx/buttonclick.ogg, saw %d" % len(clicks))

prefs = ""
for path in glob.glob(os.path.join(USER, "profiles", "*", "prefs.txt")) + glob.glob(os.path.join(USER, "profiles", "*", "*.txt")):
    prefs += open(path, encoding="utf-8").read() + "\n"
sl = re.search(r"^soundlevel[=\t ]+(\d+)", prefs, re.M)
ml = re.search(r"^musiclevel[=\t ]+(\d+)", prefs, re.M)
so = re.search(r"^soundoff[=\t ]+(\d+)", prefs, re.M)
if not (sl and ml and so):
    errors.append("run1: the per-user prefs lack soundlevel/musiclevel/soundoff (found: %s)" % prefs.strip().replace("\n", ", "))
else:
    if not (near(int(sl.group(1)), 414, 30) and near(int(ml.group(1)), 287, 30)):
        errors.append("run1: stored levels %s/%s are not near 414/287" % (sl.group(1), ml.group(1)))
    if so.group(1) != "1":
        errors.append("run1: soundoff is %s, expected 1" % so.group(1))

# ------------------------------------------------------------------------------------------------------------------ run 2
RUN2 = """expect-window scripts/mainmenu.lua 20000
wait 800
dump
"""
log2, lv2 = run("run2", RUN2, set())
if len(lv2) != 1:
    errors.append("run2: expected 1 dump with an audio line, got %d" % len(lv2))
elif len(lv1) == 8:
    if not (near(lv2[0][0], lv1[7][0], 0.001) and near(lv2[0][1], lv1[7][1], 0.001) and lv2[0][2]):
        errors.append("run2: the levels did not persist: run 1 ended %s, run 2 shows %s" % (lv1[7], lv2[0]))
if len(re.findall(r"\[audio\] voice audio/music/theme\.ogg group=music loop=1", log2)) != 1:
    errors.append("run2: the fresh engine did not start the theme once")

if errors:
    print("FAIL")
    for e in errors:
        print("  " + e)
    sys.exit(1)
print("greatcc_audio: ok")
