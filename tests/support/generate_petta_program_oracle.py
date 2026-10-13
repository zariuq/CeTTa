#!/usr/bin/env python3
"""Generate reproducible finite PeTTa programs for the Lean/native oracle.

The population exercises ordered equations, independent alternatives, correlated
bindings, recursive selection, collection and println effects. Random requests
use well-typed small integers and ground data; each request uses the same source
on the model and native routes. This is a finite test population, not a theorem
about arbitrary programs or native integer overflow.
"""

import argparse
import hashlib
import json
from pathlib import Path
import random


PROGRAM = """(= (pick (Cons $h $t)) $h)
(= (pick (Cons $h (Cons $n $t))) (pick (Cons $n $t)))
(= (dup $x) $x)
(= (dup $x) $x)
(= (tag $x) (Pair $x $x))
(= (relay $x) (tag $x))
(= (same (Pair $x $x)) hit)
(= (same (Pair $x $y)) miss)
(= (len Nil) 0)
(= (len (Cons $h $t)) (+ 1 (len $t)))
(= (visit $x) (let $discard (println! (Seen $x)) (Pair $x $x)))
"""

FOCUSED = [
    ("empty", "(empty)"),
    ("symbol", "marker"),
    ("nullary-data", "(Marker)"),
    ("quoted-number-string", '"01"'),
    ("nested-boolean-string", '(Box true false "True")'),
    ("ordered-equation-duplicates", "(dup 7)"),
    ("independent-calls", "(Pair (dup 1) (dup 2))"),
    ("shared-let-correlation", "(let $x (superpose (1 1 2)) (Pair $x $x))"),
    ("empty-choice", "(superpose ())"),
    ("choice-keeps-empty-occurrence", "(superpose ((empty) () (Box 1) (empty) (Box 1)))"),
    ("captured-alias", "(let $x (superpose (1 2)) (let $y $x (Pair $x $y)))"),
    ("pattern-binder-correlation", "(let (Pair $x $x) (superpose ((Pair 1 1) (Pair 1 2) (Pair 2 2))) (tag $x))"),
    ("equation-repeated-variable", "(same (Pair 1 1))"),
    ("equation-pattern-rejection", "(same (Pair 1 2))"),
    ("finite-recursive-selection", "(pick (Cons 7 (Cons 7 (Cons 9 Nil))))"),
    ("empty-recursive-selection", "(pick Nil)"),
    ("nested-recursive-count", "(len (Cons a (Cons b (Cons c Nil))))"),
    ("nested-call-continuation", "(relay (pick (Cons 1 (Cons 2 Nil))))"),
    ("condition-ordered-branches", "(if (superpose (true false true)) (dup yes) (dup no))"),
    ("case-commits-before-empty-body", "(case 1 ((1 (empty)) ($rest unexpected)))"),
    ("collapse-preserves-duplicates", "(collapse (superpose (1 1 2)))"),
    ("empty-collapse-is-one-value", "(collapse (empty))"),
    ("arithmetic-after-choice", "(+ (superpose (1 1 2)) (dup 3))"),
    ("integer-remainders", "(Pair (% -5 3) (% 5 -3))"),
    ("typed-equality-strings", '(Pair (== "01" "1") (== true true))'),
    ("quoted-call-is-inert", "(quote (dup 7))"),
    ("effect-once-per-alternative", "(visit (superpose (1 1 2)))"),
    ("effect-before-empty", "(let $seen (println! (Seen 7)) (empty))"),
    ("effect-under-collapse", "(collapse (visit (superpose (1 1 2))))"),
    ("pattern-rejection-after-effect", "(let mismatch (println! (Seen 9)) value)"),
]


def expression(head, *arguments):
    return "(" + " ".join((head, *arguments)) + ")"


def cons(items):
    result = "Nil"
    for item in reversed(items):
        result = expression("Cons", item, result)
    return result


