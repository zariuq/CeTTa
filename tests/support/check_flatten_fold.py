#!/usr/bin/env python3
"""Ordered concatenation folds and their source/value boundary."""
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
!(foldl-atom ((1 2) () (2 3)) () $out $block (union-atom $out $block))
!(foldl-atom () () $out $block (union-atom $out $block))
!(let* (($left (cons-atom (quote (+ 1 2)) (cons-atom kept ())))
        ($right (list:from-expression (kept last))) ($blocks ($left $right)))
   (foldl-atom $blocks () $out $block (union-atom $out $block)))
; A written reduction and a captured value of the same spelling differ.
!(let $held (quote (+ 1 2)) (foldl-atom (a b) 0 $out $block $held))
!(let $held (quote (union-atom $out $block))
   (foldl-atom ((a) (b)) () $out $block $held))
!(foldl-atom ((1) invalid (2)) () $out $block (union-atom $out $block))
!(let $tail (cons-atom 1 $open)
   (collapse (foldl-atom ($tail) () $out $block (union-atom $out $block))))
!(let $blocks (map-atom (list:to-expression (list:range 512)) $i ($i 7 $i))
   (let $flat (foldl-atom $blocks () $out $block (union-atom $out $block))
     (Size (size-atom $flat) (index-atom $flat 0) (index-atom $flat 1535))))
'''
    observations = []
    for reference in ('0', '1'):
        run = subprocess.run(command + ['-e', source], cwd=ROOT, text=True,
            capture_output=True, timeout=60,
            env=dict(os.environ, CETTA_FLATTEN_FOLD_REFERENCE=reference,
                     CETTA_FLATTEN_FOLD_DEBUG='1'))
        assert run.returncode == 0, (run.stdout, run.stderr)
        observations.append(run.stdout)
        lines = run.stdout.splitlines()
        assert lines[:5] == ['true', '(1 2 2 3)', '()', '((+ 1 2) kept kept last)', '(+ 1 2)'], run.stdout
        assert '(Size 1536 0 511)' in lines, run.stdout
        if reference == '0':
            assert 'flatten-fold blocks=512 items=1536' in run.stderr, run.stderr
        else:
            assert 'flatten-fold' not in run.stderr, run.stderr
    assert observations[0] == observations[1], observations
    bounded = subprocess.run(command + ['--fuel', '100000', '-e', source[:source.index('!(let $blocks (map-atom')]],
        cwd=ROOT, text=True, capture_output=True, timeout=30,
        env=dict(os.environ, CETTA_FLATTEN_FOLD_DEBUG='1'))
    assert bounded.returncode == 0 and 'flatten-fold' not in bounded.stderr, (bounded.stdout, bounded.stderr)
    assert bounded.stdout == '\n'.join(observations[0].splitlines()[:-1]) + '\n', bounded.stdout
    print('FlattenFold 18 0')


if __name__ == '__main__':
    main()
