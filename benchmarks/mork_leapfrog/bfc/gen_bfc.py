#!/usr/bin/env python3
"""Backward chaining done as forward chaining: Hilbert-style proof search.

Nil Geisweiller's backward-via-forward chainer, in the form Adam Vandervorst
optimized and baked into MORK (`mork bench bfc`, kernel/src/main.rs, upstream
1c44f70).  The theorem to prove is a target formula; the search works
backwards from it by stating, at each depth, the open subgoals as stored
open terms

    (sol depth hyps (C (/ premise conclusion) proof))

with variables in them.  One round turns depth k+1 into depth k by unifying a
premise with one of the three Lukasiewicz axioms (closing it) or by inverse
modus ponens (opening a fresh premise $a).  At depth 0 with no open
hypotheses, a solution whose formula unifies with the target is a proof.

What this asks of an engine: stored terms with variables, two-sided
unification during the join, identification of alpha-equivalent facts (MORK
gets it from its de Bruijn encoding), and an exec that writes the next round's
execs (staging).

Size knob: the theorem, by the size of its shortest proof
(id 5, pm2.43 7, jarr 13, imim1 15, loowoz 19, pm2.83 25, loolin 26).
The space grows roughly exponentially in the proof size.

Writes NAME.mm2, NAME.metta, NAME.heads, NAME.values, NAME.expect.json.
A second knob, --bound N, searches a theorem for proofs of exactly N steps
(N above its shortest proof size finds longer proofs and many more subgoals).

usage: gen_bfc.py SIZE [--bound N] [OUTDIR]
"""
import json, sys
from pathlib import Path

# (theorem, expected proofs) from MORK kernel/src/main.rs `fn bfc`, upstream 1c44f70
TARGETS = {
    5: ("(> p p)", ["(1 (1 (2 (M (M I)))))"]),
    7: ("(> (> p (> p s)) (> p s))", ["(1 (2 (M (2 (2 (M (M I)))))))"]),
    13: ("(> (> (> p s) x) (> s x))", ["(1 (1 (1 (M (2 (2 (M (M (1 (M (2 (M (M I)))))))))))))",
                                      "(1 (1 (M (1 (2 (1 (M (2 (M (M (2 (M (M I)))))))))))))"]),
    15: ("(> (> p s) (> (> s x) (> p x)))", ["(1 (1 (2 (1 (M (2 (M (M (2 (M (1 (M (2 (M (M I)))))))))))))))"]),
    19: ("(> (> (> p s) (> p x)) (> (> s p) (> s x)))",
         ["(1 (1 (1 (M (2 (2 (M (M (1 (M (2 (M (M (2 (1 (M (2 (M (M I)))))))))))))))))))",
          "(1 (1 (1 (M (2 (2 (M (M (2 (1 (M (2 (M (M (1 (M (2 (M (M I)))))))))))))))))))",
          "(1 (1 (M (1 (2 (1 (M (2 (M (M (2 (M (M (2 (1 (M (2 (M (M I)))))))))))))))))))"]),
    25: ("(> (> p (> s x)) (> (> p (> x o)) (> p (> s o))))", None),
    26: ("(> (> (> p s) (> s p)) (> s p))", None),
}

AXIOMS = """(axiom 1 (> $p (> $s $p)))
(axiom 2 (> (> $p (> $s $x)) (> (> $p $s) (> $p $x))))
(axiom 3 (> (> (! $p) (! $s)) (> $s $p)))
"""

# Adam's program, verbatim apart from comments
MM2_PROGRAM = """
(exec (2)
    (, (target $mps (C $ta $tx)))
    (, ;; Initialize source
       (sol $mps 1 (C (/ $ta $ta) I))
       ;; Expand one step forward
       (exec (2 $mps)
            (, ;; Capture inner self
               (exec (2 $ski) $ptrn $tplt)
               (dec $ski $ki)
               (gte $ki $hi)
               (inc $hi $shi)
               ;; for each axiom
               (axiom $r $constraint))
            (, ;; Apply current proof to axiom-$r
               (exec (3 $r)
                    (, (sol $ski $shi (C (/ $constraint $b) $f)))
                    (, (sol $ki $hi (C $b ($r $f)))))
               ;; Apply mp^i to current proof
               (exec (4 0)
                    (, (sol $ski $hi (C (/ $b $c) $f)))
                    (, (sol $ki $shi (C (/ (> $a $b) (/ $a $c)) (M $f)))))
               ;; Respawn inner self
               (exec (7 0)
                    (, (lte 0 0))
                    (, (exec (2 $ki) $ptrn $tplt)))))))

;; Unify solution with target when maximum depth and hypotheses count
;; falls to Z.
(exec (3 0 0)
      (, (target $mps (C $ta $tx))
         (sol 0 0 (C $ta $tx)))
      (, (final 0 0 (C $ta $tx))))
"""

