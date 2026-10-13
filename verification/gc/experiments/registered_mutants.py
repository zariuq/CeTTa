#!/usr/bin/env python3
"""Control faults for the counted-leaf protocol; no engine or network required."""
import argparse
import json
from pathlib import Path
import resource
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parents[3]
args.output.mkdir(parents=True, exist_ok=True)
source = (root / 'src/registered_symbols.c').read_text()
cases = {
    'borrow-without-hold': ('bool ok = retain(table, entry);\n    if (ok) *out = (RegisteredSymbolBorrow)',
                           'bool ok = entry != NULL;\n    if (ok) *out = (RegisteredSymbolBorrow)'),
    'clone-without-hold': ('bool ok = retain(table, find_owned(table, source));',
                          'bool ok = find_owned(table, source) != NULL;'),
    'keep-index-high-water': ('(void)rehash(table, table->capacity / 2);', '(void)0;'),
}
resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
results = []
for name, (before, after) in cases.items():
    assert source.count(before) == 1, name
    mutant = args.output / (name + '.c')
    mutant.write_text(source.replace(before, after))
    binary = args.output / name
    command = ['cc', '-std=c11', '-O1', '-g', '-pthread', '-DCETTA_TEST_HOOKS',
               '-I' + str(root / 'src'), str(mutant),
               *[str(root / x) for x in ('src/registered_symbol_owner.c', 'src/atom.c',
                   'src/symbol.c', 'src/binding/frame_identity.c', 'tests/test_registered_symbols.c')],
               '-lm', '-o', str(binary)]
    build = subprocess.run(command, text=True, capture_output=True, timeout=60)
    (args.output / (name + '.build.log')).write_text(build.stdout + build.stderr)
    if build.returncode:
        raise SystemExit('Control failed to compile: ' + name)
    run = subprocess.run([str(binary)], text=True, capture_output=True, timeout=30)
    (args.output / (name + '.run.log')).write_text(run.stdout + run.stderr)
    detected = run.returncode != 0 and 'Assertion' in run.stderr
    results.append({'mutant': name, 'compiled': True, 'exit': run.returncode, 'detected': detected})
    if not detected:
        raise SystemExit('Fault escaped the assertions: ' + name)
(args.output / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
print(json.dumps(results))
