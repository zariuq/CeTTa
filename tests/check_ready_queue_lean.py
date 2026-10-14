#!/usr/bin/env python3
"""Compare native ready-order traces with qualified canonical Lean definitions."""

import argparse
import hashlib
import itertools
import json
import os
from pathlib import Path
import random
import shutil
import subprocess
import time


def identity(path):
    path = Path(path).resolve()
    data = path.read_bytes()
    return {'path': str(path), 'bytes': len(data),
            'sha256': hashlib.sha256(data).hexdigest()}


def verify(row):
    if identity(row['path']) != row:
        raise RuntimeError('qualified input changed: ' + row['path'])


def commands():
    operations = [f'{action} {task}' for action in ('admit', 'remove')
                  for task in range(3)] + ['visit 0']
    # Every operation sequence of length four at every small capacity,
    # including zero. Duplicate admission/removal and head/tail cases occur.
    for capacity in range(4):
        for trace in itertools.product(operations, repeat=4):
            yield f'reset {capacity}'
            yield from trace
    # Longer reproducible traces exercise repeated link reuse and full queues.
    rng = random.Random(20261011)
    for capacity in (1, 2, 3, 8, 16):
        yield f'reset {capacity}'
        for _ in range(4000):
            action = rng.choice(('admit', 'remove', 'visit'))
            yield f'{action} {rng.randrange(16)}'


