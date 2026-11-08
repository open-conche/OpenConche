"""High Scores screen (HiscoreWindow + the local score natives), headless against the Reforged or the vanilla game data.

  --dataset reforged | vanilla   which game data --assets points at (the High Scores button is at 400,475 in both)

Reforged opens the dialog with `lua DisplayDialog{'ui/hiscore.lua'}` (its menu button is the Community hub).
Run 1, empty store: the screen opens with every row hidden and the online panels hidden; GetState() is 0; no Lua error.
Run 2, planted scores (a user made with `lua CreateNewUser` and LogScore natives, three users' worth through the natives):
the dump shows row 1 with the name, "<n>." rank, "$...,.../week" score and the "<Rank>: $... in N weeks" info text; row 4
and later stay hidden; Return (the OK button is default and cancel) closes the screen back to the main menu.
Run 3, a second process on the same user dir: the scores survive in user:hiscore.dat.
Both runs: no [lua-error], and only SoundEvent / ButtonSound / StartMovie were logged as unimplemented. Logical screenshots
are written for looking at.
"""
import argparse, os, re, shutil, struct, subprocess, sys

ap = argparse.ArgumentParser()
ap.add_argument("--openconche", required=True)
ap.add_argument("--assets", required=True)
ap.add_argument("--workdir", required=True)
ap.add_argument("--dataset", required=True, choices=["reforged", "vanilla"])
a = ap.parse_args()
shutil.rmtree(a.workdir, ignore_errors=True)
os.makedirs(a.workdir)
errors = []
ALLOWED = {"SoundEvent", "StartMovie", "ButtonSound"}
# Vanilla: the menu's High Scores button. Reforged: that button opens the Community hub (whose High Scores button runs
# DisplayDialog "ui/hiscore.lua"), so the dialog is opened the way the hub does.
START = "expect-window ui/mainmenu.lua 60000\nwait 1300\n"
OPEN = START + ("click 400 475\n" if a.dataset == "vanilla" else "lua DisplayDialog{'ui/hiscore.lua'}\n") + \
    "expect-window ui/hiscore.lua 5000\nwait 1500\n"
CLOSE = "key Return\nexpect-window ui/mainmenu.lua 5000\nwait 500\n"


def run(tag, body, user, extra=()):
    work = os.path.join(a.workdir, tag)
    os.makedirs(work, exist_ok=True)
    script = os.path.join(work, "t.script")
    with open(script, "w") as f:
        f.write(body.replace("@W@", work))
    unimpl = os.path.join(work, "unimplemented.txt")
    r = subprocess.run([a.openconche, "--headless", "--assets", a.assets, "--userdir", user, "--script", script, "--dump-windows",
                        "--log-unimplemented", unimpl, *extra], cwd=work, capture_output=True, text=True, timeout=240)
    out = r.stdout
    with open(os.path.join(work, "out.txt"), "w") as f:
        f.write(out)
    if r.returncode != 0 or "[lua-error]" in out or "[engine-error]" in out or "FAIL" in out:
        errors.append("%s: exit %d, tail %r" % (tag, r.returncode, out[-800:]))
    if os.path.exists(unimpl):
        for l in open(unimpl):
            if l.split("\t")[0] not in ALLOWED:
                errors.append("%s: unexpected unimplemented native %s" % (tag, l.strip()))
    for must in ("ok expect-window ui/hiscore.lua", "ok expect-window ui/mainmenu.lua"):
        if must not in out:
            errors.append("%s: missing '%s'" % (tag, must))
    return out, work


def dumps(out):
    """The window dumps in order: a list of {name: [line, ...]} (several windows may share a name, e.g. the Reforged rows)."""
    res, cur = [], None
    for line in out.splitlines():
        if re.match(r"\s*(Screen|Modal)\b", line) and cur is None:
            pass
        m = re.match(r"\s*(\w+) \"([^\"]*)\" id=\d+ rect=(-?\d+),(-?\d+),(\d+),(\d+)( hidden)?(.*)", line)
        if m:
            if m.group(1) == "Screen":
                cur = {}
                res.append(cur)
            if cur is not None:
                cur.setdefault(m.group(2), []).append((m.group(1), line.strip(), m.group(7) is not None))
    return res


def label(line):
    m = re.search(r'label="([^"]*)"', line)
    return m.group(1) if m else ""


