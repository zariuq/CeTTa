#!/usr/bin/env python3
"""Fixed multi-helper transfer, independent replay, bytes and program execution."""

from __future__ import annotations

import argparse
from copy import deepcopy
import json
import os
from pathlib import Path
import statistics
import subprocess
import time

from check_binary_codec import ROOT, parse_forms, tensor
from check_ic_archive import syntax
from check_ic_context_library import candidate, sites
from check_ic_library import candidate as entered_candidate, clause, is_symbol, plain
from check_ic_library_stack import encoded, execute, expand_one, restore
from check_ic_library_transfer import transfer
from check_ic_search import check_controls
from check_source_codec import reference


def stages(layers, document):
    result = []
    for codec, name in layers:
        result.append((codec, name, deepcopy(document[3][0])))
        document = expand_one(codec, name, document)
    return result


def reuse(layers, donor, target):
    """Replay fixed donor stages; no generalization or receiving-source fitting."""
    current, installed, support = deepcopy(target), [], 0
    for _codec, name, definition in reversed(stages(layers, donor)):
        one = deepcopy(donor)
        one[3] = [definition]
        found = transfer(one, current)
        if found is None:
            return ['Library:Refused', ['Library:TransferStep', json.dumps(name), 'InsufficientSupport']]
        current, count = found
        support += count
        installed.insert(0, ('LC2', name))
    assert current[3][:len(layers)] == donor[3][:len(layers)], 'fixed definition changed'
    assert restore(installed, current) == target
    return current, installed, support


HELPERS = r'''
!(import! &self (library lib_ic_library_transfer))
!(import! &self (library system))
(= (stack-transfer-test:proposal $donor $source)
   (case (ic:reuse-library Deferred $donor $source)
     (((Library:Candidate $bytes $support)
       (case (ic:export-library $bytes)
         (((Library:ExportMany $layers $document $text $original)
           (StackTransferred $support (tensor:inspect $bytes) (== $original $source)
             (tensor:inspect (binary:utf8 $text)))) ($failed-export $failed-export))))
      ($failed-transfer $failed-transfer))))
(= (stack-transfer-test:serialized $stages $reader $layers $document $support)
   (if (== $stages ()) (Library:Replay $layers $document $support)
     (case (car-atom $stages)
       (((Library:Stage $codec $name $definition)
         (case (library-transfer:document $name $definition $reader $document)
           (((Library:Candidate $bytes $here)
             (case (ic:export-library $bytes)
               (((Library:Export $actual $after $text $before)
                 (if (and (== $actual $name) (== $before $document))
                   (let* (($next (cons-atom (Library:Layer "LC2" $name) $layers)) ($total (+ $support $here)))
                     (stack-transfer-test:serialized (cdr-atom $stages) $reader $next $after $total))
                   (Library:Refused Reconstruction)))
                ($bad-export (Library:Refused InvalidTransfer)))))
            ((Library:Refused $reason) (Library:Refused (Library:TransferStep $name $reason)))
            ($bad-proposal (Library:Refused InvalidTransfer)))))
        ($bad-stage (Library:Refused InvalidTransfer))))))
(= (stack-transfer-test:compare $layers (Syntax:Document $version $reader $forms) $document)
   (case (library-stack:stages $layers (Syntax:Document $version $reader $forms))
     (((Library:Stages $newest)
       (let $stages (list:reverse $newest)
         (== (library-transfer:replay $stages $reader () $document 0)
             (stack-transfer-test:serialized $stages $reader () $document 0))))
      ($failed-stages $failed-stages))))
'''


