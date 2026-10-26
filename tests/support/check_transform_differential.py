#!/usr/bin/env python3
"""transform's native firing against its authored library and its specification.

Usage: check_transform_differential.py CETTA_BINARY [--programs N] [--seed S]

The fixed programs (tests/petta/transform_*.metta) must answer their expected
output, the output SWI-PeTTa gives with the same library, both when tf:fire
runs natively and with CETTA_TRANSFORM_REFERENCE=1 (the authored firing), and
with lib_transform_specs, the operation's specification, imported instead.
The native firing must have run in each of them and declined nowhere.
Seeded programs over random stores mix every mode, an `every` at random
positions, comma templates and both destinations, and must answer alike on
all three.  A program that redefines one of the library's operations, or
gives one of its data constructors an equation, runs the authored definition:
the presentation is refused and the answers are those of the authored firing.
"""
import argparse
import os
import random
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
FIXED = ["transform_concurrency", "transform_rules", "transform_aggregates"]
HEADER = ("!(import! &self (library lib_transform))\n"
          "(= (show $s) (sort-atom (collapse (match $s $a $a))))\n")
LIBRARY_IMPORT = "!(import! &self (library lib_transform))"
SPECS_IMPORT = "!(import! &self (library lib_transform_specs))"


def run(binary, path, reference, trace, fuel=None, workers=None, machine="1"):
    env = dict(os.environ, CETTA_PETTA_SEARCH_MACHINE=machine)
    env.pop("CETTA_TRANSFORM_REFERENCE", None)
    if reference:
        env["CETTA_TRANSFORM_REFERENCE"] = "1"
    if trace:
        env["CETTA_TRANSFORM_TRACE"] = "1"
    if workers is not None:
        env["CETTA_OPEN_EQUATIONS_REFERENCE"], env["CETTA_PREPARED_PURE_REFERENCE"] = workers
    command = [binary, "--lang", "petta"]
    if fuel is not None:
        command += ["--fuel", str(fuel)]
    p = subprocess.run(command + [path], cwd=ROOT, env=env,
                       capture_output=True, text=True, timeout=120)
    return p.returncode, p.stdout, p.stderr


def trace_lines(stderr):
    return [line for line in stderr.splitlines() if line.startswith("[transform]")]


def check_fixed(binary):
    for stem in FIXED:
        path = os.path.join(ROOT, "tests", "petta", stem + ".metta")
        with open(os.path.join(ROOT, "tests", "petta", stem + ".expected")) as f:
            expected = f.read()
        code, native, stderr = run(binary, path, False, True)
        _, reference, _ = run(binary, path, True, False)
        trace = trace_lines(stderr)
        fired = sum(1 for line in trace if line.startswith("[transform] fired"))
        if native != expected or reference != expected:
            print(f"FAIL: {stem}: {'native' if native != expected else 'reference'} "
                  "answers differ from the expected output")
            return False
        if code != 0 or fired == 0 or any("declined" in line or "refused" in line
                                          for line in trace):
            print(f"FAIL: {stem}: the native firing did not run throughout")
            return False
        with open(path) as f:
            source = f.read()
        if source.count(LIBRARY_IMPORT) != 1:
            print(f"FAIL: {stem}: expected one import of lib_transform")
            return False
        with tempfile.TemporaryDirectory(prefix="cetta-transform-specs-") as work:
            specs_path = os.path.join(work, stem + ".metta")
            with open(specs_path, "w") as f:
                f.write(source.replace(LIBRARY_IMPORT, SPECS_IMPORT))
            _, specs, _ = run(binary, specs_path, False, False)
        if specs != expected:
            print(f"FAIL: {stem}: lib_transform_specs answers differ from the expected output")
            return False
    return True


def check_source_fuel(binary):
    header = (LIBRARY_IMPORT + "\n!(bind! &s (new-space))\n"
              "!(add-atom &s (job a))\n")
    programs = {
        "take": header + "!(collapse (transform &s (take (job $j)) (hit $j)))\n",
        "effect": header + "!(collapse (transform &s (, (take (job $j)) "
                  "(let $v (println! entered))) (hit $j)))\n",
        "every": header + "".join(f"!(add-atom &s (job n{i}))\n" for i in range(12)) +
                 "!(collapse (transform &s (every (take (job $j))) (hit $j)))\n",
    }
    budgets = [0, 1, 3, 7, 15, 31, 63, 127, 255, 511, 1023, 2047, -1]
    with tempfile.TemporaryDirectory(prefix="cetta-transform-fuel-") as work:
        for name, source in programs.items():
            path = os.path.join(work, name + ".metta")
            with open(path, "w") as stream:
                stream.write(source)
            for machine, oem, prepared in [(m, o, p) for m in ("0", "1")
                                          for o in ("0", "1") for p in ("0", "1")]:
                workers = (oem, prepared)
                for fuel in budgets:
                    native = run(binary, path, False, True, fuel, workers, machine)
                    authored = run(binary, path, True, False, fuel, workers, machine)
                    diagnostics = "\n".join(line for line in native[2].splitlines()
                                            if not line.startswith("[transform]"))
                    if (native[0], native[1], diagnostics) != \
                            (authored[0], authored[1], authored[2].rstrip("\n")):
                        print(f"FAIL: transform source fuel: {name}, {fuel}, {machine}, {workers}")
                        print("native:", native, "authored:", authored)
                        return False
                    fired = any(line.startswith("[transform] fired")
                                for line in trace_lines(native[2]))
                    if (fuel >= 0 and fired) or (fuel < 0 and (not fired or native[0])):
                        print(f"FAIL: transform source-fuel admission: {name}, {fuel}, {machine}, {workers}")
                        return False
    print(f"PASS: transform preserves source-fuel prefixes and effects "
          f"({len(programs) * 8 * len(budgets) * 2} observations)")
    return True


