#!/usr/bin/env python3
"""Generates src/games/choc3/generated/Stubs_choc3.inc from the API tables in docs/re (script-host design section 6), and
Stubs_choc2.inc from docs/re/api-choc2.tsv, and Stubs_choc1.inc from
docs/re/api-choc1.tsv (docs/re/choc1-fit.md).

    python3 -I tools/gen_stubs.py            write all three files and print the cross-checks
    python3 -I tools/gen_stubs.py --check    exit 1 if a committed generated file is out of date
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ENGINE = ROOT
OUT = os.path.join(ENGINE, "src", "games", "choc3", "generated", "Stubs_choc3.inc")
TSVS = [os.path.join(ROOT, "docs", "re", "api-globals.tsv"), os.path.join(ROOT, "docs", "re", "api-windows.tsv")]
OUT_CHOC2 = os.path.join(ENGINE, "src", "games", "choc2", "generated", "Stubs_choc2.inc")
CHOC2_TSV = os.path.join(ROOT, "docs", "re", "api-choc2.tsv")
CHOC2_GLOBALS_TSV = os.path.join(ROOT, "docs", "re", "lua-globals-choc2.tsv")
# Read-and-never-set names in Chocolatier 2's scripts that are not engine natives: Lua's own library, the particle-script
# state (shared with Chocolatier 3, run in their own Lua state) and variables the scripts read before setting.
CHOC2_NOT_NATIVE = set("""Allocate Anim CreateParticles SetBlendMode SetNumParticles SetTexture Vec2 angel assert devAlpha dofile f2dRotation
    fAge fExpire fFade fGreater fRange fTimeScale finalInc font gAOLFlag gChocoSplash gSparkleTexture genButtonStyle ipairs kBlendNormal
    loadstring n null pColor pPosition pScale pUp pairs prodIcon require rollY setmetatable string table tonumber tostring type
    LedgerLabelFont gVersionString""".split())
OUT_CHOC1 = os.path.join(ENGINE, "src", "games", "choc1", "generated", "Stubs_choc1.inc")
CHOC1_TSV = os.path.join(ROOT, "docs", "re", "api-choc1.tsv")
REFORGED_TSV = os.path.join(ROOT, "docs", "re", "lua-globals-reforged.tsv")
LUA_FILES = [os.path.join(ENGINE, "lua", "prelude.lua"), os.path.join(ENGINE, "lua", "host.lua")]

# Rows whose source is a Lua chunk are not natives, and particle/sprite state functions stay out of the main state.
SKIP_SOURCE = re.compile(r"ScreenInit|gScreenInit|particles\.lua|animationscript")

# Natives that the original has but the TSVs do not list (design section 6, known gaps): (name, return kind).
EXTRA_STUBS = [("GetTimer", "number"), ("GetConfig", "nil"), ("IsEnabled", "bool"), ("StringToClipboard", "none")]

# Game window types registered by ChocoThreeMain (startup-and-assets 2.4) that are not DSL rows in the TSVs, plus the
# XSellWindow gap.
EXTRA_WINDOW_TYPES = ["ChocolateFactory", "CoffeeFactory", "CratesGame", "CreditsWindow", "CurveEditor", "DookieDropper",
                      "FireworksWindow", "MapWindow", "PortWindow", "RecipeCreator", "RecipeDesigner", "SignEditor",
                      "SlotMachine", "OptionsWindow", "HiscoreWindow", "LedgerWindow", "XSellWindow"]

KINDS = {"none": "None", "nil": "Nil", "string": "String", "number": "Number", "bool": "Bool", "table": "Table"}


def return_kind(text):
    t = text.strip().lower()
    if t in ("", "(none)", "nothing"):
        return "none"
    for word, kind in (("string", "string"), ("number", "number"), ("int", "number"), ("float", "number"),
                       ("bool", "bool"), ("table", "table")):
        if t.startswith(word):
            return kind
    return "nil"


def read_tsv(path):
    with open(path, encoding="utf-8") as f:
        lines = f.read().split("\n")
    header = lines[0].split("\t")
    src_col = next(i for i, h in enumerate(header) if h.startswith("source"))
    for line in lines[1:]:
        if not line.strip():
            continue
        c = line.split("\t")
        c += [""] * (len(header) - len(c))
        yield {"name": c[0], "kind": c[1], "args": c[2], "returns": c[3], "source": c[src_col]}


def lua_defined_names():
    names = set()
    for p in LUA_FILES:
        if not os.path.exists(p):
            continue
        for line in open(p, encoding="utf-8"):
            m = re.match(r"function\s+([A-Za-z_]\w*)\s*\(", line) or re.match(r"([A-Za-z_]\w*)\s*=", line)
            if m:
                names.add(m.group(1))
    return names


def generate():
    defined = lua_defined_names()
    stubs, types, api, seen = [], [], set(), set()
    for path in TSVS:
        for r in read_tsv(path):
            if r["kind"] == "function":
                api.add(r["name"])
            if r["kind"] == "constructor" and r["name"] not in types:
                types.append(r["name"])
            if r["kind"] != "function" or SKIP_SOURCE.search(r["source"]) or r["name"] in defined or r["name"] in seen:
                continue
            seen.add(r["name"])
            stubs.append((r["name"], return_kind(r["returns"]), r["args"].replace('"', "'")))
    for name, kind in EXTRA_STUBS:
        if name not in seen:
            seen.add(name)
            stubs.append((name, kind, ""))
        api.add(name)
    for t in EXTRA_WINDOW_TYPES:
        if t not in types:
            types.append(t)
    out = ["// Generated by tools/gen_stubs.py from docs/re/api-globals.tsv and api-windows.tsv. Do not edit.",
           "// STUB(name, return kind, argument text): a native the engine has not implemented yet.",
           "// WINDOW_TYPE(name): a window constructor registered through AddWindowType.",
           "// API_NAME(name): every function name in the tables, used to validate real native names."]
    for n, k, a in stubs:
        out.append('STUB("%s", Ret::%s, "%s")' % (n, KINDS[k], a))
    for t in types:
        out.append('WINDOW_TYPE("%s")' % t)
    for n in sorted(api | defined):
        out.append('API_NAME("%s")' % n)
    return "\n".join(out) + "\n", api | set(types), defined


def read_choc2_rows():
    """Rows of api-choc2.tsv as dicts (name kind args returns alias evidence note); every row must have 7 columns. Lines
    starting with # are comments, and a later row of the same name replaces the earlier one."""
    with open(CHOC2_TSV, encoding="utf-8") as f:
        lines = f.read().split("\n")
    header = lines[0].split("\t")
    assert header[:6] == ["name", "kind", "args", "returns", "alias", "evidence"], header
    rows = []
    where = {}  # name -> index in rows: a later row of the same name replaces the earlier one (an override block)
    for n, line in enumerate(lines[1:], 2):
        if not line.strip() or line.startswith("#"):
            continue
        c = line.split("\t")
        if len(c) < 6:
            sys.exit("%s:%d: expected at least 6 tab-separated columns, got %d" % (CHOC2_TSV, n, len(c)))
        c += [""] * (7 - len(c))
        row = dict(name=c[0], kind=c[1], args=c[2], returns=c[3], alias=c[4], evidence=c[5], note=c[6])
        if c[0] in where:
            print("note: %s:%d replaces the earlier row of %s" % (CHOC2_TSV, n, c[0]), file=sys.stderr)
            rows[where[c[0]]] = row
        else:
            where[c[0]] = len(rows)
            rows.append(row)
    return rows


