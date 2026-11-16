#!/usr/bin/env python3
"""Generate the nondeterministic-search corpus.

Every program prints one line, so CeTTa (--lang petta) and SWI-PeTTa can be
compared answer for answer.  Families:

  fanout_K        K nested picks over 64 values, accumulator 7*acc+p, filter mod 97
  fanout_flat3    the same three picks written as nested lets
  fanout_eqs3     pick written as 64 equations instead of one superpose
  fanmod_K        accumulator kept mod 101: the reachable states stay bounded
  queens_N        N-queens by equation nondeterminism, pruning by a safety walk
  perms_N         permutations by nondeterministic selection
  magic3          3x3 magic squares by selection with row pruning
  subset_N        subsets of N numbers summing to a target (overlapping equations)
  dag_WxD         path enumeration through a layered graph stored as facts
  chain_N         backward chaining over rules and facts stored in the space
  agg_K           counted and summed aggregates (collapse size, foldall)
  pair_indep      two independent picks whose product is collected
"""
import sys
from pathlib import Path

OUT = Path(__file__).resolve().parent / "corpus"


def picks(name, width):
    return f"(= ({name}) (superpose (" + " ".join(str(i) for i in range(width)) + ")))\n"


def upto():
    return "(= (upto $n) (if (== $n 0) Nil (Cons $n (upto (- $n 1)))))\n"


def sel():
    return ("(= (sel (Cons $x $xs)) (Pair $x $xs))\n"
            "(= (sel (Cons $x $xs)) (let (Pair $y $ys) (sel $xs) (Pair $y (Cons $x $ys))))\n")


def fanout(k):
    return (picks("pick", 64) +
            "(= (walk $k $acc) (if (== $k 0) $acc (let $p (pick) (walk (- $k 1) (+ (* $acc 7) $p)))))\n"
            f"!(size-atom (collapse (let $s (walk {k} 0) (if (== (% $s 97) 0) $s (empty)))))\n")


def fanout_flat3():
    return (picks("pick", 64) +
            "!(size-atom (collapse (let $a (pick) (let $b (pick) (let $c (pick) "
            "(let $s (+ (* (+ (* $a 7) $b) 7) $c) (if (== (% $s 97) 0) $s (empty))))))))\n")


def fanout_eqs3():
    eqs = "".join(f"(= (pick) {i})\n" for i in range(64))
    return (eqs +
            "(= (walk $k $acc) (if (== $k 0) $acc (let $p (pick) (walk (- $k 1) (+ (* $acc 7) $p)))))\n"
            "!(size-atom (collapse (let $s (walk 3 0) (if (== (% $s 97) 0) $s (empty)))))\n")


def fanmod(k):
    return (picks("pick", 64) +
            "(= (walk $k $acc) (if (== $k 0) $acc (let $p (pick) (walk (- $k 1) (% (+ (* $acc 7) $p) 101)))))\n"
            f"!(size-atom (collapse (let $s (walk {k} 0) (if (== $s 0) $s (empty)))))\n")


def queens(n):
    return ("(= (pickc (Cons $x $xs)) $x)\n"
            "(= (pickc (Cons $x (Cons $y $ys))) (pickc (Cons $y $ys)))\n" +
            upto() +
            "(= (safe $c $placed $dist)\n"
            "   (if (== $placed Nil) True\n"
            "       (let (Cons $h $t) $placed\n"
            "         (if (== $c $h) False\n"
            "             (if (== (- $c $h) $dist) False\n"
            "                 (if (== (- $h $c) $dist) False\n"
            "                     (safe $c $t (+ $dist 1))))))))\n"
            "(= (place $n $cols $placed)\n"
            "   (if (== $n 0) $placed\n"
            "       (let $c (pickc $cols)\n"
            "         (if (safe $c $placed 1)\n"
            "             (place (- $n 1) $cols (Cons $c $placed))\n"
            "             (empty)))))\n"
            f"!(size-atom (collapse (place {n} (upto {n}) Nil)))\n")


def perms(n):
    return (sel() + upto() +
            "(= (perm Nil) Nil)\n"
            "(= (perm (Cons $x $xs)) (let (Pair $y $ys) (sel (Cons $x $xs)) (let $r (perm $ys) (Cons $y $r))))\n"
            f"!(size-atom (collapse (perm (upto {n}))))\n")


