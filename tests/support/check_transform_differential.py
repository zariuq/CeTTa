#!/usr/bin/env python3
"""transform's native firing against its authored library and its specification.

Usage: check_transform_differential.py CETTA_BINARY [--programs N] [--seed S]

The fixed programs (tests/petta/transform_*.metta) must answer their expected
output both when tf:fire runs natively and with lib_transform_swi (the full
authored firing), and with lib_transform_specs, the operation's
specification, imported instead.  SWI-PeTTa answers alike with the same library
where it runs a call once; stable-fixed c3adb86 runs a call once per type
declaration of its head, and transform has two.
The native firing must have run in each of them and declined nowhere.
Seeded programs over random stores mix every mode, an `all` or `once` at random
positions, comma templates and both destinations, and must answer alike on
all three.  Answers are compared as bags, since no MeTTa defines an order of
execution: the answers of a collapse, of one statement and of a collection
(the generated templates mark each as (collected C)) may come in any order.  A program that redefines one of the library's operations, or
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
# transform_conjunctions holds absent, unique and collect of patterns headed by
# ',', which range over joint matches.
FIXED = ["transform_concurrency", "transform_rules", "transform_aggregates",
         "transform_match_alignment", "transform_conjunctions"]
HEADER = ("!(import! &self (library lib_transform))\n"
          "(= (show $s) (sort-atom (collapse (match $s $a $a))))\n")
LIBRARY_IMPORT = "!(import! &self (library lib_transform))"
SWI_IMPORT = "!(import! &self (library lib_transform_swi))"
SPECS_IMPORT = "!(import! &self (library lib_transform_specs))"


def run(binary, path, reference, trace, fuel=None, workers=None, machine="1"):
    env = dict(os.environ, CETTA_PETTA_SEARCH_MACHINE=machine)
    reference_dir = None
    if reference:
        with open(path) as source_file:
            source = source_file.read().replace(LIBRARY_IMPORT, SWI_IMPORT)
        reference_dir = tempfile.TemporaryDirectory(prefix="cetta-transform-reference-")
        path = os.path.join(reference_dir.name, "reference.metta")
        with open(path, "w") as reference_file:
            reference_file.write(source)
    if trace:
        env["CETTA_TRANSFORM_TRACE"] = "1"
    if workers is not None:
        env["CETTA_OPEN_EQUATIONS_REFERENCE"], env["CETTA_PREPARED_PURE_REFERENCE"] = workers
    command = [binary, "--lang", "petta"]
    if fuel is not None:
        command += ["--fuel", str(fuel)]
    p = subprocess.run(command + [path], cwd=ROOT, env=env,
                       capture_output=True, text=True, timeout=120)
    if reference_dir:
        reference_dir.cleanup()
    return p.returncode, p.stdout, p.stderr


def atoms_of(text):
    """The atoms of one output line, as nested tuples of tokens."""
    tokens, i = [], 0
    while i < len(text):
        c = text[i]
        if c in "()":
            tokens.append(c)
            i += 1
        elif c.isspace():
            i += 1
        elif c == '"':
            j = i + 1
            while j < len(text) and text[j] != '"':
                j += 2 if text[j] == "\\" else 1
            tokens.append(text[i:j + 1])
            i = j + 1
        else:
            j = i
            while j < len(text) and not text[j].isspace() and text[j] not in "()":
                j += 1
            tokens.append(text[i:j])
            i = j

    def atom(k):
        if tokens[k] != "(":
            return tokens[k], k + 1
        items, k = [], k + 1
        while tokens[k] != ")":
            item, k = atom(k)
            items.append(item)
        return tuple(items), k + 1

    atoms, k = [], 0
    while k < len(tokens):
        item, k = atom(k)
        atoms.append(item)
    return atoms


def bag_key(x):
    return repr(x)


def as_bags(x):
    """A collection's elements, (collected C), sorted, at any depth."""
    if not isinstance(x, tuple):
        return x
    if len(x) == 2 and x[0] == "collected" and isinstance(x[1], tuple):
        return ("collected", tuple(sorted((as_bags(e) for e in x[1]), key=bag_key)))
    return tuple(as_bags(e) for e in x)


