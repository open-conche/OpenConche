"""Non-ASCII paths in --userdir, --log and --assets-adjacent output: the log and the user files must be written
inside the UTF-8-named folder (the Windows exe uses SDL_main, so argv is UTF-8 there).

On macOS this only proves the file-opening paths are UTF-8 clean; the real proof of argv handling is a Windows run."""
import argparse, os, shutil, subprocess, sys

ap = argparse.ArgumentParser()
ap.add_argument("--openconche", required=True)
ap.add_argument("--assets", required=True)
ap.add_argument("--workdir", required=True)
a = ap.parse_args()

base = os.path.join(a.workdir, "Chocolaté")
shutil.rmtree(base, ignore_errors=True)
os.makedirs(base)
userdir = os.path.join(base, "ünïcode user")
log = os.path.join(base, "résumé.log")
script = os.path.join(base, "sçript.script")
with open(script, "w", encoding="utf-8") as f:
    f.write("expect-window ui/mainmenu.lua 60000\nquit\n")

r = subprocess.run([a.openconche, "--headless", "--assets", a.assets, "--userdir", userdir, "--log", log,
                    "--script", script], capture_output=True, timeout=120)
errors = []
out = r.stdout.decode("utf-8", "replace")
if r.returncode != 0:
    errors.append("exit code %d, stderr %r" % (r.returncode, r.stderr[-300:]))
if "[script] ok expect-window ui/mainmenu.lua" not in out:
    errors.append("script did not reach the main menu:\n" + out[-600:])
if not os.path.isfile(log) or os.path.getsize(log) == 0:
    errors.append("log file %r was not written" % log)
else:
    text = open(log, encoding="utf-8", errors="replace").read()
    if "[log] opened" not in text or "[script] ok expect-window" not in text:
        errors.append("log lacks expected lines:\n" + text[:400])
if not os.path.isdir(userdir) or not os.listdir(userdir):
    errors.append("no user files written in %r" % userdir)
if errors:
    print("\n".join(errors))
    sys.exit(1)
print("ok: log %d bytes, user files %s" % (os.path.getsize(log), sorted(os.listdir(userdir))))
