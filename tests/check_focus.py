"""Focus loss and regain (docs/re/focus-and-display.md), headless against the real Reforged game. `focus lost|gained` in
the script stands in for the OS telling the app it left or came back to the foreground.

  1. Port: focus lost opens ui/ui_pause.lua over the Zurich port, and the pause dialog's Player:SaveGame() ran (a counter
     around SaveGameString, and the profile's save.txt changed on disk before the final quit). Focus gained leaves the dialog up; Resume Game closes it and the port is the top modal again.
     A second focus lost while the dialog is open (gUIPaused) opens nothing more.
  2. Chocolate factory: during play focus lost pauses the game (paused=1), focus gained keeps it paused, OK resumes it.
  3. World map in flight: focus lost stops the clouds and pauses the travel, with one pause dialog; after focus gained and
     Resume Game the clouds drift again and the flight continues without jumping across the time spent away.
  4. The main menu on top: focus lost and gained open nothing and cause no Lua error.
"""
import argparse, os, re, shutil, subprocess, sys, types

ap = argparse.ArgumentParser()
ap.add_argument("--openconche", required=True)
ap.add_argument("--assets", required=True)
ap.add_argument("--workdir", required=True)
a = ap.parse_args()
shutil.rmtree(a.workdir, ignore_errors=True)
os.makedirs(a.workdir)
errors = []

MENU = "expect-window ui/mainmenu.lua 60000\nwait 1300\n"
INTRO = (MENU + "click 310 345\nexpect-window ui/ui_entername.lua 5000\nwait 600\n"
         "type Tester\nkey Return\nexpect-window ui/ui_difficulty.lua 3000\nwait 800\nclick 328 400\nwait 5000\n"
         "click 400 346\nwait 1500\nclick 503 346\nwait 1500\n" + "click 400 346\nwait 1500\n" * 4)
COUNT = "lua gSaves = 0; local o = SaveGameString; SaveGameString = function(d) gSaves = gSaves + 1; return o(d) end\n"

# The game's Lua has no io, so run() checks the profile's save.txt from outside, after the run: before.png is written just before
# focus lost and pause.png just after the pause dialog's save, so the save's mtime must lie between the two. The exit save would
# write the file again, hence NOSAVE after pause.png (the counter stays). Headless `wait` is simulated time (a few ms of wall
# clock), so the markers are compared by mtime after exit rather than polled while the game runs.
NOSAVE = "lua SaveGameString = function(d) gSaves = gSaves + 1 end\n"
PORT = (INTRO + COUNT + "dump\nscreenshot-logical @W@/before.png\nwait 500\nfocus lost\nexpect-window ui/ui_pause.lua 3000\nwait 800\nexpect-lua 1000 gSaves == 1\n"
        "screenshot-logical @W@/pause.png\n" + NOSAVE + "wait 500\nfocus gained\nwait 300\nexpect-window ui/ui_pause.lua 1000\n"
        "focus lost\nwait 300\nexpect-lua 1000 gSaves == 1 and gUIPaused\n"
        # Resume Game sits at (425, 359); the dialog closes and the port is live again.
        "click 425 359\nwait 1500\nexpect-lua 3000 not gUIPaused\nexpect-window zur_market 2000\nquit\n")

COFFEE = ""
SETUP = ("lua f = zur_factory; f:MarkOwned(); p = _AllProducts.b01; Player.rank = 1; "
         "for k, n in pairs(p:GetNeeds()) do Player.ingredients[k] = 1000 end\n")
RUN = ("lua gRun = p:RunMinigame{factory = f}; if gRun >= 0 then f:SetProduction(p, gRun); TickSim(1) end\n"
       "expect-window ui/chocolate_factory.lua 5000\nwait 500\n")
FACTORY = (INTRO + SETUP + RUN + "click 327 398\nwait 1500\nexpect-dump 1000 paused=0\nfocus lost\nwait 100\n"
           "expect-dump 1000 paused=1\nscreenshot-logical @W@/factory_paused.png\nfocus gained\nwait 500\nexpect-dump 1000 paused=1\n"
           "click 399 403\nwait 300\nexpect-dump 1000 paused=0\nwait 1000\nexpect-dump 1000 paused=0\nquit\n")

