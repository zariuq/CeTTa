#!/usr/bin/env python3
"""Private map/flat-map ingress against its authored execution route."""
import argparse
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cetta', required=True, type=Path)
    args = parser.parse_args()
    command = [str(args.cetta.resolve()), '--lang', 'petta']
    source = '''!(import! &self (library list))
!(let $held (quote (+ 1 2)) (map-atom (a b) $x $held))
!(let $held (quote (== 1 1)) (filter-atom (a b) $x $held))
!(let* (($s (new-space)) ($items (1 2 1))
        ($stored (let $rows (map-atom $items $x (row $x)) (add-atoms $s $rows))))
   (collapse (get-atoms $s)))
!(let* (($s (new-space)) ($items (1 2))
        ($stored (let $rows
          (foldl-atom (map-atom $items $x
            (cons-atom (row $x) (cons-atom (again $x) ())))
            () $out $batch (union-atom $out $batch))
          (add-atoms $s $rows))))
   (collapse (get-atoms $s)))
!(let* (($s (new-space)) ($held (quote (+ 1 2)))
        ($items (list:to-expression (list:range 37)))
        ($stored (let $rows (map-atom $items $x $held) (add-atoms $s $rows))))
   (Held (size-atom (collapse (match $s (+ 1 2) kept)))))
; A compiled relation must retain the producer/consumer composition too.
(= (ingress-through-call $s $items)
   (let $rows (map-atom $items $x (wrapped $x)) (add-atoms $s $rows)))
(= (ingress-under-case $items)
   (case $items (($ready
     (let* (($s (new-space)) ($stored (ingress-through-call $s $ready)))
       (Wrapped (collapse (get-atoms $s))))))))
!(ingress-under-case (5 4 3 2 1))
(= (ingress-held-call $s $items $value)
   (let $rows (map-atom $items $x $value) (add-atoms $s $rows)))
!(let* (($s (new-space)) ($items (a b)) ($held (quote (+ 1 2)))
        ($stored (ingress-held-call $s $items $held)))
   (HeldCall (collapse (get-atoms $s))))
(= (ingress-revisable $x) (old-row $x))
(= (ingress-revision-call $items)
   (let* (($s (new-space))
          ($stored (let $rows (map-atom $items $x (ingress-revisable $x))
                     (add-atoms $s $rows))))
     (Revision (collapse (get-atoms $s)))))
!(ingress-revision-call (1 2))
!(remove-atom &self (= (ingress-revisable $x) (old-row $x)))
(= (ingress-revisable $x) (new-row $x))
!(ingress-revision-call (1 2))
; Captured tables are values even when their elements look executable.
(= (ingress-table-entry $table $position) (index-atom $table (if (< $position 9) 0 1)))
!(let* (($s (new-space)) ($table (quote ((+ 1 2) (+ 3 4))))
        ($positions (list:to-expression (list:range 17)))
        ($stored (let $rows (map-atom $positions $position
                   (ingress-table-entry $table $position)) (add-atoms $s $rows))))
   (CapturedTable (size-atom (collapse (match $s (+ 1 2) kept)))
                  (size-atom (collapse (match $s (+ 3 4) kept)))))
; A later ambiguous projection cannot publish its earlier accepted prefix.
(= (ingress-choice 0) (choice a))
(= (ingress-choice 1) (choice b))
(= (ingress-choice 1) (choice c))
!(let* (($s (new-space)) ($items (0 1))
        ($stored (let $rows (map-atom $items $x (ingress-choice $x)) (add-atoms $s $rows))))
   (ChoiceSnapshot (collapse (get-atoms $s))))
; Effects run once on the authored route, before destination publication.
!(let* (($s (new-space)) ($effects (new-space)) ($items (1 2 3))
        ($stored (let $rows (map-atom $items $x
                   (let $done (add-atom $effects observed) (effect-row $x)))
                   (add-atoms $s $rows))))
   (Effects (size-atom (collapse (get-atoms $effects))) (collapse (get-atoms $s))))
!(let* (($s (new-space)) ($seed (add-atom $s sentinel)) ($items (3 2 1))
        ($caught (catch (let $rows (map-atom $items $x (row (/ 1 (- $x 2))))
                          (add-atoms $s $rows)))))
   (FaultBoundary (collapse (get-atoms $s))))
; Declaration delivery belongs to the canonical program mutation boundary.
!(let* (($s (new-space)) ($equation (quote (= (ingress-created) 7)))
        ($items (prefix $equation))
        ($stored (let $rows (map-atom $items $x $x) (add-atoms $s $rows))))
   (MetadataBoundary (evalc (ingress-created) $s) (size-atom (collapse (get-atoms $s)))))
; A collection also observed outside the sink is not private to the append.
!(let* (($s (new-space)) ($items (4 5)))
   (let $rows (map-atom $items $x (row $x))
     (let $stored (add-atoms $s $rows) (Escaping $rows))))
!(let* (($s (new-space)) ($items ()))
   (let $done (let $rows (map-atom $items $x (row $x)) (add-atoms $s $rows))
     (Empty (collapse (get-atoms $s)))))
; Intersection observes ordered occurrences, including exhausted RHS copies.
(= (ingress-intersection $s $items $right)
   (let $rows (map-atom $items $x (Common $x (intersection-atom ($x $x) $right)))
     (add-atoms $s $rows)))
!(let* (($s (new-space)) ($items (list:to-expression (list:range 19)))
        ($stored (ingress-intersection $s $items (1 2 3))))
   (IntersectionSnapshot (collapse (get-atoms $s))))
!(let* (($s (new-space)) ($items ((a a b c) (b b a) ()))
        ($stored (let $rows (map-atom $items $x
                   (Common (intersection-atom $x (a b b)))) (add-atoms $s $rows))))
   (IntersectionOrder (collapse (get-atoms $s))))
!(let* (($s (new-space)) ($items (1 2))
        ($stored (let $rows (map-atom $items $x
          (Common (intersection-atom (cons-atom $x (cons-atom $x ()))
                                    (cons-atom $x ())))) (add-atoms $s $rows))))
   (IntersectionSpines (collapse (get-atoms $s))))
!(let* (($s (new-space)) ($items (a b)) ($held (quote ((+ 1 2) (+ 3 4))))
        ($stored (let $rows (map-atom $items $x
          (Common $x (intersection-atom $held $held))) (add-atoms $s $rows))))
   (IntersectionHeld (collapse (get-atoms $s))))
; An empty left operand does not hide RHS effects.
!(let* (($s (new-space)) ($effects (new-space)) ($items (1 2))
        ($stored (let $rows (map-atom $items $x
          (Common (intersection-atom ()
            (let $_ (add-atom $effects seen) (a b))))) (add-atoms $s $rows))))
   (IntersectionEffects (size-atom (collapse (get-atoms $effects)))
     (collapse (get-atoms $s))))
; A declared String can still name code; Grounded metatype is not inertness.
; A late callable head must retain its evaluation and effect on replay.
(: ingress-callable-item String)
(= (ingress-callable-item) (let $_ (println! "CallableItem") (2)))
!(let* (($s (new-space)) ($items (0 1 2 3 4 5 ingress-callable-item))
        ($stored (let $rows (map-atom $items $x
                    (Common (intersection-atom ($x) (1 2))))
                   (add-atoms $s $rows))))
   (CallableSnapshot (collapse (get-atoms $s))))
'''
    observations = []
    for reference in ('0', '1'):
        run = subprocess.run(command + ['-e', source], cwd=ROOT, text=True,
            capture_output=True, timeout=60,
            env=dict(os.environ, CETTA_COLLECTION_INGRESS_REFERENCE=reference,
                     CETTA_FLATTEN_FOLD_REFERENCE=reference,
                     CETTA_COLLECTION_INGRESS_DEBUG='1'))
        assert run.returncode == 0, (run.stdout, run.stderr)
        observations.append(run.stdout)
        lines = run.stdout.splitlines()
        structured_lines = [line for line in lines if line != '"CallableItem"']
        assert structured_lines[:5] == ['true', '((+ 1 2) (+ 1 2))', '()',
            '((row 1) (row 2) (row 1))', '((row 1) (again 1) (row 2) (again 2))'], run.stdout
        for expected in ['(Held 37)', '(Wrapped ((wrapped 5) (wrapped 4) (wrapped 3) (wrapped 2) (wrapped 1)))',
                         '(HeldCall ((+ 1 2) (+ 1 2)))',
                         '(Revision ((old-row 1) (old-row 2)))',
                         '(Revision ((new-row 1) (new-row 2)))',
                         '(CapturedTable 9 8)',
                         '(ChoiceSnapshot ((choice a) (choice b)))',
                         '(ChoiceSnapshot ((choice a) (choice b) (choice a) (choice c)))',
                         '(Effects 3 ((effect-row 1) (effect-row 2) (effect-row 3)))',
                         '(FaultBoundary (sentinel))', '(MetadataBoundary 7 2)',
                         '(Escaping ((row 4) (row 5)))', '(Empty ())']:
            assert expected in lines, run.stdout
        intersection_rows = ' '.join(
            f'(Common {value} ({value}))' if value in (1, 2, 3) else f'(Common {value} ())'
            for value in range(19))
        for expected in [f'(IntersectionSnapshot ({intersection_rows}))',
                         '(IntersectionOrder ((Common (a b)) (Common (b b a)) (Common ())))',
                         '(IntersectionSpines ((Common (1)) (Common (2))))',
                         '(IntersectionHeld ((Common a ((+ 1 2) (+ 3 4))) (Common b ((+ 1 2) (+ 3 4)))))',
                         '(IntersectionEffects 2 ((Common ()) (Common ())))']:
            assert expected in lines, run.stdout
        assert '(CallableSnapshot ((Common ()) (Common (1)) (Common (2)) (Common ()) (Common ()) (Common ()) (Common (2))))' in lines, run.stdout
        assert run.stdout.count('CallableItem') == 1, run.stdout
        assert 'collection-ingress committed rows=7' not in run.stderr, run.stderr
        if reference == '0':
            assert 'collection-ingress committed rows=37' in run.stderr, run.stderr
            assert 'collection-ingress committed rows=4' in run.stderr, run.stderr
            assert 'collection-ingress committed rows=5' in run.stderr, run.stderr
            assert 'collection-ingress committed rows=17' in run.stderr, run.stderr
            assert 'collection-ingress committed rows=19' in run.stderr, run.stderr
        else:
            assert 'collection-ingress committed' not in run.stderr, run.stderr
    assert observations[0] == observations[1], observations
    # Semantic fuel selects the authored route, without a speculative append.
    run = subprocess.run(command + ['--fuel', '1000000', '-e', source],
        cwd=ROOT, text=True, capture_output=True, timeout=60,
        env=dict(os.environ, CETTA_COLLECTION_INGRESS_DEBUG='1'))
    assert run.returncode == 0 and 'collection-ingress committed' not in run.stderr, (
        run.stdout, run.stderr)
    assert run.stdout == observations[0], run.stdout
    print('CollectionIngress 59 0')


if __name__ == '__main__':
    main()
