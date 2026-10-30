#!/usr/bin/env python3
"""Independent source-template transfer, complete costs and fresh execution."""

from __future__ import annotations

import argparse
from collections import Counter
from copy import deepcopy
import json
from pathlib import Path
import subprocess

from check_binary_codec import ROOT, observation, parse_forms, tensor
from check_ic_archive import syntax as construct_source
from check_ic_context_library import candidate, encoded, mentions, sites, split, variables
from check_ic_library import clause, lc1, plain, symbol
from check_ic_search import check_controls
from check_source_codec import reference, text_arg


def transfer(learned, target):
    """Reify the exported body; do not generate or select a new template."""
    definition = learned[3][0]
    left, body = clause(definition)
    name, arity = left[0][2].decode(), len(left) - 1
    if any(mentions(form[2], name) for form in target[3]):
        return None
    modes = {}

    def port(index, ready):
        index = int(index)
        if not 0 <= index < arity or index in modes and modes[index] != ready:
            raise ValueError('unsupported parameter interface')
        modes[index] = ready
        return 'port', index

    def reify(term):
        if term[0] == 'Syntax:Variable':
            return port(term[1], True)
        if term[0] == 'Syntax:Expr' and len(term[1]) == 1 and term[1][0][0] == 'Syntax:Variable':
            return port(term[1][0][1], False)
        operation = split(term)
        if operation:
            return 'call', operation[0], [reify(child) for child in operation[1]]
        if not variables(term):
            return 'literal', deepcopy(term)
        raise ValueError('unsupported source position')

    pattern = reify(body)
    if set(modes) != set(range(arity)):
        return None

    def entered(term, owner):
        ports = len(clause(target[3][owner])[0]) - 1
        return term[0] == 'Syntax:Variable' and int(term[1]) < ports or (
            term[0] == 'Syntax:Literal' and term[1] in {'I64','F64','BigInt','Rational','Bool','String'})

    def match(template, term, owner, environment):
        if template[0] == 'literal':
            return template[1] == term
        if template[0] == 'port':
            index = template[1]
            if modes[index] and not entered(term, owner):
                return False
            if index in environment:
                return term == environment[index]
            environment[index] = term
            return True
        operation = split(term)
        return bool(operation and operation[0] == template[1] and all(
            match(child, value, owner, environment) for child, value in zip(template[2], operation[1])))

    replacements = []
    for owner, path, _bound, term in sites(target):
        if any(form == owner and path[:len(where)] == where for form, where, _value in replacements):
            continue
        environment = {}
        if match(pattern, term, owner, environment):
            arguments = [deepcopy(environment[i]) if modes[i] else
                         ['Syntax:Expr',[symbol('|->'),['Syntax:Expr',[]],deepcopy(environment[i])]]
                         for i in range(arity)]
            replacements.append((owner,path,['Syntax:Expr',[symbol(name)]+arguments]))
    if not replacements:
        return None
    result = deepcopy(target)
    for owner, path, replacement in replacements:
        term = result[3][owner][2]
        for index in path[:-1]:
            term = term[1][index]
        term[1][path[-1]] = replacement
    result[3].insert(0,deepcopy(definition))
    return result, len(replacements)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cetta',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();args.output.mkdir(parents=True,exist_ok=True)
    check_controls((ROOT/'lib/petta/lib_ic_library_transfer.metta').read_text())
    training=[
        '(= (f $x) (+ (* $x 2) 1)) (= (g $y) (+ (* $y 3) 1))',
        '(= (f $x) (if (unknown-f $x) (then-f $x) (else-f $x))) '
        '(= (g $y) (if (unknown-g $y) (then-g $y) (else-g $y)))',
        '(= (f $x) (+ $x $x)) (= (g $y) (+ $y $y))',
    ]
    payload=[f'item-{i}' for i in range(40)]
    shared='(quote ('+' '.join(payload)+'))'
    training.extend([
        f'(= (f $x) (if (first-test $x) {shared} (first-else $x))) '
        f'(= (g $y) (if (second-test $y) {shared} (second-else $y)))',
        '(= (f $x) (+ 1 2)) (= (g $y) (+ 1 2))',
    ])
    learned=[]
    for text in training:
        doc=reference(text); available=sites(doc)
        seeds=[next(i for i,(owner,path,_bound,_term) in enumerate(available) if owner==which and path==(2,))
               for which in (0,1)]
        learned.append(candidate(doc,'ic-learned:transfer',*seeds)[0])
    reversed_ports=deepcopy(learned[0])
    def swap_ports(term):
        if term[0]=='Syntax:Variable':
            term[1]=str(1-int(term[1]))
        elif term[0]=='Syntax:Expr':
            for child in term[1]:swap_ports(child)
    swap_ports(reversed_ports[3][0][2][1][2])
    for form in reversed_ports[3][1:]:
        call=form[2][1][2][1]
        call[1],call[2]=call[2],call[1]
    learned.append(reversed_ports)
    examples=[
        (0,'(= (slope $x) (+ (* $x 7) 1))', [('(slope 2)',['15']),('(slope -2)',['-13'])]),
        (0,'(= (h $a $b) (+ (* $b $a) 1)) (= (twice $z) (+ (* $z $z) 1))',
         [('(h 2 3)',['7']),('(twice 4)',['17']),('(h (superpose (1 2 1)) 3)',['4','7','4'])]),
        (1,'(= (h $x) (let $v (+ $x 1) (if (> $v 0) (observe $v) (superpose ())))) '
           '(= (observe $n) (let $printed (println! $n) $n))',
         [('(h 2)',['3']),('(h -2)',[])]),
        (1,'(= (h $xs) (map-atom $xs $v (if (> $v 0) (+ $v $v) (- 0 $v))))',
         [('(h (1 -2 3))',[['2','2','6']]),('(h ())',[[]])]),
        (1,'(= (h $x) (if True (quote (datum $x)) (superpose ())))',
         [('(h (quote (+ 1 2)))',[['datum',['+','1','2']]])]),
        (1,'(= (h $x) (if True (+ (observe $x) (observe $x)) (/ 1 0))) '
           '(= (observe $n) (let $printed (println! $n) $n))', [('(h 2)',['4'])]),
        (2,'(= (same $x) (+ $x $x)) (= (different $a $b) (+ $a $b))',
         [('(same 3)',['6']),('(different 2 4)',['6'])]),
        (5,'(= (h $a $b) (+ (* $b $a) 1))', [('(h 2 3)',['7'])]),
        (4,'(= (h $data) (+ 1 2))', [('(h (quote code))',['3'])]),
        (3,f'(= (rows $x) (let* (($first (if (> $x 0) {shared} (quote negative))) '
           f'($second (if (< $x 4) {shared} (quote large)))) (quote (results $first $second))))',
         [('(rows 2)',[['results',payload,payload]]),('(rows -1)',[['results','negative',payload]]),
          ('(rows 8)',[['results',payload,'large']])]),
    ]
    code=['!(import! &self (library lib_ic_library_transfer))', '''
(= (transfer-test:reuse $artifact $source)
   (let $original (syntax:read $source)
     (case (ic:reuse-library Deferred $artifact $original)
       (((Library:Candidate $bytes $support)
         (case (ic:export-library $bytes)
           (((Library:Export $name $document $text $restored)
             (TransferCandidate $support (tensor:inspect $bytes)
               (== $original $restored) (tensor:inspect (binary:utf8 $text))))
            ($bad-export $bad-export)))) ($bad-reuse $bad-reuse)))))
''']
    wanted=[]
    for i,(which,text,queries) in enumerate(examples):
        original=reference(text); changed,support=transfer(learned[which],original)
        artifact=encoded('ic-learned:transfer',learned[which]);result_bytes=encoded('ic-learned:transfer',changed)
        code.append(f'!(println! (TransferCase {i} (transfer-test:reuse {tensor(artifact)} {text_arg(text)})))')
        wanted.append((changed,support,result_bytes))
    controls=[]
    def control(expression,expected):
        i=len(controls);code.append(f'!(println! (TransferControl {i} {expression}))')
        controls.append(['TransferControl',str(i),expected])
    first_artifact=tensor(encoded('ic-learned:transfer',learned[0]))
    for target,reason in [
        ('(= (h $x) (- (* $x 7) 1))','InsufficientSupport'),
        ('(= (h $x) (let $local (+ $x 1) (+ (* $local 7) 1)))','InsufficientSupport'),
        ('(= (ic-learned:transfer $x) $x)','NameCollision'),
        ('(: + (-> Atom Atom Atom)) (= (h $x) (+ (* $x 7) 1))','NonstandardOperators'),
        ('(quote (+ (* 3 7) 1))','InsufficientSupport'),
    ]:
        control(f'(ic:reuse-library Deferred {first_artifact} (syntax:read {text_arg(target)}))',
                ['Library:Refused',reason])
    control(f'(ic:reuse-library Deferred {tensor(b"bad")} (syntax:read ""))', ['Library:Refused','InvalidArtifact'])
    single=reference(examples[0][1]);single_candidate=wanted[0][2]
    control(f'(ic:library-check {tensor(plain(single))} {tensor(plain(single))} {tensor(single_candidate)})',
            ['IC:PackedRejected','NotSmaller'])
    positive=reference(examples[-1][1]);positive_bytes=wanted[-1][2]
    assert len(positive_bytes)<len(plain(positive))
    control(f'''(case (ic:library-check {tensor(plain(positive))} {tensor(plain(positive))} {tensor(positive_bytes)})
       (((IC:PackedAccepted $winner $before $after)
         (TransferAccepted (== $winner {tensor(positive_bytes)}) $before $after)) ($not-accepted $not-accepted)))''',
        ['TransferAccepted','true',str(len(plain(positive))),str(len(positive_bytes))])
    unused=reference('(= (ic-learned:unused $unused) (+ 1 2)) '
                     '(= (f $x) (ic-learned:unused 0)) (= (g $y) (ic-learned:unused 0))')
    control(f'(ic:reuse-library Deferred {tensor(encoded("ic-learned:unused",unused))} (syntax:read "(= (h $x) (+ 1 2))"))',
            ['Library:Refused','UnusedParameters'])
    foreign=deepcopy(reference(examples[0][1]));foreign[2][1]='"he"'
    control(f'(ic:reuse-library Deferred {first_artifact} {construct_source(foreign)})',
            ['Library:Refused','DifferentReader'])
    control(f'(ic:reuse-library Deferred (ic:source-pack (syntax:read "")) (syntax:read ""))',
            ['Library:Refused','NoLearnedLibrary'])
    control(f'(== (ic:reuse-library Deferred {tensor(lc1("ic-learned:transfer",learned[0]))} '
            f'(syntax:read {text_arg(examples[0][1])})) (Library:Candidate {tensor(wanted[0][2])} {wanted[0][1]}))', 'true')
    fixture=args.output/'transfer.metta'; fixture.write_text('\n'.join(code)+'\n')
    check_controls(fixture.read_text())
    run=subprocess.run([str(args.cetta.resolve()),'--lang','petta',str(fixture.resolve())],cwd=ROOT,
                       capture_output=True,text=True,timeout=120)
    (args.output/'transfer.log').write_text(run.stdout+run.stderr)
    forms=parse_forms(run.stdout)
    cases={int(row[1]):row[2] for row in forms if isinstance(row,list) and row and row[0]=='TransferCase'}
    actual_controls=[row for row in forms if isinstance(row,list) and row and row[0]=='TransferControl']
    errors=[];executions=[]
    for i,(changed,support,blob) in enumerate(wanted):
        actual=cases.get(i)
        if not isinstance(actual,list) or actual[:4]!=['TransferCandidate',str(support),observation(blob),'true']:
            errors.append({'case':i,'actual':str(actual)[:1500]});continue
        source=bytes(int(x[1]) for x in actual[4][3]).decode()
        if reference(source)!=changed:
            errors.append({'case':i,'reason':'source render changed'})
        (args.output/f'export-{i}.metta').write_text(source)
        _which,original,queries=examples[i]; variants=[]
        for label,body in [('original',original),('learned',source)]:
            query_text='\n'.join(f'!(println! (TransferAnswer {j} (collapse {query})))' for j,(query,_expected) in enumerate(queries))
            path=args.output/f'execute-{i}-{label}.metta';path.write_text(body+'\n'+query_text+'\n')
            result=subprocess.run([str(args.cetta.resolve()),'--lang','petta',str(path.resolve())],cwd=ROOT,
                                  capture_output=True,text=True,timeout=30)
            (args.output/f'execute-{i}-{label}.log').write_text(result.stdout+result.stderr)
            answers=[row for row in parse_forms(result.stdout) if isinstance(row,list) and row and row[0]=='TransferAnswer']
            passed=result.returncode==0 and len(answers)==len(queries) and all(
                row[1]==str(j) and Counter(map(json.dumps,row[2]))==Counter(map(json.dumps,expected))
                for j,(row,(_query,expected)) in enumerate(zip(answers,queries)))
            effects=[line for line in result.stdout.splitlines() if line and not line.startswith('(TransferAnswer ')
                     and line not in ('true','false')]
            variants.append({'variant':label,'passed':passed,'effects':effects})
        executions.append({'case':i,'passed':all(v['passed'] for v in variants) and variants[0]['effects']==variants[1]['effects'],
                           'variants':variants})
    control_errors=[{'expected':expected,'actual':actual_controls[i] if i<len(actual_controls) else None}
                    for i,expected in enumerate(controls) if i>=len(actual_controls) or actual_controls[i]!=expected]
    receipt={'passed':run.returncode==0 and len(cases)==len(wanted) and not errors and not control_errors
                     and len(actual_controls)==len(controls) and all(x['passed'] for x in executions),
             'checks':len(wanted)+len(controls),'cases':len(cases),'errors':errors,'control_errors':control_errors,'executions':executions,
             'complete_costs':[{'case':i,'original_bytes':len(plain(reference(examples[i][1]))),'helper_bytes':len(row[2]),'support':row[1]}
                               for i,row in enumerate(wanted)],
             'scope':'fixed exported helpers, independent reification/matching, complete LC2 bytes and new synthetic receiving programs; not held-out real-family utility'}
    (args.output/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print(json.dumps(receipt))
    return 0 if receipt['passed'] else 1


if __name__=='__main__':raise SystemExit(main())
