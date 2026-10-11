#!/usr/bin/env python3
"""Compare fresh native ready-order traces with the proved Lean definitions."""

import argparse
import hashlib
import itertools
import json
import os
from pathlib import Path
import random
import subprocess
import sys
import tempfile


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


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


def run(command, **kwargs):
    result = subprocess.run(command, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, **kwargs)
    if result.returncode:
        sys.stderr.write(result.stdout + result.stderr)
        result.check_returncode()
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mettapedia', type=Path, required=True)
    parser.add_argument('--lean-build', type=Path, required=True,
                        help='Successful --suite event-pump checker output directory')
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--cc', default=os.environ.get('CC', 'cc'))
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    metta = args.mettapedia.resolve()
    lean_build = args.lean_build.resolve()
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    receipt = output / 'results.json'
    receipt.write_text(json.dumps({'passed': False, 'phase': 'qualification'}, indent=2) + '\n')
    checks = json.loads((lean_build / 'results.json').read_text())
    if not checks or not all(item['returncode'] == 0 for item in checks):
        raise RuntimeError('Lean event-pump qualification has not passed')
    modules = {item['module']: item for item in checks if 'module' in item}
    if 'ReadyQueue' not in modules:
        raise RuntimeError('Lean qualification does not include ReadyQueue')
    qualified_sources = {}
    for name, item in modules.items():
        relative = Path(name.replace('.', '/') + '.lean')
        sources = [metta / 'lean/pettaclaw' / relative,
                   metta / 'lean/mettapedia' / relative]
        source = next((path for path in sources if path.is_file()), None)
        if source is None or digest(source) != item['source_sha256']:
            raise RuntimeError('Lean source changed after qualification: ' + name)
        qualified_sources[str(source.relative_to(metta))] = item['source_sha256']
    source = root / 'src/ready_queue.c'
    fixture = root / 'tests/test_ready_queue.c'
    binary = output / 'ready-queue'
    compile_flags = ['-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-I' + str(root / 'src')]
    run([args.cc, *compile_flags, str(fixture), str(source), '-o', str(binary)])
    unit = run([str(binary)], timeout=30)
    (output / 'native-unit.log').write_text(unit.stdout + unit.stderr)
    actions = list(commands())
    wire = '\n'.join(actions) + '\n'
    native = run([str(binary), '--trace'], input=wire, timeout=60)
    canonical = metta / 'lean/mettapedia'
    toolchain = (canonical / 'lean-toolchain').read_text().strip()
    env = dict(os.environ, LEAN_PATH=str(lean_build / 'lib'), LEAN_NUM_THREADS='1')
    oracle = metta / 'lean/pettaclaw/ReadyQueueOracle.lean'
    lean = run(['nice', '-n', '19', 'lean', '+' + toolchain, '-j', '1', '--run', str(oracle)],
               input=wire, env=env, cwd=metta / 'lean/pettaclaw', timeout=120)
    observed = native.stdout.splitlines()
    expected = lean.stdout.splitlines()
    if len(observed) != len(actions) or len(expected) != len(actions):
        raise RuntimeError(f'incomplete trace: actions={len(actions)}, native={len(observed)}, lean={len(expected)}')
    for index, (actual, wanted) in enumerate(zip(observed, expected)):
        if actual != wanted:
            raise AssertionError((index, actions[max(0, index-5):index+1], actual, wanted))
    (output / 'oracle.log').write_text(lean.stderr)

    mutants = {
        'pinned-head': ('if (task->next) {', 'if (0 && task->next) {'),
        'silent-retirement': ('    return task;\n}', '    cetta_ready_remove(queue, task);\n    return task;\n}'),
        'capacity-overrun': ('queue->count >= queue->capacity', 'queue->count > queue->capacity'),
    }
    rejected = []
    original = source.read_text()
    with tempfile.TemporaryDirectory(prefix='mutations-', dir=output) as temp:
        scratch = Path(temp)
        for name, (before, after) in mutants.items():
            if original.count(before) != 1:
                raise RuntimeError('mutation anchor changed: ' + name)
            mutated = scratch / (name + '.c')
            mutated.write_text(original.replace(before, after, 1))
            executable = scratch / name
            run([args.cc, *compile_flags, str(fixture), str(mutated), '-o', str(executable)])
            result = subprocess.run([str(executable)], cwd=scratch, stdout=subprocess.PIPE,
                                    stderr=subprocess.PIPE, text=True, timeout=30)
            if result.returncode == 0:
                raise AssertionError('incorrect queue passed: ' + name)
            rejected.append(name)
    result = {
        'passed': True,
        'trace_steps': len(actions),
        'exhaustive_depth': 4,
        'random_seed': 20261011,
        'mutations_rejected': rejected,
        'sources': {str(p.relative_to(root)): digest(p) for p in
                    [source, root / 'src/ready_queue.h', fixture, Path(__file__).resolve()]},
        'lean_sources': {**qualified_sources, str(oracle.relative_to(metta)): digest(oracle)},
        'toolchain': toolchain,
        'commands_sha256': hashlib.sha256(wire.encode()).hexdigest(),
        'binary_sha256': digest(binary),
        'scope': 'Finite native transition comparison; not a proof of C pointer safety or host progress',
    }
    receipt.write_text(json.dumps(result, indent=2, sort_keys=True) + '\n')
    print(json.dumps(result, sort_keys=True))


if __name__ == '__main__':
    main()
