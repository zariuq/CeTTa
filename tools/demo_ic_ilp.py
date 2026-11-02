#!/usr/bin/env python3
"""Run the native IC/ILP example and independently check its exported artifact."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tests/support'))

from check_binary_codec import parse_forms
from check_ic_context_library import encoded
from check_ic_family_transfer import payload
from check_ic_family_experiment import library_sources
from check_ic_library import plain
from check_ic_library_stack import restore
from check_ic_mil_source import background_bytes, family
from check_source_codec import reference


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cetta', type=Path, default=ROOT / 'cetta')
    parser.add_argument('--output', type=Path, default=ROOT / 'runtime/ic-ilp-demo')
    args = parser.parse_args()
    binary = args.cetta.resolve(strict=True)
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    source = ROOT / 'lib/petta/lib_ic_numeric_arrays.metta'
    source_bytes = source.read_bytes()
    original = reference(source_bytes.decode())
    example = ROOT / 'examples/petta/ic_ilp_poc.metta'
    example_text = example.read_text()
    dependencies = library_sources([example_text, source_bytes.decode()])
    for name in ('metagol_core', 'metagol_prime'):
        path = ROOT / 'lib/ilp' / (name + '.metta')
        dependencies[path] = path.read_text()
    binary_sha = hashlib.sha256(binary.read_bytes()).hexdigest()

    def run(path):
        result = subprocess.run([str(binary), '--lang', 'petta', str(path)], cwd=ROOT,
                                capture_output=True, text=True, timeout=180)
        (output / (path.stem + '.stdout')).write_text(result.stdout)
        (output / (path.stem + '.stderr')).write_text(result.stderr)
        if result.returncode or result.stderr:
            raise RuntimeError(f'{path.name} failed: {result.stderr[-2000:]}')
        return parse_forms(result.stdout)

    def one(rows, tag):
        found = [row for row in rows if isinstance(row, list) and row[:1] == [tag]]
        if len(found) != 1:
            raise AssertionError(f'expected one {tag}, found {len(found)}')
        return found[0]

    rows = run(example)
    faults = [row for row in rows if isinstance(row, list) and row[:1] == ['Poc:Fault']]
    assert not faults, faults
    compression = one(rows, 'Poc:Compression')
    _, before, after, statistics, reason, fault, resumed, reconstructed, raw_layers, wire, rendered = compression
    before, after = int(before), int(after)
    assert reason == 'RegionExhausted' and fault == 'IC:NoFault', compression[:8]
    assert resumed == reconstructed == 'true', compression[:8]
    assert int(statistics[3]) > 0 and int(statistics[4]) == 0, statistics
    artifact = payload(wire)
    text = payload(rendered).decode()
    document = reference(text)
    layers = [(json.loads(codec), json.loads(name)) for _tag, codec, name in raw_layers]
    assert layers == [('LC2', 'ic-learned:kind')], layers
    assert artifact == encoded(layers[0][1], document), 'independent codec disagrees'
    assert before == len(plain(original)) and after == len(artifact) and after < before
    assert restore(layers, document) == original, 'independent expansion disagrees'
    (output / 'learned.sic').write_bytes(artifact)
    (output / 'learned-library.metta').write_text(text + '\n')
    # syntax:write renders one source form per line in this example.
    helper = text.splitlines()[0]
    assert reference(helper)[3] == document[3][:1]
    (output / 'learned-helper.metta').write_text(helper + '\n')

    next_step = one(rows, 'Poc:Next')
    assert next_step[1:3] == [str(after), str(after)], next_step
    assert next_step[4:] == ['RegionExhausted', 'IC:NoFault', 'true'], next_step

    ilp = one(rows, 'Poc:ILP')
    _, positive, selected, contradiction, wrong_sort, predictions, background = ilp
    assert len(positive) == 2 and len(selected) == 1
    assert contradiction == wrong_sort == []
    assert selected[0] in positive
    assert predictions == [[
        ['Poc:Prediction', ['"F64Tensor"', '"Unknown"', '"BoolTensor"', '"I64Tensor"', '"BoolTensor"'],
         [['false', 'false', 'true', 'true', 'true']]],
        ['Poc:Prediction', [], [[]]],
        ['Poc:Prediction', ['"I64Tensor"', '"F64Tensor"'], [['true', 'false']]],
    ]], predictions
    extra_text = next(form[2] for form in parse_forms(example_text)
                      if isinstance(form, list) and form[:2] == ['=', ['poc:extra-source']])
    background_document = [*document[:3], document[3][:1] + reference(json.loads(extra_text))[3]]
    declarations = family([
        ('ic-learned:kind', 'TensorKind', 'Criterion'),
        ('poc:is-exact', 'Criterion', 'Bool'), ('poc:leaky-test', 'Criterion', 'Bool'),
    ], [('IC:Some', 1), ('IC:None', 0), ('Numeric:Exact', 0)])
    background_wire = payload(background)
    assert background_wire == background_bytes(declarations, background_document), 'ILP vocabulary changed'
    (output / 'ilp-background.sb1').write_bytes(background_wire)

    # Exercise the enclosing source functions as well as the extracted helper.
    queries = [(operation, kind) for operation in ('encode-criterion', 'decode-criterion')
               for kind in ('BoolTensor', 'I64Tensor', 'F64Tensor', 'Unknown')]
    calls = '\n'.join(f'!(println! (PocAnswer {i} (ic-arrays:{operation} "{kind}" Numeric:Exact)))'
                      for i, (operation, kind) in enumerate(queries))
    expected = [['PocAnswer', str(i), ['IC:Some', 'Numeric:Exact'] if kind in ('BoolTensor', 'I64Tensor') else 'IC:None']
                for i, (_, kind) in enumerate(queries)]
    for variant, code in [('original', source_bytes.decode()), ('learned', text)]:
        path = output / (variant + '-execution.metta')
        path.write_text(code + '\n' + calls + '\n')
        answers = [row for row in run(path) if isinstance(row, list) and row[:1] == ['PocAnswer']]
        assert answers == expected, (variant, answers)
    assert source.read_bytes() == source_bytes, 'source changed during the experiment'
    assert example.read_text() == example_text, 'example changed during the experiment'
    assert all(path.read_text() == content for path, content in dependencies.items()), 'library changed during the experiment'
    assert hashlib.sha256(binary.read_bytes()).hexdigest() == binary_sha, 'runtime changed during the experiment'

    receipt = {
        'passed': True,
        'source': str(source.relative_to(ROOT)),
        'source_sha256': hashlib.sha256(source_bytes).hexdigest(),
        'binary_sha256': binary_sha,
        'example_sha256': hashlib.sha256(example_text.encode()).hexdigest(),
        'library_sha256': {str(path.relative_to(ROOT)): hashlib.sha256(content.encode()).hexdigest()
                          for path, content in sorted(dependencies.items())},
        'before_bytes': before, 'after_bytes': after, 'saved_bytes': before - after,
        'helper': helper, 'search_statistics': statistics, 'completion_reason': reason,
        'resumption_identical': True, 'independent_codec_and_reconstruction': True,
        'following_search': next_step[1:], 'execution_cases_per_variant': len(queries),
        'ilp_positive_only_candidates': positive, 'ilp_selected': selected,
        'ilp_contradiction_refused': True, 'ilp_wrong_sort_refused': True,
        'ilp_predictions': predictions,
        'ilp_uses_unchanged_learned_definition': True,
        'observation': 'exact admitted source syntax; comments and whitespace are outside ST1',
        'scope': '12 structurally ranked seed pairs per IC stage; one supplied map/chain ILP skeleton; finite examples',
    }
    (output / 'result.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print(f'IC learned from {source.relative_to(ROOT)}')
    print(helper)
    print(f'Complete archive: {before:,} -> {after:,} bytes ({before - after:,} saved)')
    print('Reconstruction: native and independent checks passed; stopped/resumed search agrees')
    print(f'Following search: {next_step[4]}, no further saving in its declared region')
    print(f'Original and rewritten code: {len(queries)} execution cases agree')
    print('Typed ILP: 2 candidates fit the positive example; the negative example selects 1')
    print('New input: (F64Tensor Unknown BoolTensor I64Tensor BoolTensor)')
    print('Prediction: (False False True True True); empty and duplicate inputs also checked')
    print('Artifacts: learned-helper.metta, learned-library.metta, learned.sic, result.json')


if __name__ == '__main__':
    main()
