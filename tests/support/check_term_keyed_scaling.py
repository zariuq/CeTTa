#!/usr/bin/env python3
"""Reads of deep ground terms take work that does not grow with their depth.

Usage: check_term_keyed_scaling.py CETTA_RUNTIME_STATS_BINARY

Levels.  A set holds (rung L a), (rung L b), (held a L) and (held b L) for
each level L from 0 to N - 1, spelled (S (S ... Z)), and (next k k+1) for
each k.  Every level is then read by its term: its rows collected, counted,
joined with next, and read and joined with a variable before the level.  At
two sizes each walk answers two rows a level, and the candidate rows its
reads offer and the rows its indexes take, per answer, do not grow with N: a
column holding a ground term whose leaves are all symbols selects that
term's rows however deep it is.  An index that cuts terms at a fixed depth
offers every deeper level's rows to each read, and one keyed by a level's
deepest leaf builds an index per level.

Compares.  A space holds (recv (ch L $x)) for M levels L from depth D, a set
holds (send (ch K i)) for M other levels K, and every receive is matched
against every send: the channel's open variable leaves each pair of levels
to be compared.  From D to 4 D, the pairs of subterms compared per pair of
rows do not grow: two such levels are unequal at once by their hashes, which
differ however the levels nest.  The same levels ending in a number instead
of Z compare by walking, so their pairs must grow, or the test measures
nothing.

Rules.  A space holds a seed (: seed (Reach g0)) and, for each k, rules
(: reach_k (-> (: $p (Reach gk)) (Reach gk+1))) and (: noise_k (-> (: $p
(Reach gk)) (Noise nk))), whose types hold variables; every goal (Reach gk)
is then asked for its proofs.  The goal is a flat ground term after a
variable: keyed by its leaf gk, a rule's type is a miss, while keyed whole
every rule stays a candidate and each goal's offer is declined as broad.  At
two sizes no offer is declined.
"""
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SIZES = (40, 160)
DEPTHS = (64, 256)
CHANNEL_LEVELS = 60
RULE_COUNTS = (500, 2000)
# Work per answer or per row at the larger size, against the smaller: at
# most this factor, plus one for rounding.
GROWTH = 1.25

LEVELS = """!(bind! &t (new-space set))

(= (level $n) (if (> $n 0) (S (level (- $n 1))) Z))

(= (fill $k $n)
   (if (>= $k $n)
       ()
       (let* (($l (level $k))
              ($j (+ $k 1))
              ($a (add-atom &t (rung $l a)))
              ($b (add-atom &t (rung $l b)))
              ($c (add-atom &t (held a $l)))
              ($d (add-atom &t (held b $l)))
              ($e (add-atom &t (next $k $j))))
         (fill (+ $k 1) $n))))

!(fill 0 {n})

(= (walk $k $n $acc)
   (if (>= $k $n) $acc
       (let* (($l (level $k))
              ($xs {read}))
         (walk (+ $k 1) $n (+ $acc (size-atom $xs))))))
!(walk 0 {n} 0)
"""

WALKS = (
    ("collected", "(collapse (match &t (rung $l $x) $x))"),
    ("counted", "(collapse (match &t (rung $l $x) t))"),
    ("joined", "(collapse (match &t (, (rung $l $x) (next $k $j)) ($x $j)))"),
    ("read after a variable", "(collapse (match &t (held $x $l) $x))"),
    ("joined after a variable",
     "(collapse (match &t (, (rung $l a) (held $x $l)) $x))"),
)

CHANNELS = """!(bind! &r (new-space))
!(bind! &p (new-space set))

(= (level $n) (if (> $n 0) (S (level (- $n 1))) {bottom}))

(= (fill $i $m $d)
   (if (>= $i $m)
       ()
       (let* (($l (level (+ $d $i)))
              ($k (level (+ $d (+ $m $i))))
              ($a (add-atom &r (recv (ch $l $x))))
              ($b (add-atom &p (send (ch $k $i)))))
         (fill (+ $i 1) $m $d))))

!(fill 0 {m} {d})
!(size-atom (collapse (match &r (recv $c) (match &p (send $c) t))))
"""