def qualified_lean(metta, build):
    receipt = build / 'checks.json'
    check = json.loads(receipt.read_text())
    if not check.get('passed') or not check.get('complete'):
        raise RuntimeError('canonical Lean qualification has not passed')
    sources = check['sources']
    artifacts = check['artifacts']
    steps = check['steps']
    if (not sources or set(sources) != set(artifacts) or not steps
            or any(step['exit'] != 0 for step in steps)):
        raise RuntimeError('incomplete canonical Lean build')
    required = ('Mettapedia.Machines.ReadyQueue',
                'Mettapedia.Machines.ReadyQueueOracle')
    canonical = metta / 'lean/mettapedia'
    rows = [identity(receipt), check['lean'], identity(canonical / 'lean-toolchain')]
    for module, source in sources.items():
        expected = canonical / (module.replace('.', '/') + '.lean')
        if Path(source['path']).resolve() != expected:
            raise RuntimeError('unexpected canonical source path: ' + module)
        rows.append(source)
        rows.extend(artifacts[module])
        if not artifacts[module]:
            raise RuntimeError('missing Lean product: ' + module)
    by_name = {step['name']: step for step in steps}
    for module in required:
        if module not in sources or module not in by_name:
            raise RuntimeError('qualification omits ' + module)
        step = by_name[module]
        product = build / 'lib' / (module.replace('.', '/') + '.olean')
        if (step['source'] != sources[module]
                or '-o' not in step['command']
                or Path(step['command'][step['command'].index('-o') + 1]).resolve() != product
                or not any(Path(row['path']).resolve() == product for row in artifacts[module])):
            raise RuntimeError('unbound Lean product: ' + module)
        rows.append(step['log'])
    # The private project products must take precedence over shared products.
    if Path(check['lean_path'].split(os.pathsep)[0]).resolve() != build / 'lib':
        raise RuntimeError('qualified project library is not first in LEAN_PATH')
    for row in rows:
        verify(row)
    return check, rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mettapedia', type=Path, required=True)
    parser.add_argument('--lean-build', type=Path, required=True,
                        help='Canonical joined Lean build with checks.json and retained lib products')
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--cc', default=os.environ.get('CC', 'cc'))
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    metta = args.mettapedia.resolve()
    lean_build = args.lean_build.resolve()
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    receipt = output / 'results.json'
    if receipt.exists():
        raise RuntimeError('output directory already contains an attempt')
    calls, guarded, products = [], [], []
    result = {'passed': False, 'phase': 'qualification'}

    def save():
        result.update(calls=calls, required=guarded, products=products)
        receipt.write_text(json.dumps(result, indent=2, sort_keys=True) + '\n')

    def run(command, label, *, stdin=None, env=None, timeout=60, reject=False):
        out, err = output / (label + '.stdout'), output / (label + '.stderr')
        start = time.monotonic()
        raw, timed_out = None, False
        try:
            with out.open('xb') as stdout, err.open('xb') as stderr:
                with (stdin.open('rb') if stdin else open(os.devnull, 'rb')) as input_file:
                    raw = subprocess.run(command, stdin=input_file, stdout=stdout,
                                         stderr=stderr, cwd=root, env=env,
                                         timeout=timeout).returncode
        except subprocess.TimeoutExpired:
            timed_out = True
            raise
        finally:
            calls.append({'command': command, 'cwd': str(root), 'raw_exit': raw,
                          'timed_out': timed_out, 'seconds': time.monotonic() - start,
                          'stdout': identity(out), 'stderr': identity(err),
                          'stdin': identity(stdin) if stdin else None,
                          'environment_overrides': {key: env[key] for key in
                              ('LEAN_PATH', 'LEAN_NUM_THREADS') if env and key in env}})
            save()
        if (reject and raw == 0) or (not reject and raw != 0):
            raise RuntimeError(f'{label}: unexpected exit {raw}')
        return out

    save()
    try:
        check, lean_rows = qualified_lean(metta, lean_build)
        compiler = shutil.which(args.cc)
        if not compiler:
            raise RuntimeError('C compiler not found: ' + args.cc)
        compiler = str(Path(compiler).resolve())
        source = root / 'src/ready_queue.c'
        header = root / 'src/ready_queue.h'
        fixture = root / 'tests/test_ready_queue.c'
        guarded = [*lean_rows, identity(source), identity(header), identity(fixture),
                   identity(__file__), identity(compiler)]
        result['phase'] = 'native-compile'
        save()
        compile_flags = ['-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-I' + str(root / 'src')]
        objects = []
        for name, unit in (('queue', source), ('fixture', fixture)):
            obj, dep = output / (name + '.o'), output / (name + '.d')
            run([compiler, *compile_flags, '-MMD', '-MF', str(dep), '-c', str(unit),
                 '-o', str(obj)], 'compile-' + name)
            objects.append(obj)
            products.extend((identity(obj), identity(dep)))
        binary = output / 'ready-queue'
        run([compiler, *compile_flags, *(str(x) for x in objects), '-o', str(binary)], 'link')
        products.append(identity(binary))
        run([str(binary)], 'native-unit', timeout=30)
        actions = list(commands())
        wire = '\n'.join(actions) + '\n'
        inputs = output / 'commands.txt'
        inputs.write_text(wire)
        products.append(identity(inputs))
        native = run([str(binary), '--trace'], 'native-trace', stdin=inputs, timeout=60)
        canonical = metta / 'lean/mettapedia'
        oracle = canonical / 'Mettapedia/Machines/ReadyQueueOracle.lean'
        env = dict(os.environ, LEAN_PATH=check['lean_path'], LEAN_NUM_THREADS='1')
        expected_path = run([check['lean']['path'], '-j', '1', '--run', str(oracle)],
                            'lean-trace', stdin=inputs, env=env, timeout=120)
        observed = native.read_text().splitlines()
        expected = expected_path.read_text().splitlines()
        if len(observed) != len(actions) or len(expected) != len(actions):
            raise RuntimeError(f'incomplete trace: actions={len(actions)}, native={len(observed)}, lean={len(expected)}')
        for index, (actual, wanted) in enumerate(zip(observed, expected)):
            if actual != wanted:
                raise AssertionError((index, actions[max(0, index-5):index+1], actual, wanted))
        result.update(phase='mutations', trace_steps=len(actions), exhaustive_depth=4,
                      random_seed=20261011, commands_sha256=identity(inputs)['sha256'])
        save()
        mutants = {
            'pinned-head': ('if (task->next) {', 'if (0 && task->next) {'),
            'silent-retirement': ('    return task;\n}', '    cetta_ready_remove(queue, task);\n    return task;\n}'),
            'capacity-overrun': ('queue->count >= queue->capacity', 'queue->count > queue->capacity'),
        }
        rejected = []
        original = source.read_text()
        scratch = output / 'mutations'
        scratch.mkdir()
        for name, (before, after) in mutants.items():
            if original.count(before) != 1:
                raise RuntimeError('mutation anchor changed: ' + name)
            mutated = scratch / (name + '.c')
            mutated.write_text(original.replace(before, after, 1))
            executable, obj, dep = scratch / name, scratch / (name + '.o'), scratch / (name + '.d')
            products.append(identity(mutated))
            run([compiler, *compile_flags, '-MMD', '-MF', str(dep), '-c', str(mutated),
                 '-o', str(obj)], 'compile-' + name)
            run([compiler, *compile_flags, str(objects[1]), str(obj), '-o', str(executable)], 'link-' + name)
            products.extend((identity(obj), identity(dep), identity(executable)))
            run([str(executable)], 'reject-' + name, timeout=30, reject=True)
            rejected.append(name)
        for row in [*guarded, *products]:
            verify(row)
        result.update(passed=True, phase='complete', mutations_rejected=rejected,
                      native_mutation_runs=len(rejected),
                      scope='Finite native transition comparison; not a proof of C pointer safety or host progress',
                      foreign_source_closure_complete=check.get('foreign_source_closure_complete', False))
    finally:
        changes = []
        for row in [*guarded, *products]:
            try:
                now = identity(row['path'])
            except FileNotFoundError:
                now = None
            if now != row:
                changes.append({'before': row, 'after': now})
        result['input_or_product_deltas'] = changes
        if changes:
            result['passed'] = False
        save()
    print(json.dumps(result, sort_keys=True))
    if not result['passed']:
        raise RuntimeError('ready queue qualification did not pass')


if __name__ == '__main__':
    main()
