#!/usr/bin/env python3
"""Byte-compare the *.png and unimplemented*.txt files under a baseline test_out against a current one.

Usage: python3 -I compare_test_out.py <baseline> <current> [--ignore glob]...

Every such file under <baseline> must exist at the same relative path under <current> with identical bytes. Files only
in <current> are not reported. --ignore globs match the relative path (forward slashes) with fnmatch, or the file name.
Exit status: 0 when everything matches, 1 on any missing or differing file, 2 on bad arguments.
"""
import argparse
import fnmatch
import os
import sys


def wanted(name):
    return name.endswith(".png") or (name.startswith("unimplemented") and name.endswith(".txt"))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("baseline")
    ap.add_argument("current")
    ap.add_argument("--ignore", action="append", default=[], metavar="GLOB")
    args = ap.parse_args()
    if not os.path.isdir(args.baseline) or not os.path.isdir(args.current):
        print("both arguments must be directories", file=sys.stderr)
        return 2

    rels = []
    for dirpath, _dirs, files in os.walk(args.baseline):
        for f in files:
            if wanted(f):
                rels.append(os.path.relpath(os.path.join(dirpath, f), args.baseline))
    rels.sort()

    missing, differ, checked = [], [], 0
    for rel in rels:
        posix = rel.replace(os.sep, "/")
        if any(fnmatch.fnmatchcase(posix, g) or fnmatch.fnmatchcase(os.path.basename(rel), g) for g in args.ignore):
            continue
        checked += 1
        cur = os.path.join(args.current, rel)
        if not os.path.isfile(cur):
            missing.append(posix)
            continue
        with open(os.path.join(args.baseline, rel), "rb") as a, open(cur, "rb") as b:
            if a.read() != b.read():
                differ.append(posix)

    for m in missing:
        print("MISSING  " + m)
    for d in differ:
        print("DIFFERS  " + d)
    print(f"{checked} files compared, {len(missing)} missing, {len(differ)} differing")
    return 1 if missing or differ else 0


if __name__ == "__main__":
    sys.exit(main())
