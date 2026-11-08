"""Chocolate check (docs/design/factories.md section 10): a scripted factory run produces goods, and they arrive in
inventory. Headless, against the real Reforged game.

How a factory is reached: a fresh Story game (the new-player flow of check_crates.py) has no owned factory and the port
buildings need quests, so a `lua` line stands in for buying one: `zur_factory:MarkOwned()`, the b01 ingredients set to 1000
each, and Player.rank = 1 so the first-miss tutorial pause is on (a fresh game starts at rank 2). Then the
Product:RunMinigame call and the ui_factory.lua MakeChocolates tail run from a `lua` line, exactly as the Make button does.

  1. The intro overlay is up (screenshot). Go; the countdown shows (screenshot at 1.5 s).
  2. At 4.3 s one deliberate click at (100, 590) is a miss: the game pauses (warnOnMiss), expect-dump paused=1; OK resumes.
  3. `autoshoot` plays the rest; a screenshot mid-run; expect-dump state=5 is the game-over fill; Done is clicked.
  4. expect-lua: Player.products.b01 rose by the score, and every need was consumed by need * score.
  5. The final detail line: score equals the GetScore() the script read, misses=1, at least one product was counted.
  6. The same seed gives the same score on a second run (--twice).
"""
import argparse, os, re, shutil, subprocess, sys

ap = argparse.ArgumentParser()
ap.add_argument("--openconche", required=True)
ap.add_argument("--assets", required=True)
ap.add_argument("--workdir", required=True)
ap.add_argument("--displays", default="scale1")
ap.add_argument("--game", choices=("chocolate", "coffee"), default="chocolate")
ap.add_argument("--twice", action="store_true", help="run the first display a second time and compare the score")
a = ap.parse_args()
shutil.rmtree(a.workdir, ignore_errors=True)
os.makedirs(a.workdir)
errors = []

COFFEE = a.game == "coffee"
CODE = "c01" if COFFEE else "b01"
WINDOW = "ui/coffee_factory.lua" if COFFEE else "ui/chocolate_factory.lua"
DETAIL = 'CoffeeFactory "FactoryGame"' if COFFEE else 'ChocolateFactory "FactoryGame"'
MIN_SCORE = 3 if COFFEE else 5

DISPLAYS = {"scale1": ("--scale", "1"), "scale2": ("--scale", "2"),
            "fullscreen": ("--fullscreen", "--display-size", "2560x1440")}

INTRO = ("expect-window ui/mainmenu.lua 60000\nwait 1300\nclick 310 345\nexpect-window ui/ui_entername.lua 5000\nwait 600\n"
         "type Tester\nkey Return\nexpect-window ui/ui_difficulty.lua 3000\nwait 800\nclick 328 400\nwait 5000\n"
         "click 400 346\nwait 1500\nclick 503 346\nwait 1500\n" + "click 400 346\nwait 1500\n" * 4)
SETUP = ("lua f = zur_factory; f:MarkOwned(); " + ('f:Equip("beverage"); ' if COFFEE else "") + "p = _AllProducts.%s; Player.rank = 1; " % CODE +
         "for k, n in pairs(p:GetNeeds()) do Player.ingredients[k] = 1000 end; gBefore = Player.products.%s or 0\n" % CODE +
         "lua gCheckIng = function() for k, n in pairs(p:GetNeeds()) do if Player.ingredients[k] ~= 1000 - n * gRun then "
         "return false end end return gRun > 0 end\n")
RUN = ("lua gRun = p:RunMinigame{factory = f}; if gRun >= 0 then f:SetProduction(p, gRun); TickSim(1) end\n"
       "expect-window %s 5000\nwait 500\nscreenshot @W@/intro.png\n" % WINDOW)
# Screen positions: the overlay popup is at (149, 141): Go (327, 398), the pause OK (399, 403), Done (471, 400).
# Coffee: after the time-out the hopper keeps turning its stock into cases, one a second, before the game ends.
PLAY = ("click 327 398\nwait 1500\nscreenshot @W@/countdown.png\nwait 2800\nclick 100 590\nexpect-dump 3000 paused=1\n"
        "screenshot @W@/paused.png\nclick 399 403\nwait 300\nautoshoot 62000\nwait 20000\nscreenshot @W@/play.png\n"
        "expect-dump @T@ state=5\ndump\nscreenshot @W@/gameover.png\nlua gScore = GetScore()\nclick 471 400\n"
        "expect-lua 3000 gRun == gScore and gRun >= 1 and Player.products.%s == gBefore + gRun\n" % CODE +
        "expect-lua 3000 gCheckIng()\nquit\n")
PLAY = PLAY.replace("@T@", "200000" if COFFEE else "60000")


def run(display):
    work = os.path.join(a.workdir, display)
    shutil.rmtree(os.path.join(work, "user"), ignore_errors=True)
    os.makedirs(work, exist_ok=True)
    script = os.path.join(work, "t.script")
    with open(script, "w") as f:
        f.write((INTRO + SETUP + RUN + PLAY).replace("@W@", work))
    unimpl = os.path.join(work, "unimplemented.txt")
    r = subprocess.run([a.openconche, "--headless", *DISPLAYS[display], "--assets", a.assets, "--userdir", os.path.join(work, "user"),
                        "--script", script, "--dump-windows", "--log-unimplemented", unimpl],
                       cwd=work, capture_output=True, text=True, timeout=900)
    out = r.stdout
    if r.returncode != 0 or "[lua-error]" in out or "FAIL" in out:
        errors.append("%s: exit %d, tail %r" % (display, r.returncode, out[-800:]))
        return None
    for p in ("expect-dump paused=1", "expect-dump state=5", "expect-lua gRun == gScore", "expect-lua gCheckIng()"):
        if "ok " + p not in out:
            errors.append("%s: missing %r" % (display, p))
    line = ""
    for l in out.splitlines():
        if l.strip().startswith(DETAIL) and "state=5" in l:
            line = l
    m = re.search(r"score=(\d+) bonus=(\d+) misses=(\d+)", line)
    if not m:
        errors.append("%s: no final state=5 detail line" % display)
        return None
    score, bonus, misses = map(int, m.groups())
    if misses != 1:
        errors.append("%s: expected exactly the one deliberate miss, got %d" % (display, misses))
    if score - bonus < 1:
        errors.append("%s: no product reached the belt end (score %d, bonus %d)" % (display, score, bonus))
    if COFFEE:
        d = re.search(r"delivered=(\d+)", line)
        if not d or int(d.group(1)) != score - bonus or int(d.group(1)) < 1:
            errors.append("%s: coffee: cases delivered %r, expected score - bonus = %d >= 1" % (display, d and d.group(1), score - bonus))
    if score < MIN_SCORE:
        errors.append("%s: a scripted run should make some goods, score %d" % (display, score))
    try:
        for l in open(unimpl):
            name = l.split("\t")[0]
            if not (name.startswith("SoundEvent") or name in ("ButtonSound", "StartMovie")):
                errors.append("%s: unexpected unimplemented stub %s" % (display, l.strip()))
    except OSError:
        pass
    print("%s: score %d (bonus %d), misses %d" % (display, score, bonus, misses))
    return score


first = None
for d in a.displays.split(","):
    s = run(d)
    if first is None:
        first = s
if a.twice and first is not None:
    again = run(a.displays.split(",")[0])
    if again != first:
        errors.append("the same seed gave scores %r and %r" % (first, again))

if errors:
    print("FAIL")
    for e in errors:
        print(" ", e)
    sys.exit(1)
print("factory ok")
