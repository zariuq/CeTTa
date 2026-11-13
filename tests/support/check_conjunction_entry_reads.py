#!/usr/bin/env python3
"""A conjunctive match answers from its space as the space stood when the
match was called, whatever its answers add or remove (PeTTa spec, [DIFF-20]).

Random programs: a space of facts over three heads, duplicates included,
and sometimes a list cell with an open tail that the answers remove; a
conjunction of two or three patterns sharing variables, a pattern sometimes
a union of two, (| P1 P2), whose branches bind the same variables and are
read in turn in the extended profile, while the stable profile reads the
union as written, a pattern no fact meets; answers that add
atoms the patterns read, or remove one occurrence per answer. Each
program runs the conjunction in three places: at the top of a query, as the
body of a function the query calls, and in a function whose caller writes
between its answers.  The model joins the patterns over the facts at the
call.  It is compared with CeTTa on the bag of answers, and on the facts the
answers leave, which the adds or the removals determine whatever the order
of the answers.

usage: check_conjunction_entry_reads.py CETTA [--programs N] [--seed S]"""
import argparse
import itertools
import os
import random
import subprocess
import sys
import tempfile

HEADS = ["p", "q", "r"]
VALUES = ["0", "1"]
VARIABLES = ["$x", "$y", "$z"]


def random_facts(rng):
    facts = []
    for _ in range(rng.randint(4, 10)):
        head = rng.choice(HEADS)
        facts.append((head, rng.choice(VALUES), rng.choice(VALUES)))
    # Duplicates: some facts twice or three times.
    for fact in list(facts):
        if rng.random() < 0.3:
            facts.extend([fact] * rng.randint(1, 2))
    rng.shuffle(facts)
    return facts


def random_pattern(rng, used):
    head = rng.choice(HEADS)
    terms = []
    for _ in range(2):
        if rng.random() < 0.75:
            if used and rng.random() < 0.6:
                terms.append(rng.choice(sorted(used)))
            else:
                v = rng.choice(VARIABLES)
                used.add(v)
                terms.append(v)
        else:
            terms.append(rng.choice(VALUES))
    return (head, terms[0], terms[1])


def random_leg(rng, used):
    """A pattern, or a union of two whose branches bind the same variables."""
    first = random_pattern(rng, used)
    if rng.random() < 0.75:
        return first
    a, b = (first[2], first[1]) if rng.random() < 0.5 else (first[1], first[2])
    return ("|", first, (rng.choice([h for h in HEADS if h != first[0]]), a, b))


def branches(leg, unions):
    """The patterns a leg reads: a union's branches in turn where unions are
    read, none where it is read as written; any other leg itself."""
    if leg[0] == "|":
        return [leg[1], leg[2]] if unions else []
    return [leg]


def join(facts, legs, unions):
    """The answers of the conjunction over FACTS: assignments with the
    multiplicity of every choice of a fact for each pattern a leg reads."""
    answers = []

    def walk(index, env):
        if index == len(legs):
            answers.append(dict(env))
            return
        for head, a, b in branches(legs[index], unions):
            for fact in facts:
                if fact[0] != head:
                    continue
                local = dict(env)
                ok = True
                for term, value in ((a, fact[1]), (b, fact[2])):
                    if term.startswith("$"):
                        if term in local and local[term] != value:
                            ok = False
                            break
                        local[term] = value
                    elif term != value:
                        ok = False
                        break
                if ok:
                    walk(index + 1, local)

    walk(0, {})
    return answers


def text(leg):
    if leg[0] == "|":
        return "(| %s %s)" % (text(leg[1]), text(leg[2]))
    return "(%s %s %s)" % leg


