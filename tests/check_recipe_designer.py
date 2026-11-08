"""The RecipeDesigner check (docs/design/factories.md; RecipeDesigner, BTSetTint, SetRectangleColor), played headless against the real
Reforged game.

The new-player flow of check_crates.py, then the Test Kitchen is opened with `lua DisplayDialog {"ui/ui_kitchen.lua"}`
(sim/kitchen.lua opens it the same way from the building; the building needs a quest, so the Lua command stands in), and
the design pane is entered with the global DesignMode(), which is what the Design button's command calls (the button is
disabled until a full recipe has been tasted, and that part of the kitchen is not what this slice is about).

  1. DesignMode() runs SetRecipeType("bar") and RandomDesign(): after 3 s the first design is on screen (screenshot) and
     the four layers hold bar pictures; layer 1 never has the blank option.
  2. The Lua keeps its own index per layer (starting at 0; RandomDesign does not update it, as in the original), so the
     arrows count from 0: four right arrows on layer 1 (4 entries) wrap to bar/layer1_01, one left arrow wraps at -1 to
     bar/layer1_04, and two on layer 2 give bar/layer2_03. Screenshot.
  3. Randomize: a screenshot during the 2 s animation and one after. In the dump the displayed pictures are the chosen
     ones, the display and option windows of each layer carry the same tint (BTSetTint through SetRecipeTint), and the
     tint is not white for at least one layer (the random roll tinted it); highlight windows stay white.
"""
import argparse, os, re, shutil, subprocess, sys

ap = argparse.ArgumentParser()
ap.add_argument("--openconche", required=True)
ap.add_argument("--assets", required=True)
ap.add_argument("--workdir", required=True)
ap.add_argument("--displays", default="scale1")
a = ap.parse_args()
shutil.rmtree(a.workdir, ignore_errors=True)
os.makedirs(a.workdir)
errors = []

DISPLAYS = {"scale1": ("--scale", "1"), "fullscreen": ("--fullscreen", "--display-size", "2560x1440")}

INTRO = ("expect-window ui/mainmenu.lua 60000\nwait 1300\nclick 310 345\nexpect-window ui/ui_entername.lua 5000\nwait 600\n"
         "type Tester\nkey Return\nexpect-window ui/ui_difficulty.lua 3000\nwait 800\nclick 328 400\nwait 5000\n"
         "click 400 346\nwait 1500\nclick 503 346\nwait 1500\n" + "click 400 346\nwait 1500\n" * 4)
SETUP = "lua f = zur_factory; f:MarkOwned()\n"
OPEN = 'lua DisplayDialog {"ui/ui_kitchen.lua"}\nexpect-window ui/ui_kitchen.lua 3000\nwait 800\nscreenshot @W@/create.png\n'
# Screen positions (the design pane): layer 1 arrows (100, 108) and (235, 108), layer 2 right arrow (235, 233), Randomize (400, 298).
# The closed drawer tabs give one picture only; hovering the Fruits tab (on screen x 614..723, y 345..385) must keep it on screen.
HOVER = ("dump\nmove 660 365\nwait 300\nscreenshot @W@/drawer_hover.png\ndump\nmove 400 100\nwait 200\n")
DESIGN = (
    "lua DesignMode()\nwait 3000\nscreenshot @W@/design.png\n"
    'expect-lua 1000 string.find(GetLayerImage(0), "^bar/layer1_0[1-4]$") ~= nil\n'
    + "click 235 108\nwait 200\n" * 4 +
    # The Lua keeps its own index per layer (design[n], starting at 0) and RandomDesign does not update it, so the
    # arrows count from 0 whatever the random design chose: four steps wrap the 4-entry list back to the first picture.
    'expect-lua 1000 GetLayerImage(0) == "bar/layer1_01"\n'
    "click 100 108\nwait 200\n"
    'expect-lua 1000 GetLayerImage(0) == "bar/layer1_04"\n'  # the wrap at -1
    "click 235 233\nwait 200\nclick 235 233\nwait 200\n"
    'expect-lua 1000 GetLayerImage(1) == "bar/layer2_03"\n'
    "screenshot @W@/arrows.png\n"
    "click 400 298\nwait 700\nscreenshot @W@/randomizing.png\nwait 2500\nscreenshot @W@/randomized.png\n"
    'expect-lua 1000 string.find(GetLayerImage(0), "^bar/layer1_0[1-4]$") ~= nil\n'
    "dump\nquit\n")


