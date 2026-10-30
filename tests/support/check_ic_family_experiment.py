#!/usr/bin/env python3
"""Frozen, family-separated real-source IC experiment; retains negative transfers."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import time

from check_binary_codec import ROOT, parse_forms, tensor
from check_ic_context_library import encoded as single_encoded
from check_ic_family_transfer import payload
from check_ic_library import is_symbol, plain
from check_ic_library_stack import encoded as stack_encoded, restore
from check_ic_library_stack_transfer import reuse
from check_ic_library_transfer import transfer
from check_ic_search import check_controls
from check_source_codec import reference, text_arg


CORPORA = {'list-combinator', 'term-row', 'tensor-postings'}


def canonical_source(text):
    """Whole-document clone key, ignoring bound names and defined-head renaming."""
    document = reference(text)
    heads = {}
    for form in document[3]:
        term = form[2]
        if term[0] != 'Syntax:Expr' or len(term[1]) != 3 or not is_symbol(term[1][0], '='):
            continue
        left = term[1][1]
        head = left[1][0] if left[0] == 'Syntax:Expr' and left[1] else left
        if head[:2] == ['Syntax:Literal', 'Symbol']:
            heads.setdefault(head[2], len(heads))

    def canonical(value):
        if isinstance(value, bytes):
            return {'bytes': value.hex()}
        if not isinstance(value, list):
            return value
        if value[:2] == ['Syntax:Literal', 'Symbol'] and value[2] in heads:
            return ['DefinedHead', heads[value[2]]]
        if value[:1] == ['Syntax:PlainName']:
            return ['Syntax:PlainName']
        return [canonical(item) for item in value]

    return hashlib.sha256(json.dumps(canonical(document), sort_keys=True).encode()).hexdigest()


def library_sources(texts):
    """Snapshot the ordinary PeTTa import closure, without executing imports."""
    pending, captured = list(texts), {}
    while pending:
        for name in re.findall(r'\(library\s+([A-Za-z0-9_:-]+)\)', pending.pop()):
            path = ROOT / 'lib/petta' / (name + '.metta')
            if path in captured:
                continue
            text = path.read_text()
            captured[path] = text
            pending.append(text)
    return captured


def validate_manifest(manifest, sources):
    if manifest.get('version') != 1:
        raise ValueError('unknown manifest version')
    training, evaluation = manifest['training'], manifest['evaluation']
    if not training or not evaluation:
        raise ValueError('both partitions are required')
    if any(row.get('corpus') not in CORPORA or not row.get('family') or not row.get('id')
           for row in training + evaluation):
        raise ValueError('missing corpus, family or identity')
    if {row['corpus'] for row in training} != CORPORA or {row['corpus'] for row in evaluation} != CORPORA:
        raise ValueError('all three corpora are required in both partitions')
    if {row['family'] for row in training} & {row['family'] for row in evaluation}:
        raise ValueError('a program family crosses the partition')
    identifiers, assignments = set(), {}
    for partition, rows in (('training', training), ('evaluation', evaluation)):
        for row in rows:
            if row['id'] in identifiers:
                raise ValueError('duplicate source identity')
            identifiers.add(row['id'])
            for path in [row['source'], *row.get('variants', [])]:
                assigned = (partition, row['family'])
                if path in assignments and assignments[path] != assigned:
                    raise ValueError('source or generated variant crosses the partition')
                assignments[path] = assigned
    clone_keys = {}
    for partition, rows in (('training', training), ('evaluation', evaluation)):
        for row in rows:
            key = canonical_source(sources[row['source']])
            if key in clone_keys and clone_keys[key] != partition:
                raise ValueError('renamed source clone crosses the partition')
            clone_keys[key] = partition


HELPERS = r'''
!(import! &self (library lib_ic_source_seeds))
!(import! &self (library lib_ic_library_transfer))
!(import! &self (library system))
(= (source-family:stage-result $original $before $region $start $ready $result)
   (case $result
     (((IC:SearchResult $best $statistics $reason $fault $next)
       (let* (($end (system:monotonic-ns)) ($released (ic:release $next)))
         (case (library-stack:view $best)
           (((Library:StackValue $layers $document $restored)
             (println! (FamilyStage $region $reason $statistics $fault
               (== $original $restored) (- $ready $start) (- $end $ready)
               (tensor:inspect $best) $layers (tensor:inspect (binary:utf8 (syntax:write $document))))))
            ($invalid (println! (FamilyFault $invalid)))))))
      ($invalid-search (println! (FamilyFault $invalid-search))))))
(= (source-family:train $artifact $suffix $limit $quantum $budget)
   (let $start (system:monotonic-ns)
     (case (library-stack:view $artifact)
       (((Library:StackValue $layers $document $original)
         (let $prepared (ic:prepare-library Deferred $document)
           (case (ic:source-seed-region $prepared $limit)
             (((Library:SeedRegion $pairs $total)
               (case (ic:continue-library Deferred $artifact $suffix $quantum (Library:Seeds $pairs))
                 (((IC:PreparedSearch $state)
                   (let $ready (system:monotonic-ns)
                     (source-family:stage-result $original $artifact (Library:SeedRegion $pairs $total)
                       $start $ready (ic:resume $state $budget))))
                  ($refused (println! (FamilyFault $refused))))))
              ($bad-region (println! (FamilyFault $bad-region)))))))
        ($bad-artifact (println! (FamilyFault $bad-artifact)))))))
(= (source-family:receive $original $proposal)
   (case $proposal
     (((Library:Candidate $bytes $support)
       (let $before (ic:source-pack $original)
         (case (library-stack:view $bytes)
           (((Library:StackValue $layers $document $restored)
             (println! (FamilyReceived $support (== $original $restored)
               (ic:library-check $before $before $bytes)
               (tensor:inspect $bytes) $layers (tensor:inspect (binary:utf8 (syntax:write $document))))))
            ($invalid (println! (FamilyFault $invalid)))))))
      ($refused (println! (FamilyRefused $refused))))))
'''


def encoded(layers, document):
    if not layers:
        return plain(document)
    return single_encoded(layers[0][1], document) if len(layers) == 1 else stack_encoded(layers, document)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cetta', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--manifest', type=Path, default=ROOT / 'tests/fixtures/ic_source_families.json')
    parser.add_argument('--stages', type=int, default=2)
    parser.add_argument('--seeds', type=int, default=12)
    parser.add_argument('--budget', type=int, default=128)
    parser.add_argument('--quantum', type=int, default=2048)
    args = parser.parse_args()
    if min(args.stages, args.seeds, args.quantum) < 1 or args.budget < 0:
        parser.error('invalid experiment bounds')
    args.output.mkdir(parents=True, exist_ok=True)
    manifest_bytes = args.manifest.read_bytes()
    manifest = json.loads(manifest_bytes)
    sources = {row['source']: (ROOT / row['source']).read_text()
               for row in manifest['training'] + manifest['evaluation']}
    validate_manifest(manifest, sources)
    dependencies = library_sources([HELPERS, *sources.values()])
    # Retain the fixed split before asking the native learner for any proposal.
    (args.output / 'manifest.json').write_bytes(manifest_bytes)
    binary = args.cetta.resolve(strict=True)
    binary_sha = hashlib.sha256(binary.read_bytes()).hexdigest()
    commands = []

    def run(name, source, timeout=180):
        check_controls(source)
        path = args.output / (name + '.metta')
        path.write_text(source)
        start = time.perf_counter()
        result = subprocess.run([str(binary), '--lang', 'petta', str(path.resolve())], cwd=ROOT,
                                capture_output=True, text=True, timeout=timeout)
        elapsed = time.perf_counter() - start
        (args.output / (name + '.log')).write_text(result.stdout + result.stderr)
        commands.append({'name': name, 'returncode': result.returncode, 'whole_process_seconds': elapsed})
        if result.returncode:
            raise RuntimeError((name, result.returncode, result.stderr[-1200:]))
        return parse_forms(result.stdout)

    trained = []
    for index, row in enumerate(manifest['training']):
        original = reference(sources[row['source']])
        current, artifact, layers, stages = original, plain(original), [], []
        for stage in range(args.stages):
            suffix = f'h{index}{stage}'
            forms = run(f'train-{row["id"]}-{stage}', HELPERS + f'''
!(source-family:train {tensor(artifact)} "{suffix}" {args.seeds} {args.quantum} {args.budget})
''')
            found = [form for form in forms if isinstance(form, list) and form[:1] == ['FamilyStage']]
            if len(found) != 1:
                raise AssertionError(forms[-3:])
            result = found[0]
            assert result[5] == 'true', result[:6]
            after = payload(result[8])
            text = payload(result[10]).decode()
            document = reference(text)
            next_layers = [(json.loads(codec), json.loads(name)) for _tag, codec, name in result[9]]
            assert after == encoded(next_layers, document), 'native/reference encoded bytes disagree'
            assert restore(next_layers, document) == original, 'native/reference reconstruction disagree'
            assert len(after) <= len(artifact), 'accepted best increased complete bytes'
            improved = len(after) < len(artifact)
            stages.append({'suffix': suffix, 'region': result[1], 'reason': result[2],
                           'statistics': result[3], 'first_fault': result[4],
                           'before_bytes': len(artifact), 'after_bytes': len(after), 'improved': improved,
                           'preparation_ns': int(result[6]), 'search_ns': int(result[7]),
                           'continuation': 'explicitly released after observation; no whole-region claim on budget exhaustion'})
            current, artifact, layers = document, after, next_layers
            if not improved:
                break
        (args.output / f'library-{row["id"]}.sic').write_bytes(artifact)
        (args.output / f'library-{row["id"]}.metta').write_text(text)
        trained.append({'id': row['id'], 'corpus': row['corpus'], 'family': row['family'],
                        'source': row['source'], 'source_sha256': hashlib.sha256(sources[row['source']].encode()).hexdigest(),
                        'original_bytes': len(plain(original)), 'best_bytes': len(artifact), 'layers': layers,
                        'stages': stages, 'artifact_sha256': hashlib.sha256(artifact).hexdigest(),
                        '_document': current, '_artifact': artifact})
    # No receiving source can change a selected definition, name or library.
    frozen = [{'id': row['id'], 'layers': row['layers'], 'artifact_sha256': row['artifact_sha256']}
              for row in trained]
    (args.output / 'frozen-libraries.json').write_text(json.dumps(frozen, indent=2) + '\n')
    cohort = []
    for receiver in manifest['evaluation']:
        text = sources[receiver['source']]
        original = reference(text)
        baseline = None
        if receiver.get('queries'):
            appendix = '\n'.join(f'!(println! (FamilyProbe {i} (collapse {query["call"]})))'
                                 for i, query in enumerate(receiver['queries']))
            baseline = run(f'original-{receiver["id"]}', text + '\n' + appendix + '\n')
            probes = [form for form in baseline if isinstance(form, list) and form[:1] == ['FamilyProbe']]
            assert [form[2] for form in probes] == [parse_forms(query['expected'])[0] for query in receiver['queries']]
        for donor in trained:
            row = {'training': donor['id'], 'receiving': receiver['id'], 'corpus': receiver['corpus'],
                   'family': receiver['family'], 'source': receiver['source'],
                   'source_sha256': hashlib.sha256(text.encode()).hexdigest(),
                   'original_bytes': len(plain(original)), 'accepted': False, 'execution': {'run': False}}
            if not donor['layers']:
                row['reason'] = 'NoLearnedLibrary'
                cohort.append(row)
                continue
            forms = run(f'transfer-{donor["id"]}-{receiver["id"]}', HELPERS + f'''
!(let $original (syntax:read {text_arg(text)})
   (source-family:receive $original (ic:reuse-library Deferred {tensor(donor['_artifact'])} $original)))
''')
            if len(donor['layers']) == 1:
                proposal = transfer(donor['_document'], original)
                expected = proposal if proposal is None else (proposal[0], donor['layers'], proposal[1])
            else:
                proposal = reuse(donor['layers'], donor['_document'], original)
                expected = proposal if isinstance(proposal, tuple) else None
            results = [form for form in forms if isinstance(form, list) and form[:1] in (['FamilyReceived'], ['FamilyRefused'])]
            assert len(results) == 1, forms[-3:]
            result = results[0]
            if expected is None:
                assert result[0] == 'FamilyRefused', result[:3]
                row['reason'] = result[1]
            else:
                document, layers, support = expected
                wanted = encoded(layers, document)
                assert result[:3] == ['FamilyReceived', str(support), 'true'], result[:3]
                assert payload(result[4]) == wanted
                exported = payload(result[6]).decode()
                assert reference(exported) == document
                accepted = len(wanted) < len(plain(original))
                assert result[3][0] == ('IC:PackedAccepted' if accepted else 'IC:PackedRejected')
                destination = args.output / f'export-{donor["id"]}-{receiver["id"]}.metta'
                destination.write_text(exported)
                row.update({'support': support, 'helper_bytes': len(wanted), 'accepted': accepted,
                            'reason': 'Accepted' if accepted else 'NotSmaller', 'export': destination.name})
                if baseline is not None:
                    actual = run(f'execute-{donor["id"]}-{receiver["id"]}', exported + '\n' + appendix + '\n')
                    assert actual == baseline, 'ordered answers or effects changed'
                    row['execution'] = {'run': True, 'passed': True, 'queries': len(receiver['queries']),
                                        'scope': 'entire receiving source loaded; declared query cohort only'}
                if receiver.get('component') == 'parameters':
                    directory = args.output / f'component-{donor["id"]}-{receiver["id"]}'
                    command = [sys.executable, str(ROOT / 'tests/support/check_tensor_program_parameters.py'),
                               '--cetta', str(binary), '--output', str(directory), '--library-source', str(destination.resolve())]
                    process = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=240)
                    (args.output / f'component-{donor["id"]}-{receiver["id"]}.log').write_text(process.stdout + process.stderr)
                    component = json.loads((directory / 'receipt.json').read_text())
                    assert process.returncode == 0 and component['passed'], component.get('mismatches')
                    row['execution'] = {'run': True, 'passed': True, 'checks': component['checks'],
                                        'scope': 'complete finite-parameter consumer gate, including prediction'}
            cohort.append(row)
    assert hashlib.sha256(binary.read_bytes()).hexdigest() == binary_sha
    assert args.manifest.read_bytes() == manifest_bytes
    assert all((ROOT / source).read_text() == text for source, text in sources.items())
    assert all(path.read_text() == text for path, text in dependencies.items())
    assert all(hashlib.sha256(row['_artifact']).hexdigest() == row['artifact_sha256'] for row in trained)
    receipt = {'passed': True, 'kind': 'family-separated-frozen-source-library-experiment',
               'manifest_sha256': hashlib.sha256(manifest_bytes).hexdigest(), 'binary_sha256': binary_sha,
               'library_dependency_sha256': {str(path.relative_to(ROOT)): hashlib.sha256(text.encode()).hexdigest()
                                            for path, text in sorted(dependencies.items())},
               'training': [{key: value for key, value in row.items() if not key.startswith('_')} for row in trained],
               'cohort': cohort, 'commands': commands, 'bounds': vars(args) | {'cetta': str(args.cetta),
                   'output': str(args.output), 'manifest': str(args.manifest)},
               'selection': 'native structural ranking and native complete-cost acceptance on training sources only',
               'partition': 'declared program families; generated variants assigned with their source; whole-document renamed-clone check',
               'accepted_transfers': sum(row['accepted'] for row in cohort),
               'not_claimed': ['unrestricted discovery', 'full operator coverage', 'complete behavioral equivalence',
                               'four-form execution-cost qualification', 'execution speedup', 'retained-heap improvement']}
    (args.output / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print(json.dumps({key: receipt[key] for key in ('passed', 'kind', 'accepted_transfers', 'not_claimed')}))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
