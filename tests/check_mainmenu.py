"""The real Reforged Main reaches DoMainWindow("ui/mainmenu.lua") headless.

Runs `expect-window ui/mainmenu.lua 60000` then `quit` twice and asserts: exit 0, no [lua-error], under 5 s wall time,
the [debug] LOAD counts are non-zero and identical across the two runs, the window tree dump shows the menu, and the
unimplemented file was written. A second scenario checks that expect-window on a window that never appears fails the
run with exit 1."""
import argparse, os, re, subprocess, sys, time

ap = argparse.ArgumentParser()
ap.add_argument("--openconche", required=True)
ap.add_argument("--assets", required=True)
ap.add_argument("--workdir", required=True)
ap.add_argument("--test-assets", required=True)
a = ap.parse_args()
os.makedirs(a.workdir, exist_ok=True)
script = os.path.join(a.workdir, "mainmenu.script")
with open(script, "w") as f:
    f.write("expect-window ui/mainmenu.lua 60000\nquit\n")
unimpl = os.path.join(a.workdir, "unimplemented.txt")


def run(script_path, *extra):
    if os.path.exists(unimpl):
        os.remove(unimpl)
    t = time.time()
    # cheatmode=1 turns on Reforged's DebugOut, which is where the [LOAD] counts come from.
    r = subprocess.run([a.openconche, "--headless", "--assets", a.assets, "--userdir", os.path.join(a.workdir, "user"),
                        "--config", "cheatmode=1", "--script", script_path, "--dump-windows",
                        "--log-unimplemented", unimpl, *extra],
                       cwd=a.workdir, capture_output=True, text=True, timeout=120)
    return r, time.time() - t


errors = []
counts = []
for n in (1, 2):
    r, wall = run(script)
    if r.returncode != 0:
        errors.append("run %d: exit code %d, stderr %r, tail %r" % (n, r.returncode, r.stderr[-300:], r.stdout[-500:]))
    if "[lua-error]" in r.stdout:
        errors.append("run %d: [lua-error] in the log: %s" % (n, [l for l in r.stdout.splitlines() if "[lua-error]" in l][:3]))
    if "[engine-error]" in r.stdout or "[native-error]" in r.stdout:
        errors.append("run %d: engine or native error in the log" % n)
    if not re.search(r"\[script\] ok expect-window ui/mainmenu\.lua", r.stdout):
        errors.append("run %d: expect-window did not succeed:\n%s" % (n, r.stdout[-600:]))
    if wall >= 5:
        errors.append("run %d: took %.1f s, limit is 5 s" % (n, wall))
    loads = dict((k, int(v)) for v, k in re.findall(r"\[LOAD\] Loaded (\d+) (\w+)", r.stdout))
    for k in ("ingredients", "products", "ports", "quests"):
        if loads.get(k, 0) <= 0:
            errors.append("run %d: no non-zero [LOAD] Loaded ... %s line (got %r)" % (n, k, loads))
    counts.append(loads)
    if not re.search(r'^\s+Modal "mainmenu" id=\d+ rect=0,0,800,600 script="ui/mainmenu\.lua"$', r.stdout, re.M):
        errors.append("run %d: --dump-windows does not show the main menu modal:\n%s" % (n, r.stdout[-800:]))
    if not re.search(r'Button "free_play"', r.stdout):
        errors.append("run %d: the window dump has no free_play button" % n)
    if not os.path.exists(unimpl) or not open(unimpl).read().strip():
        errors.append("run %d: the unimplemented file was not written or is empty" % n)
if len(counts) == 2 and counts[0] != counts[1]:
    errors.append("[LOAD] counts differ between runs: %r vs %r" % (counts[0], counts[1]))

# A window that never appears fails the run.
bad = os.path.join(a.workdir, "missing.script")
with open(bad, "w") as f:
    f.write("expect-window no_such_window 300\nquit\n")
r, _ = run(bad)
if r.returncode != 1 or "[script] FAIL expect-window no_such_window" not in r.stdout:
    errors.append("a missing window should exit 1 with a FAIL line, got exit %d: %r" % (r.returncode, r.stdout[-300:]))

# A pending expect-window fails the run when the app stops for another reason, and without a script host.
def run_plain(assets, text, *extra):
    p = os.path.join(a.workdir, "plain.script")
    with open(p, "w") as f:
        f.write(text)
    return subprocess.run([a.openconche, "--headless", "--assets", assets, "--userdir", os.path.join(a.workdir, "user"),
                           "--script", p, *extra], cwd=a.workdir, capture_output=True, text=True, timeout=60)


for scenario, why in (("mainret", "Main returning"), ("quit", "kQuitNow")):
    r = run_plain(os.path.join(a.test_assets, "ui", scenario), "expect-window never_appears 60000\nquit\n")
    if r.returncode != 1 or "[script] FAIL expect-window never_appears" not in r.stdout:
        errors.append("%s with a pending expect-window should exit 1 with FAIL, got %d: %r" % (why, r.returncode, r.stdout[-300:]))
r = run_plain(os.path.join(a.test_assets, "gfx"), "expect-window anything\nquit\n")
if r.returncode != 1 or "[script] FAIL expect-window anything" not in r.stdout:
    errors.append("expect-window without a script host should fail, got %d: %r" % (r.returncode, r.stdout[-300:]))
r = run_plain(os.path.join(a.test_assets, "ui", "swaperr"), "expect-window ui/first.lua 5000\nquit\n")
if r.returncode != 1 or "Lua error" not in r.stdout:
    errors.append("a modal whose script errored should fail expect-window, got %d: %r" % (r.returncode, r.stdout[-300:]))

if errors:
    print("FAIL:\n  " + "\n  ".join(errors))
    sys.exit(1)
print("ok: LOAD counts %r, identical across runs" % counts[0])
