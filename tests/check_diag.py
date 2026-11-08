"""Real-play diagnostics: --log writes a timestamped file and the `diag` command (same report as the F10 hotkey) fills it.

A new player is taken to the Zurich port view, then `diag` runs (and F10 is pressed, which must not reach the game). The log
must hold the report's sections: engine clock, Main coroutine (state and traceback), modal stack, pending messages, window
animations, the globals, travel and the window tree. Every line must carry a "[+<n>ms]" prefix and the Lua DebugOut / engine
lines must be there too."""
import argparse, os, re, shutil, subprocess, sys

ap = argparse.ArgumentParser()
ap.add_argument("--openconche", required=True)
ap.add_argument("--assets", required=True)
ap.add_argument("--workdir", required=True)
a = ap.parse_args()
shutil.rmtree(a.workdir, ignore_errors=True)
os.makedirs(a.workdir)

START = """expect-window ui/mainmenu.lua 60000
wait 1300
click 310 345
expect-window ui/ui_entername.lua 5000
wait 600
type Tester
key Return
expect-window ui/ui_difficulty.lua 3000
wait 800
click 328 400
expect-window ui/ui_quest_offer.lua 5000
wait 1500
lua DoPopModal('ui/ui_quest_offer.lua')
wait 300
lua SwapToModal("ui/portview.lua")
wait 3000
"""
script = START + """lua DebugOut("diag-test-marker")
diag
key F10
wait 100
"""
errors = []


def run(name, text):
    spath = os.path.join(a.workdir, name + ".script")
    open(spath, "w").write(text)
    lg = os.path.join(a.workdir, name + ".log")
    r = subprocess.run([a.openconche, "--headless", "--assets", a.assets, "--userdir", os.path.join(a.workdir, "user_" + name),
                        "--script", spath, "--log", lg], cwd=a.workdir, capture_output=True, text=True, timeout=900)
    if r.returncode != 0:
        errors.append("%s: exit %d, tail %r" % (name, r.returncode, r.stdout[-600:]))
    return open(lg, encoding="utf-8").read() if os.path.exists(lg) else ""


log = os.path.join(a.workdir, "openconche.log")
text = run("openconche", script)
r = None
if not text:
    errors.append("no log file")
lines = [l for l in text.split("\n") if l]
bad = [l for l in lines if not re.match(r"\[\+\d+ms\] ", l)]
if bad:
    errors.append("lines without a timestamp: %r" % bad[:3])
for want in ["[log] opened", "[assets]", "[diag] ---- BEGIN diagnostics", "[diag] ---- END diagnostics",
             "engine clock:", "Main coroutine: suspended in WaitForMessage", "Main traceback", "modal stack (", "(top)",
             "pending messages (", "window animations (", "globals: gTravelActive=", "gQuitActive=", "gUIPaused=",
             "travel: ", "window tree:", "[windows] modal depth", "[input] key F10", "[app] exit"]:
    if want not in text:
        errors.append("log lacks %r" % want)
if text.count("BEGIN diagnostics") != 2:
    errors.append("expected 2 reports (diag and F10), got %d" % text.count("BEGIN diagnostics"))
tb = text[text.find("Main traceback"):]
if "#0 " not in tb or "in global 'Main'" not in tb:
    errors.append("traceback lacks '#0 ' or \"in global 'Main'\"")
# F10 never reaches the game: the key line is followed at once by the report and nothing else reacts to it.
m = re.search(r"\[input\] key F10\n(.*)\n", text)
if not m or "BEGIN diagnostics" not in m.group(1):
    errors.append("F10 was not answered by the report alone: %r" % (m and m.group(1)))
if "MAIN IS DEAD" in text:
    errors.append("a healthy run reports MAIN IS DEAD")

# Main dies: the loud line, a traceback and the heading in a later report.
dead = run("dead", START + """lua gCommandQueue = 5
wait 500
diag
quit
""")
if "[script] Main died:" not in dead or "the game will no longer respond" not in dead:
    errors.append("dead: no '[script] Main died' line")
d = dead[dead.find("[script] Main died"):]
if "#0 " not in d or "in global 'Main'" not in d:
    errors.append("dead: no traceback with '#0 ' and \"in global 'Main'\" after the Main died line")
if "MAIN IS DEAD" not in dead[dead.find("BEGIN diagnostics"):]:
    errors.append("dead: the diag report lacks MAIN IS DEAD")

# A flight with --log: started, the last event returns, arrived.
# The opening dialogs clicked through as check_map.py does, so a real Player exists on the port view.
TRAVEL_START = """expect-window ui/mainmenu.lua 60000
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
""" + "click 400 346\nwait 1500\n" * 4 + "expect-window ui/portview.lua 3000\n"
trav = run("travel", TRAVEL_START + """lua capetown:Unlock()
click 115 435
expect-window ui/mapview.lua 3000
wait 500
move 426 270
wait 400
click 426 270
expect-lua 2000 gTravelActive
expect-lua 8000 Player.portName == "capetown" and not gTravelActive
wait 300
quit
""")
for want in ["[travel] started", "[travel] event 1/", "handler start", "event 2/2 handler returned", "[travel] arrived"]:
    if want not in trav:
        errors.append("travel: log lacks %r" % want)
if "[travel] cancelled" in trav:
    errors.append("travel: a finished flight logged cancelled")

if errors:
    print("\n".join(errors))
    sys.exit(1)
print("ok: --log timestamps every line; diag and F10 write the report (%d log lines)" % len(lines))
