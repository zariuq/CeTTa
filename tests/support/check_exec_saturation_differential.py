#!/usr/bin/env python3
"""exec:saturate's semi-naive rounds against its authored rounds.

Usage: check_exec_saturation_differential.py CETTA_BINARY [--stats] [--programs N]

Fixed programs cover the shapes saturation meets (closures, symmetric and
multi-template directives, joins through constants, directives that never
match, duplicate and open facts, empty template lists), and seeded programs
draw random graphs and rule sets.  Under HE each program must leave the same
facts, as a multiset, and answer the same, with the realization as with
CETTA_EXEC_SATURATION_REFERENCE=1 (the authored rounds).  Declined shapes (a
disjunctive query, a template headed by `=`, a redefined operation) answer
alike too.  With --stats the binary must be a runtime-stats build: every
admitted program took the semi-naive rounds and no declined one did.
"""
import os
import random
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
LIB = os.path.join(ROOT, "lib", "lib_exec.metta")

CLOSURE = ("(exec base (, (edge $x $y)) (, (reach $x $y))) "
           "(exec step (, (reach $x $y) (edge $y $z)) (, (reach $x $z)))")

# (name, facts, directives, admitted, extra source lines)
FIXED = [
    ("chain", [f"(edge n{i} n{i + 1})" for i in range(1, 12)], CLOSURE, True, []),
    ("cycle", ["(edge a b)", "(edge b c)", "(edge c a)"], CLOSURE, True, []),
    ("symmetric", ["(link a b)", "(link b c)"],
     "(exec sym (, (link $x $y)) (, (conn $x $y) (conn $y $x))) "
     "(exec hop (, (conn $x $y) (conn $y $z)) (, (conn $x $z)))", True, []),
    ("grand", ["(parent ann bob)", "(parent bob cid)", "(parent cid dan)",
               "(parent bob eve)"],
     "(exec g (, (parent $x $y) (parent $y $z)) (, (grand $x $z))) "
     "(exec gg (, (grand $x $y) (parent $y $z)) (, (great $x $z)))", True, []),
    ("constant-join", ["(edge a b)", "(edge b c)", "(edge c d)", "(mark c)"],
     "(exec m (, (mark $y) (edge $x $y)) (, (mark $x)))", True, []),
    ("never", ["(edge a b)"],
     "(exec none (, (missing $x)) (, (found $x)))", True, []),
    ("duplicates", ["(edge a b)", "(edge a b)", "(edge b c)"], CLOSURE, True, []),
    ("open-fact", ["(likes $anyone pizza)", "(likes ann soup)"],
     "(exec fan (, (likes $p $f)) (, (fan $p $f)))", True, []),
    ("shared-variables", ["(self $x $x)", "(pair $x $y)"],
     "(exec s (, (self $a $b)) (, (same $a $b))) "
     "(exec q (, (pair $a $b)) (, (two $a $b)))", True, []),
    ("empty-templates", ["(edge a b)"],
     "(exec quiet (, (edge $x $y)) (,)) " + CLOSURE, True, []),
    ("already-closed", ["(edge a b)", "(reach a b)"], CLOSURE, True, []),
    ("disjunctive-query", ["(a 1)", "(b 2)"],
     "(exec either (, (| (a $x) (b $x))) (, (seen $x)))", False, []),
    ("definition-template", ["(edge a b)"],
     "(exec def (, (edge $x $y)) (, (= (f $x) $y)))", False, []),
    ("redefined-size", [f"(edge n{i} n{i + 1})" for i in range(1, 6)], CLOSURE,
     False, ["!(remove-atom &self (= (exec:size $facts) "
             "(let $atoms (collapse (get-atoms $facts)) (size-atom $atoms))))",
             "(= (exec:size $facts) "
             "(let $atoms (collapse (get-atoms $facts)) "
             "(+ 0 (size-atom $atoms))))"]),
]


def random_program(rng, index):
    nodes = [f"v{k}" for k in range(rng.randint(3, 9))]
    facts = []
    for _ in range(rng.randint(2, 14)):
        rel = rng.choice(["edge", "edge", "link"])
        facts.append(f"({rel} {rng.choice(nodes)} {rng.choice(nodes)})")
    if rng.random() < 0.3:
        facts.append(f"(mark {rng.choice(nodes)})")
    rules = []
    for r in range(rng.randint(1, 4)):
        kind = rng.choice(["closure", "sym", "hop", "mark", "pair", "multi"])
        key = f"r{index}x{r}"
        if kind == "closure":
            rules.append(f"(exec {key}a (, (edge $x $y)) (, (reach $x $y)))")
            rules.append(f"(exec {key}b (, (reach $x $y) (edge $y $z)) (, (reach $x $z)))")
        elif kind == "sym":
            rules.append(f"(exec {key} (, (link $x $y)) (, (link $y $x)))")
        elif kind == "hop":
            rules.append(f"(exec {key} (, (edge $x $y) (link $y $z)) (, (edge $x $z)))")
        elif kind == "mark":
            rules.append(f"(exec {key} (, (mark $y) (edge $x $y)) (, (mark $x)))")
        elif kind == "pair":
            rules.append(f"(exec {key} (, (edge $x $y) (edge $y $x)) (, (pair $x $y)))")
        else:
            rules.append(f"(exec {key} (, (edge $x $y)) (, (out $x) (in $y)))")
    return (f"seed{index}", facts, " ".join(rules), True, [])


