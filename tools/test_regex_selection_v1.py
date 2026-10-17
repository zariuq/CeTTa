#!/usr/bin/env python3
"""Compare selected captures with independent, explicitly scoped oracles.

The generated portable profile uses Python; the nullable-loop corpus can use
the optional pinned Rust engine. Neither is a C runtime dependency. Every
comparison observes spans and all captures, not recognition alone.
"""

import argparse
import itertools
import json
from pathlib import Path
import random
import re
import subprocess


def literal(text):
    return ("lit", text)


def capture(body):
    return ("capture", body)


def count_value(n):
    return "(re:count-succ " * n + "re:count-zero" + ")" * n


def source(pattern, next_group=1):
    kind, *args = pattern
    if kind == "lit":
        return args[0], f"(re:literal {ord(args[0])})", next_group
    if kind == "empty":
        return "", "re:epsilon", next_group
    if kind in ("seq", "alt"):
        left, lvalue, next_group = source(args[0], next_group)
        right, rvalue, next_group = source(args[1], next_group)
        text = left + right if kind == "seq" else f"(?:{left}|{right})"
        return text, f"(re:{kind} {lvalue} {rvalue})", next_group
    if kind == "capture":
        group = next_group
        text, value, next_group = source(args[0], next_group + 1)
        return f"({text})", f"(re:capture {group} {value})", next_group
    if kind in ("star", "plus", "optional"):
        text, value, next_group = source(args[0], next_group)
        greedy = args[1]
        quantifier = {"star": "*", "plus": "+", "optional": "?"}[kind]
        return (f"(?:{text}){quantifier}" + ("" if greedy else "?"),
                f"(re:{kind} {value} {'Greedy' if greedy else 'Lazy'})", next_group)
    if kind == "repeat":
        text, value, next_group = source(args[0], next_group)
        low, high, greedy = args[1:]
        preference = "Greedy" if greedy else "Lazy"
        if low == high:
            suffix, value = f"{{{low}}}", f"(re:repeat-exact {value} {count_value(low)})"
        elif high is None:
            suffix, value = f"{{{low},}}", f"(re:repeat-at-least {value} {count_value(low)} {preference})"
        else:
            suffix, value = f"{{{low},{high}}}", f"(re:repeat-bounded {value} {count_value(low)} {count_value(high-low)} {preference})"
        return f"(?:{text}){suffix}" + ("" if greedy else "?"), value, next_group
    raise ValueError(kind)


def cases():
    a, b, empty = literal("a"), literal("b"), ("empty",)
    bases = [a, empty, ("alt", a, empty), ("alt", empty, a),
             ("optional", a, True), ("star", a, True)]
    patterns = [("alt", a, ("seq", a, b)), literal("λ")]
    for body in bases:
        for greedy in (True, False):
            patterns += [("star", capture(body), greedy),
                         ("plus", capture(body), greedy),
                         ("seq", ("star", capture(body), greedy), b)]
    patterns += [("plus", ("alt", capture(a), b), True),
                 ("alt", ("seq", capture(a), literal("c")), ("seq", a, b)),
                 ("star", capture(("star", capture(a), True)), True)]
    for body in (a, ("alt", a, empty), ("optional", a, True)):
        for greedy in (True, False):
            patterns += [("repeat", capture(body), 1, None, greedy),
                         ("repeat", capture(body), 2, None, greedy),
                         ("repeat", capture(body), 1, 3, greedy)]
    inputs = ["", "a", "b", "aa", "ab", "ba", "aλb"]
    return itertools.product(patterns, inputs, ("search", "full", "all"))


def observe(binary, arguments):
    result = subprocess.run([str(binary), *arguments], check=True,
                            capture_output=True, text=True, timeout=20)
    return json.loads(result.stdout)


def portable_patterns(seed, count):
    """Independent backtracking oracle profile: no nullable unbounded loops.

    Finite nullable counts are included. Nullable unbounded loops have a
    different capture policy and belong in the named graph-profile corpus.
    """
    rng = random.Random(seed)

    def generate(depth):
        if not depth:
            value = rng.choice([literal("a"), literal("b"), literal("λ"), ("empty",)])
            return value, value[0] == "empty"
        kind = rng.choice(["seq", "alt", "capture", "optional", "star", "plus", "repeat"])
        body, nullable = generate(depth - 1)
        greedy = bool(rng.randrange(2))
        if kind in ("seq", "alt"):
            other, other_nullable = generate(depth - 1)
            return (kind, body, other), (nullable and other_nullable if kind == "seq" else nullable or other_nullable)
        if kind == "capture":
            return capture(body), nullable
        if kind in ("star", "plus") and nullable:
            kind = "optional"
        if kind == "repeat":
            low = rng.choice([1, 2])
            high = rng.choice([low, low + 2] + ([] if nullable else [None]))
            return (kind, body, low, high, greedy), nullable
        return (kind, body, greedy), nullable or kind in ("optional", "star")

    seen = set()
    while len(seen) < count:
        pattern, _ = generate(3)
        if pattern not in seen:
            seen.add(pattern)
            yield pattern


def python_observation(regex, text, mode):
    compiled = re.compile(regex)
    found = compiled.fullmatch(text) if mode == "full" else compiled.search(text)
    if found is None:
        return dict(outcome="no-match", groups=[])
    groups = []
    for i in range(compiled.groups + 1):
        start, end = found.span(i)
        groups.append(None if start < 0 else [len(text[:start].encode()), len(text[:end].encode())])
    return dict(outcome="match", groups=groups)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--native", type=Path, required=True)
    parser.add_argument("--oracle", type=Path)
    parser.add_argument("--python-random", type=int, default=0, metavar="COUNT",
                        help="compare a generated portable subset with Python's independent engine")
    parser.add_argument("--seed", type=int, default=20261016)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if not args.oracle and not args.python_random:
        parser.error("supply --oracle or --python-random COUNT")
    if args.python_random < 0:
        parser.error("COUNT must be nonnegative")
    mismatches = []
    count = 0
    if args.python_random:
        observations = itertools.product(portable_patterns(args.seed, args.python_random),
            ["", "a", "b", "aa", "ab", "ba", "aab", "aba", "aaa", "aλb"], ("search", "full"))
        profile = "python-regular-no-nullable-unbounded-loops-v1"
    else:
        observations, profile = cases(), "rust-regex-1.12.3-regular"
    for pattern, text, mode in observations:
        regex, value, _ = source(pattern)
        expected = (python_observation(regex, text, mode) if args.python_random else
                    observe(args.oracle.resolve(), [mode, regex, text]))
        actual = observe(args.native.resolve(), ["--probe", mode, value, text])
        count += 1
        if actual != expected:
            mismatches.append(dict(pattern=regex, value=value, input=text,
                                   mode=mode, expected=expected, actual=actual))
    report = dict(profile=profile, seed=args.seed, comparisons=count,
                  failures=len(mismatches), mismatches=mismatches)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n")
    print(f"(RegexDifferentialV1Summary {count} {len(mismatches)})")
    for mismatch in mismatches[:8]:
        print(json.dumps(mismatch, ensure_ascii=False))
    return bool(mismatches)


if __name__ == "__main__":
    raise SystemExit(main())
