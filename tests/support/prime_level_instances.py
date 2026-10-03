#!/usr/bin/env python3
"""Judgments with level variables against their written instances.

A declaration with a level variable is used at an instance the kernel finds
(Mettapedia, TypeTheory/UniverseLevel/LeastInstance.lean).  The same
declaration written at a fixed level is a control for it:

  a judgment that holds with the instance written out at some level has an
  instance, so the judgment with the variable is not refuted, and it is not
  left undetermined either for the shapes generated here;

  an established judgment with the variable holds at some written instance.

`generate SEED COUNT FILE` writes a seeded Prime program: COUNT declarations
over one level variable, each also written at the levels 0..9, and queries
that use them, each followed by its written instances.  A query is the type
of a use, a check of a use against an expected type, an equality of a use
with itself seen in a universe, or the type of a use whose first argument is
a use of another declaration (with one written instance per pair of levels).
The labels of the queries go to FILE.labels.

`generate-two SEED COUNT FILE` writes the second family: declarations over two
level variables, with a maximum of the two in result position, argument
constants at finite levels and at omega and omega + 1, and written instances
of each variable at 0..7, omega, omega + 1 and omega + 2.

`check LABELS OUTPUT` reads the output of the program and compares each
query with its written instances.  It exits with status 1 if a refuted query
has an established instance, if an established query has none, or if an
undetermined query has an established instance.
"""

import random
import sys

LEVELS = list(range(0, 8))
INSTANCES = list(range(0, 10))
ARGUMENT_KINDS = ["X", "X1", "F", "F1"]
RESULT_KINDS = ["num", "U", "U1", "FN", "N2U"]


def level(variable, plus):
    if isinstance(variable, int):
        return str(variable + plus)
    return f"(+ {variable} {plus})" if plus else variable


def argument_type(kind, variable):
    return {
        "X": f"(u {level(variable, 0)})",
        "X1": f"(u {level(variable, 1)})",
        "F": f"(-> (u {level(variable, 0)}) num)",
        "F1": f"(-> (u {level(variable, 1)}) num)",
    }[kind]


def result_type(kind, variable):
    return {
        "num": "num",
        "U": f"(u {level(variable, 0)})",
        "U1": f"(u {level(variable, 1)})",
        "FN": f"(-> (u {level(variable, 0)}) num)",
        "N2U": f"(-> num (u {level(variable, 1)}))",
    }[kind]


def declared_type(arguments, result, variable):
    text = result_type(result, variable)
    for index, kind in reversed(list(enumerate(arguments))):
        text = f"(-> (A{index} : {argument_type(kind, variable)}) {text})"
    return text


