#!/usr/bin/env python3
"""Compile deliberate matcher defects and require the graph gate to reject each."""
import argparse
import hashlib
import json
from pathlib import Path
import shlex
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--cc', required=True)
parser.add_argument('--flags', required=True)
parser.add_argument('--libs', required=True)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
output = args.output.resolve()
output.mkdir(parents=True, exist_ok=True)
compiler = shlex.split(args.cc)
flags = shlex.split(args.flags) + ['-DCETTA_RUNTIME_STATS_IMPL=1',
    '-ffunction-sections', '-fdata-sections']
sources = ['tests/test_term_match_graph.c', 'src/petta_semantics.c', 'src/atom.c',
           'src/symbol.c', 'src/binding/frame_identity.c', 'src/name_key.c',
           'src/atom_blob.c', 'src/term_canon.c']


def invoke(command):
    result = subprocess.run(command, cwd=root, capture_output=True, text=True, timeout=180)
    assert result.returncode == 0, (command, result.stdout[-3000:], result.stderr[-3000:])


objects = []
for index, source in enumerate(sources):
    target = output / f'common-{index}.o'
    invoke(compiler + flags + ['-c', source, '-o', str(target)])
    objects.append(str(target))

original = (root / 'src/term_graph.c').read_text()
begin = original.index('CettaTermMatchStatus term_graph_match_extend(')
prefix, kernel = original[:begin], original[begin:]


def replace_once(text, old, new):
    assert text.count(old) == 1, old
    return text.replace(old, new, 1)


mutants = {'control': original}
direction = replace_once(kernel,
    'Atom *pattern = mode == CETTA_TERM_MATCH_REVERSE',
    'Atom *pattern = mode != CETTA_TERM_MATCH_REVERSE')
direction = replace_once(direction,
    'Atom *subject = mode == CETTA_TERM_MATCH_REVERSE',
    'Atom *subject = mode != CETTA_TERM_MATCH_REVERSE')
mutants['wrong-direction'] = prefix + direction
mutants['inconsistent-hole'] = prefix + replace_once(kernel,
    'if (old != SIZE_MAX) {', 'if (old != SIZE_MAX) continue;\n            if (old != SIZE_MAX) {')
mutants['lost-protection'] = prefix + replace_once(kernel,
    'term_match_subject_vars(pairs, pair_count, mode, &subject_vars)',
    'term_match_subject_vars(pairs, 0u, mode, &subject_vars)')
mutants['noninjective-renaming'] = prefix + replace_once(kernel,
    'if (previous && previous->var_id != a.leaf->var_id)',
    'if (false && previous && previous->var_id != a.leaf->var_id)')
mutants['partial-publication'] = prefix + replace_once(kernel,
    'CettaTermMatchStatus result = CETTA_TERM_MATCH_NO_MEMORY;\n',
    'out->images.count = 0u;\n    CettaTermMatchStatus result = CETTA_TERM_MATCH_NO_MEMORY;\n')
receipts = []
for name, source in mutants.items():
    path = output / f'{name}.c'
    path.write_text(source)
    obj = output / f'{name}.o'
    binary = output / name
    invoke(compiler + flags + ['-c', str(path), '-o', str(obj)])
    invoke(compiler + flags + ['-Wl,--gc-sections', '-o', str(binary), str(obj)] +
           objects + shlex.split(args.libs))
    result = subprocess.run([str(binary)], cwd=root, capture_output=True, text=True, timeout=40)
    if name == 'control':
        assert result.returncode == 0 and 'PASS:' in result.stdout, result
    else:
        assert result.returncode == -6 and 'Assertion' in result.stderr, (
            name, result.returncode, result.stdout, result.stderr)
    receipts.append({'mutation': name, 'sha256': hashlib.sha256(source.encode()).hexdigest(),
                     'status': result.returncode, 'diagnostic': result.stderr.strip()})
(output / 'receipts.json').write_text(json.dumps(receipts, indent=2) + '\n')
print(f'PASS: control accepted and {len(mutants) - 1} native matcher defects rejected')
