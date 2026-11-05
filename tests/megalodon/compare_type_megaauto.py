#!/usr/bin/env python3
"""Megaauto's salt searches, native against the Megalodon binary.

Each round rewrites a template document: every search placeholder (?*? or
?t:*?) starts at a random salt, some short (the search then advances the
first word byte by byte), some full length (it counts in the first words).
With --fixed, each placeholder gets a random salt to use as it is, and the
round's document ends at the first salt that finds no proof.  Both tools
check the round's document, and their output (rows, the salts found,
failures) and exit status must be the same.  A search that agrees on the
salt it finds agrees on every salt it tried before.  Rounds that differ are
kept in the work directory with both outputs.
"""

import argparse
import pathlib
import random
import re
import subprocess
import sys

PLACEHOLDER = re.compile(r"\?(t:)?\*\?")


def run(cmd, cwd, timeout):
    try:
        p = subprocess.run(cmd, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                           timeout=timeout)
        return p.returncode, p.stdout
    except subprocess.TimeoutExpired:
        return "timeout", b""


def start_salt(rng):
    if rng.random() < 0.5:
        n = rng.randint(1, 8)
    else:
        n = 64
    return "".join(rng.choice("0123456789abcdef") for _ in range(n))


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--megalodon", required=True, help="the Megalodon binary")
    ap.add_argument("--tool", required=True, help="mg-sexprinfo")
    ap.add_argument("--template", required=True, help="a document with ?*? or ?t:*? placeholders")
    ap.add_argument("-I", dest="includes", action="append", default=[], help="a signature")
    ap.add_argument("--rounds", type=int, default=40)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--fixed", action="store_true", help="random salts used as they are, not searched from")
    ap.add_argument("--timeout", type=int, default=300)
    ap.add_argument("--work", required=True, help="directory for the rounds' documents")
    args = ap.parse_args()

    template = pathlib.Path(args.template).read_text()
    places = len(PLACEHOLDER.findall(template))
    if not places:
        sys.exit("no ?*? or ?t:*? placeholder in %s" % args.template)
    work = pathlib.Path(args.work)
    work.mkdir(parents=True, exist_ok=True)
    includes = []
    for sig in args.includes:
        includes += ["-I", str(pathlib.Path(sig).resolve())]
    megalodon = str(pathlib.Path(args.megalodon).resolve())
    tool = str(pathlib.Path(args.tool).resolve())

    same = differ = failed = 0
    for r in range(args.rounds):
        rng = random.Random(args.seed * 1000003 + r)
        star = "" if args.fixed else "*"
        doc = PLACEHOLDER.sub(lambda m: "?%s%s%s?" % (m.group(1) or "", star, start_salt(rng)), template)
        name = "round%04d" % r
        path = work / (name + ".mg")
        path.write_text(doc)
        ref = run([megalodon, "-sexprinfo"] + includes + [path.name], work, args.timeout)
        ours = run([tool, "-kernel"] + includes + [path.name], work, args.timeout)
        failed += ref[0] != 0
        if ref == ours and ref[0] != "timeout":
            same += 1
            path.unlink()
            continue
        differ += 1
        (work / (name + ".megalodon")).write_bytes(ref[1])
        (work / (name + ".native")).write_bytes(ours[1])
        print("DIFFER %s: exit %s (Megalodon) and %s (native)" % (path, ref[0], ours[0]))
    print("(MegalodonMegaautoSearches template=%s mode=%s rounds=%d placeholders=%d "
          "ended_by_failure=%d same=%d differ=%d)"
          % (pathlib.Path(args.template).name, "fixed" if args.fixed else "search", args.rounds,
             args.rounds * places, failed, same, differ))
    return 1 if differ else 0


if __name__ == "__main__":
    sys.exit(main())
