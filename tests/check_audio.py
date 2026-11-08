"""Audio end to end (docs/design/audio.md), headless: the device-less pump renders on the simulated clock, so
everything is read from the `audio ...` lines of the script driver's `dump` and the [audio] lines of the --log file.

  menu     Boots to the main menu: the menu music event fired and is playing (main_menu_), a hover fired the rollover
           sound, a click fired ui_click, Options opens, a slider drag changes the level live (and only that level), the
           mute box toggles mute=1/0, OK stores the level in the prefs, focus lost/gained pauses and resumes everything.
  game     (Reforged) Starts a game and opens the chocolate factory: the factory events fire (intro, start bell, the
           music event), a Stop_* ends a loop, focus loss pauses (and the factory's own pause stays after regain), OK
           resumes.
  repeat   Two identical menu runs give identical [audio] log lines.
  help     (Reforged) Help and back keep the menu music: one `main_menu` build in the whole run, never a second.
  music    (Reforged) Port Zurich has the zurich environment and the Zurich_ arrangement; the map plays map_ with no
           environment; a flight runs the travel_plane loop and landing ends it; the port has its environment and sting
           again, and after 25 s of port time the timer sends Stop_Music and then a port_music<n> cue (a C3 Gameplay
           arrangement). The game run ends the chocolate factory: intro, body, then the outro, and closing the results
           brings the port environment back.
  Every run: exit 0, no [lua-error], no "[unimplemented] SoundEvent" and no "[unimplemented] ButtonSound" line.

--dataset reforged | vanilla | mac (the vanilla game gets its files from the CADI container through CadSource; the Macintosh
build's table comes from its cadi/cadilite.spec, and its menu, port and factory runs are checked from the log below).

  mac      Boots the Macintosh assets: the spec loads with 0 errors, the menu plays Main_Menu_v2 once, entering Zurich
           plays its sting and then the bed (Stop_Environments first), the sting ends and nothing plays until the port
           timer sends port_music<n>, and a factory run plays the intro and the looping body, fires the launch event,
           and logs no missing file."""
import argparse, os, re, shutil, subprocess, sys

ap = argparse.ArgumentParser()
ap.add_argument("--openconche", required=True)
ap.add_argument("--assets", required=True)
ap.add_argument("--workdir", required=True)
ap.add_argument("--dataset", choices=("reforged", "vanilla", "mac"), default="reforged")
a = ap.parse_args()
a.openconche, a.assets, a.workdir = (os.path.abspath(x) for x in (a.openconche, a.assets, a.workdir))
shutil.rmtree(a.workdir, ignore_errors=True)
os.makedirs(a.workdir)
errors = []
vanilla = a.dataset == "vanilla"
mac = a.dataset == "mac"

MENU = "expect-window ui/mainmenu.lua 60000\nwait 1300\n"
OPTIONS_XY = (550, 455)  # centre of the main menu's Options button on both menus
NEWGAME_XY = (400, 345) if vanilla else (310, 345)


def run(name, text, extra=()):
    work = os.path.join(a.workdir, name)
    os.makedirs(work)
    script = os.path.join(work, "t.script")
    with open(script, "w") as f:
        f.write(text.replace("@W@", work))
    cmd = [a.openconche, "--headless", "--assets", a.assets, "--userdir", os.path.join(work, "user"), "--script", script,
           "--dump-windows", "--log", os.path.join(work, "log.txt")] + list(extra)
    outf, errf = os.path.join(work, "out.txt"), os.path.join(work, "err.txt")
    with open(outf, "w") as fo, open(errf, "w") as fe:
        proc = subprocess.Popen(cmd, cwd=work, stdout=fo, stderr=fe, text=True)
        try:
            proc.wait(900)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()
    out, err = open(outf).read(), open(errf).read()
    log = open(os.path.join(work, "log.txt")).read() if os.path.exists(os.path.join(work, "log.txt")) else ""
    if proc.returncode != 0 or "[lua-error]" in out or "FAIL" in out:
        errors.append("%s: exit %d, tail %r %r" % (name, proc.returncode, out[-700:], err[-300:]))
    for line in (out + log).splitlines():
        if "[unimplemented] SoundEvent" in line or "[unimplemented] ButtonSound" in line:
            errors.append("%s: %s" % (name, line.strip()))
    return out, log, work


