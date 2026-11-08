"""Static check: every SoundEvent("...") literal in both Lua trees (the vanilla Lua in choco_assets and the Reforged assets),
and every port cadikey / music_key, is either in the engine's event table (EVENT and ALIAS rows of the generated
AudioEvents.inc, which is built from docs/re/audio-events.tsv) or on the documented-silent list below. Names found in
neither are printed and fail the run. Names that only the silent list covers are printed as information.

--lua DIR may be given more than once (default: the two trees next to this repository). A tree that has
cadi/cadilite.spec is a Macintosh tree and is checked on its own against its table (docs/design/mac-audio.md): the event blocks of its
cadi/cadilite.spec plus the native rows the choc3 module adds (direct files, group volumes, Pause/Unpause, Stop_Music, the
fireworks). Names in neither are listed in MAC_SILENT, which is what the Mac build never played; anything else fails."""
import argparse, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
ap = argparse.ArgumentParser()
ap.add_argument("--lua", action="append")
a = ap.parse_args()
alldirs = a.lua or [os.path.join(ROOT, "choco_assets"), os.path.join(ROOT, "Chocolatier-Decadence-Reforged", "assets")]
mac_dirs = [d for d in alldirs if os.path.isfile(os.path.join(d, "cadi", "cadilite.spec"))]
dirs = [d for d in alldirs if d not in mac_dirs]

# Documented silent: the original matches nothing for these (docs/re/audio.md s.10.8, s.1), so the engine plays nothing.
SILENT = {
    "saloons": "no CADI object (AU s.10.8)",
    "train_station": "no CADI object (AU s.10.8)",
    "cadi/ui_click.ogg": "a file path given to the CADI path in 12 Reforged calls; matches nothing (AU s.1)",
    "gobi_desert_sting": "vanilla spelling; the clip is gobidesert_sting (AU s.10.8)",
    "falkland_islands_sting": "vanilla spelling; the clip is falklands_sting (AU s.10.8)",
}
SILENT_PREFIX = (("port_music", "port_music1..3 are stem-clip cues, not EVENT/ALIAS rows (AU s.10.4; PortWindow::changeMusic sends them)"),)

inc = open(os.path.join(ROOT, "src", "games", "choc3", "audio", "generated", "AudioEvents.inc"), encoding="utf-8").read()
table = set(re.findall(r'^(?:EVENT|ALIAS)\("([^"]*)"', inc, re.M))
if len(table) < 100:
    print("FAIL: the event table has only %d names; is AudioEvents.inc current?" % len(table))
    sys.exit(1)


def known(name):
    return name in table or name.startswith("firework")  # fireworkN: the direct path, sfx/<name>.ogg


def silent_reason(name):
    if name in SILENT:
        return SILENT[name]
    for p, why in SILENT_PREFIX:
        if name.startswith(p):
            return why
    return None


call = re.compile(r"SoundEvent\s*\(\s*([\"'])((?:(?!\1).)*)\1")
create = re.compile(r"\bCreatePort\s*\(\s*([\"'])((?:(?!\1).)*)\1")
key = re.compile(r"\b(cadikey|music_key)\s*=\s*([\"'])((?:(?!\2).)*)\2")
def scan(dirs):
    used = {}  # name -> (where, kind)
    files = 0
    for d in dirs:
        if not os.path.isdir(d):
            print("note: %s not found, skipped" % d)
            continue
        for dp, _, fns in os.walk(d):
            for fn in fns:
                if not fn.lower().endswith(".lua"):
                    continue
                files += 1
                path = os.path.join(dp, fn)
                rel = os.path.relpath(path, ROOT)
                is_port = "/ports/" in rel.replace("\\", "/")
                created, keyed = [], False  # a port's cadikey defaults to its name (sim/port.lua) unless its file sets one
                for line in open(path, encoding="utf-8", errors="replace"):
                    for m in call.finditer(line):
                        if "--" not in line[:m.start()]:  # skip commented-out calls
                            used.setdefault(m.group(2), (rel, "SoundEvent"))
                    for m in create.finditer(line):
                        if "--" not in line[:m.start()]:
                            created.append(m.group(2))
                    for m in key.finditer(line):
                        if "--" in line[:m.start()]:
                            continue
                        name = m.group(3)
                        keyed = True
                        used.setdefault(name, (rel, m.group(1)))
                        if m.group(1) == "cadikey" and is_port:  # a port's music event is <cadikey>_sting (Port:EnterPort)
                            used.setdefault(name + "_sting", (rel, "cadikey + _sting"))
                if not keyed:
                    for name in created:
                        used.setdefault(name, (rel, "CreatePort"))
                        used.setdefault(name + "_sting", (rel, "CreatePort + _sting"))
    if not used:
        print("FAIL: no SoundEvent literals found in %s" % dirs)
        sys.exit(1)
    return used, files


def check(used, files, is_known, silent_reason, label):
    """Prints the verdict for one tree; returns the number of unknown names."""
    unknown, silent = [], []
    for name in sorted(used):
        if is_known(name):
            continue
        why = silent_reason(name)
        (silent if why else unknown).append((name, used[name], why))
    print("%schecked %d names from %d Lua files; %d in the table, %d documented silent, %d unknown"
          % (label, len(used), files, len(used) - len(silent) - len(unknown), len(silent), len(unknown)))
    for name, (where, kind), why in silent:
        print("  silent  %-28s %s (%s): %s" % (name, where, kind, why))
    for name, (where, kind), _ in unknown:
        print("  UNKNOWN %-28s %s (%s)" % (name, where, kind))
    return len(unknown)


used, files = scan(dirs)
bad = check(used, files, known, silent_reason, "")

# The Macintosh tree against its own table: the spec's events plus the native rows.
MAC_SILENT = {  # the Mac build matches nothing for these (docs/design/mac-audio.md s.3), so it plays nothing
    "machine_spin": "no spec event, although machine_spin.ogg ships",
    "Stop_travel_plane": "no spec event; travel_plane is a one-shot that ends by itself",
    "casino_ambience": "no spec event",
    "test_kitchen": "no spec event",
    "saloons": "no spec event",
    "train_station": "no spec event",
    "plantation": "no spec event",
    "factory": "no spec event",
    "shop": "no spec event",
}
for mac_dir in mac_dirs:
    spec = os.path.join(mac_dir, "cadi", "cadilite.spec")
    mac_table = set(re.findall(r'^:"([^"]*)"', open(spec, encoding="utf-8", errors="replace").read(), re.M))
    for name, kind_, group_ in re.findall(r'^EVENT\("([^"]*)", Kind::(\w+), Group::(\w+)', inc, re.M):
        if kind_ in ("Direct", "Volume", "Builtin") or (kind_ == "Silent" and group_ == "Direct"):
            mac_table.add(name)
    mac_table.add("Stop_Music")  # hard-coded on the Mac
    mused, mfiles = scan([mac_dir])
    bad += check(mused, mfiles, lambda n: n in mac_table or n.startswith("firework"), MAC_SILENT.get, "mac: ")
if bad:
    print("FAIL: %d name(s) are in neither an event table nor a documented-silent list" % bad)
    sys.exit(1)
print("audio names ok")