def check_png(path, tag):
    try:
        d = open(path, "rb").read(24)
        w, h = struct.unpack(">II", d[16:24])
        if (w, h) != (800, 600):
            errors.append("%s: %s is %dx%d" % (tag, path, w, h))
    except OSError as e:
        errors.append("%s: no screenshot %s" % (tag, e))


user = os.path.join(a.workdir, "user")

# ---- run 1: empty ----
out, work = run("empty", OPEN + "dump\nscreenshot-logical @W@/empty.png\nlua print('STATE', GetState())\n"
                "lua print('AVAIL', tostring(ScoreAvailable()))\n" + CLOSE + "quit\n", user)
check_png(os.path.join(work, "empty.png"), "empty")
if not re.search(r"STATE\s+0\b", out):
    errors.append("empty: GetState() is not 0")
if not re.search(r"AVAIL\s+false", out):
    errors.append("empty: ScoreAvailable() should be false")
d = dumps(out)
screen = d[0] if d else {}
for i in range(1, 11):
    for n in ("name%d" % i, "score%d" % i, "info%d" % i, str(i)):
        for kind, line, hidden in screen.get(n, []):
            if kind == "Text" and not hidden and label(line):
                errors.append("empty: %s is shown with text: %s" % (n, line))
for n in ("rightpanel", "rightpanelsmall", "server", "error", "global"):
    for kind, line, hidden in screen.get(n, []):
        if not hidden:
            errors.append("empty: %s should be hidden: %s" % (n, line))
for kind, line, hidden in sum(screen.values(), []):
    if "Connecting" in line and not hidden:
        errors.append("empty: 'Connecting to server' is shown: " + line)

# ---- run 2: planted ----
plant = ("lua CreateNewUser('Tester')\n"
         "lua LogScore(1234567, '4-000000120-000001234567', '')\n"
         "lua CreateNewUser('Bea')\n"
         "lua LogScore(250, '2-000000020-000000012345', '')\n"
         "lua CreateNewUser('Cal')\n"
         "lua LogScore(90000, '1-000000010-000000050000', '')\n"
         "lua LogScore(100, '1-000000010-000000000001', '')\n")  # lower than Cal's own: ignored
out, work = run("planted", START + plant + OPEN[len(START):] +
                "dump\nscreenshot-logical @W@/planted.png\nlua print('STATE', GetState())\nlua print('AVAIL', tostring(ScoreAvailable()))\n"
                + CLOSE + "quit\n", user)
check_png(os.path.join(work, "planted.png"), "planted")
if not re.search(r"AVAIL\s+true", out):
    errors.append("planted: ScoreAvailable() should be true")
d = dumps(out)
screen = d[0] if d else {}


def text_of(name):
    for kind, line, hidden in screen.get(name, []):
        if kind == "Text":
            return label(line), hidden
    return None, True


# Row order: Tester 1,234,567, Cal 90,000, Bea 250 (Cal is the current user; the Reforged screen logs its own Player on open,
# a lower company score that changes nothing). The native Texts hold the strings in both datasets.
for n in ("view", "submit"):  # the default policy is local-only: online buttons are disabled (hidden)
    for kind, line, hidden in screen.get(n, []):
        if kind == "Button" and not hidden:
            errors.append("planted: %s should be disabled under the default local-only policy: %s" % (n, line))
for n in ("rightpanel", "rightpanelsmall"):
    for kind, line, hidden in screen.get(n, []):
        if not hidden:
            errors.append("planted: %s should be hidden: %s" % (n, line))
want = [("Tester", "$1,234,567/week", "1.", "$1,234,567 in 120 weeks"),
        ("Cal", "$90,000/week", "2.", "$50,000 in 10 weeks"),
        ("Bea", "$250/week", "3.", "$12,345 in 20 weeks")]
for i, (nm, sc, rk, info) in enumerate(want, 1):
    for what, key, exact in (("name", nm, True), ("score", sc, True), ("rank", rk, True), ("info", info, False)):
        n = str(i) if what == "rank" else "%s%d" % (what, i)
        txt, hidden = text_of(n)
        if txt is None or hidden or (txt != key if exact else not txt.endswith(key)):
            errors.append("planted: %s is %r (hidden=%s), want %s%r" % (n, txt, hidden, "" if exact else "...", key))
if text_of("info1")[0] is not None and not text_of("info1")[0].startswith("CEO"):
    errors.append("planted: info1 should start with the rank name 'CEO': %r" % (text_of("info1"),))