def run(display):
    work = os.path.join(a.workdir, display)
    os.makedirs(work, exist_ok=True)
    script = os.path.join(work, "t.script")
    with open(script, "w") as f:
        f.write((INTRO + SETUP + OPEN + HOVER + DESIGN).replace("@W@", work))
    unimpl = os.path.join(work, "unimplemented.txt")
    r = subprocess.run([a.openconche, "--headless", *DISPLAYS[display], "--assets", a.assets, "--userdir", os.path.join(work, "user"),
                        "--script", script, "--dump-windows", "--log-unimplemented", unimpl],
                       cwd=work, capture_output=True, text=True, timeout=600)
    out = r.stdout
    if r.returncode != 0 or "[lua-error]" in out or "FAIL" in out:
        errors.append("%s: exit %d, tail %r" % (display, r.returncode, out[-800:]))
        return
    # Two dumps of the kitchen bracket the hover: the Fruits tab (page 1) must be shown with its picture both times.
    tabs = re.findall(r'Button "" id=\d+ rect=605,320,110,41 label="Fruits" type=push state=(\d) layer=\d+ command\n'
                      r'\s*Text "label"[^\n]*\n\s*Bitmap "image0"[^\n]*?(hidden )?image="image/kitchen_drawer_closed"', out)
    if len(tabs) < 2:
        errors.append("%s: expected the Fruits tab in two dumps, got %d" % (display, len(tabs)))
    else:
        if tabs[0] != ("0", "") or tabs[1] != ("2", ""):
            errors.append("%s: Fruits tab before/after hover (state, hidden) = %r, want ('0', '') then ('2', '')" % (display, tabs[:2]))
    for name in ("drawer_hover", "design", "arrows", "randomizing", "randomized"):
        if not os.path.exists(os.path.join(work, name + ".png")):
            errors.append("%s: no %s.png" % (display, name))
    tints = {}
    images = {}
    for l in out.splitlines():
        m = re.match(r'\s*(Bitmap|BitmapTint) "((?:option|display)_(?:layer|highlight)\d)" .*?(hidden )?image="([^"]*)"(?: tint=([\d.,]+))?', l)
        if m:
            tints[m.group(2)] = m.group(5)
            images[m.group(2)] = (m.group(4), bool(m.group(3)))
    if len(tints) != 16:
        errors.append("%s: expected 16 designer windows in the dump, got %d" % (display, len(tints)))
        return
    tinted = 0
    for n in range(4):
        if tints["display_layer%d" % n] != tints["option_layer%d" % n]:
            errors.append("%s: layer %d display tint %s differs from option tint %s" % (display, n, tints["display_layer%d" % n], tints["option_layer%d" % n]))
        if tints["display_layer%d" % n] != "1.00,1.00,1.00":
            tinted += 1
        if tints["display_highlight%d" % n] != "1.00,1.00,1.00":
            errors.append("%s: highlight %d was tinted" % (display, n))
        for kind in ("option", "display"):
            img, hidden = images["%s_layer%d" % (kind, n)]
            if not hidden and not re.fullmatch(r"custom/bar/layer%d_\d\d" % (n + 1), img):
                errors.append("%s: %s_layer%d shows %r" % (display, kind, n, img))
        if images["display_layer%d" % n][1] != images["option_layer%d" % n][1]:
            errors.append("%s: layer %d is hidden in one window only" % (display, n))
    if images["display_layer0"][1]:
        errors.append("%s: layer 1 has no blank option, but is hidden" % display)
    if tinted == 0:
        errors.append("%s: no layer was tinted by the random design" % display)
    try:
        for l in open(unimpl):
            name = l.split("\t")[0]
            if not (name.startswith("SoundEvent") or name in ("ButtonSound", "StartMovie")):
                errors.append("%s: unexpected unimplemented native %s" % (display, l.strip()))
    except OSError:
        pass
    print("%s: tinted layers %d, images %s" % (display, tinted, [images["display_layer%d" % n] for n in range(4)]))


for d in a.displays.split(","):
    run(d)
if errors:
    print("FAIL")
    for e in errors:
        print(" ", e)
    sys.exit(1)
print("recipe designer ok")