def dumps(out):
    """Every `audio ...` block of the output, in order, as dicts."""
    blocks, cur = [], None
    for line in out.splitlines():
        m = re.match(r"audio levels sfx=([\d.]+) music=([\d.]+) env=([\d.]+) mute=(\d) pause=(\S+)", line)
        if m:
            cur = {"sfx": float(m.group(1)), "music": float(m.group(2)), "env": float(m.group(3)), "mute": int(m.group(4)),
                   "pause": m.group(5), "music_name": None, "env_name": None, "loops": [], "fired": {}}
            blocks.append(cur)
        elif cur is not None and line.startswith("audio music "):
            cur["music_name"] = None if line.endswith(" none") else line[len("audio music "):].rsplit(" stage=", 1)[0]
        elif cur is not None and line.startswith("audio env "):
            cur["env_name"] = None if line.endswith(" none") else line.split()[2]
        elif cur is not None and line.startswith("audio loop "):
            cur["loops"].append(line.split()[2])
        elif cur is not None and line.startswith("audio fired"):
            for tok in line.split()[2:]:
                if "=" in tok:
                    k, v = tok.rsplit("=", 1)
                    cur["fired"][k] = int(v)
    return blocks


def rects(dump):
    out, stack = {}, []
    for line in dump.splitlines():
        m = re.match(r'^( +)(\w+) "([^"]*)" id=\d+ rect=(-?\d+),(-?\d+),(\d+),(\d+)', line)
        if not m:
            continue
        indent = len(m.group(1))
        x, y, w, h = map(int, m.groups()[3:])
        while stack and stack[-1][0] >= indent:
            stack.pop()
        ox, oy = (stack[-1][1], stack[-1][2]) if stack else (0, 0)
        stack.append((indent, ox + x, oy + y))
        out.setdefault(m.group(3), (ox + x, oy + y, w, h, line))
    return out


def centre(r):
    return r[0] + r[2] / 2.0, r[1] + r[3] / 2.0


def audio_lines(log):
    """The [audio] lines of the log without their timestamps."""
    return [re.sub(r"^\[\+\d+ms\] ", "", l) for l in log.splitlines() if "] [audio] " in l]


def expect(cond, msg):
    if not cond:
        errors.append(msg)


def finish():
    if errors:
        print("FAIL")
        for e in errors:
            print(" ", e)
        sys.exit(1)
    print("audio ok (%s)" % a.dataset)


