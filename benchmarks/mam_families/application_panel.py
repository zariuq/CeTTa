#!/usr/bin/env python3
"""Qualify and measure whole external PeTTa applications on pinned inputs.

Preparation records the SWI oracle, every MeTTa/Prolog dependency, and both
engines' library contents. Run five alternating baseline/candidate/control
trials using the regional cost panel's calibrated rule. Wall time and peak
RSS are reported separately; a wall-time regression is never hidden by an
instruction improvement. Failed applications remain failed panel cells.
"""
import argparse
import importlib.util
import json
import os
import shutil
import subprocess
from pathlib import Path
import statistics
import sys


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def identities(root):
    result = {}
    for directory, children, files in os.walk(root, followlinks=True):
        children[:] = sorted(x for x in children if x not in
                             ('.git', 'target', 'runtime', '__pycache__', 'res'))
        for name in sorted(files):
            path = Path(directory) / name
            if path.suffix in ('.metta', '.pl', '.sh'):
                result[str(path.relative_to(root))] = cost.sha_file(path)
    return result


def roots(args):
    return {'chainer': args.chainer_root.resolve(),
            'petta': args.petta_root.resolve(),
            'cetta-library': (args.library_root or args.cetta_root / 'lib').resolve()}


def normalized(text, kind, locations):
    # Keep test verdicts, errors, order, and duplicate answers. Only the shared
    # corpus normalizer's precise historical compiler diagnostic is omitted.
    norm = corpus.normalize_oracle_stdout if kind == 'oracle' else corpus.normalize_cetta_stdout
    ordered = tuple(sorted(locations.values(), key=lambda p: -len(str(p))))
    return corpus.semantic_stdout(norm(text, ordered[0], ordered[1:]))


def cases(locations, selected):
    entries = [dict(name='chainer-' + name, family='chainer',
                    source='examples/' + name + '.metta',
                    cetta_args=['--lang', 'petta', '--profile', 'extended'],
                    oracle_args=['--strict-det'])
               for name in ('smokes', 'flyingraven',
                            'base_rate_acceptance_game_pettachainer',
                            'inheritance_query_proof')]
    # inheritance_query_proof is a test in some chainer revisions.
    for row in entries:
        if not (locations['chainer'] / row['source']).is_file():
            test = 'tests/test_' + row['name'][8:] + '.metta'
            if (locations['chainer'] / test).is_file(): row['source'] = test
    entries += [dict(name='holbenchmark', family='petta',
                     source='examples/holbenchmark.metta',
                     cetta_args=['--lang', 'petta'], oracle_args=[])]
    if selected:
        unknown = set(selected) - {row['name'] for row in entries}
        if unknown: raise ValueError('unknown applications: ' + ', '.join(sorted(unknown)))
        entries = [row for row in entries if row['name'] in selected]
    for row in entries:
        if not (locations[row['family']] / row['source']).is_file():
            raise ValueError('missing application: ' + row['source'])
    return entries