def rules_program(n):
    facts = ["!(bind! &kb (new-space))", "!(add-atom &kb (: seed (Reach g0)))"]
    for k in range(n):
        facts.append(f"!(add-atom &kb (: reach_{k} (-> (: $p (Reach g{k})) (Reach g{k + 1}))))")
        facts.append(f"!(add-atom &kb (: noise_{k} (-> (: $p (Reach g{k})) (Noise n{k}))))")
    facts.extend(f"(goal (Reach g{k}))" for k in range(n))
    facts.append("(= (proofs $g) (match &kb (: $p $g) $p))")
    facts.append("!(size-atom (collapse (match &self (goal $g) (proofs $g))))")
    return "\n".join(facts) + "\n"


def counters(report):
    found = {}
    for line in report.splitlines():
        fields = line.split()
        if len(fields) >= 3 and fields[0] == "runtime-counter":
            found[fields[1]] = int(fields[2])
    return found


def run(binary, work, name, source, expected):
    path = os.path.join(work, name + ".metta")
    with open(path, "w") as f:
        f.write(source)
    p = subprocess.run([binary, "--emit-runtime-stats", "--profile", "extended",
                        "--lang", "petta", path],
                       cwd=ROOT, capture_output=True, text=True, timeout=600)
    answers = [l for l in p.stdout.splitlines() if l not in ("()", "true")
               and not l.startswith("runtime-")]
    if p.returncode != 0 or answers[-1:] != [str(expected)]:
        raise SystemExit(f"FAIL: {name}: answered {answers[-1:]}, expected {expected}")
    return counters(p.stdout + p.stderr)


def main():
    binary = os.path.abspath(sys.argv[1])
    with tempfile.TemporaryDirectory(prefix="cetta-term-keyed-") as work:
        for name, read in WALKS:
            per_answer = []
            for size in SIZES:
                found = run(binary, work, f"{name}_{size}".replace(" ", "_"),
                            LEVELS.format(n=size, read=read), 2 * size)
                rows = (found.get("match-native-candidates", 0) +
                        found.get("match-position-index-build-row", 0))
                per_answer.append(rows / (2 * size))
            if per_answer[1] > GROWTH * per_answer[0] + 1:
                print(f"FAIL: levels {name}: {per_answer[0]:.1f} rows an answer "
                      f"at {SIZES[0]} levels, {per_answer[1]:.1f} at {SIZES[1]}")
                return 1
        compared_rows = CHANNEL_LEVELS * CHANNEL_LEVELS
        pairs = {}
        for bottom in ("Z", "0"):
            pairs[bottom] = []
            for depth in DEPTHS:
                found = run(binary, work, f"channels_{bottom}_{depth}",
                            CHANNELS.format(bottom=bottom, m=CHANNEL_LEVELS, d=depth),
                            0)
                pairs[bottom].append(found.get("atom-compare-pair", 0) / compared_rows)
        if pairs["0"][1] < 2 * pairs["0"][0] or pairs["0"][1] < DEPTHS[0]:
            print(f"FAIL: levels ending in a number compare {pairs['0'][0]:.1f} and "
                  f"{pairs['0'][1]:.1f} pairs a row at depths {DEPTHS[0]} and "
                  f"{DEPTHS[1]}: the test measures nothing")
            return 1
        if pairs["Z"][1] > GROWTH * pairs["Z"][0] + 1:
            print(f"FAIL: levels compare {pairs['Z'][0]:.1f} pairs a pair of rows at depth "
                  f"{DEPTHS[0]} and {pairs['Z'][1]:.1f} at {DEPTHS[1]}")
            return 1
        for n in RULE_COUNTS:
            found = run(binary, work, f"rules_{n}", rules_program(n), 1)
            declined = found.get("match-position-index-broad-decline", 0)
            if declined:
                print(f"FAIL: goals over {n} rules holding variables: {declined} "
                      f"offers declined as broad")
                return 1
    print(f"PASS: levels spelled as deep terms are collected, counted, joined, and "
          f"read and joined after a variable, with work per answer that does not "
          f"grow from {SIZES[0]} to {SIZES[1]} levels; they compare in joins with "
          f"{pairs['Z'][0]:.1f} and {pairs['Z'][1]:.1f} pairs a pair of rows at depths "
          f"{DEPTHS[0]} and {DEPTHS[1]} ({pairs['0'][0]:.1f} and "
          f"{pairs['0'][1]:.1f} ending in a number); goals over {RULE_COUNTS[0]} and "
          f"{RULE_COUNTS[1]} rules holding variables are offered their rows")
    return 0


if __name__ == "__main__":
    sys.exit(main())
