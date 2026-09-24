#!/usr/bin/env python3
"""Native folds against the ordinary fold, on seeded random programs.

Usage: check_native_fold_differential.py CETTA_BINARY [--stats] [--programs N]

Each program has integer facts in flat, nested and joinable shapes, random
step functions over the item and the accumulator (literals, `+ - * % min
max`, with zero, negative and int64-boundary values), and folds over single
patterns, nested patterns, constant items, two-pattern joins and
three-pattern chains, with builtin and user-defined steps.  The native answers must equal the answers
of CETTA_FOLD_ARITH_REFERENCE=1 (the ordinary fold) exactly, errors
included.  With --stats the binary must be a runtime-stats build, and every
native route (ordered scan, run of one item, join moments, bigint
continuation) must have been taken at least once.
"""
import os
import random
import subprocess
import sys
import tempfile

INT64_MAX = 2**63 - 1

def acc_degree(expr):
    if expr == "$a":
        return 1
    if not isinstance(expr, tuple):
        return 0
    op, left, right = expr
    dl, dr = acc_degree(left), acc_degree(right)
    if op == "*":
        return dl + dr
    return max(dl, dr)

def render(expr):
    if isinstance(expr, tuple):
        return "(" + " ".join(render(part) for part in expr) + ")"
    return str(expr)

def literal(rng):
    kind = rng.random()
    if kind < 0.6:
        return rng.randint(-9, 9)
    if kind < 0.85:
        return rng.randint(-100000, 100000)
    return rng.choice([INT64_MAX, -INT64_MAX, 2**62, -(2**62), 3037000499])

def gen_expr(rng, depth):
    if depth == 0 or rng.random() < 0.3:
        return rng.choice(["$i", "$a", "$a", literal(rng)])
    op = rng.choice(["+", "-", "*", "*", "%", "min", "max"])
    left = gen_expr(rng, depth - 1)
    if op == "%":
        right = rng.choice([rng.randint(1, 50), rng.randint(-50, -1), 0,
                            1000003, "$i", "$a"])
    else:
        right = gen_expr(rng, depth - 1)
    return (op, left, right)

def item_values(rng, count):
    out = []
    for _ in range(count):
        kind = rng.random()
        if kind < 0.7:
            out.append(rng.randint(-20, 20))
        elif kind < 0.9:
            out.append(rng.randint(-10**9, 10**9))
        else:
            out.append(rng.choice([INT64_MAX, -INT64_MAX, 2**62, 0, 1, -1]))
    return out

def program(rng, index):
    lines = []
    folds = []
    steps = []
    for step_index in range(6):
        expr = gen_expr(rng, rng.randint(1, 3))
        name = f"st{index}x{step_index}"
        lines.append(f"(= ({name} $i $a) {render(expr)})")
        steps.append((name, expr))
    # Also a wrapped modular affine step and a pure translation/scaling.
    lines.append(f"(= (md{index} $i $a) (% (+ (* 31 $a) $i) 1000003))")
    lines.append(f"(= (tr{index} $i $a) (+ $a (- (* 3 $i) 2)))")
    lines.append(f"(= (sc{index} $i $a) (* $i $a))")
    lines.append(f"(= (rm{index} $i $a) (min $a (+ $i 3)))")
    lines.append(f"(= (dk{index} $i $a) (max (- $a 1) $i))")
    steps += [(f"md{index}", ("%", ("+", ("*", 31, "$a"), "$i"), 1000003)),
              (f"tr{index}", ("+", "$a", ("-", ("*", 3, "$i"), 2))),
              (f"sc{index}", ("*", "$i", "$a")),
              (f"rm{index}", ("min", "$a", ("+", "$i", 3))),
              (f"dk{index}", ("max", ("-", "$a", 1), "$i"))]
    builtins = [("+", ("+", "$i", "$a")), ("-", ("-", "$i", "$a")),
                ("*", ("*", "$i", "$a")), ("%", ("%", "$i", "$a")),
                ("min", ("min", "$i", "$a")), ("max", ("max", "$i", "$a"))]
    rows = rng.randint(3, 40)
    values = item_values(rng, rows)
    for row, value in enumerate(values):
        lines.append(f"(fd{index} {row} {value})")
        lines.append(f"(fn{index} (at {row}) (val {value}))")
    for row in range(rng.randint(2, 8)):
        lines.append(f"(ja{index} k{row % 3} {row})")
        lines.append(f"(jb{index} k{row % 3} {rng.randint(-5, 5)})")
    # A three-pattern chain with duplicate rows and dangling keys.
    for row in range(rng.randint(3, 10)):
        lines.append(f"(ca{index} u{rng.randint(0, 3)} v{rng.randint(0, 3)})")
        lines.append(f"(cb{index} v{rng.randint(0, 3)} w{rng.randint(0, 3)} {rng.randint(-9, 9)})")
        lines.append(f"(cc{index} w{rng.randint(0, 4)} z{rng.randint(0, 2)})")
    for name, expr in steps + builtins:
        degree = acc_degree(expr)
        top_mod = isinstance(expr, tuple) and expr[0] == "%" and \
            isinstance(expr[2], int) and expr[2] > 0
        small = degree >= 2 and not top_mod
        init = rng.choice([0, 1, -3, 7, rng.randint(-1000, 1000)])
        pattern = f"(fd{index} $k $v)"
        if small:
            # Growth of degree two or more: keep the stream short.
            pattern = f"(fd{index} {rng.randint(0, 2)} $v)"
        folds.append(f"!(foldall {name} (match &self {pattern} $v) {init})")
        if not small:
            folds.append(f"!(foldall {name} (match &self (fn{index} (at $k) (val $v)) $v) {init})")
            folds.append(f"!(foldall {name} (match &self (fd{index} $k $v) {rng.randint(-4, 4)}) {init})")
            folds.append(f"!(foldall {name} (match &self (, (ja{index} $c $x) (jb{index} $c $v)) $v) {init})")
            folds.append(f"!(foldall {name} (match &self (, (ca{index} $p $q) (cb{index} $q $r $v) (cc{index} $r $t)) $v) {init})")
            folds.append(f"!(foldall {name} (match &self (, (ca{index} $p $q) (cb{index} $q $r $v) (cc{index} $r $t)) {rng.randint(-3, 3)}) {init})")
    base = "\n".join(lines) + "\n"
    # An error answer ends a PeTTa document, so each fold is its own document.
    return [base + fold + "\n" for fold in folds]

