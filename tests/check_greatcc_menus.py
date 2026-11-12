"""The Great Chocolate Chase (docs/design/greatcc.md slice 3): the widget library and the menu screens on the real scripts.

Runs `--headless --userdir <scratch> --seed 1` several times (the screens that swap the main menu away cannot come back before
later slices, so each of those gets its own run) and asserts, per run: exit 0; no [lua-error] / [engine-error] / [native-error] /
[content]; the [unimplemented] and [unimplemented-type] sets equal that run's allow-list; the only texture the scripts name that
the install does not ship is ui/story/btn_trim_left.

Run "menus" (one fresh user folder, no players): the main menu says "Welcome!" with no name and hides the DEBUG and More Games
buttons; Quit opens the confirm dialog and Cancel closes it; Options opens with the sliders at 750 per mille, the checkboxes
toggle (Mute All turns the mixer's mute on, Tutorials goes off, Full Screen goes on and off), Credits opens and Close returns to
Options, Close returns to the menu; Help opens on page 1/10 with Back disabled, Next steps to page 10/10 where Next is disabled,
Back steps back, Close returns; Choose Player opens with every row empty, New opens Enter Name with OK disabled, typing enables
it, OK makes the first player (row 0 selected), Cancel leaves the list alone, a second player is added, a row click selects, OK
returns to the menu showing the chosen name; the user file lists both players.
Runs "story" and "endless" (fresh folder): the button asks for a name through Enter Name, creates the player, and swaps to
scripts/worldmap.lua (greatcc_worldmap walks the map itself); in Story the map's own intro dialog is on top, in Endless (mode 1)
the map itself is. Run "rewards": the button swaps
to scripts/rewardswindow.lua, which shows its sweet rewards (greatcc_meta looks at them). Run "scores": swaps to scripts/hiscore.lua (greatcc_scores
looks at the tables).
The screenshots are 800x600 PNGs that differ from one another where the screen changed."""
import argparse, os, re, shutil, struct, subprocess, sys

ap = argparse.ArgumentParser()
ap.add_argument("--openconche", required=True)
ap.add_argument("--assets", required=True)
ap.add_argument("--workdir", required=True)
a = ap.parse_args()
shutil.rmtree(a.workdir, ignore_errors=True)
os.makedirs(a.workdir)
errors = []

# Shipped art that is not in the install (the scripts name it; the original shows nothing there as well).
MISSING_TEXTURES = {"ui/story/btn_trim_left"}


def run(tag, script_text, allow_calls, allow_types, missing=frozenset(), shots=()):
    user = os.path.join(a.workdir, "user_" + tag)
    log = os.path.join(a.workdir, tag + ".log")
    script = os.path.join(a.workdir, tag + ".script")
    paths = {s: os.path.join(a.workdir, "%s_%s.png" % (tag, s)) for s in shots}
    with open(script, "w") as f:
        f.write(script_text % {k: v for k, v in paths.items()} if "%(" in script_text else script_text)
    p = subprocess.run([a.openconche, "--headless", "--assets", a.assets, "--userdir", user, "--seed", "1", "--script", script,
                        "--log", log], capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=180)
    out = (open(log, encoding="utf-8", errors="replace").read() if os.path.exists(log) else "") + "\n" + p.stdout + "\n" + p.stderr
    if p.returncode != 0:
        errors.append("%s: exit status %d" % (tag, p.returncode))
    for t in ("[lua-error]", "[engine-error]", "[native-error]", "[content]"):
        if t in out:
            errors.append("%s: %s in the output: %s" % (tag, t, next(l for l in out.split("\n") if t in l)))
    seen = set(re.findall(r"^(?:\[\+\d+ms\] )?\[unimplemented\] (\S+)", out, re.M))
    if seen != allow_calls:
        errors.append("%s: [unimplemented] set differs: extra %s, missing %s" % (tag, sorted(seen - allow_calls), sorted(allow_calls - seen)))
    types = set(re.findall(r"^(?:\[\+\d+ms\] )?\[unimplemented-type\] (\S+)", out, re.M))
    if types != allow_types:
        errors.append("%s: [unimplemented-type] set differs: extra %s, missing %s" % (tag, sorted(types - allow_types), sorted(allow_types - types)))
    gone = set(re.findall(r"^(?:\[\+\d+ms\] )?\[gfx\] texture '([^']+)' not found", out, re.M))
    if gone != set(missing):
        errors.append("%s: missing-texture set differs: %s (expected %s)" % (tag, sorted(gone), sorted(missing)))
    dumps = [d for d in re.split(r"^(?=\[windows\] )", p.stdout, flags=re.M) if d.startswith("[windows] ")]
    return out, dumps, user, paths


