"""Importing an original prefs.dat into the engine's user dir, then playing from it.

1. `openconche --headless --import-prefs ada_bob.dat --userdir <scratch>` (a vector written by workbench/re/prefs_dump.py: users Ada
   and Bob, Ada's save a real new-game save string captured from this engine, curuser 0) exits 0 and prints the summary.
   The source file is byte-identical afterwards (it is only read).
2. A second import into the same dir backs the dir up to <dir>-backup-<timestamp> (file list and sha1 equal to the folder
   before the import) and leaves two users (same names are replaced, not duplicated).
3. Refusals: no --userdir (exit 2; HOME/APPDATA point at the work dir so the real profile cannot be touched), a --userdir
   next to the source or inside a PlayFirst folder (exit 2), a garbage file (exit 1, nothing created), a read-only parent
   folder (exit 1, not a silent "done").
4. The real Reforged game booted on the imported dir: GetNumUsers() is 2, the current user is Ada, "Welcome back, Ada!",
   LoadGameString() is exactly the imported text, and Story Mode resumes in the Zurich port from it (a fresh game: the quest offer opens; Player.name == "Ada").
Everything runs in --workdir; the source vector is never copied over or modified."""
import argparse, glob, hashlib, os, shutil, subprocess, sys

ap = argparse.ArgumentParser()
ap.add_argument("--openconche", required=True)
ap.add_argument("--assets", required=True)
ap.add_argument("--vector", required=True, help="the ada_bob.dat vector")
ap.add_argument("--save", required=True, help="the save text inside it (ada_save.txt)")
ap.add_argument("--workdir", required=True)
a = ap.parse_args()
shutil.rmtree(a.workdir, ignore_errors=True)
os.makedirs(a.workdir)
user = os.path.join(a.workdir, "user")
errors = []
# Every run gets a fake profile root, so a guard regression can only ever write below the work dir.
ENV = dict(os.environ, HOME=a.workdir, APPDATA=a.workdir, XDG_DATA_HOME=a.workdir)


def sha(path):
    with open(path, "rb") as f:
        return hashlib.sha1(f.read()).hexdigest()


def imp(*args, userdir=user):
    cmd = [a.openconche, "--headless", "--import-prefs", a.vector] + (["--userdir", userdir] if userdir else []) + list(args)
    return subprocess.run(cmd, cwd=a.workdir, capture_output=True, text=True, timeout=60, env=ENV)


def snapshot(root):
    out = {}
    for d, _, files in os.walk(root):
        for f in files:
            full = os.path.join(d, f)
            out[os.path.relpath(full, root)] = sha(full)
    return out


before = sha(a.vector)
mtime = os.stat(a.vector).st_mtime_ns

r = imp()
if r.returncode != 0 or "users found:      2" not in r.stdout or "saves imported:   1" not in r.stdout:
    errors.append("import 1: exit %d, stdout %r, stderr %r" % (r.returncode, r.stdout[-600:], r.stderr[-300:]))
if "no backup needed" not in r.stdout:
    errors.append("import 1 should find nothing to back up: %r" % r.stdout[:300])
if not os.path.exists(os.path.join(user, "users.txt")):
    errors.append("import 1 wrote no users.txt")

before_dir = snapshot(user)
r = imp()
backups = glob.glob(user + "-backup-*")
if r.returncode != 0 or len(backups) != 1 or not os.path.exists(os.path.join(backups[0], "users.txt")):
    errors.append("import 2: exit %d, backups %r" % (r.returncode, backups))
elif snapshot(backups[0]) != before_dir or not before_dir:
    errors.append("the backup differs from the folder before the import: %r vs %r" % (snapshot(backups[0]), before_dir))
if glob.glob(user + "*.partial"):
    errors.append("a .partial backup was left behind")
if "2 replaced" not in r.stdout:
    errors.append("import 2 should replace the two users of the same names: %r" % r.stdout[:600])

if sha(a.vector) != before or os.stat(a.vector).st_mtime_ns != mtime:
    errors.append("the source vector was modified")

r = imp(userdir=None)
if r.returncode != 2 or "--userdir" not in r.stderr:
    errors.append("no --userdir: expected exit 2 naming --userdir, got %d %r" % (r.returncode, r.stderr))
