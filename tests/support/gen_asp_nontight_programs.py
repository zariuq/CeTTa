#!/usr/bin/env python3
"""Random normal programs with positive loops, in the reference driver's
format ("H : P ... : N ..." per rule, programs separated by "---"), for the
differential of the fast search against the reference search.  A program has
4 to 11 atoms and up to two constraints through self-defeating atoms; with
--negation its rules carry one to three negative atoms, so that more of the
programs have several stable models.  tests/asp/nontight_random_programs.txt
is 125 programs of seed 21 with --negation, then 125 of seed 22 without.
Usage: gen_asp_nontight_programs.py COUNT SEED [--negation]"""
import random, sys

def main():
    count, seed = int(sys.argv[1]), int(sys.argv[2])
    negation = "--negation" in sys.argv[3:]
    rng = random.Random(seed)
    out = []
    for p in range(count):
        n = rng.randint(4, 11)
        m = rng.randint(n, 2 * n + 6)
        rules = []
        for _ in range(m):
            h = rng.randrange(n)
            pos = rng.sample(range(n), rng.choice([0, 1, 1, 2, 2, 3]))
            neg = rng.sample(range(n), rng.choice([1, 1, 2, 2, 3] if negation else [0, 0, 1, 1, 2]))
            rules.append((h, pos, neg))
        for _ in range(rng.randint(0, 2)):
            v = n + len(rules)
            pos = rng.sample(range(n), rng.randint(1, 2))
            rules.append((v, pos, [v]))
        if p:
            out.append("---")
        for h, pos, neg in rules:
            out.append(f"{h} : {' '.join(map(str, pos))} : {' '.join(map(str, neg))}")
    print("\n".join(out))

main()