class Generator:
    def __init__(self, seed):
        self.rng = random.Random(seed)
        self.counter = 0

    def variable(self):
        self.counter += 1
        return "$v" + str(self.counter)

    def integer(self, depth):
        if depth == 0:
            return str(self.rng.randint(-4, 9))
        selected = self.rng.randrange(7)
        if selected == 0:
            return str(self.rng.randint(-4, 9))
        if selected == 1:
            return expression(self.rng.choice(["+", "-", "*"]), self.integer(depth - 1), self.integer(depth - 1))
        if selected == 2:
            return expression("superpose", "(" + " ".join(self.integer(0) for _ in range(self.rng.randrange(4))) + ")")
        if selected == 3:
            return expression("dup", self.integer(depth - 1))
        if selected == 4:
            binding = self.variable()
            return expression("let", binding, self.integer(depth - 1), expression("+", binding, binding))
        if selected == 5:
            return expression("if", self.boolean(depth - 1), self.integer(depth - 1), self.integer(depth - 1))
        return expression("pick", cons([self.integer(0) for _ in range(self.rng.randrange(5))]))

    def boolean(self, depth):
        if depth == 0:
            return self.rng.choice(["true", "false"])
        selected = self.rng.randrange(4)
        if selected == 0:
            return expression(self.rng.choice(["<", "<=", ">", ">=", "=="]), self.integer(depth - 1), self.integer(depth - 1))
        if selected == 1:
            return expression(self.rng.choice(["and", "or"]), self.boolean(depth - 1), self.boolean(depth - 1))
        if selected == 2:
            return expression("superpose", "(" + " ".join(self.boolean(0) for _ in range(self.rng.randrange(4))) + ")")
        return self.boolean(0)

    def value(self, depth):
        if depth == 0:
            return self.rng.choice(["a", "b", "(Tag)", "true", "false", '"True"', '"01"', '"1"', "()", str(self.rng.randint(-4, 9))])
        selected = self.rng.randrange(12)
        if selected == 0:
            return self.integer(depth)
        if selected == 1:
            return self.boolean(depth)
        if selected == 2:
            return expression(self.rng.choice(["Pair", "Box", "Tuple"]), self.value(depth - 1), self.value(depth - 1))
        if selected == 3:
            return expression("superpose", "(" + " ".join(self.value(depth - 1) for _ in range(self.rng.randrange(4))) + ")")
        if selected == 4:
            return expression(self.rng.choice(["dup", "tag", "relay"]), self.value(depth - 1))
        if selected == 5:
            binding = self.variable()
            return expression("let", binding, self.value(depth - 1), expression("Pair", binding, binding))
        if selected == 6:
            return expression("if", self.boolean(depth - 1), self.value(depth - 1), self.value(depth - 1))
        if selected == 7:
            return expression("collapse", self.value(depth - 1))
        if selected == 8:
            return expression("pick", cons([self.value(0) for _ in range(self.rng.randrange(5))]))
        if selected == 9:
            return expression("visit", self.value(depth - 1))
        if selected == 10:
            return expression("case", self.integer(0), "((0 zero) (1 one) ($other other))")
        return self.value(0)


def generate(seed, count, max_depth):
    generator = Generator(seed)
    cases = [{"name": name, "body": body} for name, body in FOCUSED]
    for index in range(count):
        body = generator.value(generator.rng.choice(list(range(1, max_depth + 1))))
        cases.append({"name": f"random-{index:03d}", "body": body})
    for case in cases:
        # The compiled document reader requires a parenthesized request. The
        # same closed wrapper is part of the source submitted to both routes.
        case["expression"] = expression("let", "$oracleResult", case["body"], "$oracleResult")
    return cases