def run(binary, path, reference, stats):
    env = dict(os.environ)
    if reference:
        env["CETTA_FOLD_ARITH_REFERENCE"] = "1"
    else:
        env.pop("CETTA_FOLD_ARITH_REFERENCE", None)
    command = [binary, "--lang", "petta"]
    if stats:
        command.append("--emit-runtime-stats")
    command.append(path)
    result = subprocess.run(command, env=env, capture_output=True, text=True,
                            timeout=300)
    return result.returncode, result.stdout, result.stderr

def main():
    args = sys.argv[1:]
    stats = "--stats" in args
    if stats:
        args.remove("--stats")
    programs = 12
    if "--programs" in args:
        position = args.index("--programs")
        programs = int(args[position + 1])
        del args[position:position + 2]
    binary = os.path.abspath(args[0])
    rng = random.Random(20260924)
    counters = {}
    with tempfile.TemporaryDirectory(prefix="native-fold-") as directory:
        folds = 0
        for index in range(programs):
            for fold_index, text in enumerate(program(rng, index)):
                path = os.path.join(directory, f"p{index}f{fold_index}.metta")
                with open(path, "w") as handle:
                    handle.write(text)
                native = run(binary, path, False, stats)
                reference = run(binary, path, True, False)
                folds += 1
                if native[0] != 0 or reference[0] != 0:
                    sys.stderr.write(
                        f"FAIL program {index} fold {fold_index}: exit "
                        f"{native[0]} / {reference[0]}\n{native[2][-2000:]}"
                        f"{reference[2][-2000:]}\n")
                    return 1
                if native[1] != reference[1]:
                    sys.stderr.write(
                        f"FAIL program {index} fold {fold_index}: native and "
                        "ordinary folds differ\n")
                    sys.stderr.write(text.splitlines()[-1] + "\n")
                    sys.stderr.write(f"native: {native[1][:300]}\n"
                                     f"ordinary: {reference[1][:300]}\n")
                    return 1
                for line in native[2].splitlines():
                    parts = line.split()
                    if len(parts) == 3 and parts[0] == "runtime-counter" and \
                            parts[1].startswith("native-fold-"):
                        counters[parts[1]] = counters.get(parts[1], 0) + \
                            int(parts[2])
    if stats:
        for route in ("native-fold-ordered-scan", "native-fold-run",
                      "native-fold-moments", "native-fold-bigint"):
            if counters.get(route, 0) == 0:
                sys.stderr.write(f"FAIL: route {route} never taken ({counters})\n")
                return 1
    print(f"PASS: {folds} folds in {programs} programs equal the ordinary fold"
          + (f" ({counters})" if stats else ""))
    return 0

if __name__ == "__main__":
    sys.exit(main())
