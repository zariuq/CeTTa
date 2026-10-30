#!/usr/bin/env python3
"""Finite source discovery beyond seeds, with independent full-cost minima."""

from __future__ import annotations

import argparse
from collections import Counter
from copy import deepcopy
import itertools
import json
from pathlib import Path
import subprocess

from check_binary_codec import ROOT, observation, parse_forms, tensor
from check_ic_context_library import candidate as pair_candidate, encoded, encoded_length, sites, split, variables
from check_ic_library import plain, symbol
from check_ic_library_transfer import clause
from check_ic_search import check_controls
from check_source_codec import reference, text_arg


def templates_from_guide(term, depth, origin):
    """Generate syntax before matching repeated captures or other guides."""
    def term_options(value,height,role,position,bindings):
        yield ('hole',role,position),position+1,[(role,position)]+bindings
        for old_role,index in bindings:
            if old_role==role:yield ('hole',role,index),position+1,bindings
        if height:
            call=split(value)
            if call:
                for args,end,assigned in many(call[1],height-1,position,bindings):
                    yield ('call',call[0],args),end,assigned
            else:
                yield ('literal',deepcopy(value),origin if variables(value) else None),position,bindings
    def many(values,height,position,bindings):
        if not values:
            yield [],position,bindings
        else:
            for first,middle,assigned in term_options(values[0],height,'Value',position,bindings):
                for rest,end,complete in many(values[1:],height,middle,assigned):
                    yield [first]+rest,end,complete
    return [pattern for pattern,_position,_bindings in term_options(term,depth,'Root',0,[])]


def match(pattern,term,origin,environment):
    if pattern[0]=='hole':
        key=pattern[1:]
        if key in environment:return environment[key]==term
        environment[key]=term;return True
    if pattern[0]=='literal':
        return pattern[1]==term and (pattern[2] is None or pattern[2]==origin)
    call=split(term)
    return bool(call and call[0]==pattern[1] and all(match(p,v,origin,environment) for p,v in zip(pattern[2],call[1])))


def ports(pattern):
    result=[]
    def walk(term):
        if term[0]=='hole' and term[1:] not in result:result.append(term[1:])
        elif term[0]=='call':
            for child in term[2]:walk(child)
    walk(pattern);return result


def entered(term,owner,document):
    count=len(clause(document[3][owner])[0])-1
    return term[0]=='Syntax:Variable' and int(term[1])<count or (
        term[0]=='Syntax:Literal' and term[1] in {'I64','F64','BigInt','Rational','Bool','String'})


def apply_pattern(document,name,pattern,ready,available=None):
    available=sites(document) if available is None else available
    holes=ports(pattern)
    def body(term):
        if term[0]=='hole':
            value=['Syntax:Variable',str(holes.index(term[1:]))]
            return value if term[1:] in ready else ['Syntax:Expr',[value]]
        if term[0]=='literal':
            if term[2] is not None:raise ValueError('free source variable is not a rigid literal')
            return deepcopy(term[1])
        return ['Syntax:Expr',[symbol(term[1])]+[body(child) for child in term[2]]]
    try:helper_body=body(pattern)
    except ValueError:return None
    selected=[]
    for origin,(owner,path,_bound,term) in enumerate(available):
        if any(form==owner and path[:len(where)]==where for form,where,_value in selected):continue
        env={}
        if not match(pattern,term,origin,env) or any(not entered(env[key],owner,document) for key in ready):continue
        arguments=[deepcopy(env[key]) if key in ready else
                   ['Syntax:Expr',[symbol('|->'),['Syntax:Expr',[]],deepcopy(env[key])]] for key in holes]
        selected.append((owner,path,['Syntax:Expr',[symbol(name)]+arguments]))
    if len(selected)<2:return None
    changed=deepcopy(document)
    for owner,path,replacement in selected:
        current=changed[3][owner][2]
        for index in path[:-1]:current=current[1][index]
        current[1][path[-1]]=replacement
    names=[['Syntax:PlainName',json.dumps('p'+str(i))] for i in range(len(holes))]
    left=['Syntax:Expr',[symbol(name)]+[['Syntax:Variable',str(i)] for i in range(len(holes))]]
    changed[3].insert(0,['Syntax:Form',names,['Syntax:Expr',[symbol('='),left,helper_body]]])
    return changed,len(selected)