def canonical(output):
    """OUTPUT with each statement's answers, each collapse's answers and each
    collection's elements in a fixed order: (LABEL ANSWERS) with ANSWERS an
    expression is a collapse, and consecutive lines with one label are one
    statement's answers."""
    lines = []
    for line in output.splitlines():
        try:
            atoms = atoms_of(line)
        except (IndexError, ValueError):
            lines.append((None, line))
            continue
        if len(atoms) == 1 and isinstance(atoms[0], tuple) and len(atoms[0]) == 2 \
                and isinstance(atoms[0][1], tuple):
            label, answers = atoms[0]
            answers = tuple(sorted((as_bags(a) for a in answers), key=bag_key))
            lines.append((label, repr((label, answers))))
        elif len(atoms) == 1 and isinstance(atoms[0], tuple) and atoms[0]:
            lines.append((atoms[0][0], repr(as_bags(atoms[0]))))
        else:
            lines.append((None, repr([as_bags(a) for a in atoms])))
    statements, at = [], 0
    while at < len(lines):
        end = at + 1
        while lines[at][0] is not None and end < len(lines) and lines[end][0] == lines[at][0]:
            end += 1
        statements.append(sorted(text for _, text in lines[at:end]))
        at = end
    return statements


def same(left, right):
    return canonical(left) == canonical(right)


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
        if not same(native, expected) or not same(reference, expected):
            print(f"FAIL: {stem}: {'native' if not same(native, expected) else 'reference'} "
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
        if not same(specs, expected):
            print(f"FAIL: {stem}: lib_transform_specs answers differ from the expected output")
            return False
    return True


def check_native_fuel(binary):
    # Native work has its own grading. Compare completed runs to the authored
    # reference, not the number of helper reductions before its cutoff.
    header = (LIBRARY_IMPORT + "\n!(bind! &s (new-space))\n"
              "!(add-atom &s (job a))\n")
    programs = {
        "take": header + "!(collapse (transform &s (take (job $j)) (hit $j)))\n",
        "effect": header + "!(collapse (transform &s (, (take (job $j)) "
                  "(let $v (println! entered))) (hit $j)))\n",
        "all": header + "".join(f"!(add-atom &s (job n{i}))\n" for i in range(12)) +
               "!(collapse (transform &s (all (take (job $j))) (hit $j)))\n",
    }
    observed = 0
    with tempfile.TemporaryDirectory(prefix="cetta-transform-fuel-") as work:
        for name, source in programs.items():
            path = os.path.join(work, name + ".metta")
            with open(path, "w") as stream:
                stream.write(source)
            authored = run(binary, path, True, False)
            for machine, oem, prepared in [(m, o, p) for m in ("0", "1")
                                          for o in ("0", "1") for p in ("0", "1")]:
                complete = False
                for fuel in (1, 7, 31, 127, 511, 2047, 8191):
                    native = run(binary, path, False, True, fuel, (oem, prepared), machine)
                    fired = any(line.startswith("[transform] fired")
                                for line in trace_lines(native[2]))
                    if native[0] == 0:
                        if not same(native[1], authored[1]) or not fired:
                            print(f"FAIL: native transform fuel completion: {name}, {fuel}, {machine}, {(oem, prepared)}")
                            print(native, authored)
                            return False
                        complete = True
                    elif complete or "fuel-exhausted" not in native[2]:
                        print(f"FAIL: native transform fuel cutoff: {name}, {fuel}, {machine}, {(oem, prepared)}")
                        print(native)
                        return False
                    observed += 1
                if not complete:
                    print(f"FAIL: native transform never completes: {name}")
                    return False
    print(f"PASS: native transform fuel cutoffs are explicit; completed answers and effects agree ({observed} runs)")
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
    all_at = rng.randrange(4) if rng.random() < 0.4 else None
    for position in range(rng.randint(1, 4)):
        pattern, names = term(rng, rng.choice(sorted(RELATIONS)))
        if rng.random() < 0.25:
            # a pattern headed by ',' is a conjunctive query in every conjunct kind
            second, more = term(rng, rng.choice(sorted(RELATIONS)))
            pattern, names = f"(, {pattern} {second})", names | more
        kind = rng.choice(["read", "read", "take", "take", "absent", "unique", "collect"])
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
            c = f"(collect {inner} {collection})"
            bound.add(collection)
            if rng.random() < 0.6:
                count = f"$n{index}{position}"
                c += f" (let {count} (size-atom {collection}))"
                bound.add(count)
        if position == all_at:
            wrap = rng.choice(["all", "all", "once"])
            c = f"({wrap} {c})" if " (let " not in c else f"({wrap} (, {c}))"
        conjuncts.append(c)
    return f"(, {' '.join(conjuncts)})", sorted(bound)


def template(rng, bound):
    used = bound[:rng.randint(0, len(bound))] if bound else []
    # a collection is a bag: marked, so the comparison reads it as one
    used = [f"(collected {v})" if v.startswith("$c") else v for v in used]
    one = f"(out {' '.join(used)})" if used else "(out)"
    if rng.random() < 0.25:
        return f"(, {one} (seen))"
    return one


def program(rng):
    lines = [HEADER, "!(bind! &s (new-space))\n"]
    for _ in range(rng.randint(6, 14)):
        fact, _ = term(rng, rng.choice(sorted(RELATIONS)))
        if rng.random() < 0.1:
            # a stored atom headed by ',' is data
            second, _ = term(rng, rng.choice(sorted(RELATIONS)))
            fact = f"(, {fact} {second})"
        for name in VARIABLES:
            fact = fact.replace(name, rng.choice(CONSTANTS))
        lines.append(f"!(add-atom &s {fact})\n")
    for index in range(rng.randint(4, 7)):
        src, bound = sources(rng, index)
        destination = " &s" if rng.random() < 0.5 else ""
        lines.append(f"!(l{index} (collapse (transform &s {src} {template(rng, bound)}{destination})))\n")
        lines.append(f"!(s{index} (show &s))\n")
    return "".join(lines)


REFERENCE_HELPERS = [
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
    if not check_fixed(binary) or not check_native_fuel(binary):
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
            if not same(native, reference):
                print(f"FAIL: seeded program {number} (seed {args.seed}) answers differently")
                print(open(path).read())
                return 1
            specs_path = os.path.join(work, f"seeded_specs_{number}.metta")
            with open(specs_path, "w") as f:
                f.write(open(path).read().replace(LIBRARY_IMPORT, SPECS_IMPORT))
            _, specs, _ = run(binary, specs_path, False, False)
            if not same(specs, reference):
                print(f"FAIL: seeded program {number} (seed {args.seed}): "
                      "lib_transform_specs answers differently")
                print(open(path).read())
                return 1
            trace = trace_lines(stderr)
            if code != 0 or any("refused" in line or "declined" in line for line in trace):
                print(f"FAIL: seeded program {number}: the native firing declined")
                return 1
            fired += sum(1 for line in trace if line.startswith("[transform] fired"))
        if fired == 0:
            print("FAIL: no seeded program fired natively")
            return 1
        for name, redefinition in REFERENCE_HELPERS:
            path = os.path.join(work, f"{name}.metta")
            with open(path, "w") as f:
                f.write(HEADER + redefinition + DECLINED_BODY)
            _, native, stderr = run(binary, path, False, True)
            baseline_path = os.path.join(work, f"{name}-baseline.metta")
            with open(baseline_path, "w") as f:
                f.write(HEADER + DECLINED_BODY)
            _, baseline, _ = run(binary, baseline_path, True, False)
            trace = trace_lines(stderr)
            if not same(native, baseline) or \
               not any(line.startswith("[transform] fired") for line in trace) or \
               any("refused" in line for line in trace):
                print(f"FAIL: {name}: a reference-only helper changed the native operation")
                return 1
        override_path = os.path.join(work, "public-override.metta")
        with open(override_path, "w") as f:
            f.write(LIBRARY_IMPORT + "\n(= (transform $s $c $t) user-transform)\n"
                    "!(transform &self () done)\n")
        code, output, stderr = run(binary, override_path, False, True)
        if code or not output.strip().endswith("user-transform") or \
                any(line.startswith("[transform] fired") for line in trace_lines(stderr)):
            print("FAIL: authored public transform override", code, output, stderr)
            return 1
    print(f"PASS: transform fires natively as its library does, and as its specification "
          f"does ({len(FIXED)} fixed, {args.programs} seeded programs, "
          f"{fired} seeded firings)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