def magic3():
    def lets(binds, body):
        for pat, src in reversed(binds):
            body = f"(let {pat} {src} {body})"
        return body

    final = ("(if (and (== (+ $g (+ $h $i)) 15) (and (== (+ $a (+ $d $g)) 15) "
             "(and (== (+ $b (+ $e $h)) 15) (and (== (+ $c (+ $f $i)) 15) "
             "(and (== (+ $a (+ $e $i)) 15) (== (+ $c (+ $e $g)) 15))))))"
             " (Sq $a $b $c $d $e $f $g $h $i) (empty))")
    row3 = lets([("(Pair $g $r7)", "(sel $r6)"), ("(Pair $h $r8)", "(sel $r7)"),
                 ("(Pair $i $r9)", "(sel $r8)")], final)
    row2 = lets([("(Pair $d $r4)", "(sel $r3)"), ("(Pair $e $r5)", "(sel $r4)"),
                 ("(Pair $f $r6)", "(sel $r5)")],
                f"(if (== (+ $d (+ $e $f)) 15) {row3} (empty))")
    row1 = lets([("(Pair $a $r1)", "(sel (upto 9))"), ("(Pair $b $r2)", "(sel $r1)"),
                 ("(Pair $c $r3)", "(sel $r2)")],
                f"(if (== (+ $a (+ $b $c)) 15) {row2} (empty))")
    return (sel() + upto() + f"(= (magic) {row1})\n" +
            "!(size-atom (collapse (magic)))\n")


def subset(n, target):
    nums = [(i * 7) % 11 + 1 for i in range(n)]
    lst = "Nil"
    for x in reversed(nums):
        lst = f"(Cons {x} {lst})"
    return ("(= (ss Nil $t) (if (== $t 0) 1 (empty)))\n"
            "(= (ss (Cons $h $r) $t) (ss $r $t))\n"
            "(= (ss (Cons $h $r) $t) (if (>= $t $h) (ss $r (- $t $h)) (empty)))\n"
            f"!(size-atom (collapse (ss {lst} {target})))\n")


def dag(width, depth):
    facts = []
    for i in range(width):
        facts.append(f"(edge s (n 0 {i}))")
    for layer in range(depth - 1):
        for i in range(width):
            for j in range(width):
                facts.append(f"(edge (n {layer} {i}) (n {layer + 1} {j}))")
    for i in range(width):
        facts.append(f"(edge (n {depth - 1} {i}) t)")
    return ("\n".join(facts) + "\n"
            "(= (paths $x) (if (== $x t) 1 (let $y (match &self (edge $x $y) $y) (paths $y))))\n"
            "!(size-atom (collapse (paths s)))\n")


def chain(n):
    # A ladder graph: two parents per rung, so derivations multiply.
    facts = []
    for i in range(n):
        facts.append(f"(parent (a {i}) (a {i + 1}))")
        facts.append(f"(parent (a {i}) (b {i + 1}))")
        facts.append(f"(parent (b {i}) (a {i + 1}))")
        facts.append(f"(parent (b {i}) (b {i + 1}))")
    return ("\n".join(facts) + "\n"
            "(rule (anc $x $y) ((parent $x $y)))\n"
            "(rule (anc $x $y) ((parent $x $z) (anc $z $y)))\n"
            "(= (prove $g) (match &self (rule $g $body) (prove-all $body)))\n"
            "(= (prove $g) (match &self $g True))\n"
            "(= (prove-all $gs) (if (== $gs ()) True (let $ok (prove (car-atom $gs)) (prove-all (cdr-atom $gs)))))\n"
            f"!(size-atom (collapse (prove (anc (a 0) (a {n})))))\n")


def dagf(width, depth):
    # The layered graph of dag(), with successors as overlapping equations.
    clauses = []
    for i in range(width):
        clauses.append(f"(= (succ s) (n 0 {i}))")
    for layer in range(depth - 1):
        for i in range(width):
            for j in range(width):
                clauses.append(f"(= (succ (n {layer} {i})) (n {layer + 1} {j}))")
    for i in range(width):
        clauses.append(f"(= (succ (n {depth - 1} {i})) t)")
    return ("\n".join(clauses) + "\n"
            "(= (paths $x) (if (== $x t) 1 (let $y (succ $x) (paths $y))))\n"
            "(= (main) (paths s))\n"
            "!(size-atom (collapse (main)))\n")