def users_file(user):
    path = os.path.join(user, "users.txt")
    if not os.path.exists(path):
        return [], None
    names, cur = [], None
    for line in open(path, encoding="utf-8").read().split("\n")[1:]:
        if line.startswith("current "):
            cur = int(line.split()[1])
        elif "\t" in line:
            names.append(line.split("\t")[0])
    return names, cur


def has(tag, d, pattern, what):
    if not re.search(pattern, d, re.M):
        errors.append("%s: %s (no match for %s)" % (tag, what, pattern))
        return None
    return re.search(pattern, d, re.M)


def top_modal(d):
    mods = re.findall(r'^  Modal "([^"]*)" .*script="([^"]*)"', d, re.M)
    return mods[-1] if mods else (None, None)


def check_png(tag, path):
    if not os.path.exists(path):
        errors.append("%s: no screenshot %s" % (tag, os.path.basename(path)))
        return None
    data = open(path, "rb").read()
    w, h = struct.unpack(">II", data[16:24])
    if (w, h) != (800, 600):
        errors.append("%s: screenshot %s is %dx%d" % (tag, os.path.basename(path), w, h))
    return data


# ---------------------------------------------------------------------------------------------------------------- menus
SHOTS = ("menu", "confirm", "options", "options2", "credits", "help1", "help10", "cp0", "en0", "en1", "cp1", "cp2")
SCRIPT = """expect-window scripts/mainmenu.lua 20000
wait 1000
dump
screenshot-logical %(menu)s
click 672 463
wait 1200
dump
screenshot-logical %(confirm)s
click 490 348
wait 900
dump
click 575 520
wait 1500
dump
screenshot-logical %(options)s
click 362 310
wait 200
dump
click 362 395
wait 200
dump
click 362 353
wait 700
dump
click 362 353
wait 700
dump
screenshot-logical %(options2)s
click 538 374
wait 2500
dump
screenshot-logical %(credits)s
click 400 512
wait 1800
dump
click 404 486
wait 1500
dump
click 338 540
wait 1500
dump
screenshot-logical %(help1)s
click 540 447
wait 300
dump
click 540 447
click 540 447
click 540 447
click 540 447
click 540 447
click 540 447
click 540 447
click 540 447
wait 300
dump
screenshot-logical %(help10)s
click 260 447
wait 300
dump
click 400 447
wait 1500
dump
click 470 545
wait 1500
dump
screenshot-logical %(cp0)s
click 400 380
wait 1200
dump
screenshot-logical %(en0)s
type Ann
wait 100
dump
screenshot-logical %(en1)s
click 310 368
wait 1500
dump
screenshot-logical %(cp1)s
click 400 380
wait 1200
click 490 368
wait 1200
dump
click 400 380
wait 1200
type Bea
wait 100
click 310 368
wait 1500
dump
screenshot-logical %(cp2)s
click 270 187
wait 300
dump
click 400 447
wait 1500
dump
"""
out, dumps, user, paths = run("menus", SCRIPT, set(), set(), MISSING_TEXTURES, SHOTS)
(D_MENU, D_QUIT, D_QUITGONE, D_OPT, D_MUTE, D_TUT, D_FULL, D_FULLOFF, D_CREDITS, D_OPTBACK, D_MENU2, D_HELP1, D_HELP2, D_HELP10,
 D_HELPBACK, D_HELPGONE, D_CP0, D_EN0, D_EN1, D_CP1, D_CP_CANCEL, D_CP2, D_CP_PICK, D_MENU3) = range(24)
if len(dumps) != 24:
    errors.append("menus: expected 24 window dumps, got %d" % len(dumps))