if mac:
    NEWGAME = (MENU + "click 400 345\nexpect-window ui/ui_entername.lua 5000\nwait 600\ntype Tester\nkey Return\n"
               "expect-window ui/portview.lua 5000\nwait 3000\n" + "click 400 346\nwait 1500\n" * 3)
    # Help and back fire nothing (only its replay_intro button sends main_menu again): the menu track plays on.
    out, log, work = run("help", MENU + "click 250 455\nexpect-window ui/ui_help.lua 5000\nwait 800\nkey Escape\nwait 1500\ndump\nquit\n")
    starts = log.count("music file cadi/audio/Main_Menu_v2.ogg loop fade 0/0")
    b = dumps(out)
    expect("ui_help.lua" in out, "help: the Help dialog never opened")
    expect(starts == 1, "help: Help and back must not restart the menu music: %d starts" % starts)
    expect(b and b[-1]["music_name"] == "cadi/audio/Main_Menu_v2.ogg", "help: the menu music should play after Help: %r" % b[-1:])
    # Menu and port: the sting plays once, then the bed; the port timer later sends Stop_Music and port_music<n>.
    out, log, work = run("port", NEWGAME + "dump\nwait 40000\ndump\nquit\n")
    lines = audio_lines(log)
    expect(any(re.fullmatch(r"\[audio\] cadilite.spec: \d+ events, 0 errors, \d+ duplicates dropped, \d+ without an engine equivalent", l)
               for l in lines), "port: the spec summary line is missing or reports errors: %r" % [l for l in lines if "cadilite" in l][:8])
    unknown = sorted(re.findall(r"unknown event (\S+)", log))
    expect(unknown == [], "port: unknown audio events: %r" % unknown)
    expect(not [l for l in lines if "cadilite.spec:" in l and ("malformed" in l or "unknown directive" in l)], "port: spec parse errors")
    fire = [l for l in lines if "music file" in l or "environment" in l or "stop " in l]
    expect(log.count("music file cadi/audio/Main_Menu_v2.ogg loop fade 0/0") == 1, "port: the menu music should start once")
    i = next((k for k, l in enumerate(fire) if "Zurich_v3.ogg once" in l), -1)
    expect(i > 0 and "Stop_Music -> stop music" in fire[i - 1], "port: the Zurich sting should follow Stop_Music: %r" % fire[:8])
    expect(i >= 0 and any("zurich -> environment" in l for l in fire[i:i + 3]), "port: the Zurich bed should follow the sting")
    b = dumps(out)
    expect(len(b) == 2 and b[0]["env_name"] == "zurich", "port: the Zurich bed should be playing: %r" % [x["env_name"] for x in b])
    cue = [k for k, l in enumerate(fire) if re.search(r"music file cadi/audio/\S+ (once|loop)", l) and k > i]
    expect(len(cue) == 1, "port: exactly one music file (the port_music cue) should follow the sting in 40 s: %r" % [fire[k] for k in cue])
    expect(len(b) == 2 and b[1]["music_name"] is not None and "Zurich_v3" not in b[1]["music_name"],
           "port: the port timer should have started a different music file: %r" % [x["music_name"] for x in b])
    # The factory: Stop_Environments, then the intro, and the body loops.
    FACTORY = (NEWGAME + "lua f = zur_factory; f:MarkOwned(); p = _AllProducts.b01; Player.rank = 1; "
               "for k, n in pairs(p:GetNeeds()) do Player.ingredients[k] = 1000 end\n"
               "lua gRun = p:RunMinigame{factory = f}; if gRun >= 0 then f:SetProduction(p, gRun); TickSim(1) end\n"
               "expect-window ui/chocolate_factory.lua 5000\nwait 500\ndump\nclick 327 398\nwait 2000\n" +
               "move 400 300\nclick 400 300\nwait 400\nclick 300 250\nwait 400\nclick 500 250\nwait 400\n" * 6 + "dump\nquit\n")
    out, log, work = run("factory", FACTORY)
    lines = audio_lines(log)
    expect("[audio] Stop_Environments -> stop environments" in lines, "factory: Stop_Environments never fired")
    expect(any("music file cadi/audio/C3_Minigame1_Intro.ogg loop" in l for l in lines), "factory: the intro should loop")
    expect(any("music file cadi/audio/C3_Minigame1_Body_60sec.ogg loop" in l for l in lines), "factory: the body should loop")
    expect("[audio] launch -> oneshot audio/launch.ogg" in lines, "factory: the gun's launch event should play launch.ogg")
    unknown = sorted(set(re.findall(r"unknown event (\S+)", log)))
    expect(unknown == ["start_bell"], "factory: the only unknown event should be start_bell (the Mac spec has none): %r" % unknown)
    expect(not [l for l in lines if "missing file" in l], "factory: missing files: %r" % [l for l in lines if "missing file" in l][:5])
    b = dumps(out)
    expect(len(b) == 2 and b[1]["env_name"] is None and b[1]["music_name"] and "Minigame1" in b[1]["music_name"],
           "factory: the body music should play with no environment: %r" % [(x["music_name"], x["env_name"]) for x in b])
    finish()
    sys.exit(0)


# --------------------------------------------------------------------------------------------------------- menu
out, log, work = run("probe", MENU + "click %d %d\nexpect-window ui/ui_options.lua 5000\nwait 1200\nquit\n" % OPTIONS_XY)
d = rects(out)
for n in ("sfxlevelslider", "musiclevelslider", "ambientlevelslider", "mutebox", "ok"):
    expect(n in d, "probe: Options has no %s" % n)