def program_and_model(rng):
    facts = random_facts(rng)
    used = set()
    legs = [random_leg(rng, used) for _ in range(rng.choice([2, 2, 3]))]
    read = [b for leg in legs for b in branches(leg, True)]
    variables = sorted({t for leg in read for t in leg[1:] if t.startswith("$")})
    if not variables:
        return None
    mode = rng.choice(["add", "remove"])
    # The atom an answer writes: one of the heads the legs read, built from
    # the answer's values.
    write_head = rng.choice([leg[0] for leg in read])
    w1 = rng.choice(variables)
    w2 = rng.choice(variables + VALUES)
    answer_tuple = "(" + " ".join(variables) + ")"
    write = "(%s %s %s)" % (write_head, w1, w2)
    op = "add-atom" if mode == "add" else "remove-atom"
    pattern = "(, " + " ".join(text(leg) for leg in legs) + ")"
    # A list cell with an open tail, which the answers remove and no pattern
    # reads: its removal must not stop the reading at the call.
    junk = rng.random() < 0.4
    write_all = ("(let $j (remove-atom &s (junk (1 2))) %s)" % answer_tuple
                 if junk else answer_tuple)
    lines = ["!(bind! &s (new-space))"]
    lines += ["!(add-atom &s %s)" % text(f) for f in facts]
    if junk:
        lines.append("!(let $l (cons-atom 1 $t) (add-atom &s (junk $l)))")
    shape = rng.choice(["top", "body", "caller"])
    if shape == "top":
        lines.append("!(sort-atom (collapse (match &s %s (let $w (%s &s %s) %s))))"
                     % (pattern, op, write, write_all))
    elif shape == "body":
        lines.append("(= (conj) (match &s %s (let $w (%s &s %s) %s)))"
                     % (pattern, op, write, write_all))
        lines.append("!(sort-atom (collapse (conj)))")
    else:
        lines.append("(= (conj) (match &s %s %s))" % (pattern, answer_tuple))
        lines.append("!(sort-atom (collapse (let %s (conj) (let $w (%s &s %s) %s))))"
                     % (answer_tuple, op, write, write_all))
    lines.append("!(sort-atom (collapse (match &s ($h $u $v) ($h $u $v))))")

    def value(term, env):
        return env[term] if term.startswith("$") else term

    def expected(unions):
        answers = join(facts, legs, unions)
        written = [(write_head, value(w1, env), value(w2, env)) for env in answers]
        if mode == "add":
            final = facts + written
        else:
            final = list(facts)
            for fact in written:
                if fact in final:
                    final.remove(fact)
        return (sorted("(" + " ".join(env[v] for v in variables) + ")"
                       for env in answers),
                sorted(text(f) for f in final))

    unions = any(leg[0] == "|" for leg in legs)
    return shape, unions, lines, {"stable": expected(False),
                                  "extended": expected(True)}


def petta_sorted(items):
    """sort-atom's order on these atoms: numbers before symbols, and
    expressions by their elements in turn."""
    def key(item):
        parts = item.strip("()").split()
        return [(0, int(p)) if p.isdigit() else (1, p) for p in parts]
    return "(" + " ".join(sorted(items, key=key)) + ")"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("cetta")
    parser.add_argument("--programs", type=int, default=300)
    parser.add_argument("--seed", type=int, default=2026111201)
    args = parser.parse_args()
    rng = random.Random(args.seed)
    checked = 0
    answers_checked = 0
    shapes = {}
    failures = 0
    union_programs = 0
    with tempfile.TemporaryDirectory() as work:
        path = os.path.join(work, "program.metta")
        while checked < args.programs:
            made = program_and_model(rng)
            if not made:
                continue
            shape, unions, lines, expectations = made
            with open(path, "w") as out:
                out.write("\n".join(lines) + "\n")
            for route, profile in itertools.product(
                    ["", "CETTA_OPEN_EQUATIONS_REFERENCE=1"],
                    [[], ["--profile", "extended"]]):
                env = dict(os.environ)
                if route:
                    name, value = route.split("=")
                    env[name] = value
                run = subprocess.run([args.cetta, "--lang", "petta", *profile, path],
                                     capture_output=True, text=True, timeout=120,
                                     env=env)
                got = [l for l in run.stdout.splitlines() if l.startswith("(")]
                expected_answers, expected_final = expectations[
                    "extended" if profile else "stable"]
                want = [petta_sorted(expected_answers), petta_sorted(expected_final)]
                if run.returncode != 0 or got[-2:] != want:
                    failures += 1
                    if failures <= 3:
                        print("FAIL: program %d (%s), route %r, profile %r" %
                              (checked, shape, route or "tier",
                               " ".join(profile) or "stable"))
                        print("\n".join(lines))
                        print("expected:", want)
                        print("got:", got[-2:], run.stderr[-500:])
            checked += 1
            shapes[shape] = shapes.get(shape, 0) + 1
            union_programs += unions
            answers_checked += sum(len(answers) for answers, _ in
                                   expectations.values())
    if failures:
        print("FAIL: %d of %d runs" % (failures, 4 * checked))
        sys.exit(1)
    print("PASS: %d conjunctions writing to the atoms they read answer from the "
          "space at the call (%s; %d with a union of patterns): %d answers, on "
          "both routes and in both profiles" % (
              checked, ", ".join("%d %s" % (n, k) for k, n in
                                 sorted(shapes.items())),
              union_programs, answers_checked))


if __name__ == "__main__":
    main()