else:
    d = dumps
    # The main menu with no players.
    has("menus", d[D_MENU], r'^        Text "txt_welcome" id=\d+ rect=\S+ label="Welcome!"', "the welcome text is not 'Welcome!' with no player")
    has("menus", d[D_MENU], r'RShrinkText "txt_user" id=\d+ rect=360,105,250,35 label=""', "the user text is not empty with no player")
    has("menus", d[D_MENU], r'Button "mainmenu_debug_button" id=\d+ \S+ hidden', "the DEBUG button is not hidden")
    has("menus", d[D_MENU], r'Button "moregames" id=\d+ \S+ hidden', "the More Games button is not hidden")
    has("menus", d[D_MENU], r'TMainMenu "" id=\d+ rect=0,0,800,609', "TMainMenu is missing")
    # Quit.
    if top_modal(d[D_QUIT])[1] != "scripts/yesno.lua":
        errors.append("menus: Quit did not open scripts/yesno.lua")
    has("menus", d[D_QUIT], r'KSplit9Image "" id=\d+ rect=\S+ image="ui/backgrounds/split9_dialog_small" insets=40,40,40,40', "the confirm frame is not a KSplit9Image")
    has("menus", d[D_QUIT], r'RShrinkText "ok_body" id=\d+ rect=\S+ label="Are you sure you want to quit\?"', "the confirm body is missing")
    if top_modal(d[D_QUITGONE])[1] != "scripts/mainmenu.lua":
        errors.append("menus: Cancel did not return to the main menu")
    # Options.
    if top_modal(d[D_OPT])[1] != "scripts/options.lua":
        errors.append("menus: Options did not open")
    for name, v in (("options_sfxlevel_slider", "0.750"), ("options_musiclevel_slider", "0.750")):
        has("menus", d[D_OPT], r'Slider "%s" id=\d+ rect=\S+ value=%s' % (name, v), "%s is not at 750 per mille" % name)
    has("menus", d[D_OPT], r'Button "options_muteall_checkbox" .* off', "Mute All does not start off")
    has("menus", d[D_OPT], r'Button "options_fullscreen_checkbox" .* off', "Full Screen does not start off")
    has("menus", d[D_OPT], r'Button "options_tutorials_checkbox" .* on$', "Tutorials does not start on")
    has("menus", d[D_OPT], r'Bitmap "sound_fill" id=\d+ rect=288,170,225,\d+', "the sound fill is not 225 wide (0.75 of the texture)")
    has("menus", d[D_MUTE], r'Button "options_muteall_checkbox" .* on command', "Mute All did not switch on")
    has("menus", d[D_MUTE], r"audio levels .* mute=1", "the mixer is not muted")
    has("menus", d[D_TUT], r'Button "options_tutorials_checkbox" .* off', "Tutorials did not switch off")
    has("menus", d[D_FULL], r'Button "options_fullscreen_checkbox" .* on command', "Full Screen did not switch on")
    has("menus", d[D_FULLOFF], r'Button "options_fullscreen_checkbox" .* on command'.replace(" on command", " off command"), "Full Screen did not switch off again")
    # Credits and back.
    if top_modal(d[D_CREDITS])[1] != "scripts/credits.lua":
        errors.append("menus: Credits did not open")
    has("menus", d[D_CREDITS], r'TCredits "" id=\d+ rect=0,140,800,396 time=40000 intropause=2000', "TCredits is missing")
    if top_modal(d[D_OPTBACK])[1] != "scripts/options.lua":
        errors.append("menus: Close on Credits did not return to Options")
    if top_modal(d[D_MENU2])[1] != "scripts/mainmenu.lua":
        errors.append("menus: Close on Options did not return to the main menu")
    # Help.
    if top_modal(d[D_HELP1])[1] != "scripts/help.lua":
        errors.append("menus: Help did not open")
    has("menus", d[D_HELP1], r'VHelpWindow "" id=\d+ rect=\S+ page=1/10', "Help does not start on page 1/10")
    has("menus", d[D_HELP1], r'RDisableButton "btn_back" .* layer=3 disabled', "Back is not disabled on page 1")
    has("menus", d[D_HELP1], r'RDisableButton "btn_next" .* layer=0$', "Next is not enabled on page 1")
    has("menus", d[D_HELP1], r'Text "txt_title" id=\d+ rect=0,0,280,65 label="Introduction"', "the title of page 1 is wrong")
    has("menus", d[D_HELP1], r'Text "txt_page" id=\d+ rect=\S+ label="1/10"', "the page counter of page 1 is wrong")
    has("menus", d[D_HELP2], r'VHelpWindow "" id=\d+ rect=\S+ page=2/10', "Next did not step to page 2")
    has("menus", d[D_HELP2], r'RDisableButton "btn_back" .* layer=0$', "Back is not enabled on page 2")
    has("menus", d[D_HELP10], r'VHelpWindow "" id=\d+ rect=\S+ page=10/10', "Next presses did not reach page 10")
    has("menus", d[D_HELP10], r'RDisableButton "btn_next" .* layer=3 disabled', "Next is not disabled on page 10")
    has("menus", d[D_HELP10], r'Text "txt_page" id=\d+ rect=\S+ label="10/10"', "the page counter of page 10 is wrong")
    has("menus", d[D_HELPBACK], r'VHelpWindow "" id=\d+ rect=\S+ page=9/10', "Back did not step to page 9")
    has("menus", d[D_HELPBACK], r'RDisableButton "btn_next" .* layer=0$', "Next is not enabled again on page 9")
    if top_modal(d[D_HELPGONE])[1] != "scripts/mainmenu.lua":
        errors.append("menus: Close on Help did not return to the main menu")
    # Choose Player.
    if top_modal(d[D_CP0])[1] != "scripts/chooseplayer.lua":
        errors.append("menus: Choose Player did not open")
    has("menus", d[D_CP0], r'TChoosePlayer "" id=\d+ rect=0,0,491,404', "TChoosePlayer is missing")
    has("menus", d[D_CP0], r'Button "chooseplayer_new_button" id=\d+ rect=\S+ label="New" type=push state=0 layer=0 command', "New is not shown")
    has("menus", d[D_CP0], r'Button "chooseplayer_ok_button" id=\d+ rect=\S+ hidden', "OK is not hidden with no players")
    if len(re.findall(r'Button "chooseplayer_button_\d" id=\d+ rect=\S+ hidden', d[D_CP0])) != 10:
        errors.append("menus: not all ten player rows are hidden with no players")
    if top_modal(d[D_EN0])[1] != "scripts/entername.lua":
        errors.append("menus: New did not open Enter Name")
    has("menus", d[D_EN0], r'RDisableButton "ok" id=\d+ rect=\S+ label="" type=push state=0 layer=3 command disabled', "OK is not disabled on an empty name")
    has("menus", d[D_EN0], r'RTextEditDisable "entername_name_textedit" .* text="" .* length=20 focused disable="ok"', "the name field is wrong")
    has("menus", d[D_EN0], r'KSplit9Image "" id=\d+ rect=\S+ image="gfx/textfield" insets=5,5,5,5', "the name field frame is missing")
    has("menus", d[D_EN1], r'RDisableButton "ok" id=\d+ rect=\S+ label="" type=push state=0 layer=0 command$', "OK is not enabled after typing")
    has("menus", d[D_EN1], r'text="Ann"', "the typed name is not in the field")
    has("menus", d[D_CP1], r'Button "chooseplayer_button_0" id=\d+ rect=\S+ label="" type=radio state=0 layer=1 on', "the first player's row is not selected")
    has("menus", d[D_CP1], r'RShrinkText "namefield" id=\d+ rect=\S+ label="Ann\s+"', "the first player's name is not in row 0")
    has("menus", d[D_CP1], r'Button "chooseplayer_ok_button" id=\d+ rect=\S+ label="OK"', "OK is not shown with a player")
    if top_modal(d[D_CP_CANCEL])[1] != "scripts/chooseplayer.lua":
        errors.append("menus: Cancel on Enter Name did not return to Choose Player")
    if len(re.findall(r'RShrinkText "namefield" id=\d+ rect=\S+ label="[^"\s]', d[D_CP_CANCEL])) != 1:
        errors.append("menus: Cancel changed the player list")
    names_shown = re.findall(r'label="(\w+)\s+" size', d[D_CP2])
    if names_shown != ["Bea", "Ann"] and sorted(names_shown) != ["Ann", "Bea"]:
        errors.append("menus: the two player names are not in the rows: %s" % names_shown)
    has("menus", d[D_CP2], r'Button "chooseplayer_button_1" id=\d+ rect=\S+ label="" type=radio state=0 layer=1 on', "the second player's row is not selected")
    has("menus", d[D_CP_PICK], r'Button "chooseplayer_button_0" id=\d+ rect=\S+ label="" type=radio state=\d layer=\d on', "the click on row 0 did not select it")
    has("menus", d[D_CP_PICK], r'Button "chooseplayer_button_1" id=\d+ rect=\S+ label="" type=radio state=0 layer=0 off', "row 1 is still selected")
    if top_modal(d[D_MENU3])[1] != "scripts/mainmenu.lua":
        errors.append("menus: OK on Choose Player did not return to the main menu")
    has("menus", d[D_MENU3], r'Text "txt_welcome" id=\d+ rect=\S+ label="Welcome,"', "the welcome text did not change with a player")
    has("menus", d[D_MENU3], r'RShrinkText "txt_user" id=\d+ rect=360,105,250,35 label="Ann!"', "the chosen player's name is not on the menu")