for n in ("name4", "score4", "4", "info4", "name10"):
    txt, hidden = text_of(n)
    if txt not in ("", None) or not hidden:
        errors.append("planted: row beyond the list is not blank and hidden: %s %r hidden=%s" % (n, txt, hidden))
if a.dataset == "reforged":
    pd = [(l, h) for k, l, h in screen.get("playerdisplay1", [])]
    if not pd or label(pd[0][0]) != "Tester":
        errors.append("planted: Reforged playerdisplay1 should read Tester: %r" % (pd,))

# ---- run 3: persistence ----
if not os.path.exists(os.path.join(user, "hiscore.dat")):
    errors.append("no hiscore.dat in the user dir")
out, work = run("again", OPEN + "dump\nscreenshot-logical @W@/again.png\n" + CLOSE + "quit\n", user)
d = dumps(out)
screen = d[0] if d else {}
if text_of("name1")[0] != "Tester" or text_of("name3")[0] != "Bea":
    errors.append("again: the scores did not survive a restart: %r %r" % (text_of("name1"), text_of("name3")))



def abs_center(out, name):
    """Absolute centre of the first dump's window `name` (the dump has parent-relative rects, two spaces per depth)."""
    stack = []
    for line in out.splitlines():
        m = re.match(r"( *)(\w+) \"([^\"]*)\" id=\d+ rect=(-?\d+),(-?\d+),(\d+),(\d+)", line)
        if not m:
            continue
        depth = len(m.group(1)) // 2 - 1
        if depth < 0:
            continue
        del stack[depth:]
        x, y, w, h = (int(m.group(i)) for i in (4, 5, 6, 7))
        ox = sum(p[0] for p in stack)
        oy = sum(p[1] for p in stack)
        if m.group(3) == name and m.group(2) == "Button":
            return ox + x + w // 2, oy + y + h // 2
        stack.append((x, y))
    return None


# ---- run 4 (vanilla): hiscorelocal=0 puts the online UI back. view is enabled and rightpanel shown in state 0; pressing view
# ends in state 5 (the offline request fails) with the error text shown and the local rows still listed; viewlocal comes back.
if a.dataset == "vanilla":
    cfg = ("--config", "hiscorelocal=0")
    out, work = run("online_off", OPEN + "dump\n", user, cfg)  # the script ends here; only the dump is used, to find `view`
    pos = abs_center(out, "view")
    d = dumps(out)
    screen = d[0] if d else {}
    if not pos:
        errors.append("online_off: no view button in the dump")
    for n, want_hidden in (("rightpanel", False), ("rightpanelsmall", True), ("view", False), ("viewlocal", True), ("submit", False)):
        for kind, line, hidden in screen.get(n, []):
            if hidden != want_hidden and (kind == "Button" or kind == "Window"):
                errors.append("online_off: %s hidden=%s, want %s: %s" % (n, hidden, want_hidden, line))
    if pos:
        out, work = run("online_view", OPEN + "lua print('STATE0', GetState())\nclick %d %d\nwait 600\ndump\n"
                        "screenshot-logical @W@/online_error.png\nlua print('STATE1', GetState())\n" % pos + CLOSE + "quit\n", user, cfg)
        d = dumps(out)
        screen = d[0] if d else {}
        if not re.search(r"STATE0\s+0\b", out) or not re.search(r"STATE1\s+5\b", out):
            errors.append("online_view: clicking view should end in state 5")
        for kind, line, hidden in screen.get("error", []):
            if kind == "Text" and (hidden or "Unable to connect" not in line):
                errors.append("online_view: the error text is not shown: " + line)
        if not screen.get("error"):
            errors.append("online_view: no error text in the dump")
        for kind, line, hidden in screen.get("server", []):
            if kind == "Text" and not hidden:
                errors.append("online_view: 'Connecting' still shown in state 5: " + line)
        txt, hidden = text_of("name1")
        if txt != "Tester" or hidden:
            errors.append("online_view: the local rows should still show: %r hidden=%s" % (txt, hidden))
        for n, want_hidden in (("viewlocal", False), ("view", True)):
            for kind, line, hidden in screen.get(n, []):
                if kind == "Button" and hidden != want_hidden:
                    errors.append("online_view: %s hidden=%s, want %s" % (n, hidden, want_hidden))
        check_png(os.path.join(work, "online_error.png"), "online_view")

if errors:
    print("FAIL")
    for e in errors:
        print(" ", e)
    sys.exit(1)
print("hiscore %s ok" % a.dataset)