if not errors:
    sl, ok, mb = d["sfxlevelslider"], d["ok"], d["mutebox"]
    sx, sy, sw, sh = sl[:4]
    knob_x = sx + (sw - 32) * 0.75 + 16
    y = sy + sh / 2.0
    target = sx + sw * 0.25
    mx, my = centre(mb)
    ox, oy = centre(ok)
    MENU_RUN = (MENU + "move 300 300\nwait 100\nclick %d %d\nexpect-window ui/ui_options.lua 5000\nwait 1200\ndump\n" % OPTIONS_XY +
                "move %g %g\ndown %g %g\nmove %g %g\nmove %g %g\nup %g %g\nwait 200\ndump\n"
                % (knob_x, y, knob_x, y, (knob_x + target) / 2, y, target, y, target, y) +
                "click %g %g\nwait 300\ndump\nclick %g %g\nwait 300\ndump\n" % (mx, my, mx, my) +
                "focus lost\nwait 100\ndump\nfocus gained\nwait 100\ndump\n"
                "click %g %g\nwait 1500\ndump\nquit\n" % (ox, oy))
    out, log, work = run("menu", MENU_RUN)
    b = dumps(out)
    if len(b) != 7:
        errors.append("menu: expected 7 audio dumps, got %d" % len(b))
    if len(b) == 7:
        opened, dragged, mute1, mute0, lost, gained, closed = b
        expect(opened["music_name"] == "main_menu_", "menu: the menu music should be playing: %r" % opened["music_name"])
        expect(opened["fired"].get("main_menu") == 1, "menu: the main_menu event should have fired once: %r" % opened["fired"])
        expect(opened["fired"].get("cadi/ui_click.ogg", 0) >= 1, "menu: the Options click should have fired ui_click: %r" % opened["fired"])
        expect(opened["sfx"] == 0.75 and opened["music"] == 0.65 and opened["env"] == 0.5 and opened["mute"] == 0,
               "menu: default levels expected: %r" % opened)
        expect(abs(dragged["sfx"] - 0.25) < 0.011, "menu: the drag should set the sfx level to about 0.25: %r" % dragged["sfx"])
        expect(dragged["music"] == 0.65 and dragged["env"] == 0.5, "menu: the sfx drag changed another level: %r" % dragged)
        expect(mute1["mute"] == 1 and mute0["mute"] == 0, "menu: the mute box should toggle mute=1 then 0: %r %r" % (mute1["mute"], mute0["mute"]))
        expect(lost["pause"] == "focus", "menu: focus lost should pause: %r" % lost["pause"])
        expect(gained["pause"] == "none", "menu: focus gained should resume: %r" % gained["pause"])
        expect(closed["music_name"] == "main_menu_", "menu: the music should still play after Options closed: %r" % closed["music_name"])
        expect(closed["sfx"] == dragged["sfx"] or abs(closed["sfx"] - 0.25) < 0.011, "menu: OK should keep the dragged level: %r" % closed["sfx"])
    prefs = os.path.join(work, "user", "prefs.txt")
    text = open(prefs).read() if os.path.exists(prefs) else ""
    m = re.search(r"soundlevel\t(\d+)", text)
    expect(m and 235 <= int(m.group(1)) <= 260, "menu: OK should store soundlevel near 250 in the prefs: %r" % text)
    expect("[audio] main_menu -> music main_menu_" in log, "menu: no music line in the log")
    expect(log.count("[audio] main_menu -> music main_menu_") == 1,
           "menu: Options and back must not build the menu music again: %d builds" % log.count("[audio] main_menu -> music main_menu_"))
    expect(not re.search(r"\[audio\] unknown event", log), "menu: unknown audio event in the log: %s" %
           [l for l in log.splitlines() if "unknown event" in l][:3])
    if vanilla:
        expect("cadi/audio/ not found" not in out + log, "menu: the vanilla run should find its CADI audio")
    # Two identical runs give identical audio logs.
    out2, log2, _ = run("menu2", MENU_RUN)
    expect(audio_lines(log) == audio_lines(log2) and len(audio_lines(log)) > 5, "repeat: the [audio] log lines differ between runs")