def generate_choc2():
    """Stubs_choc2.inc: STUB / ALIAS / WINDOW_TYPE / GLOBAL_NUMBER / API_NAME rows. alias is a Chocolatier 3 native name
    (ALIAS), "@native" (hand-written in src/games/choc2/Choc2Natives.cpp: API_NAME only) or "-" (a logged STUB)."""
    out = ["// Generated by tools/gen_stubs.py from docs/re/api-choc2.tsv. Do not edit.",
           "// STUB(name, return kind, argument text): a Chocolatier 2 native the engine has not implemented yet.",
           "// ALIAS(name, target): bound as the Chocolatier 3 native `target` under `name` when that target is real.",
           "// WINDOW_TYPE(name): a window constructor registered through AddWindowType.",
           "// GLOBAL_NUMBER(name, value): an engine-set global.",
           "// API_NAME(name): every function name in the table."]
    names = set()
    for r in read_choc2_rows():
        n, kind = r["name"], r["kind"]
        names.add(n)
        if kind == "function":
            if r["alias"] == "-":
                out.append('STUB("%s", Ret::%s, "%s")' % (n, KINDS[return_kind(r["returns"])], r["args"].replace('"', "'")))
            elif r["alias"] != "@native":
                out.append('ALIAS("%s", "%s")' % (n, r["alias"]))
            out.append('API_NAME("%s")' % n)
        elif kind == "constructor":
            out.append('WINDOW_TYPE("%s")' % n)
        elif kind == "global":
            out.append('GLOBAL_NUMBER("%s", %s)' % (n, float(r["args"])))
        else:
            sys.exit("%s: unknown kind '%s' for %s" % (CHOC2_TSV, kind, n))
    return "\n".join(out) + "\n", names


