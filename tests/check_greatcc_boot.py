"""The Great Chocolate Chase (docs/design/greatcc.md slice 1) boots to its main menu on this engine, from either asset layout:
the Windows install (assets.pfp + loose settings.xml, strings/, splash/, credits/) or the Mac bundle's loose plain-text tree.

Runs `--headless --userdir <scratch> --seed 1` with a script that waits for scripts/mainmenu.lua (the three splashes in
scripts/mainloop.lua have passed by then), dumps the window tree and takes a screenshot. Asserts: exit 0; the [game] line
says greatcc; no [lua-error] / [engine-error] / [native-error]; the set of [unimplemented] names equals ALLOWED_UNIMPLEMENTED
and the set of [unimplemented-type] names equals ALLOWED_TYPES (so a native that becomes real, or a new gap, shows up here);
the menu holds the TMainMenu window and its buttons; the screenshot is 800x600 and not one colour. A second run with
`--config fixbugs=0` logs that the original-bug switch is off (the first logs it on)."""
import argparse, os, re, shutil, struct, subprocess, sys, zlib

ap = argparse.ArgumentParser()
ap.add_argument("--openconche", required=True)
ap.add_argument("--assets", required=True)
ap.add_argument("--workdir", required=True)
a = ap.parse_args()
shutil.rmtree(a.workdir, ignore_errors=True)
os.makedirs(a.workdir)
errors = []

# Natives the menu reaches that are not built: none (_SetState writes the TGameState store). The menu's windows
# (TMainMenu, RShrinkText) are real since slice 3.
ALLOWED_UNIMPLEMENTED = set()
# Window types still placeholders on the way to the menu: none.
ALLOWED_TYPES = set()


def run(tag, extra, script_text):
    user = os.path.join(a.workdir, "user_" + tag)
    shot = os.path.join(a.workdir, tag + ".png")
    log = os.path.join(a.workdir, tag + ".log")
    script = os.path.join(a.workdir, tag + ".script")
    with open(script, "w") as f:
        f.write(script_text.replace("@SHOT@", shot))
    cmd = [a.openconche, "--headless", "--assets", a.assets, "--userdir", user, "--seed", "1", "--script", script,
           "--log", log] + extra
    p = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=120)
    text = open(log, encoding="utf-8", errors="replace").read() if os.path.exists(log) else ""
    return p, text + "\n" + p.stdout + "\n" + p.stderr, shot


def png_colours(path):
    data = open(path, "rb").read()
    w, h, depth, ctype = struct.unpack(">IIBB", data[16:26])
    pos, idat = 8, b""
    while pos < len(data):
        n, typ = struct.unpack(">I4s", data[pos:pos + 8])
        if typ == b"IDAT":
            idat += data[pos + 8:pos + 8 + n]
        pos += 12 + n
    raw = zlib.decompress(idat)
    bpp = {2: 3, 6: 4}.get(ctype)
    assert depth == 8 and bpp, "unsupported PNG"
    stride = w * bpp
    prev, colours = bytearray(stride), set()
    for y in range(h):
        f, line = raw[y * (stride + 1)], bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            l = line[i - bpp] if i >= bpp else 0
            u = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if f == 1: line[i] = (line[i] + l) & 255
            elif f == 2: line[i] = (line[i] + u) & 255
            elif f == 3: line[i] = (line[i] + (l + u) // 2) & 255
            elif f == 4:
                pa, pb, pc = abs(u - c), abs(l - c), abs(l + u - 2 * c)
                line[i] = (line[i] + (l if pa <= pb and pa <= pc else u if pb <= pc else c)) & 255
        for x in range(w):
            colours.add(bytes(line[x * bpp:x * bpp + 3]))
        prev = line
    return w, h, colours


p, out, shot = run("menu", [], "expect-window scripts/mainmenu.lua 20000\nwait 1500\ndump\nscreenshot-logical @SHOT@\n")
if p.returncode != 0:
    errors.append("exit status %d" % p.returncode)
if not re.search(r"\[game\] greatcc ", out):
    errors.append("the [game] line does not say greatcc")
for tag in ("[lua-error]", "[engine-error]", "[native-error]", "[content]"):
    if tag in out:
        errors.append("%s in the output: %s" % (tag, next(l for l in out.split("\n") if tag in l)))
if "[script] ok expect-window scripts/mainmenu.lua" not in out:
    errors.append("scripts/mainmenu.lua never became the top modal")
if "[greatcc] fixbugs on" not in out:
    errors.append("the default run does not log fixbugs on")
seen = set(re.findall(r"^(?:\[\+\d+ms\] )?\[unimplemented\] (\S+)", out, re.M))
if seen != ALLOWED_UNIMPLEMENTED:
    errors.append("[unimplemented] set differs: extra %s, missing %s" % (sorted(seen - ALLOWED_UNIMPLEMENTED),
                                                                         sorted(ALLOWED_UNIMPLEMENTED - seen)))
types = set(re.findall(r"^(?:\[\+\d+ms\] )?\[unimplemented-type\] (\S+)", out, re.M))
if types != ALLOWED_TYPES:
    errors.append("[unimplemented-type] set differs: extra %s, missing %s" % (sorted(types - ALLOWED_TYPES),
                                                                              sorted(ALLOWED_TYPES - types)))
for needle in ('Modal "mainmenu_dialog"', "TMainMenu", 'Button "mainmenu_adventure_button"', 'Button "mainmenu_quit"',
               "Copyright (C) 2008 PlayFirst"):
    if needle not in out:
        errors.append("%r is not in the menu's window dump" % needle)
if not os.path.exists(shot):
    errors.append("no screenshot")
else:
    w, h, colours = png_colours(shot)
    if (w, h) != (800, 600):
        errors.append("screenshot is %dx%d" % (w, h))
    if len(colours) < 2:
        errors.append("the screenshot is one colour")

p2, out2, _ = run("nofix", ["--config", "fixbugs=0"], "expect-window scripts/mainmenu.lua 20000\n")
if p2.returncode != 0:
    errors.append("--config fixbugs=0: exit status %d" % p2.returncode)
if "[greatcc] fixbugs off" not in out2:
    errors.append("--config fixbugs=0 does not turn the switch off")

for e in errors:
    print("FAIL:", e)
print("greatcc_boot: %s" % ("FAILED" if errors else "ok"))
sys.exit(1 if errors else 0)
