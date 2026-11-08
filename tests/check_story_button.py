"""Clicking the Reforged main menu's Story Mode button opens ui/ui_entername.lua.

The button's centre is taken from a --dump-windows run (rects there are parent-relative, so they are summed up the
tree). The same logical click is then played at windowed scale 1, windowed scale 2 and fullscreen 2560x1440, which
proves the click mapping in every display mode. A click on bare background must not open the dialog."""
import argparse, os, re, subprocess, sys

ap = argparse.ArgumentParser()
ap.add_argument("--openconche", required=True)
ap.add_argument("--assets", required=True)
ap.add_argument("--workdir", required=True)
a = ap.parse_args()
os.makedirs(a.workdir, exist_ok=True)
errors = []


def run(name, script_text, *display):
    work = os.path.join(a.workdir, name)
    os.makedirs(work, exist_ok=True)
    script = os.path.join(work, "t.script")
    with open(script, "w") as f:
        f.write(script_text)
    r = subprocess.run([a.openconche, "--headless", *display, "--assets", a.assets, "--userdir", os.path.join(work, "user"),
                        "--script", script, "--dump-windows", "--log", os.path.join(work, "input.log")], cwd=work, capture_output=True, text=True, timeout=120)
    r.log = open(os.path.join(work, "input.log"), encoding="utf-8").read()
    return r


def story_centre(dump):
    """Absolute logical centre of the Button whose first image is the Story Mode 'up' picture."""
    stack = []  # (indent, x, y)
    pending = None
    for line in dump.splitlines():
        m = re.match(r'^( +)(\w+) "([^"]*)" id=\d+ rect=(-?\d+),(-?\d+),(\d+),(\d+)', line)
        if not m:
            continue
        indent, typ, x, y, w, h = len(m.group(1)), m.group(2), *map(int, m.groups()[3:])
        while stack and stack[-1][0] >= indent:
            stack.pop()
        ox = (stack[-1][1] if stack else 0)
        oy = (stack[-1][2] if stack else 0)
        stack.append((indent, ox + x, oy + y))
        if typ == "Bitmap" and "button_mm_story_up" in line and len(stack) >= 2:
            # the image's parent is the button
            _, bx, by = stack[-2]
            bw, bh = w, h
            return (bx + bw / 2.0, by + bh / 2.0)
        # modals are not children of the Screen but share indent 2 with it, so a plain stack is right here
    return None


# 1. Where is the button?
r = run("discover", "expect-window ui/mainmenu.lua 60000\nwait 100\nquit\n", "--scale", "1")
centre = story_centre(r.stdout)
if r.returncode != 0 or not centre:
    print("FAIL: could not find the Story Mode button in the dump (exit %d)\n%s" % (r.returncode, r.stdout[-1500:]))
    sys.exit(1)
cx, cy = centre
print("story button centre: %g, %g" % (cx, cy))

play = ("expect-window ui/mainmenu.lua 60000\nwait 1300\nclick %g %g\nexpect-window ui/ui_entername.lua 5000\nexpect-label cancel Cancel\nquit\n" % (cx, cy))
for name, display in (("scale1", ("--scale", "1")), ("scale2", ("--scale", "2")),
                      ("fullscreen", ("--fullscreen", "--display-size", "2560x1440"))):
    r = run(name, play, *display)
    if r.returncode != 0:
        errors.append("%s: exit %d, tail %r" % (name, r.returncode, r.stdout[-600:]))
    if "[lua-error]" in r.stdout:
        errors.append("%s: [lua-error] in the log" % name)
    if "[script] ok expect-label cancel" not in r.stdout:
        errors.append("%s: the dialog's cancel button has no label" % name)
    if not re.search(r"\[script\] ok expect-window ui/ui_entername\.lua", r.stdout):
        errors.append("%s: ui_entername.lua did not open:\n%s" % (name, r.stdout[-600:]))
    m = re.search(r"\[input\] mouse-down ([\d.]+) ([\d.]+) button 1\n", r.log)
    if not m or abs(float(m.group(1)) - cx) > 1 or abs(float(m.group(2)) - cy) > 1:
        errors.append("%s: the click reached the app at %s, expected about %g,%g" % (name, m.groups() if m else None, cx, cy))

# 2. Negative control: bare background opens nothing.
r = run("background", "expect-window ui/mainmenu.lua 60000\nwait 1300\nclick 40 300\nexpect-window ui/ui_entername.lua 700\nquit\n",
        "--scale", "1")
if r.returncode == 0 or "[script] FAIL expect-window ui/ui_entername.lua" not in r.stdout:
    errors.append("a click on bare background opened the dialog (exit %d)" % r.returncode)

if errors:
    print("FAIL:\n  " + "\n  ".join(errors))
    sys.exit(1)
print("ok: the Story Mode click opens ui_entername.lua at scale 1, scale 2 and fullscreen")