def generate_choc1():
    """Stubs_choc1.inc from the `engine` column (12th) of api-choc1.tsv: sdk (bound by the SDK or Chocolatier 3 under the same
    name: API_NAME only), alias:<Choc3Name> (ALIAS), @native (hand-written in src/games/choc1: API_NAME only), - (a logged STUB
    for a function, a WINDOW_TYPE placeholder for a window class; a window class may also be @native: registered by hand) or skip (not a native: no row)."""
    with open(CHOC1_TSV, encoding="utf-8") as f:
        lines = f.read().split("\n")
    header = lines[0].split("\t")
    if len(header) != 12 or header[11] != "engine":
        sys.exit("%s: the 12th column must be 'engine'" % CHOC1_TSV)
    out = ["// Generated by tools/gen_stubs.py from docs/re/api-choc1.tsv. Do not edit.",
           "// STUB(name, return kind, argument text): a Chocolatier 1 native the engine has not implemented yet.",
           "// ALIAS(name, target): bound as the Chocolatier 3 native `target` under `name` when that target is real.",
           "// WINDOW_TYPE(name): a window constructor registered through AddWindowType (only if the name is still free).",
           "// API_NAME(name): every function name the table binds."]
    names = set()
    for n, line in enumerate(lines[1:], 2):
        if not line.strip():
            continue
        c = line.split("\t")
        if len(c) != 12:
            sys.exit("%s:%d: expected 12 tab-separated columns, got %d" % (CHOC1_TSV, n, len(c)))
        name, kind, args, returns, how = c[0], c[1], c[2], c[3], c[11]
        if how == "skip":
            continue
        if kind == "window-class":
            if how == "-":
                out.append('WINDOW_TYPE("%s")' % name)
            elif how not in ("sdk", "@native"):
                sys.exit("%s:%d: a window class is 'sdk', '@native', '-' or 'skip', not '%s'" % (CHOC1_TSV, n, how))
            continue
        if kind != "function":
            sys.exit("%s:%d: unknown kind '%s'" % (CHOC1_TSV, n, kind))
        names.add(name)
        if how == "-":
            out.append('STUB("%s", Ret::%s, "%s")' % (name, KINDS[return_kind(returns)], args.replace('"', "'")))
        elif how.startswith("alias:"):
            out.append('ALIAS("%s", "%s")' % (name, how[6:]))
        elif how not in ("sdk", "@native"):
            sys.exit("%s:%d: unknown engine value '%s'" % (CHOC1_TSV, n, how))
        out.append('API_NAME("%s")' % name)
    return "\n".join(out) + "\n"


def cross_check_choc2(known):
    if not os.path.exists(CHOC2_GLOBALS_TSV):
        return
    missing = []
    for line in open(CHOC2_GLOBALS_TSV, encoding="utf-8").read().split("\n")[1:]:
        name = line.split("\t")[0]
        if name and name not in known and name not in CHOC2_NOT_NATIVE:
            missing.append(name)
    print("Chocolatier 2 reads %d names that no table defines:" % len(missing))
    print("  " + " ".join(missing))


def cross_check(api, defined):
    if not os.path.exists(REFORGED_TSV):
        return
    known = api | defined
    missing = []
    for line in open(REFORGED_TSV, encoding="utf-8").read().split("\n")[1:]:
        name = line.split("\t")[0]
        if name and name[0].isupper() and name not in known:
            missing.append(name)
    print("Reforged uses %d capitalised names that neither the tables nor the Lua prelude/host define:" % len(missing))
    print("  " + " ".join(missing))


def main():
    text, api, defined = generate()
    text2, names2 = generate_choc2()
    text1 = generate_choc1()
    if "--check" in sys.argv:
        bad = 0
        for path, want in ((OUT, text), (OUT_CHOC2, text2), (OUT_CHOC1, text1)):
            current = open(path, encoding="utf-8").read() if os.path.exists(path) else ""
            if current != want:
                print("%s is out of date: run python3 -I tools/gen_stubs.py" % os.path.basename(path), file=sys.stderr)
                bad = 1
        if not bad:
            print("Stubs.inc, Stubs_choc2.inc and Stubs_choc1.inc are up to date")
        return bad
    for path, want in ((OUT, text), (OUT_CHOC2, text2), (OUT_CHOC1, text1)):
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            f.write(want)
    cross_check(api, defined)
    cross_check_choc2(api | defined | names2 | {t for t in api})
    return 0


sys.exit(main())
