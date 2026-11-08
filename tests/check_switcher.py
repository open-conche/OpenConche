"""The switcher's three zero-CLI entry points, tested headless through the guard (docs/design/game-finder.md 2a).

A headless run never opens the GUI screen, but the chain must still reach it and log the needs-a-display guard:
(1) the Ctrl+Shift+G chord (also proving the event's key name is SDL's capitalised "G", the bug that made the
first cut of the chord dead), (2) the Cmd variant (macOS users: KMOD_GUI surfaces as InputEvent::gui, so it must
behave exactly like ctrl), (3) a click in the OpenConche badge's rect (the badge itself is a windowed-only draw;
the hit test is active headless on purpose, so this chain is testable). Boots vanilla to the menu first, so the
game - not the launcher - is what would receive the chord without this working.
"""
import argparse, os, shutil, subprocess, sys

ap = argparse.ArgumentParser()
ap.add_argument("--openconche", required=True)
ap.add_argument("--assets", required=True)
ap.add_argument("--workdir", required=True)
a = ap.parse_args()
shutil.rmtree(a.workdir, ignore_errors=True)
os.makedirs(a.workdir)
script = os.path.join(a.workdir, "t.script")
# The badge's rect (kBadgeRect 466,14 320x40): click its middle. Headless keeps the hit test active past the
# badge's on-screen lifetime (it is a windowed welcome strip, gone after 10 s), so the click needs no timing.
with open(script, "w") as f:
    f.write("expect-window ui/mainmenu.lua 60000\n"
            "wait 300\n"
            "key ctrl+shift+g\n"
            "key alt+shift+g\n"  # must NOT reach the switcher
            "key gui+shift+g\n"
            "click 626 34\n"     # inside the badge
            "click 626 90\n"     # outside it: reaches the game, proves stray clicks pass through
            "wait 300\n"
            "quit\n")
r = subprocess.run([a.openconche, "--headless", "--assets", a.assets,
                    "--userdir", os.path.join(a.workdir, "user"), "--script", script],
                   cwd=a.workdir, capture_output=True, text=True, timeout=120)
out = r.stdout + r.stderr
errors = []
if r.returncode != 0:
    errors.append("exit %d, tail %r" % (r.returncode, out[-500:]))
for name in ("ok expect-window ui/mainmenu.lua",):
    if name not in out:
        errors.append("%s missing (the game did not boot to the menu)" % name)
guard = "[finder] the finder needs a display"
n_guard = out.count(guard)
if n_guard != 3:
    errors.append("expected the switcher guard %d times (ctrl chord, cmd chord, badge click), got %d - "
                  "alt+shift+g must not count: %r" % (3, n_guard, out[-1500:]))
if errors:
    print("FAIL:\n  " + "\n  ".join(errors))
    sys.exit(1)
print("OK")
