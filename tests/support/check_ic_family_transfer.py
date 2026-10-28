#!/usr/bin/env python3
"""Training-only helper selection and complete held-out library qualification."""

from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import time

from check_binary_codec import ROOT, parse_forms, tensor
from check_ic_context_library import candidate, clause, encoded, is_symbol, plain, sites, split
from check_ic_library_transfer import transfer
from check_ic_search import check_controls
from check_source_codec import reference, text_arg


TRAINING = 'lib/petta/lib_binary_schema.metta'
NAME = 'ic-learned:expr?'
# A fixed receiving cohort. Selection above this boundary sees no receiving
# source. Closely related numerical files remain in one evaluation family;
# generated HE/PeTTa variants are never independent training/test examples.
EVALUATION = [
    ('list-combinator', 'lib/petta/list.metta'),
    ('term-row-transform', 'lib/petta/lib_transform.metta'),
    ('numerical-tensor', 'lib/petta/lib_tensor_program_parameters.metta'),
    ('numerical-tensor', 'lib/petta/lib_tensor_program_search.metta'),
    ('search-control', 'lib/petta/lib_ic_search.metta'),
]


HELPERS = r'''
!(import! &self (library lib_ic_library_transfer))
(= (family-test:checked $original $bytes)
   (let $before (ic:source-pack $original)
     (case (ic:library-check $before $before $bytes)
       (((IC:PackedAccepted $winner $old $new) (Accepted $old $new))
        ((IC:PackedRejected $reason) (Rejected $reason))))))
(= (family-test:export $original $proposal)
   (case $proposal
     (((Library:Candidate $bytes $support)
       (case (ic:export-library $bytes)
         (((Library:Export $name $document $text $restored)
           (FamilyExport $support (== $original $restored)
             (family-test:checked $original $bytes)
             (tensor:inspect $bytes) (tensor:inspect (binary:utf8 $text))))
          ($invalid-export $invalid-export)))) ($refused $refused))))
'''


