#!/usr/bin/env python3
"""Compare selected finite exec batches implemented in MeTTa with MORK.

The engine under test uses ordinary MeTTa only. MORK is an external oracle.
Expected stores are specified independently. Structural membership and total
cardinality observe code-shaped data without evaluating it while printing.
"""
import argparse
from collections import Counter
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]

CASES = [
    dict(name='two-hop', initial=['(edge a b)', '(edge b c)',
        '(exec 0 (, (edge $x $y) (edge $y $z)) (, (path $x $z) (back $z $x)))'],
        keys=['0'], expected=['(edge a b)', '(edge b c)', '(path a c)', '(back c a)']),
    dict(name='duplicate-proposals', initial=['(edge a b)', '(edge a c)',
        '(exec 0 (, (edge a $x)) (, same same))'], keys=['0'],
        expected=['(edge a b)', '(edge a c)', 'same']),
    dict(name='snapshot', initial=['(edge a b)', '(edge b c)', '(edge c d)',
        '(exec 0 (, (edge $x $y) (edge $y $z)) (, (edge $x $z)))'], keys=['0'],
        expected=['(edge a b)', '(edge b c)', '(edge c d)', '(edge a c)', '(edge b d)']),
    dict(name='self-read', initial=['(exec 0 (, (exec 0 $q $t)) (, self-read))'],
        keys=['0'], expected=['self-read']),
    dict(name='created-rule-deferred', initial=['seed',
        '(exec 0 (, seed) (, (exec 1 (, seed) (, done))))'], keys=['0'],
        expected=['seed', '(exec 1 (, seed) (, done))']),
    dict(name='created-rule-runs-later', initial=['seed',
        '(exec 0 (, seed) (, (exec 1 (, seed) (, done))))'], keys=['0', '1'],
        expected=['seed', 'done']),
    dict(name='code-shaped-data', initial=[
        '(exec 0 (,) (, (+ 2 3) (Error data reason) Empty))'], keys=['0'],
        expected=['(+ 2 3)', '(Error data reason)', 'Empty']),
    dict(name='no-matches', initial=['seed', '(exec 0 (, absent) (, unexpected))'],
        keys=['0'], expected=['seed']),
    dict(name='empty-sink', initial=['seed', '(exec 0 (, seed) (,))'], keys=['0'],
        expected=['seed']),
    dict(name='multiple-matches-empty-sink', initial=['(edge a b)', '(edge a c)',
        '(exec 0 (, (edge $x $y)) (,))'], keys=['0'],
        expected=['(edge a b)', '(edge a c)']),
    dict(name='generated-variable-rule', initial=['(edge a b)', '(edge b c)',
        '(exec 0 (,) (, (exec 1 (, (edge $x $y)) (, (seen $x $y)))))'], keys=['0', '1'],
        expected=['(edge a b)', '(edge b c)', '(seen a b)', '(seen b c)']),
    dict(name='variable-head', initial=['(data a)', '(other b)',
        '(exec 0 (, ($tag $value)) (, (tagged $tag $value)))'], keys=['0'],
        expected=['(data a)', '(other b)', '(tagged data a)', '(tagged other b)']),
    dict(name='repeated-variable', initial=['(pair a a)', '(pair a b)',
        '(exec 0 (, (pair $x $x)) (, (same $x)))'], keys=['0'],
        expected=['(pair a a)', '(pair a b)', '(same a)']),
    dict(name='nested-pattern', initial=['(pair (node a) (node b))',
        '(exec 0 (, (pair (node $x) (node $y))) (, (pair (node $y) (node $x))))'],
        keys=['0'], expected=['(pair (node a) (node b))', '(pair (node b) (node a))']),
]


def digest(path): return hashlib.sha256(path.read_bytes()).hexdigest()


def run(command, cwd, env, stem):
    try:
        p = subprocess.run(command, cwd=cwd, env=env, capture_output=True, text=True, timeout=20)
        record = dict(command=command, rc=p.returncode, stdout=p.stdout, stderr=p.stderr)
    except subprocess.TimeoutExpired:
        record = dict(command=command, rc='timeout', stdout='', stderr='')
    stem.with_suffix('.json').write_text(json.dumps(record, indent=2) + '\n')
    return record