def measure(cetta, output, layers, donor, target, expected, pairs):
    paths = {}
    for variant, operation in [('data', 'library-transfer:replay'), ('serialized', 'stack-transfer-test:serialized')]:
        source = HELPERS+f'''
(= (transfer-cost:result $start $ready $end (Library:Candidate $bytes $support))
   (println! (TransferCost (- $ready $start) (- $end $ready) (tensor:inspect $bytes))))
(= (transfer-cost:ready $start $target
     (Library:ExportMany $layers (Syntax:Document $version $reader $forms) $text $original))
   (case (library-stack:stages $layers (Syntax:Document $version $reader $forms))
     (((Library:Stages $newest)
       (let* (($stages (list:reverse $newest)) ($ready (system:monotonic-ns))
              ($replay ({operation} $stages $reader () $target 0))
              ($result (library-transfer:publish $layers (Syntax:Document $version $reader $forms) $target $replay))
              ($end (system:monotonic-ns)))
         (transfer-cost:result $start $ready $end $result)))
      ($failed-stages (println! (TransferCostFault $failed-stages))))))
!(let* (($start (system:monotonic-ns)) ($donor {tensor(encoded(layers,donor))})
         ($target {syntax(target)}) ($export (ic:export-library $donor)))
   (transfer-cost:ready $start $target $export))
'''
        check_controls(source)
        path = output/f'cost-{variant}.metta';path.write_text(source);paths[variant] = path
    samples = []
    for pair in range(pairs):
        for variant in (['data','serialized'] if pair%2==0 else ['serialized','data']):
            log = output/f'cost-{pair}-{variant}.log'
            started = time.perf_counter()
            with log.open('w') as stream:
                process = subprocess.Popen([str(cetta.resolve()), '--lang','petta',str(paths[variant].resolve())],
                                           cwd=ROOT,stdout=stream,stderr=subprocess.STDOUT)
                _pid,status,usage = os.wait4(process.pid,0)
                process.returncode = os.waitstatus_to_exitcode(status)
            wall = time.perf_counter()-started
            rows = [row for row in parse_forms(log.read_text()) if isinstance(row,list) and row[:1]==['TransferCost']]
            if process.returncode or len(rows)!=1:
                raise RuntimeError((variant,process.returncode,log.read_text()[-1200:]))
            row = rows[0]
            blob = bytes(int(x[1]) for x in row[3][3])
            assert blob==expected, 'benchmark observation differs'
            samples.append({'pair':pair,'variant':variant,'preparation_ns':int(row[1]),
                            'replay_publication_ns':int(row[2]),'whole_process_seconds':wall,
                            'peak_rss_kib':usage.ru_maxrss})
    ratios=[]
    for pair in range(pairs):
        values={s['variant']:s['replay_publication_ns'] for s in samples if s['pair']==pair}
        ratios.append(values['data']/values['serialized'])
    medians={variant:{key:statistics.median(s[key] for s in samples if s['variant']==variant)
                      for key in ('preparation_ns','replay_publication_ns','whole_process_seconds','peak_rss_kib')}
             for variant in paths}
    return {'pairs':pairs,'samples':samples,'medians':medians,'paired_data_over_serialized':ratios,
            'retained_native_heap':None,'allocations':None,
            'scope':'same prevalidated receiving fixture, fixed stages and complete publication; donor load/validation/recovery charged separately, whole-process includes imports; peak RSS is not retained heap'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cetta', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--measure-pairs', type=int, default=0,
                        help='optional rotated cost panel on the larger fixed receiving fixture')
    args = parser.parse_args()
    if args.measure_pairs < 0:
        parser.error('measure-pairs must be nonnegative')
    args.output.mkdir(parents=True, exist_ok=True)
    for name in ('lib_ic_library_transfer.metta', 'lib_ic_library_stack.metta'):
        check_controls((ROOT/'lib/petta'/name).read_text())
    first, second, third = ('ic-learned:first', 'ic-learned:second', 'ic-learned:third')
    independent = reference(f'''
      (= ({first} $p0) (+ $p0 1)) (= ({second} $p0) (* $p0 2))
      (= (f $x) ({first} $x)) (= (g $x) ({first} $x))
      (= (d $x) ({second} $x)) (= (e $x) ({second} $x))''')
    closed_dependency = reference(f'''
      (= ({first} $p0) (+ ({second} 3) $p0)) (= ({second} $p0) (* $p0 2))
      (= (f $x) ({first} $x)) (= (g $x) ({first} $x))
      (= (d $x) ({second} $x)) (= (e $x) ({second} $x))''')
    newer_dependency = reference(f'''
      (= ({second} $p0) (* $p0 2)) (= ({first} $p0) (+ ({second} $p0) 1))
      (= (f $x) ({first} $x)) (= (g $x) ({first} $x))''')
    three = reference(f'''
      (= ({third} $p0) (* $p0 2)) (= ({second} $p0) (+ ({third} $p0) 1))
      (= ({first} $p0) (- ({second} $p0) 5))
      (= (f $x) ({first} $x)) (= (g $x) ({first} $x))''')
    effects = reference(f'''
      (= ({first} $p0 $p1) (if (> ($p0) 0) ($p1) (superpose ())))
      (= ({second} $p0) (+ $p0 $p0))
      (= (f $x) ({first} (|-> () ({second} $x)) (|-> () (observe $x))))
      (= (g $x) ({first} (|-> () ({second} $x)) (|-> () (observe $x))))
      (= (observe $x) (let $noted (println! (StackEffect $x)) $x))''')
    alpha, beta = 'alpha-'*64, 'beta-'*64
    training = reference('\n'.join(
        [f'(= (a{i} $x) (if (> $x {i}) "{alpha}" 0))' for i in range(4)] +
        [f'(= (b{i} $x) (if (< $x {i}) "{beta}" 0))' for i in range(4)]))
    one = entered_candidate(training, first, 0, 1)[0]
    choices = sites(one)
    seeds = [i for i, (owner, path, _bound, _term) in enumerate(choices)
             if path == (2,) and any(is_symbol(clause(one[3][owner])[0][0], name) for name in ('b0', 'b1'))]
    assert len(seeds) == 2
    learned = candidate(one, second, *seeds)[0]
    donors = [
        ([('LC1', first), ('LC1', second)], independent),
        ([('LC2', first), ('LC2', second)], closed_dependency),
        ([('LC2', second), ('LC2', first)], newer_dependency),
        ([('LC2', third), ('LC2', second), ('LC2', first)], three),
        ([('LC2', first), ('LC2', second)], effects),
        ([('LC2', second), ('LC1', first)], learned),
    ]
    for layers, document in donors:
        restore(layers, document)
    examples = [
        (0, '(= (increment $z) (+ $z 1)) (= (double $q) (* $q 2))',
         [('(increment 6)', ['7']), ('(double (superpose (2 3 2)))', ['4','6','4'])]),
        (1, '(= (offset $x) (+ (* 3 2) $x)) (= (other $x) (+ (* 4 2) $x))',
         [('(offset 5)', ['11']), ('(other 5)', ['13']), ('(offset (superpose (2 3 2)))', ['8','9','8'])]),
        (2, '(= (slope $a) (+ (* $a 2) 1)) (= (other $b) (- (* $b 2) 1)) '
            '(= (unseen $b) (+ (* $b 3) 1))',
         [('(slope -2)', ['-3']), ('(other 5)', ['9']), ('(unseen 2)', ['7']),
          ('(slope (superpose (2 3 2)))', ['5','7','5'])]),
        (3, '(= (pipeline $a) (- (+ (* $a 2) 1) 5)) '
            '(= (data $a) (quote (- (+ (* $a 2) 1) 5)))',
         [('(pipeline -2)', ['-8']), ('(pipeline 3)', ['2']),
          ('(data 2)', [['-', ['+', ['*','2','2'], '1'], '5']])]),
        (4, '(= (new $x) (if (> (+ $x $x) 0) (observe $x) (superpose ()))) '
            '(= (observe $x) (let $noted (println! (StackEffect $x)) $x))',
         [('(new 2)', ['2']), ('(new -1)', []), ('(new (superpose (2 -1 2)))', ['2','2'])]),
        (2, '(= (repeated $x) (+ (* $x 2) 1)) (= (repeated $x) (+ (* $x 2) 1))',
         [('(repeated 2)', ['5','5']), ('(repeated (superpose (2 3 2)))', ['5','7','5','5','7','5'])]),
        (5, '\n'.join([f'(= (left{i} $datum) (if (> $datum {i+41}) "{alpha}" 0))' for i in range(4)] +
                       [f'(= (right{i} $datum) (if (< $datum {i+41}) "{beta}" 0))' for i in range(4)]),
         [('(left0 100)', [json.dumps(alpha)]), ('(left0 -1)', ['0']),
          ('(right0 -1)', [json.dumps(beta)]), ('(right0 100)', ['0'])]),
    ]
    code, expected, wanted = [HELPERS], [], []

    def control(call, result):
        index = len(expected)
        code.append(f'!(println! (StackTransferControl {index} {call}))')
        expected.append(['StackTransferControl', str(index), result])

    for index, (layers, donor) in enumerate(donors):
        rows = '(' + ' '.join(f'(Library:Layer "{label}" "{name}")' for label, name in layers) + ')'
        recovered = '(' + ' '.join(f'(Library:Stage "{label}" "{name}" {syntax(definition)})'
                                  for label, name, definition in stages(layers, donor)) + ')'
        control(f'(== (library-stack:stages {rows} {syntax(donor)}) (Library:Stages {recovered}))', 'true')
    for index, (which, text, queries) in enumerate(examples):
        layers, donor = donors[which]
        target = reference(text)
        changed, output_layers, support = reuse(layers, donor, target)
        blob = encoded(output_layers, changed)
        wanted.append((target, changed, blob, support, queries))
        rows = '('+' '.join(f'(Library:Layer "{label}" "{name}")' for label,name in layers)+')'
        control(f'(stack-transfer-test:compare {rows} {syntax(donor)} {syntax(target)})', 'true')
        code.append(f'!(println! (StackTransferCase {index} (stack-transfer-test:proposal '
                    f'{tensor(encoded(layers, donor))} {syntax(target)})))')
        checker = f'(ic:library-check {tensor(plain(target))} {tensor(plain(target))} {tensor(blob)})'
        if len(blob) < len(plain(target)):
            control(f'(case {checker} (((IC:PackedAccepted $winner $before $after) '
                    f'(StackAccepted $before $after)) ($rejected $rejected)))',
                    ['StackAccepted', str(len(plain(target))), str(len(blob))])
        else:
            control(checker, ['IC:PackedRejected', 'NotSmaller'])
    assert len(wanted[-1][2]) < len(plain(wanted[-1][0])), 'the full library must pay its own cost'

    donor_bytes = tensor(encoded(*donors[0]))
    control(f'(ic:reuse-library Deferred {donor_bytes} (syntax:read ""))',
            ['Library:Refused', ['Library:TransferStep', json.dumps(second), 'InsufficientSupport']])
    partial = reference('(= (only-double $x) (* $x 2))')
    control(f'(ic:reuse-library Deferred {donor_bytes} {syntax(partial)})',
            ['Library:Refused', ['Library:TransferStep', json.dumps(first), 'InsufficientSupport']])
    control(f'''(let $original {syntax(partial)}
       (let $failed (ic:reuse-library Deferred {donor_bytes} $original)
         (StackNoPartial (== $original {syntax(partial)})
           (== $failed (Library:Refused (Library:TransferStep "{first}" InsufficientSupport))))))''',
            ['StackNoPartial', 'true', 'true'])
    collision = reference(f'(= ({first} $x) $x) (= (double $x) (* $x 2))')
    control(f'(ic:reuse-library Deferred {donor_bytes} {syntax(collision)})', ['Library:Refused','NameCollision'])
    foreign = deepcopy(wanted[0][0]); foreign[2][1] = '"he"'
    control(f'(ic:reuse-library Deferred {donor_bytes} {syntax(foreign)})', ['Library:Refused','DifferentReader'])
    nonstandard = reference('(: + (-> Atom Atom Atom)) (= (h $x) (+ $x 1))')
    control(f'(ic:reuse-library Deferred {donor_bytes} {syntax(nonstandard)})', ['Library:Refused','NonstandardOperators'])
    control(f'(ic:reuse-library Deferred {tensor(b"bad")} {syntax(wanted[0][0])})', ['Library:Refused','InvalidArtifact'])
    control(f'(ic:reuse-library Deferred (ic:source-pack {syntax(wanted[0][0])}) {syntax(wanted[0][0])})',
            ['Library:Refused','NoLearnedLibrary'])
    target, changed, _blob, support, _queries = wanted[0]
    forged = deepcopy(changed)
    forged[3][0] = reference(f'(= ({first} $p0) (+ $p0 99))')[3][0]
    output_layers = '('+' '.join(f'(Library:Layer "LC2" "{name}")' for _label, name in donors[0][0])+')'
    control(f'(library-transfer:publish {output_layers} {syntax(donors[0][1])} {syntax(target)} '
            f'(Library:Replay {output_layers} {syntax(forged)} {support}))', ['Library:Refused','ChangedDefinitions'])
    wrong_reader = deepcopy(changed);wrong_reader[2][1] = '"he"'
    control(f'(library-transfer:publish {output_layers} {syntax(donors[0][1])} {syntax(target)} '
            f'(Library:Replay {output_layers} {syntax(wrong_reader)} {support}))', ['Library:Refused','DifferentReader'])
    invalid = deepcopy(donors[0][1]); invalid[3] = invalid[3][1:]
    control(f'(ic:reuse-library Deferred {tensor(encoded(donors[0][0],invalid))} {syntax(target)})',
            ['Library:Refused','InvalidLayers'])
    control(f'(library-stack:stages {output_layers} {syntax(invalid)})', ['Library:Refused','Reconstruction'])
    unused = deepcopy(donors[0][1])
    unused[3][0] = reference(f'(= ({first} $unused) (+ 1 2))')[3][0]
    unused[3][2:4] = reference(f'(= (f $x) ({first} 0)) (= (g $x) ({first} 0))')[3]
    unused_layers = [('LC2', first), ('LC1', second)]
    restore(unused_layers, unused)
    receiving = reference('(= (double $x) (* $x 2)) (= (three $x) (+ 1 2))')
    control(f'(ic:reuse-library Deferred {tensor(encoded(unused_layers,unused))} {syntax(receiving)})',
            ['Library:Refused',['Library:TransferStep', json.dumps(first),'UnusedParameters']])
    program = '\n'.join(code)+'\n'
    check_controls(program)
    path = args.output/'transfer.metta';path.write_text(program)
    result = subprocess.run([str(args.cetta.resolve()), '--lang','petta',str(path.resolve())], cwd=ROOT,
                            capture_output=True,text=True,timeout=120)
    (args.output/'transfer.log').write_text(result.stdout+result.stderr)
    forms = parse_forms(result.stdout)
    actual_controls = [row for row in forms if isinstance(row,list) and row[:1]==['StackTransferControl']]
    errors = [{'control':i,'expected':want,'actual':actual_controls[i] if i<len(actual_controls) else None}
              for i,want in enumerate(expected) if i>=len(actual_controls) or want!=actual_controls[i]]
    actual = {int(row[1]):row[2] for row in forms if isinstance(row,list) and row[:1]==['StackTransferCase']}
    executions=[]
    for i,(original,changed,blob,support,queries) in enumerate(wanted):
        row=actual.get(i)
        if not isinstance(row,list) or row[:2]!=['StackTransferred',str(support)] or row[3]!='true':
            errors.append({'case':i,'actual':str(row)[:1800]});continue
        def payload(value):
            assert value[:2]==['TensorData','u8']
            return bytes(int(pair[1]) for pair in value[3])
        if payload(row[2])!=blob:
            errors.append({'case':i,'reason':'different complete bytes'});continue
        source=payload(row[4]).decode()
        if reference(source)!=changed:
            errors.append({'case':i,'reason':'different emitted source'});continue
        executions.append(execute(args.cetta,args.output,i,original,source,queries))
    passed=result.returncode==0 and not errors and len(actual_controls)==len(expected) and len(actual)==len(wanted) \
        and len(executions)==len(wanted) and all(x['passed'] for x in executions)
    cost_panel = measure(args.cetta,args.output,*donors[5],wanted[-1][0],wanted[-1][2],args.measure_pairs) \
        if passed and args.measure_pairs else None
    receipt={'passed':passed,
             'checks':len(expected)+len(wanted),'controls':len(expected),'cases':len(wanted),'errors':errors,
             'executions':executions,'cost_panel':cost_panel,'complete_costs':[{'case':i,'original_bytes':len(plain(original)),
               'joint_bytes':len(blob),'replacement_occurrences_including_helpers':support}
               for i,(original,_changed,blob,support,_queries) in enumerate(wanted)],
             'scope':'unchanged fixed multi-helper transfer, recovery/replay order, independent LM1 bytes and fresh execution; not held-out real-family utility or ILP search completeness'}
    (args.output/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print(json.dumps({**receipt,'errors':len(errors),'executions':len(executions),
                      'cost_panel':cost_panel['medians'] if cost_panel else None}))
    return 0 if receipt['passed'] else 1


if __name__=='__main__':
    raise SystemExit(main())
