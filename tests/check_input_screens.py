"""The screens that need text input, sliders, FillWindow and layered labels, played
headless against the real game.

  --case new_player     Story Mode, type a name (key, shift+key, type), Backspace, Enter: ui_difficulty.lua follows.
                        Also: the placeholder name is cleared by the first key, OK is disabled until there is text,
                        and the caret blinks (two shots 400 ms apart differ only in a 1 pixel wide bar).
  --case options        Options: drag a volume slider, check the value in the dump, click OK to close.
  --case help           Help: FillWindow fills help_contents, Next pages on, OK closes.
  --case change_player  Change Player: a user made with `lua CreateNewUser`, then the real main menu button. Shows the roster,
                        then New, Rename and Delete Player, then closes.
Run at each of --displays (e.g. "scale1,fullscreen")."""
import argparse, os, re, shutil, struct, subprocess, sys, zlib

ap = argparse.ArgumentParser()
ap.add_argument("--openconche", required=True)
ap.add_argument("--assets", required=True)
ap.add_argument("--workdir", required=True)
ap.add_argument("--case", required=True, choices=["new_player", "options", "help", "change_player"])
ap.add_argument("--displays", default="scale1")
a = ap.parse_args()
os.makedirs(a.workdir, exist_ok=True)
errors = []

DISPLAYS = {"scale1": ("--scale", "1"), "scale2": ("--scale", "2"),
            "fullscreen": ("--fullscreen", "--display-size", "2560x1440")}
MENU = {"story": (310, 345), "options": (550, 455), "help": (250, 455)}  # centres of the main menu buttons
START = "expect-window ui/mainmenu.lua 60000\nwait 1300\n"