def observation_source(case):
    # Each expected atom is checked as a pattern. Ground cases plus total size
    # make missing, extra, and duplicate stored rows observable independently.
    lines = ['!(bind! &work (new-space))']
    lines += ['!(add-atom-nodup &work ' + atom + ')' for atom in case['initial']]
    lines += ['!(exec:step-key &work ' + key + ')' for key in case['keys']]
    lines += [f'!(match &work {atom} (verified {i}))'
              for i, atom in enumerate(case['expected'])]
    lines += ['!(let $rows (collapse (match &work $row unit)) '
              '(let $count (size-atom $rows) (cardinality $count)))']
    return '\n'.join(lines) + '\n'


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--cetta', type=Path, required=True)
    p.add_argument('--mork', type=Path, help='Optional external MORK oracle')
    p.add_argument('--cetta-root', type=Path, default=ROOT)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--engine', type=Path, default=ROOT / 'lib/lib_exec.metta')
    args = p.parse_args()
    args.out = args.out.resolve()
    args.cetta = args.cetta.resolve()
    if args.mork: args.mork = args.mork.resolve()
    args.out.mkdir(parents=True, exist_ok=False)
    runner_path = args.cetta_root / 'benchmarks/mam_families/run.py'
    spec = importlib.util.spec_from_file_location('runner', runner_path)
    runner = importlib.util.module_from_spec(spec); spec.loader.exec_module(runner)
    report = dict(cetta=dict(path=str(args.cetta), sha256=digest(args.cetta)),
                  mork=dict(path=str(args.mork), sha256=digest(args.mork)) if args.mork else None,
                  library_sha256=digest(args.engine), cases=[])
    for case in CASES:
        source = args.out / (case['name'] + '.metta')
        prefix = ('!(import! &self ' + json.dumps(str(args.engine.resolve())) + ')\n' if args.engine.resolve() == ROOT / 'lib/lib_exec.metta'
                  else args.engine.read_text())
        source.write_text(prefix + observation_source(case))
        mm2 = args.out / (case['name'] + '.mm2')
        mm2.write_text('\n'.join(case['initial']) + '\n')
        store = args.out / (case['name'] + '.store')
        # This pinned MORK CLI checks its bound after interpreting one exec:
        # --steps 0 fires one selected directive, --steps 1 fires two.
        row = dict(name=case['name'], mork_correct=None, routes=[])
        if args.mork:
            result = run([str(args.mork), 'run', str(mm2), '--steps', str(len(case['keys']) - 1),
                          '--instrumentation', '0', str(store)], HERE, os.environ,
                         args.out / (case['name'] + '-mork'))
            observed = store.read_text().splitlines() if store.exists() else []
            observed = [line.strip() for line in observed if line.strip()]
            row['mork_correct'] = result['rc'] == 0 and Counter(observed) == Counter(case['expected'])
        expected = Counter([f'(verified {i})' for i in range(len(case['expected']))] +
                           [f'(cardinality {len(case["expected"])})'])
        for language in ('petta', 'he'):
            for reference, shared in ([('0', '1'), ('1', '1')] if language == 'petta' else
                                      [('0', '1'), ('1', '1'), ('0', '0'), ('1', '0')]):
                env = dict(os.environ, CETTA_OPEN_EQUATIONS_REFERENCE=reference,
                           CETTA_HE_SHARED_EXECUTION=shared)
                result = run([str(args.cetta), '--lang', language, '--profile', 'extended', str(source)],
                             HERE, env, args.out / f'{case["name"]}-{language}-{reference}-{shared}')
                atoms = runner.answer_atoms(result['stdout'], language == 'he')
                markers = [atom for atom in atoms if atom.startswith(('(verified ', '(cardinality '))]
                good = result['rc'] == 0 and not result['stderr'] and Counter(markers) == expected
                row['routes'].append(dict(language=language, reference=reference, shared=shared, correct=good,
                                          observed=markers))
        report['cases'].append(row)
        (args.out / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
        print(case['name'], row['mork_correct'], [r['correct'] for r in row['routes']], flush=True)
    return 0 if all((c['mork_correct'] is not False) and all(r['correct'] for r in c['routes'])
                    for c in report['cases']) else 1

if __name__ == '__main__': raise SystemExit(main())
