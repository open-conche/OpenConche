#!/usr/bin/env python3
"""Enforces the layering rules of docs/design/engine-and-games.md section 2. Run: python3 -I tools/check_layering.py [--root <repo root>]

Rules (on #include "..." lines under src/ and tests/):
  - engine directories (platform gfx audio assets ui sdk script) include nothing under games/;
  - app/ includes from games/ only games/Registry.h and games/choc3/ImportCommand.h;
  - games/common includes no games/choc*;
  - games/choc3 includes no games/choc2 or games/choc1;
  - games/choc2 and games/choc1 include no other module than choc3 (and not each other);
  - games/greatcc includes no other module (only games/common, the SDK and the engine);
  - nothing includes the old paths game/ or script/games/;
  - the text of the old Game enum values occurs nowhere in the engine tree;
  - the code of the engine directories (.h, .cpp, .inc, `//` and `/* */` comments stripped) holds none of BANNED_TOKENS,
    which are Chocolatier knowledge (docs/design/engine-and-games.md section 8);
  - no `games::` in engine code, so a forward declaration cannot reach a module without an include;
  - no `generated/` directory under an engine directory (generated game tables live under games/), except GENERATED_ALLOWED.
Exit status 1 when anything is violated.
"""
import argparse
import os
import re
import sys

INCLUDE = re.compile(r'^\s*#\s*include\s*"([^"]+)"')
ENGINE_DIRS = {"platform", "gfx", "audio", "assets", "ui", "sdk", "script"}
APP_ALLOWED = {"games/Registry.h", "games/choc3/ImportCommand.h"}
TEXT_EXT = {".h", ".cpp", ".inc", ".cmake", ".txt", ".py", ".md", ".tsv", ".lua"}
CODE_EXT = {".h", ".cpp", ".inc"}
# Tokens that name Chocolatier content and so must not appear in engine code. Each slice of the core cleanup adds its own.
BANNED_TOKENS = ["1946", "date_format", "month_", "ingredients.xml", "products.xml", "ports.xml", "quests.xml",
                 "CreateIngredient", "CreateProductFromXML", "knownRecipes", "AudioEvents.inc", "EventTable::builtin",
                 "ui_click", "firework", "Chocolatier3", "setMusicState", "C2Mode", "gameplay", "minigame",
                 "factory_wait", "sfx/sfx_typing"]
# Generated directories still allowed under an engine directory (paths relative to src/). None is currently allowed: the finder
# screen is fully painted at runtime, and the binary ships with no committed or generated data file.
GENERATED_ALLOWED: set[str] = set()
COMMENTS = re.compile(r'//[^\n]*|/\*.*?\*/', re.S)
BANNED_TEXT = "Game" + "::" + "Choc"  # spelled in two pieces so this file does not trip itself


def module_of(path):
    """The first path segments under src/ that name a layer: 'games/choc2', 'games/common', 'app', ..."""
    parts = path.split("/")
    if parts[0] == "games" and len(parts) > 2:
        return "games/" + parts[1]
    return parts[0]


def check_include(rel, inc):
    """rel: the including file relative to src/ ('' when under tests/); inc: the quoted include. Returns a message or None."""
    if inc.startswith("game/") or inc.startswith("script/games/"):
        return "includes old path '%s'" % inc
    if not inc.startswith("games/"):
        return None
    src = module_of(rel) if rel else "tests"
    dst = module_of(inc)
    if src in ENGINE_DIRS:
        return "engine code includes '%s'" % inc
    if src == "app":
        return None if inc in APP_ALLOWED else "app/ includes '%s' (allowed: %s)" % (inc, ", ".join(sorted(APP_ALLOWED)))
    if src == "games/common" and (dst.startswith("games/choc") or dst == "games/greatcc"):
        return "games/common includes '%s'" % inc
    if src == "games/choc3" and dst in ("games/choc2", "games/choc1"):
        return "games/choc3 includes '%s'" % inc
    if src == "games/greatcc" and dst.startswith("games/") and dst not in ("games/greatcc", "games/common"):
        return "games/greatcc includes '%s'" % inc
    if dst == "games/greatcc" and src not in ("games/greatcc", "tests", "games"):  # "games" = Registry.cpp
        return "%s includes '%s'" % (src, inc)
    others = ("games/choc2", "games/choc1")
    if src in others and dst in others and dst != src:
        return "%s includes '%s'" % (src, inc)
    return None


def strip_comments(text):
    """Blanks `//` and `/* */` comments, keeping newlines so line numbers hold. Not string-aware: a `//` inside a string
    literal (a URL) drops the rest of that line, which can only hide a token, never invent one."""
    return COMMENTS.sub(lambda m: "\n" * m.group(0).count("\n"), text)


def check_engine_code(relroot, text):
    """Messages for the token, games:: rules on one file of an engine directory (relroot relative to the engine root)."""
    out = []
    for n, line in enumerate(strip_comments(text).split("\n"), 1):
        for tok in BANNED_TOKENS:
            if tok in line:
                out.append("%s:%d: engine code contains '%s'" % (relroot, n, tok))
        if "games::" in line:
            out.append("%s:%d: engine code names 'games::'" % (relroot, n))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
    root = os.path.abspath(ap.parse_args().root)
    problems = []
    for base, dirs, files in os.walk(root):
        dirs[:] = [d for d in dirs if not d.startswith("build") and d not in (".git", "__pycache__")]
        relbase = os.path.relpath(base, root).replace(os.sep, "/")
        if relbase == ".":  # the engine tree only: docs/, third_party/ and the workbench are not subject to the law
            dirs[:] = [d for d in dirs if d in ("src", "tests", "tools", "lua", "cmake")]
        for d in dirs:
            rel = (relbase + "/" + d) if relbase != "." else d
            if d == "generated" and rel.startswith("src/") and rel[4:].split("/")[0] in ENGINE_DIRS and rel[4:] not in GENERATED_ALLOWED:
                problems.append("%s: generated/ directory under an engine directory" % rel)
        for name in files:
            path = os.path.join(base, name)
            relroot = os.path.relpath(path, root).replace(os.sep, "/")
            if os.path.splitext(name)[1] not in TEXT_EXT:
                continue
            try:
                lines = open(path, encoding="utf-8", errors="replace").read().split("\n")
            except OSError:
                continue
            under_src = relroot.startswith("src/")
            under_tests = relroot.startswith("tests/")
            if under_src and relroot[4:].split("/")[0] in ENGINE_DIRS and os.path.splitext(name)[1] in CODE_EXT:
                problems.extend(check_engine_code(relroot, "\n".join(lines)))
            for n, line in enumerate(lines, 1):
                if BANNED_TEXT in line and relroot != "tools/check_layering.py":
                    problems.append("%s:%d: contains '%s'" % (relroot, n, BANNED_TEXT))
                if name.endswith((".h", ".cpp", ".inc")) and (under_src or under_tests):
                    m = INCLUDE.match(line)
                    if m:
                        msg = check_include(relroot[4:] if under_src else "", m.group(1))
                        if msg:
                            problems.append("%s:%d: %s" % (relroot, n, msg))
    for p in problems:
        print(p)
    print("layering: %d violation(s)" % len(problems))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
