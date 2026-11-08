"""Chocolatier 2 music and ambience (docs/re/choc2-audio.md), headless: the device-less audio pump runs on the simulated clock, so
everything is read from the `audio ...` lines of the script driver's `dump` (--dump-windows) and the [audio] lines of the --log file.

Run 1:
  menu      The main menu plays music/gameplay4.ogg (looped menu track) and no ambience.
  port      A new game enters San Francisco: the port's bed (sfx/*_loop.ogg) loops in the environment group and one of
            music/gameplay1..3.ogg plays (never gameplay4).
  trip      The Travel button opens the map (the same bed and track go on), a trip to New York starts the destination
            sting music/newyork_sting.ogg; it is not cut by the arrival port view's SetMusicState("normal"); about six
            seconds later it has ended and a gameplay track plays again.
  minigame  The factory mini-game dialog (ui/factory_minigame.lua) shows music/minigame_wait.ogg and no ambience; a
            ("") request shows music/minigame.ogg, PauseMusic marks the music paused and ResumeMusic clears it; a plain
            ("normal") brings the rotation back.
Run 2 repeats run 1 with the same seed: the [audio] log lines are identical.
Every run: exit 0, no [lua-error] / [native-error], SetMusicState / SetAmbient / PauseMusic / ResumeMusic not reported unimplemented."""
import argparse, os, re, shutil, subprocess, sys

ap = argparse.ArgumentParser()
ap.add_argument("--openconche", required=True)
ap.add_argument("--assets", required=True)
ap.add_argument("--workdir", required=True)
ap.add_argument("--verbose", action="store_true")
a = ap.parse_args()
a.openconche, a.assets, a.workdir = (os.path.abspath(x) for x in (a.openconche, a.assets, a.workdir))
shutil.rmtree(a.workdir, ignore_errors=True)
os.makedirs(a.workdir)
errors = []

POP = "lua DoPopModal('sf_tutor'); DoPopModal('simplelogo'); DoPopModal('ui/recipebook.lua'); DoPopModal('ui/medal_award.lua')\nwait 800\n"
SCRIPT = ("expect-window ui/mainmenu.lua 20000\nwait 1500\ndump\n"                                              # 0: menu
          "click 400 345\nexpect-window ui/entername.lua 5000\nwait 500\ntype Tester\nkey Return\nwait 2000\nwait 10000\n"
          "click 400 300\nexpect-window ui/portview.lua 5000\nwait 1500\n" + POP * 14 +
          "dump\n"                                                                                           # 1: San Francisco
          "click 192 417\nexpect-window ui/map.lua 5000\nwait 1500\ndump\n"                                  # 2: the map
          "click 205 88\nexpect-window ui/chartalk.lua 5000\nwait 500\nclick 300 303\nexpect-window ui/map.lua 5000\nwait 700\ndump\n"  # 3: sting
          "expect-window ui/portview.lua 8000\nwait 1000\ndump\n"                                           # 4: arrived, sting goes on
          "wait 6000\ndump\n"                                                                                # 5: rotation again
          # The dialog normally comes from the factory status screen (a factory the player owns); a stand-in factory with a
          # real recipe product is enough for the dialog's own music and ambience calls.
          "lua DisplayDialog{'ui/factory_minigame.lua', factory = {name = 'sanfrancisco', product = LItem:ByName('b_01'), "
          "character = {'x'}}, char = LCharacter:ByName('sf_tutor')}\nwait 1500\ndump\n"                                 # 6: factory_wait
          "lua SetMusicState('')\nwait 500\ndump\n"                                                         # 7: minigame
          "lua SetAmbient('sfx/port_loop.ogg'); MinigamePause()\nwait 500\ndump\n"                               # 8: paused, bed running
          "lua ResumeMusic()\nwait 500\ndump\n"                                                             # 9: resumed
          "lua SetMusicState('normal')\nwait 500\ndump\nquit\n")                                           # 10: rotation


