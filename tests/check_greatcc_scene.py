"""The Great Chocolate Chase (docs/design/greatcc.md slice 7): the sprite scene on the real scripts.

The Options screen holds a VObjectWindow with one object, the steam animation (`ui/anim_steam`, a `continuous` object whose
script loops). Runs `--headless --userdir <scratch> --seed 1`, opens Options and asserts: exit 0; no [lua-error] /
[engine-error] / [native-error] / [content]; there is no [unimplemented] call and no [unimplemented-type];
the window dump shows `VObjectWindow "sprite_window"` with one sprite; no asset the scene loads is reported missing; and two
screenshots a moment apart differ (the steam moves) while both are real 800x600 images."""
import argparse, os, re, shutil, struct, subprocess, sys, zlib

ap = argparse.ArgumentParser()
ap.add_argument("--openconche", required=True)
ap.add_argument("--assets", required=True)
ap.add_argument("--workdir", required=True)
a = ap.parse_args()
shutil.rmtree(a.workdir, ignore_errors=True)
os.makedirs(a.workdir)
errors = []

user = os.path.join(a.workdir, "user")
shots = [os.path.join(a.workdir, "options_%d.png" % i) for i in (1, 2, 3)]
script = os.path.join(a.workdir, "scene.script")
with open(script, "w") as f:
    f.write("""expect-window scripts/mainmenu.lua 20000
wait 800
lua CreateNewUser('Ann')
wait 100
click 575 520
wait 1500
dump
screenshot %s
wait 330
screenshot %s
wait 330
screenshot %s
""" % tuple(shots))
log = os.path.join(a.workdir, "scene.log")
p = subprocess.run([a.openconche, "--headless", "--assets", a.assets, "--userdir", user, "--seed", "1", "--script", script,
                    "--log", log], capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=180)
out = (open(log, encoding="utf-8", errors="replace").read() if os.path.exists(log) else "") + "\n" + p.stdout + "\n" + p.stderr
if p.returncode != 0:
    errors.append("exit status %d" % p.returncode)
for t in ("[lua-error]", "[engine-error]", "[native-error]", "[content]", "[sprite-lua]", "did not load", "[script] FAIL"):
    if t in out:
        errors.append("%s in the output: %s" % (t, next(l for l in out.split("\n") if t in l)))
calls = set(re.findall(r"^(?:\[\+\d+ms\] )?\[unimplemented\] (\S+)", out, re.M))
if calls:
    errors.append("[unimplemented] set is %s" % sorted(calls))
if re.search(r"\[unimplemented-type\]", out):
    errors.append("an [unimplemented-type] line: %s" % next(l for l in out.split("\n") if "[unimplemented-type]" in l))
if not re.search(r'^\s*VObjectWindow "sprite_window" id=\d+ rect=0,0,800,600 sprites=1$', p.stdout, re.M):
    errors.append("no VObjectWindow \"sprite_window\" with one sprite in the dump")


def png(path):
    """(width, height, set of distinct pixels) of an 8-bit RGB/RGBA PNG, or None."""
    if not os.path.exists(path):
        return None
    raw = open(path, "rb").read()
    if raw[:8] != b"\x89PNG\r\n\x1a\n":
        return None
    pos, idat, w, h, ct = 8, b"", 0, 0, 0
    while pos < len(raw):
        n, kind = struct.unpack(">I4s", raw[pos:pos + 8])
        body = raw[pos + 8:pos + 8 + n]
        if kind == b"IHDR":
            w, h, depth, ct = struct.unpack(">IIBB", body[:10])
        elif kind == b"IDAT":
            idat += body
        pos += 12 + n
    return w, h, ct, zlib.decompress(idat)


imgs = [png(s) for s in shots]
for s, i in zip(shots, imgs):
    if i is None:
        errors.append("%s is not a PNG" % os.path.basename(s))
    elif (i[0], i[1]) != (800, 600):
        errors.append("%s is %dx%d, not 800x600" % (os.path.basename(s), i[0], i[1]))
if all(imgs):
    if len({i[3] for i in imgs}) < 2:
        errors.append("the three screenshots are identical: the steam does not move")
    if len(set(imgs[0][3])) < 8:
        errors.append("the first screenshot is nearly uniform")

if errors:
    print("FAIL")
    for e in errors:
        print(" ", e)
    sys.exit(1)
print("ok")