names, cur = users_file(user)
if names != ["Ann", "Bea"]:
    errors.append("menus: the user file lists %s, not Ann and Bea" % names)
if cur != 0:
    errors.append("menus: the current player is %s, not Ann (0)" % cur)

data = {s: check_png("menus", paths[s]) for s in SHOTS}
if all(data.values()):
    for s in SHOTS:
        if data[s] == data["menu"] and s != "menu":
            errors.append("menus: screenshot %s equals the main menu" % s)
    for x, y in (("options", "options2"), ("help1", "help10"), ("cp0", "cp1"), ("cp1", "cp2"), ("en0", "en1")):
        if data[x] == data[y]:
            errors.append("menus: screenshots %s and %s are identical" % (x, y))

# --------------------------------------------------------------------------------------- first run: Story and Endless
for tag, click in (("story", "333 447"), ("endless", "480 447")):
    text = """expect-window scripts/mainmenu.lua 20000
wait 1000
click %s
wait 1500
dump
type Zed
wait 100
click 310 368
wait 3500
dump
""" % click
    out, dumps, user, _ = run(tag, text, set(), set(), MISSING_TEXTURES)
    if len(dumps) != 2:
        errors.append("%s: expected 2 window dumps, got %d" % (tag, len(dumps)))
        continue
    if top_modal(dumps[0])[1] != "scripts/entername.lua":
        errors.append("%s: the first run did not ask for a name" % tag)
    has(tag, dumps[0], r'RDisableButton "ok" .* disabled', "OK is not disabled on the empty name")
    # Story mode shows the intro dialog on the map (worldmap.lua CheckIntro); an endless mode, now that GetCurrentGameMode is
    # real, skips it and leaves the map itself on top.
    expect_top = "scripts/intro.lua" if tag == "story" else "scripts/worldmap.lua"
    if top_modal(dumps[1])[1] != expect_top:
        errors.append("%s: %s is not on top" % (tag, expect_top))
    if 'script="scripts/worldmap.lua"' not in dumps[1]:
        errors.append("%s: scripts/worldmap.lua is not in the modal chain" % tag)
    if users_file(user)[0] != ["Zed"]:
        errors.append("%s: the user file does not list Zed: %s" % (tag, users_file(user)[0]))

# ---------------------------------------------------------------------------------------------------- Rewards, Scores
for tag, click, script, calls, types in (
        ("rewards", "125 460", "scripts/rewardswindow.lua", set(), set()),
        ("scores", "235 515", "scripts/hiscore.lua", set(), set())):
    text = "expect-window scripts/mainmenu.lua 20000\nwait 1000\nclick %s\nwait 3000\ndump\n" % click
    out, dumps, user, _ = run(tag, text, calls, types)
    if len(dumps) != 1 or top_modal(dumps[0])[1] != script:
        errors.append("%s: did not swap to %s" % (tag, script))

for e in errors:
    print("FAIL:", e)
print("greatcc_menus: %s" % ("FAILED" if errors else "ok"))
sys.exit(1 if errors else 0)
