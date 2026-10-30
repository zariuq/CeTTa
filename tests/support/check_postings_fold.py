#!/usr/bin/env python3
"""Indexed numeric-state folds against authored and independent observations."""
import argparse
import json
import os
from pathlib import Path
import random
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def sexpr(value):
    if isinstance(value, str):
        return json.dumps(value)
    if isinstance(value, list):
        return '(' + ' '.join(map(sexpr, value)) + ')'
    text = repr(value)
    if isinstance(value, float) and 'e' in text and '.' not in text.split('e')[0]:
        text = text.replace('e', '.0e')
    return text


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cetta', required=True, type=Path)
    args = parser.parse_args()
    prefix = '''!(import! &self (library postings))
(= (fold-summary (Summary $n $sum) $group $count $weight)
   (Summary (+ $n $count) (+ $sum (* $weight $count))))
(= (fold-bucket $group $weight) (- $group (% $group 2)))
(= (fold-signed-key $group $weight) (if (< $weight 0) (- 0 $group) $group))
(= (fold-copy (Summary $n $sum) $group $count $weight) (Summary (+ $n $count) $weight))
(= (fold-identity $state $group $count $weight) $state)
'''
    rng = random.Random(22101)
    calls = []
    for size in (0, 1, 7, 32, 97, 257):
        blocks = [[[f'k{rng.randrange(4)}' for _ in range(rng.randrange(1, 4))],
                   rng.choice([-1, 0, 3, 4294967296]), rng.randrange(-2, 10)]
                  for _ in range(size)]
        if size == 257:
            blocks = [[[f'k{i%4}'], i*4294967296, i%7] for i in range(size)]
        queries = [[f'k{rng.randrange(5)}', rng.choice([1e16, 1.0, -1e16, 0.25])]
                   for _ in range(9)]
        for bound in (-3, 0, 5, 11):
            totals = {}
            for key, weight in queries:
                counts = {}
                for keys, group, position in blocks:
                    if position < bound:
                        for candidate in keys:
                            if candidate == key:
                                counts[group] = counts.get(group, 0) + 1
                for group, count in sorted(counts.items()):
                    n, score = totals.get(group, (0, 0.0))
                    totals[group] = n + count, score + weight * count
            expected = '(' + ' '.join(f'({group} (Summary {n} {sexpr(score)}))'
                                      for group, (n, score) in totals.items()) + ')'
            calls.append(f'''!(let $p (postings:prepare-blocks {sexpr(blocks)})
              (== (postings:fold-table-before $p {sexpr(queries)} {bound}
                    (Summary 0 0.0) fold-summary) {expected}))''')
            buckets = {}
            for key, weight in queries:
                counts = {}
                for keys, group, position in blocks:
                    if position < bound:
                        for candidate in keys:
                            if candidate == key:
                                counts[group] = counts.get(group, 0) + 1
                for group, count in sorted(counts.items()):
                    bucket = 2 * (group // 2)
                    n, score = buckets.get(bucket, (0, 0.0))
                    buckets[bucket] = n + count, score + weight * count
            expected = '(' + ' '.join(f'({group} (Summary {n} {sexpr(score)}))'
                                      for group, (n, score) in buckets.items()) + ')'
            calls.append(f'''!(let $p (postings:prepare-blocks {sexpr(blocks)})
              (== (postings:fold-table-by-before $p {sexpr(queries)} {bound}
                    fold-bucket (Summary 0 0.0) fold-summary) {expected}))''')
    # Structural hashes cannot represent the numeric-promotion equality used
    # by authored matching. Probe both ingress and lookup, also inside keys.
    for stored, requested in [('1', '1.0'), ('1.0', '1'),
                              ('(key 1)', '(key 1.0)'), ('-0.0', '0.0')]:
        calls.append(f'''!(let $p (postings:prepare-blocks ((({stored}) 0 0)))
          (== (postings:fold-table-before $p (({requested} 0.25)) 1
                (Summary 0 0.0) fold-summary) ((0 (Summary 1 0.25)))))''')
    calls.append('''!(let $p (postings:prepare-blocks
      (((9007199254740992) 0 0) ((9007199254740993) 0 0)))
      (== (postings:fold-table-before $p ((9007199254740992.0 0.25)) 1
        (Summary 0 0.0) fold-summary) ((0 (Summary 1 0.25)))))''')
    calls.append('''!(let $p (postings:prepare-blocks ((("a") 2 0) (("a") 3 0)))
      (== (postings:fold-table-by-before $p (("a" 0.25) ("a" -1.0)) 1
            fold-signed-key (Summary 0 0.0) fold-summary)
          ((2 (Summary 1 0.25)) (3 (Summary 1 0.25))
           (-2 (Summary 1 -1.0)) (-3 (Summary 1 -1.0)))))''')
    # Column observation retains first-group order, field dtypes and scalar
    # bits. It is the ordinary row observation followed by typed construction.
    column_blocks = '((("a" "a") 9 0) (("b") -2 0) (("a") 9 3))'
    for queries, bound in [('()', 4), ('(("a" 0.25) ("b" 0.5))', 0),
                           ('(("a" 0.25) ("b" 0.5))', 1),
                           ('(("b" 0.25) ("a" -1.0))', 4)]:
        calls.append(f'''!(let* (($p (postings:prepare-blocks {column_blocks}))
          ($columns (postings:fold-table-columns-before $p {queries} {bound}
            postings:group-identity (Summary 0 0.0) fold-summary (i64 f64)))
          ($rows (postings:fold-table-by-reference $p {queries} {bound}
            postings:group-identity (Summary 0 0.0) fold-summary)))
          (== $columns (postings:columns-from-rows (i64 f64) $rows)))''')
    calls.append('''!(let* (($p (postings:prepare-blocks ((("a") 9 0) (("b") -2 0))))
        ($columns (postings:fold-table-columns-before $p (("a" 0.25) ("b" 0.5)) 1
          postings:group-identity (Summary 0 0.0) fold-summary (i64 f64))))
      (== $columns (postings:column-result (tensor:from-values i64 (2) (9 -2))
        ((tensor:from-values i64 (2) (1 1)) (tensor:from-values f64 (2) (0.25 0.5))))))''')
    for weights, step, expected in [('(0.0)', 'fold-copy', '0.0'),
                                     ('(-0.0)', 'fold-copy', '-0.0'),
                                     ('(1.0e16 1.0 -1.0e16)', 'fold-summary', '0.0')]:
        queries = '(' + ' '.join(f'("a" {weight})' for weight in weights[1:-1].split()) + ')'
        calls.append(f'''!(let* (($p (postings:prepare-blocks ((("a") 0 0))))
          ($columns (postings:fold-table-columns-before $p {queries} 1
            postings:group-identity (Summary 0 0.0) {step} (i64 f64))))
          (== (index-atom (postings:column-values $columns) 1)
              (tensor:from-values f64 (1) ({expected}))))''')
    # Unsupported native leaf types and mixed int/float columns take the
    # authored constructors; exact integer values do not become float fields.
    calls.append('''!(let* (($p (postings:prepare-blocks ((("a") 0 0))))
        ($columns (postings:fold-table-columns-before $p (("a" 7)) 1
          postings:group-identity (Summary 0 0.0) fold-copy (i64 f64))))
      (== (index-atom (postings:column-values $columns) 1)
          (tensor:from-values f64 (1) (7.0))))''')
    calls.append('''!(let* (($p (postings:prepare-blocks ((("a") 0 0))))
        ($columns (postings:fold-table-columns-before $p (("a" 0.25)) 1
          postings:group-identity (Summary 0 0.0) fold-summary (i64 f32))))
      (== (index-atom (postings:column-values $columns) 1)
          (tensor:from-values f32 (1) (0.25))))''')
    calls.append('''!(let $p (postings:prepare-blocks ((("a") 0 0)))
      (== (postings:fold-table-columns-before $p (("a" 0.25)) 1
            postings:group-identity (Summary 0 0.0) fold-summary (f64))
          (Postings:Fault column-state)))''')
    calls.append('''!(let* (($p (postings:prepare-blocks ((("a") 0 0))))
                           ($alias $p) ($closed (postings:close $p)))
      (== (postings:fold-table-columns-before $alias () 1
            postings:group-identity (Summary 0 0.0) fold-summary (i64 f64))
          (Postings:Fault closed-or-forged-index)))''')
    wide = ' '.join(str(i) for i in range(32))
    wide_types = ' '.join(['i64'] * 32)
    calls.append(f'''!(let* (($p (postings:prepare-blocks ((("a") 9 0) (("b") -2 0))))
        ($columns (postings:fold-table-columns-before $p (("a" 1.0) ("b" 1.0)) 1
          postings:group-identity (Wide {wide}) fold-identity ({wide_types})))
        ($expected (map-atom ({wide}) $field (tensor:from-values i64 (2) ($field $field)))))
      (== $columns (postings:column-result (tensor:from-values i64 (2) (9 -2))
          $expected)))''')
    varying_blocks = [[['a'] * (i % 5 + 1), i, 0] for i in range(17)]
    expected_labels = ' '.join(str(i) for i in range(17))
    expected_counts = ' '.join(str(i % 5 + 1) for i in range(17))
    expected_sums = ' '.join(str((i % 5 + 1) * 0.25) for i in range(17))
    calls.append(f'''!(let* (($p (postings:prepare-blocks {sexpr(varying_blocks)}))
        ($columns (postings:fold-table-columns-before $p (("a" 0.25)) 1
          postings:group-identity (Summary 0 0.0) fold-summary (i64 f64))))
      (== $columns (postings:column-result (tensor:from-values i64 (17) ({expected_labels}))
          ((tensor:from-values i64 (17) ({expected_counts}))
           (tensor:from-values f64 (17) ({expected_sums}))))))''')
    # Same captured data is reused through an ordinary function, then a new
    # equation invalidates the callback's single-answer admission.
    controls = '''
(= (fold-carrier $state $group $count $weight)
   (if (== (get-metatype $state) List) (Summary 111 0.0) (Summary 222 0.0)))
(= (fold-return-list $state $group $count $weight)
   (__cetta_lib_list_from_expression (Summary 0 0.0)))
!(let* (($p (postings:prepare-blocks ((("a") 0 0))))
        ($initial (__cetta_lib_list_from_expression (Summary 0 0.0))))
   (== (postings:fold-table-before $p (("a" 1.0)) 1 $initial fold-carrier)
       ((0 (Summary 111 0.0)))))
!(let* (($p (postings:prepare-blocks ((("a") 0 0))))
        ($rows (postings:fold-table-before $p (("a" 1.0)) 1
                 (Summary 0 0.0) fold-return-list)))
   (== (get-metatype (index-atom (index-atom $rows 0) 1)) List))
(= (fold-choice $state $group $count $weight)
   (superpose ((Summary 1 1.0) (Summary 2 2.0))))
!(let* (($p (postings:prepare-blocks ((("a") 0 0)))) ($space (context-space)))
   (== (postings:try-table-counts $p table-fold-before
         ($space (("a" 1.0)) 1 postings:group-identity (Summary 0 0.0) fold-choice)) Postings:Fallback))
!(let $p (postings:prepare-blocks ((("a") 0 0)))
   (postings:fold-table-before $p (("a" 1.0)) 1 (Summary 0 0.0) fold-choice))
(= (fold-late (State 0) $group $count $payload) (State 1))
(= (fold-late (State 1) $group $count $payload) (State 2))
(= (fold-late (State 1) $group $count $payload) (State 3))
!(let* (($p (postings:prepare-blocks ((("a") 0 0)))) ($space (context-space)))
   (== (postings:try-table-counts $p table-fold-before
         ($space (("a" 1.0) ("a" 1.0)) 1 postings:group-identity (State 0) fold-late)) Postings:Fallback))
!(let $p (postings:prepare-blocks ((("a") 0 0)))
   (postings:fold-table-before $p (("a" 1.0) ("a" 1.0)) 1 (State 0) fold-late))
(= (fold-effect $state $group $count $weight)
   (let $saved (add-atom &fold-effects observed) $state))
!(let* (($p (postings:prepare-blocks ((("a") 0 0) (("a") 1 0))))
        ($rows (postings:fold-table-before $p (("a" 1.0)) 1
                 (Summary 0 0.0) fold-effect)))
   (Effects $rows (size-atom (collapse (match &fold-effects observed yes)))))
(= (project-effect $group $payload)
   (let $saved (add-atom &projection-effects observed) 0))
!(let* (($p (postings:prepare-blocks ((("a") 2 0) (("a") 3 0))))
        ($rows (postings:fold-table-by-before $p (("a" 0.25)) 1
                 project-effect (Summary 0 0.0) fold-summary)))
   (ProjectedEffects $rows (size-atom (collapse (match &projection-effects observed yes)))))
(= (project-late 0 $payload) 0)
(= (project-late 1 $payload) 0)
(= (project-late 1 $payload) 2)
!(let $p (postings:prepare-blocks ((("a") 0 0) (("a") 1 0)))
   (postings:fold-table-by-before $p (("a" 0.25)) 1
     project-late (Summary 0 0.0) fold-summary))
!(let* (($p (postings:prepare-blocks ((("a") 0 0))))
        ($columns (postings:fold-table-columns-before $p (("a" 1.0)) 1
          postings:group-identity (Summary 0 0.0) fold-choice (i64 f64))))
   (ColumnChoice (tensor:inspect (index-atom (postings:column-values $columns) 1))))
!(let* (($p (postings:prepare-blocks ((("a") 0 0))))
        ($columns (postings:fold-table-columns-before $p (("a" 1.0)) 1
          postings:group-identity (Summary 0 0.0) fold-effect (i64 f64))))
   (ColumnEffects (tensor:inspect (index-atom (postings:column-values $columns) 0))
     (size-atom (collapse (match &fold-effects observed yes)))))
!(let* (($p (postings:prepare-blocks ((("a") 0 0)))) ($alias $p)
        ($closed (postings:close $p)))
   (postings:fold-table-before $alias () 1 (Summary 0 0.0) fold-summary))
'''
    command = [str(args.cetta.resolve()), '--lang', 'petta']
    observations = []
    for reference in ('0', '1'):
        run = subprocess.run(command + ['-e', prefix + '\n'.join(calls) + controls],
                             cwd=ROOT, env=dict(os.environ, CETTA_POSTINGS_FOLD_REFERENCE=reference),
                             text=True, capture_output=True, timeout=90)
        assert run.returncode == 0, (run.stdout, run.stderr)
        lines = run.stdout.splitlines()
        assert lines[1:1+len(calls)] == ['true'] * len(calls), run.stdout
        assert lines[1+len(calls):3+len(calls)] == ['true', 'true'], run.stdout
        assert 'Postings:Fallback' not in '\n'.join(lines[1:1+len(calls)]), run.stdout
        assert '(Effects ((0 (Summary 0 0.0)) (1 (Summary 0 0.0))) 2)' in lines, run.stdout
        assert '(ProjectedEffects ((0 (Summary 2 0.5))) 2)' in lines, run.stdout
        assert '((0 (Summary 2 0.5)))' in lines, run.stdout
        assert '((0 (Summary 1 0.25)) (2 (Summary 1 0.25)))' in lines, run.stdout
        assert '((0 (Summary 1 1.0)))' in lines and '((0 (Summary 2 2.0)))' in lines, run.stdout
        assert '((0 (State 2)))' in lines and '((0 (State 3)))' in lines, run.stdout
        assert '(ColumnChoice (TensorData f64 (1) ((0 1.0))))' in lines, run.stdout
        assert '(ColumnChoice (TensorData f64 (1) ((0 2.0))))' in lines, run.stdout
        assert '(ColumnEffects (TensorData i64 (1) ((0 0))) 3)' in lines, run.stdout
        assert lines[-1] == '(Postings:Fault closed-or-forged-index)', run.stdout
        observations.append((run.stdout, run.stderr))
    assert observations[0] == observations[1], observations
    run = subprocess.run(command + ['-e', prefix + '\n'.join(calls)], cwd=ROOT,
        env=dict(os.environ, CETTA_POSTINGS_REFERENCE='1'), text=True,
        capture_output=True, timeout=90)
    assert run.returncode == 0 and run.stdout.splitlines()[1:] == ['true']*len(calls), (
        run.stdout, run.stderr)
    probe = prefix + '''!(let* (($p (postings:prepare-blocks ((("a") 0 0))))
       ($space (context-space)))
     (postings:try-table-counts $p table-fold-before
       ($space (("a" 0.25)) 1 postings:group-identity (Summary 0 0.0) fold-summary)))
!(let* (($p (postings:prepare-blocks ((("a") 0 0)))) ($space (context-space)))
   (case (postings:try-table-counts $p table-fold-columns-before
       ($space (("a" 0.25)) 1 postings:group-identity (Summary 0 0.0) fold-summary (i64 f64)))
     (((Postings:Columns $labels $columns) ColumnReady) ($other $other))))'''
    for flags, reference, expected in [([], '0', ['((0 (Summary 1 0.25)))', 'ColumnReady']),
                                      ([], '1', ['Postings:Fallback'] * 2),
                                      (['--fuel', '100000'], '0', ['Postings:Fallback'] * 2)]:
        run = subprocess.run(command + flags + ['-e', probe], cwd=ROOT,
            env=dict(os.environ, CETTA_POSTINGS_FOLD_REFERENCE=reference),
            text=True, capture_output=True, timeout=30)
        assert run.returncode == 0 and run.stdout.splitlines()[-2:] == expected, (run.stdout, run.stderr)
    projected_probe = prefix + '''!(let* (
       ($p (postings:prepare-blocks ((("a") 2 0) (("a") 3 0)))) ($space (context-space)))
     (postings:try-table-counts $p table-fold-before
       ($space (("a" 0.25)) 1 fold-bucket (Summary 0 0.0) fold-summary)))'''
    run = subprocess.run(command + ['-e', projected_probe], cwd=ROOT,
        text=True, capture_output=True, timeout=30)
    assert run.returncode == 0 and run.stdout.splitlines()[-1] == '((2 (Summary 2 0.5)))', (
        run.stdout, run.stderr)
    public_api = prefix.replace('(library postings)', 'postings') + '''
!(let $p (postings:prepare-blocks ((("a") 0 0) (("a") 1 1)))
   (postings:fold-table-by-before $p (("a" 0.25)) 2 fold-bucket (Summary 0 0.0) fold-summary))
!(let $p (postings:prepare-blocks ((("a") 0 0) (("a") 1 1)))
   (if (== (postings:fold-table-columns-before $p (("a" 0.25)) 2
             fold-bucket (Summary 0 0.0) fold-summary (i64 f64))
           (postings:columns-from-rows (i64 f64)
             (postings:fold-table-by-reference $p (("a" 0.25)) 2
               fold-bucket (Summary 0 0.0) fold-summary))) ColumnsOK ColumnsBad))'''
    for language, flags in [('he', ['--profile', 'extended']), ('prime', [])]:
        run = subprocess.run([str(args.cetta.resolve()), '--lang', language, *flags,
                              '-e', public_api], cwd=ROOT, text=True, capture_output=True, timeout=30)
        assert run.returncode == 0 and run.stdout.splitlines()[-2:] == [
            '[((0 (Summary 2 0.5)))]', '[ColumnsOK]'], (
            language, run.stdout, run.stderr)
    print(f'PostingsFold {len(calls)*3+36} 0')


if __name__ == '__main__':
    main()
