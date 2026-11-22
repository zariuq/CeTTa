"""Check document identity, relative imports, argument roles and explicit repeats."""
from pathlib import Path
import argparse
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tests/support'))
import check_transform_swi_differential as runner
import native_observation as observation

parser = argparse.ArgumentParser(description="Preserve module origins across physical source replacement")
parser.add_argument("binary", type=Path)
parser.add_argument("--work", required=True, type=Path)
parser.add_argument("--timeout", type=float, default=60)
args = parser.parse_args()
work = args.work.resolve()
work.mkdir(parents=True, exist_ok=True)
binary = args.binary.resolve()
for name in ['logical', 'physical', 'second-logical', 'second-physical']:
    (work / name).mkdir(exist_ok=True)

def run(arguments, label, successful=True):
    receipt = work / (label + '.events.jsonl')
    code, out, err = runner.run_group([str(binary), '--lang', 'petta',
        '--run-observation', str(receipt), *map(str, arguments)], str(ROOT), args.timeout)
    (work / (label + '.out')).write_text(out)
    (work / (label + '.err')).write_text(err)
    if not successful:
        assert code == 2, (label, code, out, err)
        return None
    assert code == 0, (label, code, out, err)
    found = observation.read_observation(receipt)
    assert found['final']['contract_exit'] == 0, label
    return observation.project(found, [(i, i) for i in found['queries']])

def compare(name, original, actual, origins, data=()):
    before = run([*original, *data], name + '-reference')
    flags = []
    for ordinal, path in origins:
        flags += ['--source-origin', str(ordinal), path]
    after = run([*flags, *actual, *data], name + '-origin')
    assert before == after, (name, before, after)
    print('same', name, flush=True)
    return before

logical, physical = work / 'logical', work / 'physical'
original, actual = logical / 'main.metta', physical / 'copy.metta'
(logical / 'helper.metta').write_text('(= (location) logical)\n')
(physical / 'helper.metta').write_text('(= (location) physical)\n')
text = '!(import! &self helper)\n!(location)\n!(get-args)\n'
original.write_text(text)
actual.write_text(text)
expected = compare('relative-import', [original], [actual], [(0, original)],
                   ['payload.data', '--source-origin', 'λ'])
assert run([actual, 'payload.data', '--source-origin', 'λ'], 'relative-negative') != expected

for directory in (logical, physical):
    (directory / 'consumer.metta').write_text('!(import! &self ' + str(original) + ')\n')
text = '(Row one)\n(Row one)\n!(import! &self consumer)\n!(collapse (match &self (Row $x) $x))\n'
original.write_text(text)
actual.write_text(text)
expected = compare('module-identity-authored-duplicates', [original], [actual], [(0, original)])
assert run([actual], 'identity-negative') != expected
assert expected[-1]['answers'] == [(('expression', 2), ('symbol', '6f6e65'), ('symbol', '6f6e65'))], expected

second, second_actual = work / 'second-logical/next.metta', work / 'second-physical/next.metta'
(second.parent / 'local.metta').write_text('(= (second-location) second-logical)\n')
(second_actual.parent / 'local.metta').write_text('(= (second-location) second-physical)\n')
text = '!(import! &self local)\n!(second-location)\n!(get-args)\n'
second.write_text(text)
second_actual.write_text(text)
compare('ordered-documents', [original, second], [actual, second_actual],
        [(0, original), (1, second)], ['payload.data', str(original), '"quoted"', 'سلام'])
compare('repeated-explicit-document', [original, original], [actual, actual],
        [(0, original), (1, original)])
compare('repeated-identical-metadata', [original], [actual], [(0, original), (0, original)])
compare('metadata-override', [original], [actual], [(0, second), (0, original)])

invalid = [
    ['--source-origin'],
    ['--source-origin', '-1', original, actual],
    ['--source-origin', 'not-a-number', original, actual],
    ['--source-origin', '18446744073709551616', original, actual],
    ['--source-origin', '1', original, actual],
]
for index, arguments in enumerate(invalid):
    run(arguments, 'invalid-' + str(index), successful=False)
print('PASS: six origin comparisons, two active negative controls and five metadata controls')
