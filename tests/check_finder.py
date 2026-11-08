#!/usr/bin/env python3
"""CTest `finder`: the game finder's folder resolution without the GUI screen
(docs/design/game-finder.md section 4). --pick-game/--pick-dir run the same validate-and-resolve logic the finder
window uses after the OS folder dialog, so this pins the flow end to end against the real game trees (read only):
the install roots a user actually picks, each variant's acceptance and refusal, and the exit codes.
"""
import argparse
import os
import shutil
import subprocess
import sys

fails = []


def run(openconche, args, workdir, expect_rc, expect_lines, name):
    cmd = [openconche] + args + ["--userdir", os.path.join(workdir, "user-" + name)]
    r = subprocess.run(cmd, capture_output=True, text=True)
    out = r.stdout + r.stderr
    ok = r.returncode == expect_rc
    for line in expect_lines:
        if line not in out:
            ok = False
            fails.append("%s: expected line not in output: %r" % (name, line))
    if not ok:
        print("FAIL %s (exit %d, expected %d)" % (name, r.returncode, expect_rc))
        print(out[-2000:])
    else:
        print("ok   %s" % name)
    return ok


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--openconche", required=True)
    ap.add_argument("--workdir", required=True)
    ap.add_argument("roots", nargs="+", help="NAME=path pairs of the games' install roots")
    args = ap.parse_args()
    roots = {}
    for kv in args.roots:
        k, _, v = kv.partition("=")
        if not os.path.isdir(v):
            sys.exit("root %s=%s is not a folder" % (k, v))
        roots[k] = v
    if os.path.isdir(args.workdir):
        shutil.rmtree(args.workdir)
    os.makedirs(args.workdir)

    run(args.openconche, ["--pick-game", "choc3", "--pick-dir", roots["VANILLA3"]], args.workdir, 0,
        ["[finder] variant choc3, assets"], "vanilla3 under choc3")
    run(args.openconche, ["--pick-game", "choc3-reforged", "--pick-dir", roots["REFORGED"]], args.workdir, 0,
        ["[finder] variant choc3-reforged, assets"], "reforged checkout under choc3-reforged")
    # The community marker is the split between the two Chocolatier 3 buttons.
    r = subprocess.run([args.openconche, "--pick-game", "choc3-reforged", "--pick-dir", roots["VANILLA3"],
                        "--userdir", os.path.join(args.workdir, "user-mark")], capture_output=True, text=True)
    if r.returncode == 3 and "Reforged" in r.stdout + r.stderr:
        print("ok   vanilla under choc3-reforged refused (the marker message)")
    else:
        fails.append("vanilla under choc3-reforged: exit %d, output: %s" % (r.returncode, r.stdout[-800:]))
    if "CHOC1" in roots:
        run(args.openconche, ["--pick-game", "choc1", "--pick-dir", roots["CHOC1"]], args.workdir, 0,
            ["[finder] variant choc1, assets"], "choc1 install root")
        run(args.openconche, ["--pick-game", "choc1", "--pick-dir", roots["VANILLA3"]], args.workdir, 3,
            ["[finder] "], "choc1 refused on a Chocolatier 3 folder")
    if "CHOC2" in roots:
        run(args.openconche, ["--pick-game", "choc2", "--pick-dir", roots["CHOC2"]], args.workdir, 0,
            ["[finder] variant choc2, assets"], "choc2 install root")
    run(args.openconche, ["--pick-game", "choc2", "--pick-dir", "/no/such/folder"], args.workdir, 3,
        ["[finder] no playable folder"], "a missing folder")
    r = subprocess.run([args.openconche, "--pick-game", "dd2", "--userdir", os.path.join(args.workdir, "user-bad")],
                       capture_output=True, text=True)
    if r.returncode != 2:
        fails.append("an unknown variant must be a usage error (exit 2), got %d: %s" % (r.returncode, r.stdout))

    # An automated run never reads the machine's library and never opens the launcher (README's promise): with a
    # planted games.cfg naming a playable game, a headless run without --assets still shows the test scene, and
    # --finder refuses with exit 1 (a failure, not a quiet Exit). The home is redirected so nothing real is read.
    home = os.path.join(args.workdir, "home")
    cfg = "last = choc3\nchoc3.dir = %s\nchoc3.assets = %s\n" % (roots["VANILLA3"],
                                                                 os.path.join(roots["VANILLA3"], "assets"))
    for sub in (os.path.join("Library", "Application Support", "OpenConche"),  # macOS
                os.path.join("xdg", "openconche"),                               # Linux (XDG_DATA_HOME)
                os.path.join("appdata", "OpenConche")):                          # Windows (APPDATA)
        os.makedirs(os.path.join(home, sub), exist_ok=True)
        with open(os.path.join(home, sub, "games.cfg"), "w") as f:
            f.write(cfg)
    env = dict(os.environ, HOME=home, XDG_DATA_HOME=os.path.join(home, "xdg"), APPDATA=os.path.join(home, "appdata"))
    r = subprocess.run([args.openconche, "--headless", "--frames", "1",
                        "--userdir", os.path.join(args.workdir, "user-planted")],
                       capture_output=True, text=True, env=env)
    out = r.stdout + r.stderr
    if r.returncode == 0 and "no game assets found" in out and "[finder] saved choice" not in out:
        print("ok   a planted library is ignored by an automated run")
    else:
        fails.append("planted library: exit %d, output: %s" % (r.returncode, out[-800:]))
    r = subprocess.run([args.openconche, "--finder", "--headless", "--frames", "1",
                        "--userdir", os.path.join(args.workdir, "user-finder-headless")],
                       capture_output=True, text=True, env=env)
    out = r.stdout + r.stderr
    if r.returncode == 1 and "the finder needs a display" in out:
        print("ok   --finder under an automated run is a refusal with exit 1")
    else:
        fails.append("--finder --headless: exit %d (expected 1), output: %s" % (r.returncode, out[-800:]))

    if fails:
        print("\n".join(fails))
        sys.exit(1)
    print("finder: all cases passed")
    return 0


sys.exit(main())