CONSTANTS = ["a", "b", "c", "d"]
VARIABLES = ["$x", "$y", "$z"]
RELATIONS = {"r": 2, "s": 1, "q": 2}


def term(rng, relation):
    args = [rng.choice(VARIABLES) if rng.random() < 0.6 else rng.choice(CONSTANTS)
            for _ in range(RELATIONS[relation])]
    return f"({relation} {' '.join(args)})", {a for a in args if a.startswith("$")}


def sources(rng, index):
    conjuncts, bound = [], set()
    every_at = rng.randrange(4) if rng.random() < 0.4 else None
    for position in range(rng.randint(1, 4)):
        pattern, names = term(rng, rng.choice(sorted(RELATIONS)))
        kind = rng.choice(["read", "read", "take", "take", "absent", "unique", "all"])
        if kind == "read":
            c = pattern
            bound |= names
        elif kind == "take":
            c = f"(take {pattern})"
            bound |= names
        elif kind == "absent":
            c = f"(absent {pattern})"
        elif kind == "unique":
            c = f"(unique {pattern})" if rng.random() < 0.5 else f"(unique (take {pattern}))"
            bound |= names
        else:
            collection = f"$c{index}{position}"
            inner = pattern if rng.random() < 0.6 else f"(take {pattern})"
            c = f"(all {inner} {collection})"
            bound.add(collection)
            if rng.random() < 0.6:
                count = f"$n{index}{position}"
                c += f" (let {count} (size-atom {collection}))"
                bound.add(count)
        if position == every_at:
            c = f"(every {c})" if " (let " not in c else f"(every (, {c}))"
        conjuncts.append(c)
    return f"(, {' '.join(conjuncts)})", sorted(bound)


def template(rng, bound):
    used = bound[:rng.randint(0, len(bound))] if bound else []
    one = f"(out {' '.join(used)})" if used else "(out)"
    if rng.random() < 0.25:
        return f"(, {one} (seen))"
    return one


def program(rng):
    lines = [HEADER, "!(bind! &s (new-space))\n"]
    for _ in range(rng.randint(6, 14)):
        fact, _ = term(rng, rng.choice(sorted(RELATIONS)))
        for name in VARIABLES:
            fact = fact.replace(name, rng.choice(CONSTANTS))
        lines.append(f"!(add-atom &s {fact})\n")
    for index in range(rng.randint(4, 7)):
        src, bound = sources(rng, index)
        destination = " &s" if rng.random() < 0.5 else ""
        lines.append(f"!(l{index} (collapse (transform &s {src} {template(rng, bound)}{destination})))\n")
        lines.append(f"!(s{index} (show &s))\n")
    return "".join(lines)


DECLINED = [
    ("redefined-operation", "(= (tf:valid $all $todo) True)\n"),
    ("constructor-equation", "(= (tf:row $f $i) (wrapped $f $i))\n"),
]
DECLINED_BODY = ("!(bind! &s (new-space))\n!(add-atom &s (job a))\n!(add-atom &s (job a))\n"
                 "!(add-atom &s (worker w))\n"
                 "!(d1 (collapse (transform &s (, (take (job $j)) (take (job $k))) (pair $j $k) &s)))\n"
                 "!(d2 (show &s))\n"
                 "!(d3 (collapse (transform &s (, (take (worker $w)) (take (worker $v))) (both $w $v))))\n"
                 "!(d4 (show &s))\n")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("binary")
    parser.add_argument("--programs", type=int, default=60)
    parser.add_argument("--seed", type=int, default=20261026)
    args = parser.parse_args()
    binary = os.path.abspath(args.binary)
    if not check_fixed(binary) or not check_source_fuel(binary):
        return 1
    rng = random.Random(args.seed)
    with tempfile.TemporaryDirectory(prefix="cetta-transform-") as work:
        fired = 0
        for number in range(args.programs):
            path = os.path.join(work, f"seeded_{number}.metta")
            with open(path, "w") as f:
                f.write(program(rng))
            code, native, stderr = run(binary, path, False, True)
            _, reference, _ = run(binary, path, True, False)
            if native != reference:
                print(f"FAIL: seeded program {number} (seed {args.seed}) answers differently")
                print(open(path).read())
                return 1
            specs_path = os.path.join(work, f"seeded_specs_{number}.metta")
            with open(specs_path, "w") as f:
                f.write(open(path).read().replace(LIBRARY_IMPORT, SPECS_IMPORT))
            _, specs, _ = run(binary, specs_path, False, False)
            if specs != reference:
                print(f"FAIL: seeded program {number} (seed {args.seed}): "
                      "lib_transform_specs answers differently")
                print(open(path).read())
                return 1
            trace = trace_lines(stderr)
            if code != 0 or any("declined" in line or "refused" in line for line in trace):
                print(f"FAIL: seeded program {number}: the native firing declined")
                return 1
            fired += sum(1 for line in trace if line.startswith("[transform] fired"))
        if fired == 0:
            print("FAIL: no seeded program fired natively")
            return 1
        for name, redefinition in DECLINED:
            path = os.path.join(work, f"{name}.metta")
            with open(path, "w") as f:
                f.write(HEADER + redefinition + DECLINED_BODY)
            _, native, stderr = run(binary, path, False, True)
            _, reference, _ = run(binary, path, True, False)
            trace = trace_lines(stderr)
            if native != reference or any(line.startswith("[transform] fired") for line in trace) or \
               not any("refused" in line for line in trace):
                print(f"FAIL: {name}: the authored definition did not run")
                return 1
    print(f"PASS: transform fires natively as its library does, and as its specification "
          f"does ({len(FIXED)} fixed, {args.programs} seeded programs, "
          f"{fired} seeded firings)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