os.makedirs(os.path.join(a.workdir, "src"))
garbage = os.path.join(a.workdir, "src", "garbage.dat")
with open(garbage, "wb") as f:
    f.write(bytes((i * 37 + 11) & 255 for i in range(300)))
fresh = os.path.join(a.workdir, "fresh")
r = subprocess.run([a.openconche, "--headless", "--import-prefs", garbage, "--userdir", fresh], cwd=a.workdir,
                   capture_output=True, text=True, timeout=60, env=ENV)
if r.returncode != 1 or os.path.exists(fresh):
    errors.append("garbage file: expected exit 1 and no user dir, got %d (dir exists %s) %r" % (r.returncode, os.path.exists(fresh), r.stderr))

# A --userdir inside the source's folder, or under a PlayFirst folder, is refused before anything is written.
shutil.copy(garbage, os.path.join(a.workdir, "src", "prefs.dat"))
for bad in (os.path.join(a.workdir, "src", "u"), os.path.join(a.workdir, "PlayFirst", "game")):
    r = subprocess.run([a.openconche, "--headless", "--import-prefs", os.path.join(a.workdir, "src", "prefs.dat"), "--userdir", bad],
                       cwd=a.workdir, capture_output=True, text=True, timeout=60, env=ENV)
    if r.returncode != 2 or os.path.exists(bad):
        errors.append("--userdir %s should be refused (exit 2, nothing created): %d %r" % (bad, r.returncode, r.stderr))

# A target that cannot be written must fail loudly (POSIX only, and not as root).
if os.name != "nt" and os.geteuid() != 0:
    ro = os.path.join(a.workdir, "ro")
    os.makedirs(ro)
    os.chmod(ro, 0o555)
    try:
        r = imp(userdir=os.path.join(ro, "user"))
        if r.returncode != 1 or "FAILED" not in r.stderr:
            errors.append("read-only parent: expected exit 1 with FAILED, got %d %r %r" % (r.returncode, r.stdout[-200:], r.stderr))
    finally:
        os.chmod(ro, 0o755)

with open(a.save, "r", encoding="utf-8", newline="") as f:
    save = f.read()
script = os.path.join(a.workdir, "boot.script")
with open(script, "w") as f:
    f.write("expect-window ui/mainmenu.lua 60000\nwait 1300\n"
            "lua print('NUMUSERS', GetNumUsers())\n"
            "lua print('CURNAME', GetCurrentUserName(), GetCurrentUser(), GetUserName(1))\n"
            "lua print('WELCOME', GetLabel('welcome'))\n"
            "lua print('SAVE', LoadGameString())\n"
            "click 310 345\nexpect-window ui/portview.lua 15000\nwait 1500\n"
            "expect-lua 1000 Player.name == 'Ada' and Player.mode == 'story'\n"
            "quit\n")
r = subprocess.run([a.openconche, "--headless", "--assets", a.assets, "--userdir", user, "--script", script], cwd=a.workdir,
                   capture_output=True, text=True, timeout=180, env=ENV)
out = r.stdout
if r.returncode != 0 or "[lua-error]" in out or "FAIL" in out:
    errors.append("boot: exit %d, tail %r" % (r.returncode, out[-600:]))


def field(tag):
    for line in out.splitlines():
        i = line.find(tag + "\t")
        if i >= 0:
            return line[i + len(tag) + 1:]
    return None


if field("NUMUSERS") != "2":
    errors.append("GetNumUsers: %r" % field("NUMUSERS"))
if field("CURNAME") != "Ada\t0\tBob":
    errors.append("current user / names: %r" % field("CURNAME"))
if "Welcome back, Ada!" not in (field("WELCOME") or ""):
    errors.append("welcome label: %r" % field("WELCOME"))
if field("SAVE") != save:
    s = field("SAVE") or ""
    errors.append("LoadGameString differs from the imported text (%d vs %d bytes)" % (len(s), len(save)))

if errors:
    print("FAIL:\n  " + "\n  ".join(errors))
    sys.exit(1)
print("ok: import (twice, with a backup), refusals, then Reforged boots as Ada with her imported save and resumes the story in port")
