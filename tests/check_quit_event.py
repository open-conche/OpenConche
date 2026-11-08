"""The real quit event, headless against the Reforged game: what the window's close box, Cmd-Q / Alt-F4 and a terminal
Ctrl-C all produce is an SDL_EVENT_QUIT, which the script driver's `quit` command never sends (it stops the app directly).
SDL3 turns SIGTERM into exactly that event (its default signal handlers), so each case sends SIGTERM to the game at a
known moment and reads the result from the output and the profile's save.txt.

  1. yes      Port, after a purchase: the quit event opens ui/ui_generic_yn.lua (AskQuit); Yes saves and posts kQuitNow
              ("[app] exit: kQuitNow"); save.txt holds the state from before the quit.
  2. pending  A second quit event while that dialog is still open quits directly - the user's rule,
              "Control-C => Exit dialog, Ctrl+C again -> just quit. We save anyway": one request asks, a second
              one means it. The exit save at the end of the normal path holds the state from before the quit
              ("close request again: quitting now"); no in-game click is needed. (A windowed close box now yields
              one event only, SDL_HINT_QUIT_ON_LAST_WINDOW_CLOSE=0; headless cannot press it, so that is checked
              by eye.)
  3. flight   A quit event during a flight is refused by AskQuit (as the original) but remembered ("[app] close request
              ignored: travel active"). The flight is held with `focus lost` (the pause dialog pauses travel, gTravelActive
              stays true) so the event arrives mid-flight for sure; Resume Game lets it land; the dialog then opens by
              itself on landing, exactly once ("[app] quit dialog delivered after landing" counted), Yes saves the new port.
No case may log a [lua-error]. Not run on Windows (no SIGTERM delivery)."""
import argparse, os, re, signal, subprocess, sys, time

ap = argparse.ArgumentParser()
ap.add_argument("--openconche", required=True)
ap.add_argument("--assets", required=True)
ap.add_argument("--workdir", required=True)
a = ap.parse_args()
a.openconche, a.assets, a.workdir = (os.path.abspath(x) for x in (a.openconche, a.assets, a.workdir))
if os.name == "nt":
    print("skipped: SIGTERM is not an SDL quit event on Windows")
    sys.exit(0)
os.makedirs(a.workdir, exist_ok=True)
errors = []

STATE = 'lua print("STATE", Player.money, Player.time, Player.subticks, tostring(Player.portName), tostring(gTravelActive), tostring(gQuitActive))\n'
# A new user "Tester", through the intro, one purchase of sugar in the Zurich market (check_endtoend.py's clicks).
PORT = """expect-window ui/mainmenu.lua 60000
wait 1300
click 310 345
expect-window ui/ui_entername.lua 5000
wait 600
type Tester
key Return
expect-window ui/ui_difficulty.lua 3000
wait 800
click 328 400
wait 5000
click 400 346
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
expect-lua 1000 Player.money == 20000 and Player.time == 1 and Player.portName == "zurich"
click 534 230
wait 2500
click 614 191
wait 1500
click 578 280
wait 300
click 474 336
wait 2500
expect-lua 1000 Player.ingredients.sugar == 1000 and Player.money == 13000
click 578 345
wait 2500
"""
# The waits are script-clock ms; headless frames run much faster than wall time, so they are generous.
YES = PORT + STATE + 'lua print("MARK-1")\nwait 300000\nexpect-window ui/ui_generic_yn.lua 300000\nwait 500\nclick 313 396\nwait 600000\n'
PENDING = PORT + STATE + ('lua print("MARK-1")\nwait 300000\nexpect-window ui/ui_generic_yn.lua 300000\nwait 500\n'
                          'lua print("MARK-2", tostring(gQuitActive))\nwait 3000\n')
FLIGHT = PORT + """lua capetown:Unlock()
click 115 435
expect-window ui/mapview.lua 3000
wait 500
click 426 270
expect-lua 2000 gTravelActive
wait 300
focus lost
expect-window ui/ui_pause.lua 3000
wait 500
""" + STATE + """lua print("MARK-1")
wait 3000
""" + STATE + """lua print("QUIT-IN-FLIGHT", tostring(gQuitActive))
focus gained
wait 300
click 425 359
wait 1000
expect-lua 3000 not gUIPaused
expect-lua 60000 Player.portName == "capetown" and not gTravelActive
expect-window ui/ui_generic_yn.lua 10000
wait 500
""" + STATE + 'click 313 396\nwait 600000\n'