def source(facts, directives, extra):
    lines = [f'!(import! &self "{LIB}")']
    lines.extend(line for line in extra if not line.startswith("!"))
    lines.extend(line for line in extra if line.startswith("!"))
    lines.append("!(bind! &g (new-space))")
    lines.extend(f"!(add-atom &g {fact})" for fact in facts)
    lines.append(f"!(exec:saturate &g ({directives}))")
    lines.append("!(collapse (get-atoms &g))")
    return "\n".join(lines) + "\n"


def top_level(text):
    """The atoms of a printed list's contents, split at depth zero."""
    out, depth, cur = [], 0, ""
    for ch in text:
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
        if ch == " " and depth == 0:
            if cur:
                out.append(cur)
            cur = ""
        else:
            cur += ch
    if cur:
        out.append(cur)
    return out


VARIABLE = re.compile(r"\$[A-Za-z0-9_#'-]+")


def canonical(atom):
    """One fact up to a consistent renaming of its own variables: each
    distinct variable becomes $V0, $V1, ... in order of first occurrence, so
    repeated variables stay repeated and distinct ones stay distinct."""
    names = {}
    return VARIABLE.sub(
        lambda m: names.setdefault(m.group(0), f"$V{len(names)}"), atom)


def normalized(output):
    """The answers before the last, and the last answer's collapsed facts
    as a sorted multiset, each fact up to renaming of its variables: HE
    prints that answer as [(FACT ...)]."""
    lines = output.strip().splitlines()
    if not lines:
        return ("", [])
    last = lines[-1].strip()
    if not (last.startswith("[(") and last.endswith(")]")):
        return ("\n".join(lines), [])
    facts = sorted(canonical(atom) for atom in top_level(last[2:-2]))
    return ("\n".join(lines[:-1]), facts)


def check_comparison():
    """The comparison keeps variable sharing and renames consistently."""
    same = normalized("[((pair $x $y))]") == normalized("[((pair $a $b))]")
    shared = normalized("[((pair $x $x))]") == normalized("[((pair $x $y))]")
    counted = normalized("[((p a) (p a))]") == normalized("[((p a))]")
    return same and not shared and not counted


def run(binary, path, reference, stats):
    env = dict(os.environ)
    if reference:
        env["CETTA_EXEC_SATURATION_REFERENCE"] = "1"
    command = [binary]
    if stats:
        command.append("--emit-runtime-stats")
    command += ["--profile", "extended", "--lang", "he", path]
    proc = subprocess.run(command, capture_output=True, text=True, env=env,
                          timeout=300)
    admitted = None
    if stats:
        match = re.search(r"runtime-counter exec-saturation-admitted (\d+)",
                          proc.stdout + proc.stderr)
        admitted = int(match.group(1)) if match else None
    body = "\n".join(line for line in proc.stdout.splitlines()
                     if not line.startswith("runtime-counter"))
    return proc.returncode, body, admitted


def main():
    args = sys.argv[1:]
    if not args:
        print(__doc__)
        return 2
    binary = args[0]
    stats = "--stats" in args
    if not check_comparison():
        print("FAIL: the fact comparison loses variable sharing or counts")
        return 1
    count = 40
    if "--programs" in args:
        count = int(args[args.index("--programs") + 1])
    rng = random.Random(20261019)
    programs = FIXED + [random_program(rng, i) for i in range(count)]
    failures = 0
    native_taken = 0
    with tempfile.TemporaryDirectory(prefix="exec-saturation-") as work:
        for name, facts, directives, admitted, extra in programs:
            path = os.path.join(work, f"{name}.metta")
            with open(path, "w") as handle:
                handle.write(source(facts, directives, extra))
            rc_native, out_native, taken = run(binary, path, False, stats)
            rc_ref, out_ref, _ = run(binary, path, True, stats)
            if rc_native != rc_ref or normalized(out_native) != normalized(out_ref):
                failures += 1
                print(f"FAIL {name}: native and authored rounds differ")
                print("  native:  ", out_native.strip().splitlines()[-1:] )
                print("  authored:", out_ref.strip().splitlines()[-1:])
                continue
            if stats:
                if taken is None:
                    failures += 1
                    print(f"FAIL {name}: not a runtime-stats build")
                elif admitted and taken == 0:
                    failures += 1
                    print(f"FAIL {name}: admitted program took the authored rounds")
                elif not admitted and taken != 0:
                    failures += 1
                    print(f"FAIL {name}: declined shape took the semi-naive rounds")
                native_taken += 1 if taken else 0
    if failures:
        return 1
    suffix = f"; {native_taken} took the semi-naive rounds" if stats else ""
    print(f"PASS: exec:saturate leaves the authored facts and answers on "
          f"{len(programs)} programs, declined shapes included{suffix}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