def run(name):
    work = os.path.join(a.workdir, name)
    os.makedirs(work)
    script = os.path.join(work, "t.script")
    open(script, "w").write(SCRIPT)
    cmd = [a.openconche, "--headless", "--assets", a.assets, "--userdir", os.path.join(work, "user"), "--script", script, "--dump-windows",
           "--log", os.path.join(work, "log.txt"), "--log-unimplemented", os.path.join(work, "unimpl.txt")]
    r = subprocess.run(cmd, cwd=work, capture_output=True, text=True, timeout=900)
    log = open(os.path.join(work, "log.txt")).read()
    if r.returncode != 0:
        errors.append("%s: exit %d, tail %r" % (name, r.returncode, r.stdout[-600:]))
    for bad in ("[lua-error]", "[native-error]", "[native-warning]", "FAIL"):
        if bad in r.stdout:
            errors.append("%s: output contains %s" % (name, bad))
    if os.path.exists(os.path.join(work, "unimpl.txt")):
        for l in open(os.path.join(work, "unimpl.txt")):
            if l.split("\t")[0] in ("SetMusicState", "SetAmbient", "PauseMusic", "ResumeMusic"):
                errors.append("%s: %s is still unimplemented" % (name, l.split("\t")[0]))
    return r.stdout, log


def blocks(out):
    """One dict per `audio ...` dump: music name, paused flag, env name."""
    res, cur = [], None
    for line in out.splitlines():
        if line.startswith("audio levels"):
            cur = {"music": None, "paused": False, "env": None}
            res.append(cur)
        elif cur is not None and line.startswith("audio music "):
            if not line.endswith(" none"):
                cur["music"] = line[len("audio music "):].split(" stage=")[0]
                cur["paused"] = line.endswith(" paused")
        elif cur is not None and line.startswith("audio env ") and not line.endswith(" none"):
            cur["env"] = line.split()[2]
    return res


def audio_lines(log):
    return [re.sub(r"^\[\+\d+ms\] ", "", l) for l in log.splitlines() if "] [audio] " in l]


def expect(cond, msg):
    if not cond:
        errors.append(msg)


out, log = run("run1")
b = blocks(out)
if a.verbose:
    for i, x in enumerate(b):
        print(i, x)
if len(b) != 11:
    errors.append("expected 11 audio dumps, got %d" % len(b))
else:
    GAMEPLAY = ("music/gameplay1.ogg", "music/gameplay2.ogg", "music/gameplay3.ogg")
    expect(b[0]["music"] == "music/gameplay4.ogg", "menu: music is %r, want music/gameplay4.ogg" % b[0]["music"])
    expect(b[0]["env"] is None, "menu: an ambient bed plays: %r" % b[0]["env"])
    expect(b[1]["music"] in GAMEPLAY, "port: music is %r, want one of gameplay1..3" % b[1]["music"])
    bed = b[1]["env"]
    expect(bed is not None and re.fullmatch(r"sfx/\w+_loop\.ogg", bed) is not None, "port: no sfx/*_loop.ogg bed: %r" % bed)
    expect(b[2]["music"] == b[1]["music"] and b[2]["env"] == bed, "map: music/bed changed from the port: %r vs %r" % (b[2], b[1]))
    expect(b[3]["music"] == "music/newyork_sting.ogg", "trip: music is %r, want music/newyork_sting.ogg" % b[3]["music"])
    expect(b[4]["music"] == "music/newyork_sting.ogg", "arrival: the sting was cut: %r" % b[4]["music"])
    expect(b[5]["music"] in GAMEPLAY, "after the sting: music is %r, want a gameplay track" % b[5]["music"])
    expect(b[6]["music"] == "music/minigame_wait.ogg", "mini-game dialog: music is %r, want minigame_wait" % b[6]["music"])
    expect(b[6]["env"] is None, "mini-game dialog: the bed was not stopped: %r" % b[6]["env"])
    expect(b[7]["music"] == "music/minigame.ogg" and not b[7]["paused"], "mini-game: %r" % b[7])
    expect(b[8]["music"] == "music/minigame.ogg" and b[8]["paused"], "MinigamePause did not pause the music: %r" % b[8])
    expect(b[8]["env"] == "sfx/port_loop.ogg", "pause: the bed does not keep playing: %r" % b[8]["env"])
    expect(b[9]["music"] == "music/minigame.ogg" and not b[9]["paused"], "ResumeMusic did not resume: %r" % b[9])
    expect(b[10]["music"] in GAMEPLAY, "back to normal: music is %r" % b[10]["music"])

out2, log2 = run("run2")
if audio_lines(log) != audio_lines(log2):
    errors.append("two identical runs gave different [audio] lines")
    la, lb = audio_lines(log), audio_lines(log2)
    for i in range(min(len(la), len(lb))):
        if la[i] != lb[i]:
            errors.append("first difference at line %d: %r vs %r" % (i, la[i], lb[i]))
            break

if errors:
    print("FAIL\n" + "\n".join(errors))
    sys.exit(1)
print("Chocolatier 2 audio OK")