def ladder(n):
    # Two nodes per rung, every node reaches both nodes of the next rung:
    # 2^(n-1) derivations end at (a n), through 2 states a rung.
    return ("(= (child (a $i)) (a (+ $i 1)))\n"
            "(= (child (a $i)) (b (+ $i 1)))\n"
            "(= (child (b $i)) (a (+ $i 1)))\n"
            "(= (child (b $i)) (b (+ $i 1)))\n"
            "(= (reach $x $t $d) (if (== $d 0) (if (== $x $t) 1 (empty)) (let $y (child $x) (reach $y $t (- $d 1)))))\n"
            f"(= (main) (reach (a 0) (a {n}) {n}))\n"
            "!(size-atom (collapse (main)))\n")


def fanout_main(k, width=64, mod=None):
    acc = "(+ (* $acc 7) $p)" if mod is None else f"(% (+ (* $acc 7) $p) {mod})"
    filt = "(== (% $s 97) 0)" if mod is None else "(== $s 0)"
    return (picks("pick", width) +
            f"(= (walk $k $acc) (if (== $k 0) $acc (let $p (pick) (walk (- $k 1) {acc}))))\n"
            f"(= (main) (let $s (walk {k} 0) (if {filt} $s (empty))))\n"
            "!(size-atom (collapse (main)))\n")


def agg(k):
    return (picks("pick", 16) +
            "(= (merge $a $b) (+ $a $b))\n"
            "(= (gen $k $acc) (if (== $k 0) $acc (let $p (pick) (gen (- $k 1) (+ (* $acc 3) $p)))))\n"
            f"!(foldall merge (let $s (gen {k} 0) (if (== (% $s 5) 0) 1 (empty))) 0)\n")


def pair_indep():
    return (picks("pick", 256) +
            "!(size-atom (collapse (let $a (pick) (let $b (pick) (if (== (% (* $a $b) 7) 3) (P $a $b) (empty))))))\n")


PROGRAMS = {
    "fanout_2": lambda: fanout(2),
    "fanout_3": lambda: fanout(3),
    "fanout_4": lambda: fanout(4),
    "fanout_flat3": fanout_flat3,
    "fanout_eqs3": fanout_eqs3,
    "fanmod_3": lambda: fanmod(3),
    "fanmod_4": lambda: fanmod(4),
    "queens_8": lambda: queens(8),
    "queens_9": lambda: queens(9),
    "perms_7": lambda: perms(7),
    "perms_8": lambda: perms(8),
    "magic3": magic3,
    "subset_18": lambda: subset(18, 40),
    "subset_22": lambda: subset(22, 50),
    "dag_6x6": lambda: dag(6, 6),
    "dag_8x6": lambda: dag(8, 6),
    "chain_10": lambda: chain(10),
    "chain_12": lambda: chain(12),
    "agg_5": lambda: agg(5),
    "pair_indep": pair_indep,
    "dagf_6x6": lambda: dagf(6, 6),
    "dagf_8x7": lambda: dagf(8, 7),
    "ladder_20": lambda: ladder(20),
    "ladder_23": lambda: ladder(23),
    "mfan_3": lambda: fanout_main(3),
    "mfan_4": lambda: fanout_main(4),
    "mfan_5": lambda: fanout_main(5),
    "mfanmod_3": lambda: fanout_main(3, mod=101),
    "mfanmod_4": lambda: fanout_main(4, mod=101),
    "mfanmod_5": lambda: fanout_main(5, mod=101),
    "mfanmod_6": lambda: fanout_main(6, mod=101),
}


def main():
    OUT.mkdir(exist_ok=True)
    names = sys.argv[1:] or list(PROGRAMS)
    for name in names:
        (OUT / f"{name}.metta").write_text(PROGRAMS[name]())
        print(OUT / f"{name}.metta")


if __name__ == "__main__":
    main()