def reference_region(document,name,depth):
    available=sites(document);unique={};paired=0
    for left,right in itertools.combinations(range(len(available)),2):
        seed=pair_candidate(document,name,left,right,selected=available)
        if seed:
            key=json.dumps(seed[0],default=lambda value:list(value))
            unique.setdefault(key,seed[:2])
        for pattern in templates_from_guide(available[left][3],depth,left):
            envs=[]
            for origin in (left,right):
                env={}
                if not match(pattern,available[origin][3],origin,env):break
                envs.append(env)
            if len(envs)!=2:continue
            eligible=[key for key in ports(pattern) if all(entered(env[key],available[origin][0],document)
                                                          for origin,env in zip((left,right),envs))]
            for bits in itertools.product((True,False),repeat=len(eligible)):
                ready=frozenset(key for key,selected in zip(eligible,bits) if selected)
                candidate=apply_pattern(document,name,pattern,ready,available)
                if candidate:
                    paired+=1;key=json.dumps(candidate[0],default=lambda value:list(value))
                    unique.setdefault(key,candidate)
    costs=[(encoded_length(name,changed),changed,support) for changed,support in unique.values()]
    original_cost=len(plain(document));best=min([original_cost]+[cost for cost,_changed,_support in costs])
    winners=[encoded(name,changed) for cost,changed,_support in costs if cost==best]
    for cost,changed,_support in costs:assert cost==len(encoded(name,changed))
    return {'original_bytes':original_cost,'best_bytes':best,'winner_bytes':winners,
            'unique_candidates':len(unique),'paired_supported_candidates':paired,'sites':len(available)}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cetta',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--mechanism-smoke',action='store_true',help='compact ownership/continuation gate; does not replace complete region controls')
    args=parser.parse_args();args.output.mkdir(parents=True,exist_ok=True)
    check_controls((ROOT/'lib/petta/lib_ic_context_grammar.metta').read_text())
    large=json.dumps('shared-payload-'*24)
    examples=[f'(= (f $x) (if $x {large} 0)) (= (g $y) (if $y {large} 0))',
              ' '.join(f'(= ({name} $x) (if (and (== $x {a}) (== {b} {c})) {large} 0))'
                       for name,(a,b,c) in zip(('f','g','h'),((1,1,2),(1,2,1),(2,1,1)))),
              f'(= (f $x) (if (> $x 0) (observe "f" $x) {large})) '
              f'(= (g $y) (if (> $y 0) (observe "g" $y) {large})) '
              '(= (observe $label $x) (let $printed (println! $label) $x))']
    examples.append(' '.join(f'(= ({name} $x) (if (> $x 0) (observe "{name}" $x) {large}))'
                             for name in ('f','g','h','k'))+
                    ' (= (observe $label $x) (let $printed (println! $label) $x))')
    if args.mechanism_smoke:examples=examples[:1]
    regions=[reference_region(reference(text),'ic-learned:g',2) for text in examples]
    pairwise_best=None
    if not args.mechanism_smoke:
        three=reference(examples[1]);available=sites(three);pair_costs=[]
        for left,right in itertools.combinations(range(len(available)),2):
            result=pair_candidate(three,'ic-learned:g',left,right,selected=available)
            if result:pair_costs.append(encoded_length('ic-learned:g',result[0]))
        pairwise_best=min([len(plain(three))]+pair_costs)
        assert regions[1]['best_bytes']<pairwise_best
    code=['!(import! &self (library lib_ic_context_grammar))', '''
(= (grammar-test:finish $state $slices)
   (case (ic:resume $state 17)
     (((IC:SearchResult $best $stats BudgetExhausted $fault $next)
       (if (> $slices 0) (grammar-test:finish $next (- $slices 1))
         (let $closed (ic:release $next) (GrammarTestFault SliceLimit)))) ($finished $finished))))
(= (grammar-test:export $bytes)
   (case (ic:export-library $bytes)
     (((Library:Export $name $document $text $restored) (tensor:inspect (binary:utf8 $text)))
      ($no-helper $no-helper))))
(= (grammar-test:result $result)
   (case $result
     (((IC:SearchResult $best $statistics $reason $first $next)
       (let $closed (ic:release $next)
         (SourceGrammarResult $reason $statistics $first (tensor:inspect $best) (grammar-test:export $best))))
      ($bad-result $bad-result))))
(= (grammar-test:learn $text $pieces)
   (case (ic:learn-library Deferred (syntax:read $text) "g" (IC:Grammar 2) 2048)
     (((IC:PreparedSearch $state)
       (grammar-test:result (if $pieces (grammar-test:finish $state 1000) (ic:resume $state 17000))))
      ($bad-search $bad-search))))
(= (grammar-test:lifecycle $text)
   (case (ic:learn-library Deferred (syntax:read $text) "g" (IC:Grammar 2) 2048)
     (((IC:PreparedSearch $state)
       (case (ic:resume $state 0)
         (((IC:SearchResult $initial $stats $pause $fault $next)
           (case (ic:cancel $next)
             (((IC:SearchResult $same $same-stats $cancelled $same-fault $later)
               (case (grammar-test:finish $later 1000)
                 (((IC:SearchResult $best $last-stats $reason $first $finished)
                   (let $closed (ic:release $finished)
                     (SourceLifecycle $pause $cancelled $reason $first (tensor:shape $best))))
                  ($bad-finish $bad-finish)))) ($bad-cancel $bad-cancel)))) ($bad-pause $bad-pause))))
      ($bad-start $bad-start))))
''']
    for i,text in enumerate(examples):
        code.append(f'!(println! (SourceGrammarCase {i} (grammar-test:learn {text_arg(text)} False)))')
    resumed_index=len(examples)
    code.append(f'!(println! (SourceGrammarCase {resumed_index} (grammar-test:learn {text_arg(examples[0])} True)))')
    controls=[]
    def control(expression,wanted):
        i=len(controls);code.append(f'!(println! (SourceGrammarControl {i} {expression}))')
        controls.append(['SourceGrammarControl',str(i),wanted])
    control(f'(grammar-test:lifecycle {text_arg(examples[0])})',
            ['SourceLifecycle','BudgetExhausted','Cancelled','RegionExhausted','IC:NoFault',[str(regions[0]['best_bytes'])]])
    for depth,quantum,reason in [(-1,64,'InvalidGrammar'),('1.0',64,'InvalidGrammar'),(1,0,'InvalidQuantum')]:
        control(f'(ic:learn-library Deferred (syntax:read {text_arg(examples[0])}) "g" (IC:Grammar {depth}) {quantum})',
                ['IC:SearchRefused',reason])
    control(f'''(case (ic:learn-library Deferred (syntax:read {text_arg(examples[0])}) "g" (IC:Grammar 2) 64)
       (((IC:PreparedSearch $state)
         (let* (($changed (add-atom &self SourceGrammarRevisionControl)) ($answer (ic:resume $state 0))
                ($closed (ic:release $state))) $answer)) ($bad-state $bad-state)))''',
            ['IC:ResumeRejected','ChangedDependencies'])
    fixture=args.output/'source-grammar.metta';fixture.write_text('\n'.join(code)+'\n');check_controls(fixture.read_text())
    run=subprocess.run([str(args.cetta.resolve()),'--lang','petta',str(fixture.resolve())],cwd=ROOT,
                       capture_output=True,text=True,timeout=120)
    (args.output/'source-grammar.log').write_text(run.stdout+run.stderr)
    answers={int(row[1]):row[2] for row in parse_forms(run.stdout) if isinstance(row,list) and row and row[0]=='SourceGrammarCase'}
    errors=[];exports=[];executions=[]
    queries=[ [('(f True)',[large]),('(g False)',['0']),('(f (superpose (True False True)))',[large,'0',large])],
              [('(f 1)',['0']),('(g 1)',['0']),('(h 2)',[large]),('(h 1)',['0']),('(h (superpose (2 1 2)))',[large,'0',large])],
              [('(f 2)',['2']),('(g -1)',[large]),('(f (superpose (1 2 1)))',['1','2','1'])],
              [('(f 2)',['2']),('(g -1)',[large]),('(h 2)',['2']),('(k -1)',[large]),
               ('(f (superpose (1 2 1)))',['1','2','1'])] ]
    no_op=[]
    for i in range(len(examples)+1):
        which=i if i<len(examples) else 0;value=answers.get(i);region=regions[which]
        if not isinstance(value,list) or value[0]!='SourceGrammarResult':errors.append({'case':i,'actual':value});continue
        observed=value[4];accepted=[observation(blob) for blob in region['winner_bytes']]
        if region['best_bytes']==region['original_bytes']:
            accepted.append(observation(plain(reference(examples[which]))))
        if value[1]!='RegionExhausted' or value[3]!='IC:NoFault' or observed not in accepted:
            errors.append({'case':i,'reason':value[1:4],'byte_optimum':region['best_bytes'],'observed':str(observed)[:180]})
        source=bytes(int(row[1]) for row in value[5][3]).decode() if isinstance(value[5],list) and value[5][0]=='TensorData' else None
        if source is not None:
            (args.output/f'export-{i}.metta').write_text(source);exports.append(i)
            if i<len(examples):
                variants=[]
                for label,body in [('original',examples[which]),('learned',source)]:
                    query_text='\n'.join(f'!(println! (SourceAnswer {j} (collapse {call})))' for j,(call,_wanted) in enumerate(queries[which]))
                    path=args.output/f'execute-{i}-{label}.metta';path.write_text(body+'\n'+query_text+'\n')
                    result=subprocess.run([str(args.cetta.resolve()),'--lang','petta',str(path.resolve())],cwd=ROOT,
                                          capture_output=True,text=True,timeout=30)
                    (args.output/f'execute-{i}-{label}.log').write_text(result.stdout+result.stderr)
                    observed=[row for row in parse_forms(result.stdout) if isinstance(row,list) and row and row[0]=='SourceAnswer']
                    valid=result.returncode==0 and len(observed)==len(queries[which]) and all(
                        row[1]==str(j) and Counter(map(json.dumps,row[2]))==Counter(map(json.dumps,wanted))
                        for j,(row,(_call,wanted)) in enumerate(zip(observed,queries[which])))
                    effects=[line for line in result.stdout.splitlines() if line and not line.startswith('(SourceAnswer ') and line not in ('true','false')]
                    variants.append({'variant':label,'passed':valid,'effects':effects})
                executions.append({'case':i,'passed':all(v['passed'] for v in variants) and variants[0]['effects']==variants[1]['effects'],
                                   'variants':variants})
        elif region['best_bytes']==region['original_bytes']:
            no_op.append(i)
        else:errors.append({'case':i,'reason':'improving source has no exported helper'})
    if all(isinstance(answers.get(i),list) and len(answers[i])>4 for i in (0,resumed_index)) and answers[0][4]!=answers[resumed_index][4]:
        errors.append({'case':resumed_index,'reason':'resumed winner differs'})
    actual_controls=[row for row in parse_forms(run.stdout) if isinstance(row,list) and row and row[0]=='SourceGrammarControl']
    if actual_controls!=controls:errors.append({'controls':actual_controls,'expected':controls})
    result={'passed':run.returncode==0 and len(answers)==len(examples)+1 and not errors and all(row['passed'] for row in executions),
            'checks':len(examples)+1+len(controls),'mechanism_smoke':args.mechanism_smoke,
            'errors':errors,'exports':exports,'executions':executions,'no_op_cases':no_op,'three_program_pairwise_best_bytes':pairwise_best,
            'regions':[{k:v for k,v in region.items() if k!='winner_bytes'} for region in regions],
            'scope':'independent union of pairwise seeds and depth-bounded guide-shaped late matching, all calling-convention subsets, complete small-source byte minima and resumed search'}
    (args.output/'receipt.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
    return 0 if result['passed'] else 1


if __name__=='__main__':raise SystemExit(main())
