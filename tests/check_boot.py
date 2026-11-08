"""Runs the real Reforged ui/mainloop.lua headless for 120 frames.

Main reaches the main menu and then idles in DoMainWindow, so the frame loop stays alive. The window and content
natives are real now; the unimplemented file lists what is still a stub (SoundEvent, StartMovie ...)."""
import argparse, os, re, subprocess, sys

ap = argparse.ArgumentParser()
ap.add_argument("--openconche", required=True)
ap.add_argument("--assets", required=True)
ap.add_argument("--workdir", required=True)
a = ap.parse_args()
os.makedirs(a.workdir, exist_ok=True)
unimpl = os.path.join(a.workdir, "unimplemented.txt")
if os.path.exists(unimpl):
    os.remove(unimpl)


def run(*extra):
    return subprocess.run([a.openconche, "--headless", "--assets", a.assets, "--userdir", os.path.join(a.workdir, "user"),
                           "--frames", "120", "--log-unimplemented", unimpl, *extra],
                          cwd=a.workdir, capture_output=True, text=True, timeout=120)


errors = []
r = run()
if r.returncode != 0:
    errors.append("exit code %d, stderr %r" % (r.returncode, r.stderr[-500:]))
if "[app] exit after 120 frames" not in r.stdout:
    errors.append("the frame loop did not run all 120 frames:\n" + r.stdout[-800:])
if "[engine-error]" in r.stdout or "[native-error]" in r.stdout:
    errors.append("engine or native error in the log:\n" + r.stdout[-800:])
if "[script] seed 1" not in r.stdout:
    errors.append("headless runs must default to seed 1")
if not os.path.exists(unimpl):
    errors.append("--log-unimplemented file was not written")
else:
    text = open(unimpl).read()
    names = {line.split("\t")[0] for line in text.splitlines()}
    if not names & {"SoundEvent", "StartMovie"}:
        errors.append("unimplemented file should list SoundEvent or StartMovie, got: %s" % sorted(names))
    if names & {"GenericCreate", "PushModal", "LoadIngredients"}:
        errors.append("window and content natives should be real, but the stub ran: %s" % sorted(names))
    for line in text.splitlines():
        if not re.fullmatch(r"\w+\t\d+", line):
            errors.append("bad unimplemented line %r" % line)
            break

# Deterministic: a second run gives the same log and the same stub counts.
first = (r.stdout, open(unimpl).read() if os.path.exists(unimpl) else "")
r2 = run()
second = (r2.stdout, open(unimpl).read() if os.path.exists(unimpl) else "")
if first != second:
    errors.append("two runs differ")

if errors:
    print("\n".join(errors))
    sys.exit(1)
print("ok: %d unimplemented names, %d [lua-error] lines" % (len(first[1].splitlines()), r.stdout.count("[lua-error]")))