def prepare(args):
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    locations = roots(args)
    before = {name: identities(root) for name, root in locations.items()}
    manifest = dict(oracle_version=subprocess.run(['swipl', '--version'], capture_output=True, text=True).stdout.strip(),
                    library_selection='fixture' if args.library_root else 'runtime-default', roots={k: str(v) for k, v in locations.items()},
                    dependencies=before, oracle_command=args.oracle_command,
                    cases=[])
    for row in cases(locations, args.cases):
        cwd = locations[row['family']]
        command = [*args.oracle_command, str(cwd / row['source']),
                   *row['oracle_args'], '--silent']
        record = cost.runner.paired_measure(command, cwd, cost.ENVIRONMENT,
                    out / 'oracle' / row['name'], args.timeout, args.cpu, True)
        stdout = Path(record['stdout_file']).read_text()
        stderr = Path(record['stderr_file']).read_text()
        passed = record['rc'] == 0 and not record['timed_out'] and '❌' not in stdout
        if not passed: raise RuntimeError(('oracle failed', row['name'], record))
        row.update(oracle=record,
                   stdout=normalized(stdout, 'oracle', locations),
                   stderr=corpus.normalize_oracle_stderr(stderr, locations['petta'],
                                                        tuple(locations.values())))
        manifest['cases'].append(row)
    after = {name: identities(root) for name, root in locations.items()}
    if after != before: raise RuntimeError('dependencies changed while preparing oracle')
    (out / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    library = {str(path.relative_to(args.cetta_root)): cost.sha_file(path)
               for path in sorted((args.cetta_root / 'lib').rglob('*')) if path.is_file()}
    (out / 'library-identity.json').write_text(json.dumps(library, indent=2) + '\n')
    print('Prepared', len(manifest['cases']), 'checked applications', flush=True)


def check_inputs(manifest):
    locations = {k: Path(v) for k, v in manifest['roots'].items()}
    actual = {name: identities(root) for name, root in locations.items()}
    if actual != manifest['dependencies']: raise RuntimeError('application dependencies changed')
    return locations


def metric_gate(samples, field):
    translated = {}
    for label, rows in samples.items():
        translated[label] = {}
        for trial, row in rows.items():
            value = row.get(field)
            copy = dict(row)
            copy['instructions'] = int(value * 1e9) if field == 'native_wall' and value else value
            translated[label][trial] = copy
    return cost.gate.instruction_gate(*(translated[label] for label in cost.CALIBRATED))


def stage_binary(source, out, digest):
    destination = out / 'bin' / ('cetta-' + digest[:12])
    destination.parent.mkdir(exist_ok=True)
    if destination.exists():
        if cost.sha_file(destination) != digest: raise RuntimeError('staged binary changed')
    else:
        shutil.copy2(source, destination)
    return destination


def run(args):
    out = args.out.resolve()
    manifest = json.loads((out / 'manifest.json').read_text())
    locations = check_inputs(manifest)
    library = out / 'lib'
    if library.exists():
        if library.resolve() != locations['cetta-library']:
            raise RuntimeError('staged library root differs from manifest')
    else:
        library.symlink_to(locations['cetta-library'], target_is_directory=True)
    results = out / 'results'
    results.mkdir(exist_ok=False)
    original = dict(baseline=args.baseline.resolve(), candidate=args.candidate.resolve())
    original['control_a'] = original['control_b'] = original['candidate']
    identities = {label: cost.sha_file(path) for label, path in original.items()}
    binaries = {label: stage_binary(path, out, identities[label]) for label, path in original.items()}
    report = dict(cpu=args.cpu, trials=5, library_selection=manifest['library_selection'],
                  libraries=manifest['dependencies']['cetta-library'],
                  binaries={label: dict(path=str(path), sha256=identities[label]) for label, path in binaries.items()},
                  cells=[])
    def publish():
        (results / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
    for row in manifest['cases']:
        if args.cases and row['name'] not in args.cases: continue
        cwd = locations[row['family']]
        samples = {label: {} for label in cost.CALIBRATED}
        failed = set()
        cell = dict(name=row['name'], measurements=samples, status='running',
                    oracle=row['oracle'], oracle_source=manifest['oracle_command'])
        report['cells'].append(cell)
        for trial in range(5):
            for label in cost.CALIBRATED if trial % 2 == 0 else cost.CALIBRATED[::-1]:
                if label in failed: continue
                command = [str(binaries[label]), *row['cetta_args'], str(cwd / row['source'])]
                record = cost.runner.paired_measure(command, cwd, cost.ENVIRONMENT,
                          results / row['name'] / f'{label}-{trial}', args.timeout, args.cpu, True)
                stdout = Path(record['stdout_file']).read_text()
                stderr = Path(record['stderr_file']).read_text()
                observed = normalized(stdout, 'cetta', locations)
                err = corpus.normalize_oracle_stderr(stderr, locations['petta'], tuple(locations.values()))
                record.update(correct=record['rc'] == 0 and not record['timed_out'] and
                              observed == row['stdout'] and err == row['stderr'], trial=trial,
                              observation_sha256=cost.sha(json.dumps([observed, err])))
                samples[label][trial] = record
                if not record['correct']: failed.add(label)
                publish()
        cell['status'] = 'semantic-failure' if failed else 'measured'
        cell['failed_engines'] = sorted(failed)
        cell['instruction_gate'] = cost.gate.instruction_gate(*(samples[label] for label in cost.CALIBRATED))
        cell['wall_gate'] = metric_gate(samples, 'native_wall')
        cell['rss_gate'] = metric_gate(samples, 'rss_kib')
        for field, name in [('native_wall', 'medians_wall_seconds'), ('rss_kib', 'medians_rss_kib')]:
            cell[name] = {label: statistics.median(r[field] for r in series.values())
                          for label, series in samples.items() if series and all(r[field] is not None for r in series.values())}
        publish()
        check_inputs(manifest)
        print(row['name'], cell['status'], cell['instruction_gate']['status'],
              'wall', cell['wall_gate']['status'], 'rss', cell['rss_gate']['status'], flush=True)
    passed = bool(report['cells']) and all(c['status'] == 'measured' and c['instruction_gate']['accepted'] and
                          c['wall_gate']['accepted'] and c['rss_gate']['accepted'] for c in report['cells'])
    return 0 if passed else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', choices=('prepare', 'run'))
    parser.add_argument('out', type=Path)
    parser.add_argument('--cetta-root', type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument('--chainer-root', type=Path)
    parser.add_argument('--petta-root', type=Path)
    parser.add_argument('--library-root', type=Path, help='Explicit external library fixture; omitted uses the runtime libraries')
    parser.add_argument('--oracle-command', nargs='+')
    parser.add_argument('--baseline', type=Path)
    parser.add_argument('--candidate', type=Path)
    parser.add_argument('--cpu', type=int, required=True)
    parser.add_argument('--timeout', type=float, default=300)
    parser.add_argument('--cases', nargs='+')
    args = parser.parse_args()
    args.cetta_root = args.cetta_root.resolve()
    global cost, corpus
    cost = load('regional_cost_panel', args.cetta_root / 'benchmarks/mam_families/cost_panel.py')
    corpus = load('application_corpus', args.cetta_root / 'scripts/petta_corpus_manifest.py')
    if args.mode == 'prepare':
        if not all((args.chainer_root, args.petta_root, args.oracle_command)):
            parser.error('prepare requires both input roots and an oracle command')
        prepare(args)
        return 0
    if not all((args.baseline, args.candidate)):
        parser.error('run requires baseline and candidate')
    return run(args)


if __name__ == '__main__':
    sys.exit(main())