# --------------------------------------------------------------------------------------------------------- help
if not vanilla:
    out, log, work = run("help", MENU + "click 250 455\nexpect-window ui/ui_help.lua 5000\nwait 800\ndump\nkey Escape\nwait 1200\ndump\nquit\n")
    b = dumps(out)
    expect(len(b) == 2 and all(x["music_name"] == "main_menu_" for x in b), "help: the menu music should play in Help and after it: %r" %
           [x["music_name"] for x in b])
    expect(log.count("[audio] main_menu -> music main_menu_") == 1, "help: the menu music was built %d times" %
           log.count("[audio] main_menu -> music main_menu_"))
    expect("ui_help.lua" in out, "help: the Help dialog never opened")

# --------------------------------------------------------------------------------------------------------- music
if not vanilla:
    START = (MENU + "click 310 345\nexpect-window ui/ui_entername.lua 5000\nwait 600\n"
             "type Tester\nkey Return\nexpect-window ui/ui_difficulty.lua 3000\nwait 800\nclick 328 400\nwait 5000\n"
             "click 400 346\nwait 1500\nclick 503 346\nwait 1500\n" + "click 400 346\nwait 1500\n" * 4 + "expect-window ui/portview.lua 3000\n")
    MUSIC = (START + "dump\nlua capetown:Unlock()\nclick 115 435\nexpect-window ui/mapview.lua 3000\nwait 500\ndump\n"
             "move 426 270\nwait 400\nclick 426 270\nexpect-lua 2000 gTravelActive\nexpect-sprite vehicle 1 2000\nwait 1200\ndump\n"
             "expect-lua 8000 Player.portName == \"capetown\" and not gTravelActive\nexpect-window ui/portview.lua 3000\nwait 2500\ndump\n"
             "wait 50000\ndump\nquit\n")
    out, log, work = run("music", MUSIC)
    b = dumps(out)
    if len(b) != 5:
        errors.append("music: expected 5 audio dumps, got %d" % len(b))
    else:
        zurich, mapped, flying, landed, later = b
        expect(zurich["env_name"] == "zurich" and zurich["music_name"] == "Zurich_",
               "music: arriving in Zurich should play env zurich and the Zurich_ arrangement: %r %r" % (zurich["env_name"], zurich["music_name"]))
        expect(mapped["music_name"] == "map_" and mapped["env_name"] is None,
               "music: the map should play map_ with no environment: %r %r" % (mapped["music_name"], mapped["env_name"]))
        expect("travel_plane" in flying["loops"], "music: the flight should loop travel_plane: %r" % flying["loops"])
        expect("travel_plane" not in landed["loops"], "music: landing should end travel_plane: %r" % landed["loops"])
        expect(landed["env_name"] == "capetown" and landed["music_name"] in ("capetown_", None),
               "music: Cape Town should have its environment and sting: %r %r" % (landed["env_name"], landed["music_name"]))
        expect(later["music_name"] is not None and later["music_name"].startswith("C3 Gameplay "),
               "music: the port timer should have started a port_music cue by now: %r" % later["music_name"])
    lines = audio_lines(log)
    stops = [i for i, l in enumerate(lines) if l.startswith("[audio] Stop_Music")]
    cues = [i for i, l in enumerate(lines) if re.match(r"\[audio\] port_music[123] -> music C3 Gameplay [123] \(alias\)", l)]
    expect(cues and any(s < cues[0] for s in stops), "music: Stop_Music should precede the port_music cue: %r" % lines[-12:])
    expect(len(cues) == 1, "music: exactly one port_music cue expected in this run: %d" % len(cues))
    expect(not re.search(r"\[audio\] .*(not started|rejected|cannot stream)", log), "music: a track failed to start: %s" %
           [l for l in log.splitlines() if "not started" in l or "rejected" in l][:3])

# --------------------------------------------------------------------------------------------------------- game
if vanilla:
    # check_vanilla_endtoend.py's opening: new game, three dialogs, the Zurich port (dump 1), Endrit's quest, the factory dialog.
    INTRO = (MENU + "click 400 345\nexpect-window ui/ui_entername.lua 5000\nwait 600\ntype Tester\nkey Return\n"
             "expect-window ui/portview.lua 5000\nwait 3000\n" + "click 400 346\nwait 1500\n" * 3 + "dump\n"
             "click 428 270\nexpect-window ui/ui_quest_offer.lua 5000\nwait 1200\nclick 400 346\nwait 1500\n"
             "expect-window ui/ui_factory.lua 8000\nwait 500\nclick 555 336\nexpect-window ui/chocolate_factory.lua 5000\nwait 500\ndump\n")
    SETUP = RUN = ""