def payload(observation):
    assert observation[:2] == ['TensorData', 'u8']
    return bytes(int(row[1]) for row in observation[3])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cetta', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--skip-component', action='store_true', help='omit the separate numerical consumer gate')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    binary = args.cetta.resolve(strict=True)
    commands, controls = [], 0

    def run(name, source):
        check_controls(source)
        path = args.output/(name+'.metta')
        path.write_text(source)
        command = [str(binary), '--lang', 'petta', str(path.resolve())]
        started = time.perf_counter()
        result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=120)
        wall_seconds = time.perf_counter()-started
        (args.output/(name+'.log')).write_text(result.stdout+result.stderr)
        commands.append({'name': name, 'returncode': result.returncode, 'whole_process_seconds': wall_seconds})
        if result.returncode:
            raise RuntimeError((name, result.returncode, result.stderr[-1200:]))
        return parse_forms(result.stdout), result.stdout

    training_text = (ROOT/TRAINING).read_text()
    document = reference(training_text)
    available = sites(document)
    # This structural proposal is chosen using the training file only. Both
    # occurrences must observe an established formal parameter, not a local or
    # arbitrary computation masquerading as a ready value.
    seeds = []
    for index, (owner, _path, _bound, term) in enumerate(available):
        operation = split(term)
        if not operation or operation[0] != '==':
            continue
        observation, kind = operation[1]
        call = split(observation)
        ports = len(clause(document[3][owner])[0])-1
        if (call and call[0] == 'get-metatype' and is_symbol(kind, 'Expression') and
            call[1][0][0] == 'Syntax:Variable' and int(call[1][0][1]) < ports):
            seeds.append(index)
    if len(seeds) < 2:
        raise AssertionError('training no longer supplies the declared structural seed')
    seeds = seeds[:2]
    learned, support, _pattern = candidate(document, NAME, *seeds, selected=available)
    expected_artifact = encoded(NAME, learned)
    assert len(expected_artifact) < len(plain(document)), 'training does not pay for its complete helper'
    forms, _ = run('training', HELPERS+f'''
!(let $original (syntax:read {text_arg(training_text)})
   (println! (TrainingResult (family-test:export $original
     (context-library:seed (ic:prepare-library Deferred $original) "{NAME}" {seeds[0]} {seeds[1]})))))
''')
    exports = [row[1] for row in forms if isinstance(row, list) and row and row[0] == 'TrainingResult']
    assert len(exports) == 1
    training = exports[0]
    assert training[:3] == ['FamilyExport', str(support), 'true'], training[:3]
    assert training[3] == ['Accepted', str(len(plain(document))), str(len(expected_artifact))]
    artifact = payload(training[4])
    assert artifact == expected_artifact
    trained_text = payload(training[5]).decode()
    assert reference(trained_text) == learned
    (args.output/'training-helper.metta').write_text(trained_text)
    controls += 1
    # Freeze the selected artifact before loading any held-out source. The
    # receiving cohort may accept or reject it, but cannot refit its body.
    frozen_artifact_sha = hashlib.sha256(artifact).hexdigest()
    cohort, learned_consumer = [], None
    for index, (family, source_path) in enumerate(EVALUATION):
        text = (ROOT/source_path).read_text()
        original = reference(text)
        expected = transfer(learned, original)
        forms, _ = run(f'receive-{index}', HELPERS+f'''
!(let $original (syntax:read {text_arg(text)})
   (println! (ReceivingResult (family-test:export $original
     (ic:reuse-library Deferred {tensor(artifact)} $original)))))
''')
        answers = [row[1] for row in forms if isinstance(row, list) and row and row[0] == 'ReceivingResult']
        assert len(answers) == 1
        answer = answers[0]
        row = {'family': family, 'source': source_path, 'source_sha256': hashlib.sha256(text.encode()).hexdigest(),
               'original_bytes': len(plain(original)), 'support': 0, 'accepted': False}
        if expected is None:
            assert answer == ['Library:Refused', 'InsufficientSupport'], answer[:3]
        else:
            changed, count = expected
            changed_bytes = encoded(NAME, changed)
            assert answer[:3] == ['FamilyExport', str(count), 'true'], answer[:3]
            assert payload(answer[4]) == changed_bytes
            changed_text = payload(answer[5]).decode()
            assert reference(changed_text) == changed
            destination = args.output/f'receiving-{index}.metta'
            destination.write_text(changed_text)
            wanted = (['Accepted', str(len(plain(original))), str(len(changed_bytes))]
                      if len(changed_bytes) < len(plain(original)) else ['Rejected', 'NotSmaller'])
            assert answer[3] == wanted, answer[3]
            row.update({'support': count, 'helper_bytes': len(changed_bytes),
                        'accepted': wanted[0] == 'Accepted', 'source_export': destination.name})
            if source_path == 'lib/petta/lib_tensor_program_parameters.metta' and row['accepted']:
                learned_consumer = destination
        cohort.append(row)
        controls += 1
    assert hashlib.sha256(artifact).hexdigest() == frozen_artifact_sha
    # New receiving code is only an exported candidate, never an edited
    # application. It must independently pass the entire component gate.
    component = {'run': False}
    if not args.skip_component:
        if learned_consumer is None:
            raise AssertionError('the held-out numerical library did not retain a smaller complete artifact')
        directory = args.output/'numerical-consumer'
        command = [sys.executable, str(ROOT/'tests/support/check_tensor_program_parameters.py'),
                   '--cetta', str(binary), '--output', str(directory), '--library-source', str(learned_consumer.resolve())]
        result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=240)
        (args.output/'numerical-consumer.log').write_text(result.stdout+result.stderr)
        receipt = json.loads((directory/'receipt.json').read_text())
        assert result.returncode == 0 and receipt['passed'], (result.returncode, receipt.get('mismatches'))
        component = {'run': True, 'passed': True, 'checks': receipt['checks'],
                     'scope': 'entire held-out finite-parameter library gate, including new-input predictions'}
    # Exact eager observation is tested with choice, quotation, functions and
    # effects. Body matching still refuses newly declared operator meanings.
    receiving = '''(= (family-probe:kind $x) (== (get-metatype $x) Expression))
(= (family-probe:effect $x) (let $observed (println! MetatypeInputEffect) $x))
'''
    proposed = transfer(learned, reference(receiving))
    assert proposed is not None
    queries = ['(family-probe:kind 7)', '(family-probe:kind (quote (+ 1 2)))',
               '(family-probe:kind (superpose (7 (quote (+ 1 2)) 7)))',
               '(family-probe:kind (family-probe:effect (quote (+ 1 2))))']
    forms, _ = run('probe-export', HELPERS+f'''
!(let $original (syntax:read {text_arg(receiving)})
   (println! (ProbeExport (family-test:export $original (ic:reuse-library Deferred {tensor(artifact)} $original)))))
''')
    exported = next(row[1] for row in forms if isinstance(row,list) and row and row[0]=='ProbeExport')
    changed_text = payload(exported[5]).decode()
    variants = []
    for name, source in [('original', receiving), ('learned', changed_text)]:
        appendix = '\n'.join(f'!(println! (ProbeAnswer {i} (collapse {query})))' for i,query in enumerate(queries))
        answers, stdout = run('probe-'+name, source+'\n'+appendix+'\n')
        selected = [row for row in answers if isinstance(row,list) and row and row[0]=='ProbeAnswer']
        assert len(selected) == len(queries)
        variants.append({'answers': selected, 'effects': stdout.splitlines().count('MetatypeInputEffect')})
    assert variants[0] == variants[1]
    assert variants[0]['answers'][0][2] == ['false']
    assert variants[0]['answers'][1][2] == ['true']
    assert Counter(variants[0]['answers'][2][2]) == Counter(['false','true','false'])
    assert variants[0]['effects'] == 1
    controls += len(queries)
    delayed_source = '''(= (delayed-probe:first $x) (== (get-metatype (delayed-probe:emit-first $x)) Expression))
(= (delayed-probe:second $y) (== (get-metatype (delayed-probe:emit-second $y)) Expression))
(= (delayed-probe:emit-first $x) (let $seen (println! DelayedFirstEffect) $x))
(= (delayed-probe:emit-second $x) (let $seen (println! DelayedSecondEffect) $x))
'''
    delayed_document = reference(delayed_source)
    delayed_sites = sites(delayed_document)
    delayed_seeds = [next(index for index,(owner,path,_bound,_term) in enumerate(delayed_sites)
                          if owner == target and path == (2,)) for target in (0,1)]
    delayed_expected = candidate(delayed_document, 'ic-learned:delayed-meta', *delayed_seeds)[0]
    forms, _ = run('delayed-export', HELPERS+f'''
!(let $original (syntax:read {text_arg(delayed_source)})
   (println! (DelayedExport (family-test:export $original
     (context-library:seed (ic:prepare-library Deferred $original) "ic-learned:delayed-meta"
       {delayed_seeds[0]} {delayed_seeds[1]})))))
''')
    delayed_export = next(row[1] for row in forms if isinstance(row,list) and row and row[0]=='DelayedExport')
    delayed_text = payload(delayed_export[5]).decode()
    assert payload(delayed_export[4]) == encoded('ic-learned:delayed-meta', delayed_expected)
    assert reference(delayed_text) == delayed_expected and delayed_export[2] == 'true'
    delayed_queries = ['(delayed-probe:first 7)', '(delayed-probe:second (quote (+ 1 2)))',
                       '(delayed-probe:first (superpose (7 (quote (+ 1 2)) 7)))',
                       '(delayed-probe:first (superpose ()))']
    delayed_variants = []
    for name, source in [('original',delayed_source), ('learned',delayed_text)]:
        appendix = '\n'.join(f'!(println! (DelayedAnswer {index} (collapse {query})))'
                             for index,query in enumerate(delayed_queries))
        answers, stdout = run('delayed-'+name, source+'\n'+appendix+'\n')
        selected = [row for row in answers if isinstance(row,list) and row and row[0]=='DelayedAnswer']
        assert len(selected) == len(delayed_queries)
        delayed_variants.append({'answers': selected,
            'effects': [line for line in stdout.splitlines() if line in ('DelayedFirstEffect','DelayedSecondEffect')]})
    assert delayed_variants[0] == delayed_variants[1]
    assert delayed_variants[0]['answers'][-1][2] == []
    assert Counter(delayed_variants[0]['effects']) == Counter({'DelayedFirstEffect':4,'DelayedSecondEffect':1})
    controls += len(delayed_queries)
    controls_source = '(= (get-metatype $x) Symbol) (= (guard $x) (== (get-metatype $x) Expression))'
    forms, _ = run('nonstandard', HELPERS+f'''
!(println! (NonstandardResult (ic:reuse-library Deferred {tensor(artifact)} (syntax:read {text_arg(controls_source)}))))
''')
    assert ['NonstandardResult', ['Library:Refused', 'NonstandardOperators']] in forms
    controls += 1
    receipt = {'passed': True, 'checks': controls, 'training_source': TRAINING,
               'training_seeds': seeds, 'training_support': support,
               'training_bytes': [len(plain(document)), len(artifact)], 'frozen_helper_sha256': frozen_artifact_sha,
               'helper_definition': parse_forms(trained_text)[0], 'cohort': cohort, 'component': component,
               'observation_probe': variants[0], 'delayed_observation_probe': delayed_variants[0], 'commands': commands,
               'training_native_process_seconds': commands[0]['whole_process_seconds'],
               'training_timing_scope': 'native process load, seed extraction, complete-cost checking, reconstruction and export; excludes Python reference preparation',
               'scope': 'training-only seed-guided native extraction, frozen helper transfer to complete held-out source files and independent receiving-component execution',
               'not_claimed': ['broad held-out-family utility', 'unrestricted native discovery', 'execution speedup']}
    (args.output/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print(json.dumps({key: value for key,value in receipt.items() if key not in ('commands','observation_probe','delayed_observation_probe')}))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