def generated_programs(seed, count):
    """Vary authored definitions as well as requests, with finite call graphs."""
    rng = random.Random(seed)
    result = []
    for index in range(count):
        heads = [f"oracle-f{index}-{level}" for level in range(4)]
        equations = []
        for level, head in enumerate(heads):
            for branch in range(rng.randrange(1, 4)):
                constant = str(rng.randint(-3, 5))
                selected = rng.randrange(7)
                if selected == 0:
                    body = expression("+", "$x", constant)
                elif selected == 1:
                    body = expression("let", "$local", expression("+", "$x", constant),
                                      expression("*", "$local", str(rng.randint(-2, 3))))
                elif selected == 2 and level < len(heads) - 1:
                    body = expression(heads[level + 1], expression("+", "$x", constant))
                elif selected == 3:
                    body = expression("superpose", expression("$x", expression("+", "$x", constant), "$x"))
                elif selected == 4 and level < len(heads) - 1:
                    body = expression("if", expression("<", "$x", constant),
                                      expression(heads[level + 1], "$x"), expression("+", "$x", constant))
                elif selected == 5:
                    returned = expression(heads[level + 1], "$x") if level < len(heads) - 1 else "$x"
                    body = expression("let", "$seen", expression("println!", expression("ProgramSeen", str(index), str(level), "$x")), returned)
                else:
                    body = "(empty)"
                equation = expression("=", expression(head, "$x"), body)
                equations.append(equation)
                if branch == 0 and rng.randrange(3) == 0:
                    equations.append(equation)
        rng.shuffle(equations)
        source = "\n".join(equations) + "\n"
        bodies = [expression(heads[0], str(rng.randint(-3, 5))),
                  expression(heads[0], "(superpose (1 1 2))"),
                  expression("collapse", expression(heads[0], "2")),
                  expression("let", "$chosen", expression(heads[0], "1"), expression("Pair", "$chosen", "$chosen"))]
        cases = [{"name": f"program-{index:03d}-query-{query}", "body": body,
                  "expression": expression("let", "$oracleResult", body, "$oracleResult")}
                 for query, body in enumerate(bodies)]
        result.append((source, cases))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--seed", type=int, default=71010)
    parser.add_argument("--random-cases", type=int, default=256)
    parser.add_argument("--random-programs", type=int, default=0,
                        help="additional shuffled, finite equation programs, with four requests each")
    parser.add_argument("--max-depth", type=int, choices=[1, 2, 3], default=3)
    parser.add_argument("--fuel-chunks", nargs="+", type=int, default=[0, 1, 2, 3, 5, 8, 13, 100000])
    args = parser.parse_args()
    if args.random_cases < 0 or args.random_programs < 0 or any(fuel < 0 for fuel in args.fuel_chunks):
        parser.error("counts and fuel must be natural numbers")
    args.out.mkdir(parents=True, exist_ok=False)
    program_path = args.out.resolve() / "program.metta"
    program_path.write_text(PROGRAM)
    cases = generate(args.seed, args.random_cases, args.max_depth)
    requests = [{"program_path": str(program_path), "expression": case["expression"],
                 "fuel_chunks": args.fuel_chunks} for case in cases]
    program_hashes = {program_path.name: hashlib.sha256(program_path.read_bytes()).hexdigest()}
    for index, (source, program_cases) in enumerate(generated_programs(args.seed, args.random_programs)):
        path = args.out.resolve() / f"program-{index:03d}.metta"
        path.write_text(source)
        program_hashes[path.name] = hashlib.sha256(path.read_bytes()).hexdigest()
        requests.extend({"program_path": str(path), "expression": case["expression"],
                         "fuel_chunks": args.fuel_chunks} for case in program_cases)
        cases.extend(program_cases)
    requests_path = args.out / "requests.jsonl"
    requests_path.write_text("".join(json.dumps(request) + "\n" for request in requests))
    (args.out / "cases.json").write_text(json.dumps(cases, indent=2) + "\n")
    receipt = {"seed": args.seed, "focused": len(FOCUSED), "random": args.random_cases,
               "random_programs": args.random_programs, "program_identities": program_hashes,
               "max_depth": args.max_depth, "fuel_chunks": args.fuel_chunks,
               "requests": len(requests), "program_sha256": hashlib.sha256(program_path.read_bytes()).hexdigest(),
               "requests_sha256": hashlib.sha256(requests_path.read_bytes()).hexdigest()}
    (args.out / "generation.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print("Generated program requests:", len(requests), flush=True)


if __name__ == "__main__":
    main()