else:
    INTRO = (MENU + "click 310 345\nexpect-window ui/ui_entername.lua 5000\nwait 600\n"
             "type Tester\nkey Return\nexpect-window ui/ui_difficulty.lua 3000\nwait 800\nclick 328 400\nwait 5000\n"
             "click 400 346\nwait 1500\nclick 503 346\nwait 1500\n" + "click 400 346\nwait 1500\n" * 4 + "dump\n")
    SETUP = ("lua f = zur_factory; f:MarkOwned(); p = _AllProducts.b01; Player.rank = 1; "
             "for k, n in pairs(p:GetNeeds()) do Player.ingredients[k] = 1000 end\n")
    RUN = ("lua gRun = p:RunMinigame{factory = f}; if gRun >= 0 then f:SetProduction(p, gRun); TickSim(1) end\n"
           "expect-window ui/chocolate_factory.lua 5000\nwait 500\ndump\n")
GAME = (INTRO + SETUP + RUN + "click 327 398\nwait 1500\nexpect-dump 1000 paused=0\nwait 2000\ndump\n"
        # The factory is playing: a loop starts and a Stop_ ends it.
        "lua SoundEvent('machine_spin')\nwait 300\ndump\nlua SoundEvent('Stop_machine_spin')\nwait 1500\ndump\n"
        "focus lost\nwait 100\nexpect-dump 1000 paused=1\ndump\nfocus gained\nwait 300\ndump\n"
        "click 399 403\nwait 300\nexpect-dump 1000 paused=0\ndump\n"
        # The game runs out (the outro plays), then Done on the results closes the factory and the port comes back.
        + ("" if vanilla else "wait 95000\ndump\nclick 471 400\nwait 1500\ndump\n") + "quit\n")
out, log, work = run("game", GAME)
b = dumps(out)
if len(b) != (8 if vanilla else 10):
    errors.append("game: expected %d audio dumps, got %d" % (8 if vanilla else 10, len(b)))
else:
    port, factory, playing, looping, stopped, lost, gained, resumed = b[:8]
    expect(port["env_name"] == "zurich" and port["music_name"] == "Zurich_",
           "game: the Zurich port should have its environment and arrangement: %r %r" % (port["env_name"], port["music_name"]))
    if not vanilla:
        ended, after = b[8:]
        expect(ended["fired"].get("chocolate_making_game_outro", 0) >= 1, "game: the outro should have played: %r" % sorted(ended["fired"]))
        expect(ended["env_name"] is None, "game: no environment during the factory: %r" % ended["env_name"])
        expect(after["env_name"] == "zurich", "game: the port environment should be back after the factory: %r" % after["env_name"])
    expect(port["env_name"] is not None or port["music_name"] is not None, "game: the port should have music or an environment: %r" % port)
    expect(factory["music_name"] is not None and factory["music_name"].startswith("choco"),
           "game: the chocolate factory music should be playing: %r" % factory["music_name"])
    for ev in ("chocolate_making_game_intro", "start_bell"):
        expect(playing["fired"].get(ev, 0) >= 1, "game: %s should have fired: %r" % (ev, sorted(playing["fired"])))
    expect("machine_spin" in looping["loops"], "game: the machine_spin loop should be running: %r" % looping["loops"])
    expect("machine_spin" not in stopped["loops"], "game: Stop_machine_spin should have ended the loop: %r" % stopped["loops"])
    expect("unknown event" not in log, "game: unknown audio events: %s" % [l for l in log.splitlines() if "unknown event" in l][:5])
    expect(lost["pause"] == "focus+game", "game: focus lost during play should pause voices and the game: %r" % lost["pause"])
    expect(gained["pause"] == "game", "game: regaining focus must keep the factory's pause: %r" % gained["pause"])
    expect(resumed["pause"] == "none", "game: OK should resume everything: %r" % resumed["pause"])

if errors:
    print("FAIL")
    for e in errors:
        print(" ", e)
    sys.exit(1)
print("audio ok (%s)" % a.dataset)
