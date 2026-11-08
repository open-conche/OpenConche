"""Users persist across runs of the real Reforged game.

1. Story Mode with no users opens ui/ui_entername.lua; Escape cancels it (the dialog's cancel button, even with the
   TextEdit focused) and leaves no user behind.
2. The real first-time flow: Story Mode, type a name, Enter, pick the default difficulty, Done. CreateNewUser makes the
   user, the story starts, and users.txt is written.
3. A later run of the real game on the same --userdir shows "Welcome back, Tester!" in the window dump, with the
   Change Player and Free Play buttons enabled; a further run still shows it (nothing was reset)."""
import argparse, os, re, shutil, subprocess, sys

ap = argparse.ArgumentParser()
ap.add_argument("--openconche", required=True)
ap.add_argument("--assets", required=True)
ap.add_argument("--workdir", required=True)
a = ap.parse_args()
shutil.rmtree(a.workdir, ignore_errors=True)
os.makedirs(a.workdir)
user = os.path.join(a.workdir, "user")
errors = []
START = "expect-window ui/mainmenu.lua 60000\nwait 1300\n"  # the menu's Update has run by then
STORY = "click 310 345\nexpect-window ui/ui_entername.lua 5000\nwait 600\n"


def run(script_text):
    path = os.path.join(a.workdir, "t%d.script" % run.n)
    run.n += 1
    with open(path, "w") as f:
        f.write(script_text)
    return subprocess.run([a.openconche, "--headless", "--assets", a.assets, "--userdir", user, "--script", path,
                           "--dump-windows"], cwd=a.workdir, capture_output=True, text=True, timeout=120)


run.n = 0


def check_clean(tag, r):
    if r.returncode != 0 or "[lua-error]" in r.stdout or "FAIL" in r.stdout:
        errors.append("%s: exit %d, tail %r" % (tag, r.returncode, r.stdout[-500:]))


# 1. No users: Story Mode asks for a name; Escape cancels it and leaves nothing.
r = run(START + STORY + "key Escape\nwait 1500\nquit\n")
check_clean("run 1", r)
if os.path.exists(os.path.join(user, "users.txt")):
    errors.append("run 1: a user exists although no name was entered")
m = re.search(r"modal depth (\d+)", r.stdout)
if not m or m.group(1) != "2":
    errors.append("run 1: the name dialog is still open after Escape")
# (the opening FadeIn re-shows the welcome Text after Update hid it, as in the original; it just has no label)
if not re.search(r'Text "welcome" id=\d+ rect=\S+ (hidden )?label=""', r.stdout):
    errors.append("run 1: the welcome text should be empty with no users")

# 2. The whole first-time flow through the real dialogs (Done on the difficulty dialog is at 328,400).
r = run(START + STORY + "type Tester\nkey Return\nexpect-window ui/ui_difficulty.lua 3000\nwait 800\n"
        "click 328 400\nwait 3000\nquit\n")
check_clean("run 2", r)
try:
    text = open(os.path.join(user, "users.txt")).read()
except OSError:
    text = ""
    errors.append("run 2: users.txt was not written")
if "Tester" not in text:
    errors.append("run 2: users.txt has no Tester: %r" % text)

# 3 and 4. The real game greets the saved player, twice.
for n in (3, 4):
    r = run(START + "quit\n")
    check_clean("run %d" % n, r)
    if 'label="Welcome back, Tester!"' not in r.stdout:
        errors.append("run %d: no 'Welcome back, Tester!' in the dump" % n)
    if re.search(r'Button "(choose_player|free_play)" id=\d+ rect=\S+ hidden', r.stdout):
        errors.append("run %d: Change Player or Free Play is still hidden" % n)

if errors:
    print("FAIL:\n  " + "\n  ".join(errors))
    sys.exit(1)
print("ok: the user created through the name dialog is greeted by name in the next run")
