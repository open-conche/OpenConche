"""Chocolatier 2: the port's ledger is the real Ledger with DynamicWindow tabs and a badge.

Docs: docs/design/choc2-port-and-ledger.md 2.3 and section 4. A new user plays the intro into ui/portview.lua (as check_choc2_story.py
does), the tutorial dialogs are popped and the ledger is inspected through the window dump and screenshots:

1. The ledger is a Ledger (not a placeholder), the Messages tab is shown with the welcome message in `messageText`, the factories and
   inventory bodies are hidden, the badge DynamicWindow holds the `money`, `rank`, `date` and `score` Texts and the money
   reads what the ledger holds.
2. Clicking the Factory tab shows the factories body and hides the messages (and the radio follows); the Inventory tab likewise; the
   session remembers the tab (Ledger `tab=`).
3. `gSim:AdjustMoney(5000)` counts the badge money up (partway after 300 ms): after 3 s the money Text reads the old amount plus $5,000 (UpdateBadge).
4. Screenshots of each, which the author looks at.

Asserts: exit 0, no [lua-error] / [engine-error] / [native-error] / [native-warning], no [unimplemented-type] for DynamicWindow or Ledger, only
allow-listed natives unimplemented, the screenshots are 800x600 and differ."""
import argparse, os, re, shutil, struct, subprocess, sys, zlib

ap = argparse.ArgumentParser()
ap.add_argument("--openconche", required=True)
ap.add_argument("--assets", required=True)
ap.add_argument("--workdir", required=True)
a = ap.parse_args()
shutil.rmtree(a.workdir, ignore_errors=True)
os.makedirs(a.workdir)
user = os.path.join(a.workdir, "user")
errors = []

# bsgDate is the badge's date line (two arguments, semantics unknown: still a stub); the rest wait for the audio, Port and Map slices.
# The tutorial the run plays through also reaches the logo editor, the medal award and the factory status (later slices).
ALLOWED_UNIMPLEMENTED = {"StartMovie", "SetMusicState", "SetAmbient", "bsgDate", "bsgSin", "bsgCos"}
ALLOWED_PLACEHOLDER_TYPES = {"LogoEditor", "Sprite", "AnimationWindow", "FactoryStatus"}


def png_not_black(path):
    data = open(path, "rb").read()
    pos, idat = 8, b""
    while pos < len(data):
        n, typ = struct.unpack(">I4s", data[pos:pos + 8])
        if typ == b"IDAT":
            idat += data[pos + 8:pos + 8 + n]
        pos += 12 + n
    raw = zlib.decompress(idat)
    return sum(1 for b in raw if b) > len(raw) // 100


def dumps(out):
    return [b for b in re.split(r"(?=^\[windows\] modal depth)", out, flags=re.M) if b.startswith("[windows]")]


def line(block, kind, name):
    m = re.search(r'^\s*%s "%s" .*$' % (kind, name), block, flags=re.M)
    return m.group(0) if m else None


shots = {k: os.path.join(a.workdir, k + ".png") for k in ("messages", "factories", "inventory", "money")}
script_path = os.path.join(a.workdir, "run.script")
# A new Story game opens a chain of tutorial dialogs (chartalk, the logo editor, the recipe book, a medal). They are popped by name each
# round (the Lua that waits for them goes on to the next one); once the port view is alone the ledger can be clicked.
POP = "lua DoPopModal('sf_tutor'); DoPopModal('simplelogo'); DoPopModal('ui/recipebook.lua'); DoPopModal('ui/medal_award.lua')\nwait 800\n"
script = ("expect-window ui/mainmenu.lua 20000\nwait 1500\nclick 400 345\nexpect-window ui/entername.lua 5000\nwait 500\ntype Tester\n"
          "key Return\nwait 2000\nwait 10000\nclick 400 300\nexpect-window ui/portview.lua 5000\nwait 1500\n"
          + POP * 14 + "expect-dump 5000 modal depth 2\nwait 2500\n"
          "click 340 410\nwait 600\ndump\nscreenshot-logical %s\n"                  # dump 1: the Messages tab
          "click 470 410\nwait 600\ndump\nscreenshot-logical %s\n"                  # dump 2: the Factory tab
          "click 705 410\nwait 600\ndump\nscreenshot-logical %s\n"                  # dump 3: the Inventory tab
          "click 340 410\nwait 600\nlua gSim:AdjustMoney(5000)\nwait 300\ndump\nwait 3000\ndump\nscreenshot-logical %s\nquit\n"  # dumps 4, 5: counting, counted up
          % (shots["messages"], shots["factories"], shots["inventory"], shots["money"]))
open(script_path, "w").write(script)
unimpl = os.path.join(a.workdir, "unimplemented.txt")
r = subprocess.run([a.openconche, "--headless", "--assets", a.assets, "--userdir", user, "--script", script_path, "--log-unimplemented", unimpl],
                   cwd=a.workdir, capture_output=True, text=True, timeout=180)