def save_fields(userdir):
    path = os.path.join(userdir, "profiles", "Tester", "save.txt")
    if not os.path.exists(path):
        return None
    s = open(path, encoding="utf-8", errors="replace").read()
    depth, out = 0, {}
    for m in re.finditer(r'[{}]|(\w+)=([^{},]*)', s):
        if m.group(0) == "{":
            depth += 1
        elif m.group(0) == "}":
            depth -= 1
        elif depth == 1:
            out[m.group(1)] = m.group(2)
    return out


def run(name, text, marks):
    """Runs the game with the script; at each MARK-n line in the output sends SIGTERM. Returns (stdout, save fields)."""
    work = os.path.join(a.workdir, name)
    os.makedirs(work, exist_ok=True)
    user = os.path.join(work, "user")
    subprocess.run(["rm", "-rf", user])
    script = os.path.join(work, name + ".script")
    open(script, "w").write(text)
    out_path = os.path.join(work, name + ".out.txt")
    out = open(out_path, "w")
    p = subprocess.Popen([a.openconche, "--headless", "--scale", "1", "--assets", a.assets, "--userdir", user, "--script", script],
                         cwd=work, stdout=out, stderr=subprocess.STDOUT, text=True)
    deadline = time.time() + 600
    for n in range(1, marks + 1):
        mark = "MARK-%d" % n
        while time.time() < deadline and p.poll() is None:
            if mark in open(out_path, encoding="utf-8", errors="replace").read():
                break
            time.sleep(0.1)
        if p.poll() is not None:
            errors.append("%s: the app stopped before %s" % (name, mark))
            break
        p.send_signal(signal.SIGTERM)
    try:
        p.wait(timeout=max(1, deadline - time.time()))
    except subprocess.TimeoutExpired:
        p.kill()
        errors.append("%s: the app did not stop" % name)
    out.close()
    text = open(out_path, encoding="utf-8", errors="replace").read()
    if "[lua-error]" in text or "FAIL" in text:
        errors.append("%s: lua error or script failure, tail %r" % (name, text[-600:]))
    return text, save_fields(user)


def states(text):
    return [l.split("\t")[1:] for l in text.splitlines() if l.startswith("STATE\t")]


# ---- 1. quit event, Yes ----
text, save = run("yes", YES, 1)
if "[app] exit: kQuitNow" not in text:
    errors.append("yes: expected '[app] exit: kQuitNow'")
if not save or save.get("money") != "13000" or save.get("subticks") != "1":
    errors.append("yes: save.txt does not hold the state before the quit: %s" % (save and {k: save.get(k) for k in ("money", "time", "subticks", "portName")}))

# ---- 2. a second quit event while the dialog is pending: quit directly, saving ----
text, save = run("pending", PENDING, 2)
if "MARK-2\ttrue" not in text:
    errors.append("pending: gQuitActive was not set while the dialog was open")
if "close request again: quitting now" not in text:
    errors.append("pending: expected the direct-quit line for the second close request")
if "close request ignored" in text:
    errors.append("pending: the second close request must not be ignored")
if "[app] exit: kQuitNow" in text:
    errors.append("pending: kQuitNow is the in-game Yes path; the second request quits on its own")
if not save or save.get("money") != "13000" or save.get("subticks") != "1":
    errors.append("pending: the exit save did not write the state before the quit: %s" % (save and {k: save.get(k) for k in ("money", "time", "subticks")}))

# ---- 3. quit event in flight is ignored; after landing it saves ----
text, save = run("flight", FLIGHT, 1)
st = states(text)
if len(st) < 3 or st[0][4] != "true" or st[1][4] != "true":
    errors.append("flight: the script did not hold the flight: %s" % st)
elif st[1][5] not in ("false", "nil") or "QUIT-IN-FLIGHT\tnil" not in text and "QUIT-IN-FLIGHT\tfalse" not in text:
    errors.append("flight: the quit event during the flight was not ignored: %s" % st[1])
elif st[2][4] != "false" or st[2][3] != "capetown":
    errors.append("flight: the flight did not land after Resume Game: %s" % st[2])
n = text.count("[app] quit dialog delivered after landing")
if n != 1:
    errors.append("flight: the quit dialog must be delivered exactly once after landing, got %d" % n)
if text.count("[app] close request ignored: travel active") != 1:
    errors.append("flight: expected exactly one travel-active refusal")
if "[app] exit: kQuitNow" not in text:
    errors.append("flight: expected '[app] exit: kQuitNow' after landing")
if "close request ignored: travel active" not in text:
    errors.append("flight: expected the travel-active log line")
if not save or save.get("portName") != '"capetown"':
    errors.append("flight: save.txt should hold the new port: %s" % (save and {k: save.get(k) for k in ("money", "time", "portName")}))

for e in errors:
    print("FAIL", e)
print("ok" if not errors else "%d failure(s)" % len(errors))
sys.exit(1 if errors else 0)