MAPFLIGHT = (INTRO + "lua capetown:Unlock()\nclick 115 435\nexpect-window ui/mapview.lua 3000\nwait 500\nmove 426 270\nwait 400\n"
             "click 426 270\nexpect-lua 2000 gTravelActive\nexpect-sprite vehicle 1 2000\nwait 600\ndump\n"
             "focus lost\nexpect-window ui/ui_pause.lua 3000\nwait 300\ndump\nwait 1500\ndump\n"
             "focus gained\nwait 300\ndump\nclick 425 359\nwait 1000\nexpect-lua 3000 not gUIPaused\ndump\nwait 600\ndump\n"
             "expect-lua 15000 not gTravelActive\nquit\n")

MAINMENU = (MENU + "focus lost\nwait 500\nexpect-window ui/mainmenu.lua 1000\nfocus gained\nwait 500\n"
            "expect-window ui/mainmenu.lua 1000\nquit\n")


def mtime(work, rel):
    try:
        return os.stat(os.path.join(work, rel)).st_mtime_ns
    except OSError:
        return None


def run(name, text, must=(), watch_save=False):
    work = os.path.join(a.workdir, name)
    os.makedirs(work)
    script = os.path.join(work, "t.script")
    with open(script, "w") as f:
        f.write(text.replace("@W@", work))
    cmd = [a.openconche, "--headless", "--assets", a.assets, "--userdir", os.path.join(work, "user"), "--script", script,
           "--dump-windows"]
    outf, errf = os.path.join(work, "out.txt"), os.path.join(work, "err.txt")
    with open(outf, "w") as fo, open(errf, "w") as fe:
        proc = subprocess.Popen(cmd, cwd=work, stdout=fo, stderr=fe, text=True)
        try:
            proc.wait(600)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()
    out, r = open(outf).read(), types.SimpleNamespace(returncode=proc.returncode, stderr=open(errf).read())
    if watch_save:
        before, saved, after = mtime(work, "before.png"), mtime(work, "user/profiles/Tester/save.txt"), mtime(work, "pause.png")
        if None in (before, saved, after) or not (before <= saved <= after):
            errors.append("%s: save.txt not rewritten by the focus-loss save (before.png %r, save.txt %r, pause.png %r)"
                          % (name, before, saved, after))
    if r.returncode != 0 or "[lua-error]" in out or "FAIL" in out:
        errors.append("%s: exit %d, tail %r %r" % (name, r.returncode, out[-600:], r.stderr[-300:]))
    for m in must:
        if m not in out:
            errors.append("%s: missing %r" % (name, m))
    return out


out = run("port", PORT, ("[app] background", "[app] foreground"), watch_save=True)
if out.count('Modal "ui/ui_pause.lua"') > 2:  # the dumps at the end show at most the one dialog
    errors.append("port: more than one pause dialog")
out = run("factory", FACTORY, ("[app] background", "[app] foreground"))
out = run("mapflight", MAPFLIGHT, ("[app] background", "[app] foreground"))
if out.count('Modal "ui/ui_pause.lua"') > 6:
    errors.append("mapflight: more than one pause dialog")
travel = [float(m) for m in re.findall(r"travel t=([\d.]+)", out)]
clouds = re.findall(r"sprite clouds layer=\d+ pos=([-\d.]+),", out)
if len(travel) < 6 or len(clouds) < 6:
    errors.append("mapflight: expected 6 dumps with travel and clouds, got %d / %d" % (len(travel), len(clouds)))
else:
    flying, lost1, lost2, gained, resumed1, resumed2 = travel[:6]
    if not (lost1 >= flying and lost1 == lost2 == gained):
        errors.append("mapflight: the flight moved while paused: %r" % travel[:6])
    if resumed1 - gained > 0.5 or resumed2 <= resumed1:
        errors.append("mapflight: the flight jumped or did not continue after resume: %r" % travel[:6])
    if not (clouds[1] == clouds[2] == clouds[3]):
        errors.append("mapflight: the clouds drifted while the app was away: %r" % clouds[:6])
    if not (clouds[4] != clouds[3] and clouds[5] != clouds[4]):
        errors.append("mapflight: the clouds did not drift again after resume: %r" % clouds[:6])
out = run("mainmenu", MAINMENU, ("[app] background", "[app] foreground"))
if "ui_pause" in out:
    errors.append("mainmenu: a pause dialog opened")

if errors:
    print("FAIL")
    for e in errors:
        print(" ", e)
    sys.exit(1)
print("focus ok")