out = r.stdout
if r.returncode != 0:
    errors.append("exit %d, tail %r" % (r.returncode, out[-600:]))
for bad in ("[lua-error]", "[engine-error]", "[native-error]", "[native-warning]", "FAIL", "[choc2] DynamicWindow"):
    if bad in out:
        errors.append("output contains %s" % bad)
for m in re.finditer(r"^\[unimplemented-type\] (\S+)", out, flags=re.M):
    if m.group(1) not in ALLOWED_PLACEHOLDER_TYPES:
        errors.append("unexpected placeholder window type %s" % m.group(1))
if os.path.exists(unimpl):
    for l in open(unimpl):
        if l.split("\t")[0] not in ALLOWED_UNIMPLEMENTED:
            errors.append("unexpected unimplemented native %s" % l.split("\t")[0])

d = dumps(out)
if len(d) != 5:
    errors.append("expected 5 window dumps, got %d" % len(d))
else:
    msgs, fact, inv, mid, money = d
    if "ui/portview.lua" not in msgs:
        errors.append("dump 1 is not the port view")
    led = line(msgs, "Ledger", "ledger")
    if led is None or 'tab="messages"' not in led:
        errors.append("dump 1: the Ledger is missing or not on the messages tab: %r" % led)
    for name, want in (("messages", False), ("factories", True), ("inventory", True)):
        l = line(msgs, "DynamicWindow", name)
        if l is None:
            errors.append("dump 1: no DynamicWindow %s" % name)
        elif (" hidden" in l) != want:
            errors.append("dump 1: DynamicWindow %s hidden=%s: %r" % (name, not want, l))
    if line(msgs, "Text", "messageText") is None or "Welcome to Chocolatier 2" not in msgs:
        errors.append("dump 1: messageText lacks the welcome message")
    for name in ("money", "rank", "date", "score"):
        if line(msgs, "Text", name) is None:
            errors.append("dump 1: the badge has no %s Text" % name)
    led = line(msgs, "Ledger", "ledger") or ""
    m1 = re.search(r"money=(\d+) target=(\d+)", led)
    if not m1 or m1.group(1) != m1.group(2):
        errors.append("dump 1: the ledger money is still counting or missing: %r" % led)
    base = int(m1.group(2)) if m1 else 0
    if line(msgs, "Text", "money") is None or 'label="$%s"' % format(base, ",") not in line(msgs, "Text", "money"):
        errors.append("dump 1: the badge money Text is not %s: %r" % (format(base, ","), line(msgs, "Text", "money")))
    for blk, tab, hidden in ((fact, "factories", ("messages", "inventory")), (inv, "inventory", ("messages", "factories"))):
        led = line(blk, "Ledger", "ledger")
        if led is None or 'tab="%s"' % tab not in led:
            errors.append("clicking the %s tab did not show it: %r" % (tab, led))
        if " hidden" in (line(blk, "DynamicWindow", tab) or " hidden"):
            errors.append("the %s body is hidden after clicking its tab" % tab)
        for h in hidden:
            if " hidden" not in (line(blk, "DynamicWindow", h) or ""):
                errors.append("the %s body is not hidden on the %s tab" % (h, tab))
    # 300 ms after the change the counter is partway: the shown money is between the old amount and the new one.
    mm = re.search(r'label="\$([\d,]+)"', line(mid, "Text", "money") or "")
    shown = int(mm.group(1).replace(",", "")) if mm else -1
    if not base < shown < base + 5000:
        errors.append("300 ms after AdjustMoney(5000) the badge money %d is not between %d and %d" % (shown, base, base + 5000))
    m = line(money, "Text", "money")
    if m is None or 'label="$%s"' % format(base + 5000, ",") not in m:
        errors.append("AdjustMoney(5000): the badge money did not count up to $%s: %r" % (format(base + 5000, ","), m))
    led = line(money, "Ledger", "ledger")
    if led is None or "target=%d" % (base + 5000) not in led:
        errors.append("AdjustMoney(5000): the Ledger target is not %d: %r" % (base + 5000, led))
for k, p in shots.items():
    try:
        if not png_not_black(p):
            errors.append("the %s screenshot is (nearly) all black" % k)
    except OSError as e:
        errors.append("no %s screenshot: %s" % (k, e))
try:
    blobs = [open(shots[k], "rb").read() for k in ("messages", "factories", "inventory", "money")]
    if len(set(blobs)) != 4:
        errors.append("two of the ledger screenshots are equal")
except OSError:
    pass

if errors:
    print("FAIL\n" + "\n".join(errors))
    sys.exit(1)
print("Chocolatier 2 ledger OK")