def read_png(path):
    data = open(path, "rb").read()
    pos, idat = 8, b""
    while pos < len(data):
        n, typ = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        pos += 12 + n
        if typ == b"IHDR":
            w, h, depth, ctype = struct.unpack(">IIBB", body[:10])
            bpp = 3 if ctype == 2 else 4
        elif typ == b"IDAT":
            idat += body
    raw = zlib.decompress(idat)
    stride = w * bpp
    rows, prev = [], bytearray(stride)
    for y in range(h):
        f = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            x = line[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if f == 1: line[i] = (line[i] + x) & 255
            elif f == 2: line[i] = (line[i] + b) & 255
            elif f == 3: line[i] = (line[i] + (x + b) // 2) & 255
            elif f == 4:
                p = x + b - c
                pa, pb, pc = abs(p - x), abs(p - b), abs(p - c)
                line[i] = (line[i] + (x if pa <= pb and pa <= pc else (b if pb <= pc else c))) & 255
        rows.append(line)
        prev = line
    return w, h, bpp, rows


def run(name, display, script_text):
    work = os.path.join(a.workdir, name)
    shutil.rmtree(os.path.join(work, "user"), ignore_errors=True)  # a fresh profile every run
    os.makedirs(work, exist_ok=True)
    script = os.path.join(work, "t.script")
    with open(script, "w") as f:
        f.write(script_text.replace("@W@", work))
    r = subprocess.run([a.openconche, "--headless", *DISPLAYS[display], "--assets", a.assets, "--userdir",
                        os.path.join(work, "user"), "--script", script, "--dump-windows"],
                       cwd=work, capture_output=True, text=True, timeout=180)
    return r, work


def rects(dump):
    """name -> (abs x, abs y, w, h, line) of the first window with that name (rects in the dump are parent-relative)."""
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


def knob_rect(line):
    m = re.search(r"knob=(-?\d+),(-?\d+),(\d+),(\d+)", line)
    return tuple(map(int, m.groups())) if m else None


def centre(r):
    return r[0] + r[2] / 2.0, r[1] + r[3] / 2.0


def depth(dump):
    m = re.search(r"modal depth (\d+)", dump)
    return int(m.group(1)) if m else -1


def common(tag, r, expect_ok=True):
    if r.returncode != 0:
        errors.append("%s: exit %d, tail %r" % (tag, r.returncode, r.stdout[-700:]))
    if "[lua-error]" in r.stdout:
        errors.append("%s: [lua-error] in the log: %s" % (tag, [l for l in r.stdout.splitlines() if "[lua-error]" in l][:3]))
    if "FAIL" in r.stdout:
        errors.append("%s: a script step failed: %s" % (tag, [l for l in r.stdout.splitlines() if "FAIL" in l][:3]))


def new_player(display):
    story = "click %g %g\nexpect-window ui/ui_entername.lua 5000\nwait 1200\n" % MENU["story"]
    # The dialog as it opens: the placeholder is in the field, OK is disabled, and the field has the focus.
    r, _ = run("np_open_" + display, display, START + story + "quit\n")
    common("open " + display, r)
    d = rects(r.stdout)
    entry = d.get("entry")
    if not entry or 'clearinitial' not in entry[4] or 'focused' not in entry[4]:
        errors.append("%s: the entry is not a focused clearinitial TextEdit: %s" % (display, entry and entry[4]))
    ok = d.get("ok")
    if not ok or "hidden" not in ok[4]:
        errors.append("%s: OK should be disabled while the placeholder is showing: %s" % (display, ok and ok[4]))

    # Typing: the first key replaces the placeholder; key, shift+key, type; Backspace; label via GetLabel.
    typing = (story + "key a\nexpect-label entry a\nkey shift+b\ntype cd\nexpect-label entry aBcd\nkey Backspace\n"
              "expect-label entry aBc\nwait 100\nscreenshot-logical @W@/c1.png\nwait 400\nscreenshot-logical @W@/c2.png\n"
              "wait 400\nscreenshot-logical @W@/c3.png\n")
    r, work = run("np_type_" + display, display, START + typing + "quit\n")
    common("typing " + display, r)
    d = rects(r.stdout)
    if 'text="aBc"' not in d.get("entry", (0, 0, 0, 0, ""))[4]:
        errors.append("%s: the entry's text is not aBc: %s" % (display, d.get("entry")))
    if "hidden" in d.get("ok", (0, 0, 0, 0, "hidden"))[4]:
        errors.append("%s: OK stayed disabled after typing" % display)
    # The caret: consecutive shots 400 ms apart are in opposite blink phases and differ only in a thin vertical bar. A
    # one-frame shift in where the shots land can put one pair in the same phase, so one thin-bar pair out of two is enough.
    def bar(p, q):
        w, h, bpp, A = read_png(os.path.join(work, p))
        _, _, _, B = read_png(os.path.join(work, q))
        xs, ys = [], []
        for y in range(h):
            if A[y] != B[y]:
                for x in range(w):
                    if A[y][x * bpp:x * bpp + 3] != B[y][x * bpp:x * bpp + 3]:
                        xs.append(x)
                        ys.append(y)
        return bool(xs) and max(xs) - min(xs) <= 1 and max(ys) - min(ys) >= 8
    if not (bar("c1.png", "c2.png") or bar("c2.png", "c3.png")):
        errors.append("%s: the caret blink is not a thin bar between consecutive shots" % display)

    # Limits and filters: a utf8 field with length 30 takes 30 characters (the 31st is refused).
    thirty = "x" * 31
    r, _ = run("np_limit_" + display, display, START + story + "type " + thirty + "\nexpect-label entry " + "x" * 30 + "\nquit\n")
    common("limit " + display, r)

    # Enter presses the default button: the name is accepted and the difficulty dialog follows.
    r, work = run("np_enter_" + display, display, START + story + "type Zed\nkey Return\n"
                  "expect-window ui/ui_difficulty.lua 5000\nwait 1200\nscreenshot-logical @W@/after.png\nquit\n")
    common("enter " + display, r)
    if "[script] ok expect-window ui/ui_difficulty.lua" not in r.stdout:
        errors.append("%s: Enter did not lead to ui_difficulty.lua" % display)


def options(display):
    open_ = "click %g %g\nexpect-window ui/ui_options.lua 5000\nwait 1200\n" % MENU["options"]
    r, _ = run("op_open_" + display, display, START + open_ + "screenshot-logical @W@/o.png\nquit\n")
    common("open " + display, r)
    d = rects(r.stdout)
    sl = d.get("sfxlevelslider")
    ok = d.get("ok")
    if not sl or not ok:
        errors.append("%s: Options has no sfx slider or OK button" % display)
        return
    if "value=0.750" not in sl[4]:
        errors.append("%s: the sfx slider should start at 0.75: %s" % (display, sl[4]))
    if "noknob" in sl[4] or "knob=" not in sl[4]:
        errors.append("%s: the sfx slider has no knob image: %s" % (display, sl[4]))
    before = knob_rect(sl[4])
    sx, sy, sw, sh = sl[:4]
    knob_x = sx + (sw - 32) * 0.75 + 16
    y = sy + sh / 2.0
    target = sx + sw * 0.25
    # Drag the knob from its place to a quarter of the way along, then let go; a page click right of the knob must not
    # change the value.
    drag = ("move %g %g\ndown %g %g\nmove %g %g\nmove %g %g\nup %g %g\nwait 100\nscreenshot-logical @W@/drag.png\n"
            % (knob_x, y, knob_x, y, (knob_x + target) / 2, y, target, y, target, y))
    r, work = run("op_drag_" + display, display, START + open_ + drag + "move %g %g\ndown %g %g\nup %g %g\nwait 100\nquit\n"
                  % (sx + 260, y, sx + 260, y, sx + 260, y))
    common("drag " + display, r)
    d = rects(r.stdout)
    m = re.search(r"value=([0-9.]+)", d.get("sfxlevelslider", (0, 0, 0, 0, ""))[4])
    if not m or abs(float(m.group(1)) - 0.25) > 0.01:
        errors.append("%s: after the drag the sfx value should be about 0.25: %s" % (display, d.get("sfxlevelslider")))
    after = knob_rect(d["sfxlevelslider"][4])
    if not before or not after or after[0] >= before[0] - 20:
        errors.append("%s: the sfx knob should move left with the drag: %s -> %s" % (display, before, after))
    # The knob must really be drawn, also after the Full Screen toggle resized the window (SDL's software renderer
    # dropped the knob sprite then): the pixels in its box must match a screenshot taken before the toggle.
    fx, fy = centre(d["fullscreen"])
    r, work = run("op_knob_" + display, display, START + open_ + "screenshot-logical @W@/a.png\nclick %g %g\nwait 800\n"
                  "screenshot-logical @W@/b.png\nquit\n" % (fx, fy))
    common("knob after toggle " + display, r)
    kx, ky = sx + before[0], sy + before[1]
    _, _, bpa, A = read_png(os.path.join(work, "a.png"))
    _, _, _, B = read_png(os.path.join(work, "b.png"))
    diff = sum(abs(A[y][x * bpa + c] - B[y][x * bpa + c]) for y in range(int(ky) + 4, int(ky) + 28)
               for x in range(int(kx) + 4, int(kx) + 28) for c in range(3)) / (24 * 24 * 3.0)
    if diff > 25:
        errors.append("%s: the sfx knob changed or vanished after the Full Screen toggle (mean diff %.1f)" % (display, diff))
    for n in ("musiclevelslider", "ambientlevelslider"):
        if "value=0.650" not in d[n][4] and "value=0.500" not in d[n][4]:
            errors.append("%s: dragging sfx moved %s: %s" % (display, n, d[n][4]))
    # OK closes the dialog (a fade first).
    cx, cy = centre(ok)
    r, _ = run("op_close_" + display, display, START + open_ + "click %g %g\nwait 1500\nquit\n" % (cx, cy))
    common("close " + display, r)
    if depth(r.stdout) != 2:
        errors.append("%s: Options is still open after OK (modal depth %d, 2 is the main menu alone)" % (display, depth(r.stdout)))
    # The fullscreen toggle: no "screenswitchfailed" dialog (modal depth stays 3), the pref flips, and a second click
    # flips it back.
    fb = d.get("fullscreen")
    if not fb:
        errors.append("%s: Options has no fullscreen toggle" % display)
    else:
        cx, cy = centre(fb)
        click = "click %g %g\nwait 800\n" % (cx, cy)
        start = "1" if display == "fullscreen" else "0"  # the --fullscreen run starts fullscreen
        flipped = "0" if start == "1" else "1"
        for tag, n, want in (("fs1", 1, flipped), ("fs2", 2, start)):
            r, work = run("op_%s_%s" % (tag, display), display,
                          START + open_ + click * n + "screenshot-logical @W@/fs.png\nquit\n")
            common("%s %s" % (tag, display), r)
            if depth(r.stdout) != 3:
                errors.append("%s: after %d fullscreen click(s) the modal depth is %d, not 3 (a failure dialog?)" % (display, n, depth(r.stdout)))
            if "screenswitchfailed" in r.stdout or "Fullscreen toggle failed" in r.stdout:
                errors.append("%s: the fullscreen toggle reported failure" % display)
            prefs = os.path.join(work, "user", "prefs.txt")
            text = open(prefs).read() if os.path.exists(prefs) else ""
            got = re.search(r"^fullscreen\t(\d)", text, re.M)
            if not got or got.group(1) != want:
                errors.append("%s: after %d click(s) the fullscreen pref should be %s: %r" % (display, n, want, text))
            if "[unimplemented] ToggleFullScreen" in r.stdout:
                errors.append("%s: ToggleFullScreen is still a stub" % display)
    r, _ = run("op_esc_" + display, display, START + open_ + "key Escape\nwait 1500\nquit\n")
    common("escape " + display, r)
    if depth(r.stdout) != 2:
        errors.append("%s: Options did not close on Escape (modal depth %d, 2 is the main menu alone)" % (display, depth(r.stdout)))


def help_(display):
    open_ = "click %g %g\nexpect-window ui/ui_help.lua 5000\nwait 1500\n" % MENU["help"]
    r, _ = run("hp_open_" + display, display, START + open_ + "screenshot-logical @W@/h.png\nquit\n")
    common("open " + display, r)
    d = rects(r.stdout)
    hc = d.get("help_contents")
    if not hc:
        errors.append("%s: no help_contents window" % display)
        return
    # FillWindow ran ui/help_general.lua into it: the window has children now.
    lines = r.stdout.splitlines()
    i = next(k for k, l in enumerate(lines) if 'Window "help_contents"' in l)
    base = len(lines[i]) - len(lines[i].lstrip())
    kids = 0
    for l in lines[i + 1:]:
        if len(l) - len(l.lstrip()) <= base:
            break
        kids += 1
    if kids == 0:
        errors.append("%s: FillWindow left help_contents empty" % display)
    nxt = d.get("next")
    prev = d.get("prev")
    if not nxt or not prev:
        errors.append("%s: no next/prev buttons" % display)
        return
    if "hidden" not in prev[4]:
        errors.append("%s: previous should be disabled on the first page: %s" % (display, prev[4]))
    cx, cy = centre(nxt)
    r, _ = run("hp_next_" + display, display, START + open_ + "click %g %g\nwait 800\nscreenshot-logical @W@/h2.png\nquit\n" % (cx, cy))
    common("next " + display, r)
    d2 = rects(r.stdout)
    if "hidden" in d2["prev"][4]:
        errors.append("%s: previous is still disabled on the second page" % display)
    ok = d.get("ok")
    cx, cy = centre(ok)
    r, _ = run("hp_close_" + display, display, START + open_ + "click %g %g\nwait 1500\nquit\n" % (cx, cy))
    common("close " + display, r)
    if depth(r.stdout) != 2:
        errors.append("%s: Help is still open after OK (modal depth %d, 2 is the main menu alone)" % (display, depth(r.stdout)))


def change_player(display):
    # One player made through CreateNewUser, then the real main menu button (Update shows it once a user exists).
    open_ = ("lua CreateNewUser('Tester')\nlua Update()\nwait 200\nclick 117 417\n"
             "expect-window ui/ui_changeplayer.lua 5000\nwait 1500\n")
    r, _ = run("cp_open_" + display, display, START + open_ + "screenshot-logical @W@/cp.png\nquit\n")
    common("open " + display, r)
    d = rects(r.stdout)
    for n in ("name1", "name10", "icon1", "newplayer", "renameplayer", "deleteplayer", "ok"):
        if n not in d:
            errors.append("%s: the roster has no %s" % (display, n))
    if 'label="Tester"' not in d.get("name1", (0, 0, 0, 0, ""))[4]:
        errors.append("%s: the first slot should be Tester" % display)
    if "image/indicatorlight_green" not in d.get("icon1", (0, 0, 0, 0, ""))[4]:
        errors.append("%s: the first slot should be the active (green) one" % display)
    if "label=\"\"" not in d.get("name2", (0, 0, 0, 0, "label=\"?\""))[4]:
        errors.append("%s: only one slot should be filled" % display)
    # New Player: name dialog, then difficulty; the new player becomes the active one in slot 2.
    new = (open_ + "click 267 393\nexpect-window ui/ui_entername.lua 5000\nwait 600\ntype Second\nkey Return\n"
           "expect-window ui/ui_difficulty.lua 3000\nwait 800\nclick 328 400\nwait 2500\n")
    r, _ = run("cp_new_" + display, display, START + new + "screenshot-logical @W@/cp_new.png\nquit\n")
    common("new " + display, r)
    d = rects(r.stdout)
    if 'label="Second"' not in d.get("name2", (0, 0, 0, 0, ""))[4]:
        errors.append("%s: New Player did not add Second" % display)
    if "image/indicatorlight_green" not in d.get("icon2", (0, 0, 0, 0, ""))[4]:
        errors.append("%s: the new player should be the active one" % display)
    # Rename Player edits the active name (the dialog starts with it, so typing appends), Delete Player asks first.
    ren = new + "click 400 393\nexpect-window ui/ui_entername.lua 5000\nwait 600\ntype X\nkey Return\nwait 1500\n"
    r, _ = run("cp_rename_" + display, display, START + ren + "quit\n")
    common("rename " + display, r)
    if 'label="SecondX"' not in rects(r.stdout).get("name2", (0, 0, 0, 0, ""))[4]:
        errors.append("%s: Rename Player did not rename the active player to SecondX" % display)
    dele = (ren + "click 533 393\nexpect-window ui/ui_generic_yn.lua 3000\nwait 800\nclick 314 400\nwait 2000\n"
            "screenshot-logical @W@/cp_delete.png\n")
    r, _ = run("cp_delete_" + display, display, START + dele + "quit\n")
    common("delete " + display, r)
    d = rects(r.stdout)
    if 'label=""' not in d.get("name2", (0, 0, 0, 0, ""))[4] or 'label="Tester"' not in d.get("name1", (0, 0, 0, 0, ""))[4]:
        errors.append("%s: Delete Player should leave only Tester" % display)
    if "image/indicatorlight_green" not in d.get("icon1", (0, 0, 0, 0, ""))[4]:
        errors.append("%s: after the delete Tester should be active" % display)
    ok = d.get("ok")
    if ok:
        cx, cy = centre(ok)
        r, _ = run("cp_close_" + display, display, START + open_ + "click %g %g\nwait 1500\nquit\n" % (cx, cy))
        common("close " + display, r)
        if depth(r.stdout) != 2:
            errors.append("%s: Change Player is still open after OK (modal depth %d, 2 is the main menu alone)" % (display, depth(r.stdout)))


for disp in a.displays.split(","):
    {"new_player": new_player, "options": options, "help": help_, "change_player": change_player}[a.case](disp)

if errors:
    print("FAIL:\n  " + "\n  ".join(errors))
    sys.exit(1)
print("ok: %s at %s" % (a.case, a.displays))
