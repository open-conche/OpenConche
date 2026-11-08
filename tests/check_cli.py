"""CLI behaviour: error exit codes, --screenshot, --frames, determinism of screenshots."""
import argparse, os, re, shutil, subprocess, sys

ap = argparse.ArgumentParser()
ap.add_argument("--openconche", required=True)
ap.add_argument("--workdir", required=True)
a = ap.parse_args()
os.makedirs(a.workdir, exist_ok=True)


USER = os.path.join(a.workdir, "user")


def run(*args):
    return subprocess.run([a.openconche, "--headless", "--userdir", USER, *args], cwd=a.workdir, capture_output=True, text=True, timeout=60)


errors = []
open(os.path.join(a.workdir, "bad.script"), "w").write("click 1 2\nfrobnicate\n")
r = run("--script", "bad.script")
if r.returncode != 2 or "frobnicate" not in r.stderr:
    errors.append("unknown script command: code %d, stderr %r" % (r.returncode, r.stderr))
if run("--script", "missing.script").returncode != 2:
    errors.append("missing script should exit 2")
open(os.path.join(a.workdir, "badkey.script"), "w").write("click 1 2\nkey NotAKey\n")
r = run("--script", "badkey.script")
if r.returncode != 2 or "line 2" not in r.stderr or "NotAKey" not in r.stderr or "[display]" in r.stdout:
    errors.append("unknown key must be rejected at parse time before any window: code %d, %r" % (r.returncode, r.stderr))
r = run("--assets", os.path.join(a.workdir, "no_such_assets"))
if r.returncode != 2 or "no_such_assets" not in r.stderr or "[display]" in r.stdout:
    errors.append("bad --assets should exit 2 with a message: code %d, %r" % (r.returncode, r.stderr))
r = run("--no-assets", "--assets", os.path.join(a.workdir, "x"))
if r.returncode != 2 or "--no-assets" not in r.stderr:
    errors.append("--no-assets with --assets should be a usage error: code %d, %r" % (r.returncode, r.stderr))
if run("--scale", "9").returncode != 2:
    errors.append("bad --scale should exit 2")
if run("--display-size", "bad").returncode != 2:
    errors.append("bad --display-size should exit 2")


# Automated runs must not touch the default user folder: no --userdir (or the default itself) exits 2 and writes nothing.
fake_home = os.path.join(a.workdir, "fake_home")
shutil.rmtree(fake_home, ignore_errors=True)
os.makedirs(fake_home)
open(os.path.join(a.workdir, "x.script"), "w").write("wait 10\n")
env_home = dict(os.environ, HOME=fake_home, APPDATA=fake_home, XDG_DATA_HOME=fake_home)
for flags in (["--headless", "--script", "x.script"], ["--frames", "2"], ["--no-assets"], ["--screenshot", "z.png"],
              ["--dump-windows"], ["--log-unimplemented", "u.txt"]):
    r = subprocess.run([a.openconche, *flags], cwd=a.workdir, capture_output=True, text=True, timeout=60, env=env_home)
    lines = r.stderr.strip().splitlines()
    if r.returncode != 2 or len(lines) != 1 or "--userdir <scratch dir>" not in r.stderr or "default user folder" not in r.stderr:
        errors.append("%s without --userdir: code %d, stderr %r" % (flags, r.returncode, r.stderr))
    if os.listdir(fake_home) or os.path.exists(os.path.join(a.workdir, "z.png")):
        errors.append("%s without --userdir wrote files: %r" % (flags, os.listdir(fake_home)))
# --userdir naming the default folder is refused too when automated (spelled with a trailing slash and a dot segment).
if sys.platform == "darwin":
    dflt = os.path.join(fake_home, "Library", "Application Support", "Chocolatier3")
    r = subprocess.run([a.openconche, "--headless", "--userdir", dflt + "/./", "--frames", "1"], cwd=a.workdir,
                       capture_output=True, text=True, timeout=60, env=env_home)
    if r.returncode != 2 or "default user folder" not in r.stderr or os.listdir(fake_home):
        errors.append("--userdir = default with --headless: code %d, %r" % (r.returncode, r.stderr))
if run("--help").returncode != 0 or subprocess.run([a.openconche, "--headless", "--help"], capture_output=True, timeout=60).returncode != 0:
    errors.append("--help must not need --userdir")


def frames(r):
    m = re.search(r"\[app\] exit after (\d+) frames", r.stdout)
    return int(m.group(1)) if m else -1


# A trailing wait must be waited out: 500 ms is 30 frames at the simulated 60 Hz.
open(os.path.join(a.workdir, "wait.script"), "w").write("click 1 2\nwait 500\n")
r = run("--script", "wait.script")
if r.returncode != 0 or frames(r) < 30:
    errors.append("trailing wait ignored: %d frames, %r" % (frames(r), r.stderr))

# --screenshot with --frames honours n (and the image is the real 800x600 output).
r = run("--screenshot", "one.png", "--frames", "7")
if r.returncode != 0 or frames(r) != 7 or "[screenshot] one.png 800x600" not in r.stdout:
    errors.append("--screenshot --frames 7: frames %d, %r %r" % (frames(r), r.stdout, r.stderr))
r = run("--screenshot", "first.png")
if r.returncode != 0 or frames(r) != 1 or "[screenshot] first.png 800x600" not in r.stdout:
    errors.append("--screenshot alone should take the first frame: %r" % r.stdout)
r = run("--screenshot", "two.png", "--scale", "2")
if "[screenshot] two.png 1600x1200" not in r.stdout:
    errors.append("scale 2 screenshot should be 1600x1200: %r" % r.stdout)

# --screenshot with --script runs the script first; the shot comes after its fullscreen toggle and wait.
open(os.path.join(a.workdir, "late.script"), "w").write("key alt+Return\nwait 100\n")
r = run("--script", "late.script", "--screenshot", "late.png", "--display-size", "1920x1080")
if (r.returncode != 0 or "[screenshot] late.png 1920x1080" not in r.stdout or frames(r) < 6
        or r.stdout.find("fullscreen on") > r.stdout.find("[screenshot]") or "fullscreen on" not in r.stdout):
    errors.append("--screenshot with --script: %r %r" % (r.stdout, r.stderr))

# An exported SDL_VIDEO_DRIVER must not redirect headless runs.
env = dict(os.environ, SDL_VIDEO_DRIVER="bogus")
r = subprocess.run([a.openconche, "--headless", "--userdir", USER, "--frames", "2"], cwd=a.workdir, capture_output=True, text=True, timeout=60, env=env)
if r.returncode != 0:
    errors.append("SDL_VIDEO_DRIVER=bogus broke headless: %r" % r.stderr)
if run("--frames", "5").returncode != 0:
    errors.append("--frames run failed")
# Without --assets an automated run must not depend on the machine: no library read, no launcher, the test scene.
r = run("--frames", "1")
if r.returncode != 0 or "[assets]" in r.stdout or "showing the test scene" not in r.stderr:
    errors.append("headless without --assets must show the test scene (no library, no launcher): code %d, %r %r"
                  % (r.returncode, r.stdout[:300], r.stderr[:300]))
r = run("--no-assets", "--frames", "1")
if r.returncode != 0 or "[assets]" in r.stdout or "showing the test scene" not in r.stderr:
    errors.append("--no-assets should skip the search: code %d, %r" % (r.returncode, r.stdout[:300]))

if errors:
    print("FAIL:\n  " + "\n  ".join(errors))
    sys.exit(1)
print("OK")