METTA_PROGRAM = """
; the open subgoals of each depth live in a set space, as MORK's facts do
!(bind! &t (new-space set))
!(match &self (target $mps (C $ta $tx)) (add-atom &t (sol $mps 1 (C (/ $ta $ta) I))))
; hypothesis counts 0..n
(= (upto $n) (if (< $n 0) (empty) $n))
(= (upto $n) (if (< $n 1) (empty) (let $m (- $n 1) (upto $m))))
; one round, depth sk to depth k = sk-1: close a premise with an axiom ...
(= (axiom-step $sk $k)
   (let* (($hi (upto $k)) ($shi (+ $hi 1)))
     (match &self (axiom $r $c)
       (match &t (sol $sk $shi (C (/ $c $b) $f))
         (add-atom &t (sol $k $hi (C $b ($r $f))))))))
; ... or open a fresh premise by inverse modus ponens
(= (mp-step $sk $k)
   (let* (($hi (upto $k)) ($shi (+ $hi 1)))
     (match &t (sol $sk $hi (C (/ $b $c) $f))
       (add-atom &t (sol $k $shi (C (/ (> $a $b) (/ $a $c)) (M $f)))))))
(= (rounds $sk)
   (if (< $sk 1) done
       (let* (($k (- $sk 1))
              ($x (size-atom (collapse (axiom-step $sk $k))))
              ($y (size-atom (collapse (mp-step $sk $k)))))
         (rounds $k))))
!(match &self (target $mps $_) (rounds $mps))
!(match &self (target $mps (C $ta $tx))
   (match &t (sol 0 0 (C $ta $tx)) (let $_ (add-atom &t (final 0 0 (C $ta $tx))) (final 0 0 (C $ta $tx)))))
!(size-atom (collapse (match &t (sol $a $b $c) t)))
!(size-atom (collapse (match &t (final $a $b $c) t)))
"""

def main():
    args = sys.argv[1:]
    bound = None
    if '--bound' in args:
        i = args.index('--bound'); bound = int(args[i + 1]); del args[i:i + 2]
    size = int(args[0])
    outdir = Path(args[1]) if len(args) > 1 else Path(__file__).resolve().parent / 'gen'
    outdir.mkdir(parents=True, exist_ok=True)
    formula, proofs = TARGETS[size]
    name = f'bfc_{size}'
    if bound is not None and bound != size:
        name = f'bfc_{size}_b{bound}'
        proofs = None
    target = f"(target {bound or size} (C {formula} $x))\n"
    tables = ''.join(f"(dec {x} {x - 1})\n(inc {x - 1} {x})\n" for x in range(1, 27))
    tables += ''.join(f"(lte {y} {x})\n(gte {x} {y})\n" for x in range(0, 27) for y in range(0, x + 1))
    (outdir / f'{name}.mm2').write_text(f"; {name}: prove {formula}\n" + target + tables + AXIOMS + MM2_PROGRAM)
    (outdir / f'{name}.metta').write_text(f"; {name}: prove {formula}, forward rounds as in MM2\n" + target + AXIOMS + METTA_PROGRAM)
    (outdir / f'{name}.heads').write_text('sol final\n')
    (outdir / f'{name}.values').write_text('final\n')
    if proofs is not None:
        (outdir / f'{name}.expect.json').write_text(json.dumps(
            {'final': len(proofs), 'proofs': [f"(final 0 0 (C {formula} {p}))" for p in proofs]}))
    print(name, formula, 'expected proofs:', None if proofs is None else len(proofs))

if __name__ == '__main__':
    main()