def generate(seed, count, path):
    rng = random.Random(seed)
    lines = [
        "!(bind! &s (new-space))",
        "!(set:inductive &s num (u 0) (: zero num) (: suc (-> num num)))",
    ]
    for k in LEVELS:
        lines.append(f"!(add-atom &s (: S{k} (u {k})))")
        lines.append(f"!(add-atom &s (: f{k} (-> (u {k}) num)))")
    declarations = []
    for d in range(count):
        arity = rng.choice([1, 2, 2, 3])
        arguments = [rng.choice(ARGUMENT_KINDS) for _ in range(arity)]
        result = rng.choice(RESULT_KINDS)
        declarations.append((arguments, result))
        lines.append(f"!(add-atom &s (: p{d} {declared_type(arguments, result, '$l')}))")
        for instance in INSTANCES:
            lines.append(
                f"!(add-atom &s (: p{d}i{instance} "
                f"{declared_type(arguments, result, instance)}))")
    queries = []

    def emit(kind, form, controls):
        number = len({label.split(" ", 1)[0] for label, _ in queries})
        queries.append((f"q{number} {kind} poly {form(None)}", f"!(try {form(None)})"))
        for key in controls:
            queries.append((f"q{number} {kind} inst{key}", f"!(try {form(key)})"))

    def actuals(arguments):
        return [
            (f"S{rng.choice(LEVELS)}" if kind in ("X", "X1") else f"f{rng.choice(LEVELS)}")
            for kind in arguments
        ]

    for d, (arguments, result) in enumerate(declarations):
        for _ in range(3):
            actual = actuals(arguments)

            def use(key, d=d, actual=actual):
                head = f"p{d}" if key is None else f"p{d}i{key}"
                return "(" + " ".join([head] + actual) + ")"

            emit("of", lambda key, use=use: f"(type:of &s {use(key)})", INSTANCES)
            e = rng.choice(LEVELS + [8])
            expected = rng.choice(
                ["num", f"(u {e})", f"(-> (u {e}) num)", f"(-> num (u {e}))"])
            emit("check",
                 lambda key, use=use, expected=expected: f"(type:check &s {use(key)} {expected})",
                 INSTANCES)
            if result in ("U", "U1"):
                emit("eq",
                     lambda key, use=use, e=e:
                     f"(type:eq &s {use(key)} ((lam (Y : (u {e})) Y) {use(key)}))",
                     INSTANCES)
            if result == "N2U":
                emit("eq",
                     lambda key, use=use, e=e:
                     f"(type:eq &s {use(key)} ((lam (Y : (-> num (u {e}))) Y) {use(key)}))",
                     INSTANCES)
    pairs = [
        (i, j)
        for i, (_, inner_result) in enumerate(declarations)
        for j, (outer_arguments, _) in enumerate(declarations)
        if (inner_result in ("U", "U1") and outer_arguments[0] in ("X", "X1"))
        or (inner_result == "FN" and outer_arguments[0] in ("F", "F1"))
    ]
    rng.shuffle(pairs)
    for i, j in pairs[: max(4, count // 2)]:
        inner_actual = actuals(declarations[i][0])
        outer_actual = actuals(declarations[j][0][1:])

        def nested(key, i=i, j=j, inner_actual=inner_actual, outer_actual=outer_actual):
            if key is None:
                inner_head, outer_head = f"p{i}", f"p{j}"
            else:
                inner_head, outer_head = f"p{i}i{key // 10}", f"p{j}i{key % 10}"
            inner = "(" + " ".join([inner_head] + inner_actual) + ")"
            return "(" + " ".join([outer_head, inner] + outer_actual) + ")"

        emit("nested-of", lambda key, nested=nested: f"(type:of &s {nested(key)})",
             [a * 10 + b for a in INSTANCES for b in INSTANCES])
    with open(path, "w", encoding="utf-8") as stream:
        stream.write("\n".join(lines + [query for _, query in queries]) + "\n")
    with open(path + ".labels", "w", encoding="utf-8") as stream:
        stream.write(f"{len(lines)}\n")
        for label, _ in queries:
            stream.write(label + "\n")


TWO_POOL = list(range(0, 6)) + ["omega", "(+ omega 1)"]
TWO_INSTANCES = list(range(0, 8)) + ["omega", "(+ omega 1)", "(+ omega 2)"]
TWO_ARGUMENT_KINDS = ["Xa", "Xb", "Xa1", "Fa", "Fb", "Fa1", "Gab"]
TWO_RESULT_KINDS = ["num", "Ua", "Ub", "Umax", "FNa", "N2Umax"]


def level_name(value):
    return {"omega": "w", "(+ omega 1)": "w1", "(+ omega 2)": "w2"}.get(value, str(value))


def level_plus(value, plus):
    """The level `value + plus`, written as that sum: for a written instance the variable
    of the declaration is replaced by the instance and nothing else changes, so the level
    one above `(+ omega 1)` is `(+ (+ omega 1) 1)`."""
    return f"(+ {value} {plus})" if plus else str(value)


def two_argument_type(kind, a, b):
    return {
        "Xa": f"(u {level_plus(a, 0)})",
        "Xb": f"(u {level_plus(b, 0)})",
        "Xa1": f"(u {level_plus(a, 1)})",
        "Fa": f"(-> (u {level_plus(a, 0)}) num)",
        "Fb": f"(-> (u {level_plus(b, 0)}) num)",
        "Fa1": f"(-> (u {level_plus(a, 1)}) num)",
        "Gab": f"(-> (u {level_plus(a, 0)}) (u {level_plus(b, 0)}))",
    }[kind]


def two_result_type(kind, a, b):
    return {
        "num": "num",
        "Ua": f"(u {level_plus(a, 0)})",
        "Ub": f"(u {level_plus(b, 0)})",
        "Umax": f"(u (max {level_plus(a, 0)} {level_plus(b, 0)}))",
        "FNa": f"(-> (u {level_plus(a, 0)}) num)",
        "N2Umax": f"(-> num (u (max {level_plus(a, 0)} {level_plus(b, 0)})))",
    }[kind]


def two_declared_type(arguments, result, a, b):
    text = two_result_type(result, a, b)
    for index, kind in reversed(list(enumerate(arguments))):
        text = f"(-> (A{index} : {two_argument_type(kind, a, b)}) {text})"
    return text


def generate_two(seed, count, path):
    rng = random.Random(seed)
    lines = [
        "!(bind! &s (new-space))",
        "!(set:inductive &s num (u 0) (: zero num) (: suc (-> num num)))",
    ]
    for value in TWO_POOL:
        lines.append(f"!(add-atom &s (: S{level_name(value)} (u {value})))")
        lines.append(f"!(add-atom &s (: f{level_name(value)} (-> (u {value}) num)))")
    for domain in TWO_POOL:
        for codomain in TWO_POOL:
            lines.append(
                f"!(add-atom &s (: g{level_name(domain)}x{level_name(codomain)} "
                f"(-> (u {domain}) (u {codomain}))))")
    declarations = []
    for d in range(count):
        arity = rng.choice([1, 2, 2, 3])
        arguments = [rng.choice(TWO_ARGUMENT_KINDS) for _ in range(arity)]
        result = rng.choice(TWO_RESULT_KINDS)
        declarations.append((arguments, result))
        lines.append(f"!(add-atom &s (: r{d} {two_declared_type(arguments, result, '$a', '$b')}))")
        for a in TWO_INSTANCES:
            for b in TWO_INSTANCES:
                lines.append(
                    f"!(add-atom &s (: r{d}i{level_name(a)}_{level_name(b)} "
                    f"{two_declared_type(arguments, result, a, b)}))")
    queries = []
    number = 0

    def emit(kind, form):
        nonlocal number
        queries.append((f"q{number} {kind} poly {form(None)}", f"!(try {form(None)})"))
        for a in TWO_INSTANCES:
            for b in TWO_INSTANCES:
                queries.append(
                    (f"q{number} {kind} inst{level_name(a)}_{level_name(b)}",
                     f"!(try {form((a, b))})"))
        number += 1

    def actual(kind):
        if kind in ("Xa", "Xb", "Xa1"):
            return f"S{level_name(rng.choice(TWO_POOL))}"
        if kind in ("Fa", "Fb", "Fa1"):
            return f"f{level_name(rng.choice(TWO_POOL))}"
        return f"g{level_name(rng.choice(TWO_POOL))}x{level_name(rng.choice(TWO_POOL))}"

    for d, (arguments, result) in enumerate(declarations):
        for _ in range(3):
            actuals = [actual(kind) for kind in arguments]

            def use(key, d=d, actuals=actuals):
                head = (f"r{d}" if key is None
                        else f"r{d}i{level_name(key[0])}_{level_name(key[1])}")
                return "(" + " ".join([head] + actuals) + ")"

            emit("of", lambda key, use=use: f"(type:of &s {use(key)})")
            e = rng.choice(TWO_POOL + [6, "(+ omega 2)"])
            expected = rng.choice(
                ["num", f"(u {e})", f"(-> (u {e}) num)", f"(-> num (u {e}))"])
            emit("check",
                 lambda key, use=use, expected=expected: f"(type:check &s {use(key)} {expected})")
            if result in ("Ua", "Ub", "Umax"):
                emit("eq",
                     lambda key, use=use, e=e:
                     f"(type:eq &s {use(key)} ((lam (Y : (u {e})) Y) {use(key)}))")
    with open(path, "w", encoding="utf-8") as stream:
        stream.write("\n".join(lines + [query for _, query in queries]) + "\n")
    with open(path + ".labels", "w", encoding="utf-8") as stream:
        stream.write(f"{len(lines)}\n")
        for label, _ in queries:
            stream.write(label + "\n")


def verdict(line):
    if line.startswith("[(Established"):
        return "established"
    if line.startswith("[(Refuted"):
        return "refuted"
    return "undetermined"


def check(labels_path, output_path):
    with open(labels_path, encoding="utf-8") as stream:
        preamble = int(stream.readline())
        labels = [line.rstrip("\n") for line in stream]
    with open(output_path, encoding="utf-8") as stream:
        outputs = [line.rstrip("\n") for line in stream][preamble:]
    if len(outputs) != len(labels):
        print(f"FAIL: {len(outputs)} answers for {len(labels)} queries in {output_path}")
        return 1
    groups = {}
    for label, output in zip(labels, outputs):
        query, kind, which = label.split(" ", 3)[:3]
        group = groups.setdefault(query, {"kind": kind, "instances": {}})
        if which == "poly":
            group["poly"] = (verdict(output), output, label)
        else:
            group["instances"][which[4:]] = verdict(output)
    counts = {"established": 0, "refuted": 0, "undetermined": 0}
    wrong_refuted = without_instance = undetermined_with_instance = 0
    for group in groups.values():
        answer, output, label = group["poly"]
        counts[answer] += 1
        established = sorted(
            key for key, value in group["instances"].items() if value == "established")
        if answer == "refuted" and established:
            wrong_refuted += 1
            print("FAIL: refuted, yet it holds at the written instances",
                  established[:6], "|", label, "|", output)
        if answer == "established" and not established:
            without_instance += 1
            print("FAIL: established, yet it holds at no written instance |", label, "|", output)
        if answer == "undetermined" and established:
            undetermined_with_instance += 1
            print("FAIL: undetermined, yet it holds at the written instances",
                  established[:6], "|", label, "|", output)
    print(f"(PrimeLevelInstances queries={len(groups)} established={counts['established']} "
          f"refuted={counts['refuted']} undetermined={counts['undetermined']} "
          f"refuted-with-instance={wrong_refuted} established-without-instance={without_instance} "
          f"undetermined-with-instance={undetermined_with_instance})")
    return 1 if wrong_refuted or without_instance or undetermined_with_instance else 0


def main():
    if len(sys.argv) == 5 and sys.argv[1] == "generate":
        generate(int(sys.argv[2]), int(sys.argv[3]), sys.argv[4])
        return 0
    if len(sys.argv) == 5 and sys.argv[1] == "generate-two":
        generate_two(int(sys.argv[2]), int(sys.argv[3]), sys.argv[4])
        return 0
    if len(sys.argv) == 4 and sys.argv[1] == "check":
        return check(sys.argv[2], sys.argv[3])
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main())
