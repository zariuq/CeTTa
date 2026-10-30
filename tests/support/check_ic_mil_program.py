#!/usr/bin/env python3
"""Independent MP1 artifacts, typed MIL search, exact costs and resumed prediction."""

from __future__ import annotations

import argparse
from copy import deepcopy
import itertools
import json
from pathlib import Path
import subprocess

from check_binary_codec import ROOT, parse_forms, tensor, observation
from check_binary_value import Text
from check_ic_mil_source import background_bytes, family
from check_portable_value import construct, encoded
from check_ic_search import check_controls
from check_source_codec import reference as source_document
from check_mil_skeletons import reference as substitutions
from check_ic_context_library import candidate as source_candidate, criterion_seeds


def header(target, positive, negative, mode):
    return encoded(['MIL.SourceQueries',target,mode,[row[0] for row in positive],
                    [['MIL.Ex',a,b] for a,b in negative]])


def artifact(target, positive, negative, mode, background=None, program=None):
    body=(['MIL.SourceLiteral',encoded([row[1] for row in positive])] if program is None else
          ['MIL.SourceProgram',background,program])
    return encoded(['MIL.SourceTaskArchive',Text('MP1'),header(target,positive,negative,mode),body])


def evaluate(program, value, primitives):
    if program[0]=='MIL.SourceRef':return primitives[int(program[1])](value)
    if program[0]=='MIL.Id':return evaluate(program[1],value,primitives)
    if program[0]=='MIL.Chain':
        return [answer for middle in evaluate(program[1],value,primitives)
                for answer in evaluate(program[2],middle,primitives)]
    if program[0]=='MIL.Choice':return evaluate(program[1],value,primitives)+evaluate(program[2],value,primitives)
    if program[0] in ('MIL.Map','MIL.HO1'):
        return [list(row) for row in itertools.product(*(evaluate(program[-1],x,primitives) for x in value))]
    raise ValueError(program)


def accepts(program, positive, negative, mode, primitives):
    for query,wanted in positive:
        answers=evaluate(program,query,primitives)
        if wanted not in answers or mode=='MIL.Functional' and any(x!=wanted for x in answers):return False
    return all(forbidden not in evaluate(program,query,primitives) for query,forbidden in negative)


HELPERS=r'''
!(import! &self (library lib_ic_mil_program))
(= (program-test:result $expected $result)
   (case $result
     (((IC:SearchResult $best (IC:Statistics $turns $seen $accepted $faults) $reason $first $state)
       (let $closed (ic:release $state)
         (ProgramResult (== $best $expected) $seen $accepted $faults $reason $first)))
      ($failed $failed))))
(= (program-test:pieces $expected (IC:PreparedSearch $state))
   (case (ic:resume $state 1)
     (((IC:SearchResult $best $stats BudgetExhausted $first $next)
       (ProgramPaused BudgetExhausted (program-test:result $expected (ic:resume $next 2048))))
      ($failed $failed))))
(= (program-test:cancel $expected (IC:PreparedSearch $state))
   (case (ic:cancel $state)
     (((IC:SearchResult $best $stats Cancelled $first $next)
       (ProgramCancelled Cancelled (program-test:result $expected (ic:resume $next 2048))))
      ($failed $failed))))
(= (program-test:changed-source (IC:PreparedSearch $state))
   (let* (($changed (add-atom (context-space) MP1SourceChanged))
          ($result (ic:resume $state 0)) ($closed (ic:release $state))) $result))
(= (program-test:changed-model (IC:PreparedSearch $state))
   (case $state
     (((IC:SearchState (IC:Task (MIL.SourceModel $background $skeletons) $original $observation $guards $quantum)
         $queue $best $stats $first)
       (let* (($result (ic:resume (IC:SearchState
                  (IC:Task (MIL.SourceModel $background ()) $original $observation $guards $quantum)
                  $queue $best $stats $first) 0)) ($closed (ic:release $state))) $result)))))
(= (program-test:verify $original $candidate)
   (case (ic:verify Source:MIL $original $candidate)
     (((IC:SourceVerified $bytes $before $after) (ProgramVerified (== $bytes $candidate) $before $after))
      ($failed $failed))))
(= (program-test:proposal $original $background $program)
   (case (mil-program:proposal $original $background $program)
     (((IC:Candidate $bytes) (tensor:inspect $bytes)) ($failed $failed))))
'''


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cetta',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();args.output.mkdir(parents=True,exist_ok=True)
    code=[HELPERS];expected=[];comparisons=[]
    def add(call,wanted):
        index=len(expected);code.append(f'!(println! (ProgramCase {index} {call}))')
        expected.append(['ProgramCase',str(index),wanted])
    declarations=family([('ic-learned:shift','Number','Number'),('ic-learned:double','Number','Number')])
    dictionary=source_document('(= (ic-learned:shift $x) (+ $x 1.25)) (= (ic-learned:double $x) (* $x 2))')
    background=background_bytes(declarations,dictionary)
    target=['MIL.Rel','Number','Number'];mode='MIL.Functional'
    positive=[(x,x+1.25) for x in range(32)];negative=[(0,0.0),(-1.25,5.0)]
    original=artifact(target,positive,negative,mode)
    skeletons=[['MIL.Hole','p'],['MIL.Chain',['MIL.Hole','p'],['MIL.Hole','p']]]
    region=f'(MIL.SourceRegion {tensor(background)} {construct(skeletons)})'
    rows=[('p','Number','Number'),('q','Number','Number')]
    def references(value):
        if isinstance(value,str) and value in ('p','q'):return ['MIL.SourceRef',('p','q').index(value)]
        return [references(x) for x in value] if isinstance(value,list) else value
    programs=[]
    for skeleton in skeletons:
        for rendered,count in substitutions(skeleton,('Number','Number'),rows).items():
            programs.extend([references(parse_forms(rendered)[0])]*count)
    primitives=[lambda x:[x+1.25],lambda x:[x*2]]
    best=original;accepted=0
    for program in programs:
        candidate=artifact(target,positive,negative,mode,background,program)
        if accepts(program,positive,negative,mode,primitives) and len(candidate)<len(best):
            best=candidate;accepted+=1
    assert best!=original
    wanted=['ProgramResult','true',str(len(programs)),str(accepted),'0','RegionExhausted','IC:NoFault']
    examples=['MIL.Examples',[['MIL.Ex',a,b] for a,b in positive],[['MIL.Ex',a,b] for a,b in negative],mode]
    add(f'(case (ic:prepare-examples Source:MIL {construct(target)} {construct(examples)}) '
        '(((IC:SourceArtifact $bytes) (tensor:inspect $bytes)) ($failed $failed)))',observation(original))
    add(f'(program-test:proposal {tensor(original)} {tensor(background)} (MIL.SourceRef 0))',observation(best))
    add(f'(program-test:result {tensor(best)} (ic:fit Source:MIL {tensor(original)} {region} () 2048))',wanted)
    prepared=f'(ic:prepare-fit Source:MIL {tensor(original)} {region} ())'
    add(f'(program-test:pieces {tensor(best)} {prepared})',['ProgramPaused','BudgetExhausted',wanted])
    add(f'(program-test:cancel {tensor(best)} {prepared})',['ProgramCancelled','Cancelled',wanted])
    add(f'(program-test:changed-model {prepared})',['IC:ResumeRejected','InvalidArtifact'])
    add(f'(program-test:verify {tensor(original)} {tensor(best)})',
        ['ProgramVerified','true',str(len(original)),str(len(best))])
    add(f'(ic:predict Source:MIL {tensor(best)} {construct([-3.0,0.5,2.0,-3.0])})',
        ['IC:Predictions',[['-1.75'],['1.75'],['3.25'],['-1.75']]])
    add(f'(ic:predict Source:MIL {tensor(original)} ())',['IC:FitRefused','NoModel'])
    add(f'(ic:predict Source:MIL {tensor(best)} (new-space))',['IC:FitRefused','Inputs'])
    add(f'(ic:prepare-fit Source:MIL {tensor(original)} (MIL.SourceRegion {tensor(background)} ((MIL.Closure (MIL.Hole p)))) ())',
        ['IC:FitRefused','Region'])
    add(f'(program-test:result {tensor(original)} '
        f'(ic:fit Source:MIL {tensor(original)} (MIL.SourceRegion {tensor(background)} ()) () 2048))',
        ['ProgramResult','true','0','0','0','RegionExhausted','IC:NoFault'])
    changed=deepcopy(positive);changed[0]=(0.0,changed[0][1])
    altered=artifact(target,changed,negative,mode,background,['MIL.SourceRef',0])
    add(f'(ic:verify Source:MIL {tensor(original)} {tensor(altered)})',['IC:SourceRejected','Reconstruction'])
    missing=artifact(target,positive,negative,mode,b'', ['MIL.SourceRef',0])
    add(f'(ic:verify Source:MIL {tensor(original)} {tensor(missing)})',['IC:SourceRejected','InvalidArtifact'])
    wrong=artifact(target,positive,negative,mode,background,['MIL.SourceRef',99])
    add(f'(ic:verify Source:MIL {tensor(original)} {tensor(wrong)})',['IC:SourceRejected','InvalidArtifact'])
    big_doc=source_document('(= (ic-learned:shift $x) (if (== $x $x) (+ $x 1.25) '
                            f'(quote "{"payload-"*512}")))')
    big_bg=background_bytes(family([('ic-learned:shift','Number','Number')]),big_doc)
    big=artifact(target,positive,negative,mode,big_bg,['MIL.SourceRef',0])
    assert len(big)>len(original)
    add(f'(ic:verify Source:MIL {tensor(original)} {tensor(big)})',['IC:SourceRejected','NotSmaller'])
    comparisons.append({'name':'f64-label-table','original_bytes':len(original),'best_bytes':len(best),
                        'emitted':len(programs),'accepted':accepted,'larger_one-reference_program_bytes':len(big)})
    # Header and body arities are validated before a malformed raw task can
    # publish an owned search. An empty region is not global exhaustion.
    malformed=encoded(['MIL.SourceTaskArchive',Text('MP1'),header(target,positive,negative,mode),
                       ['MIL.SourceLiteral',encoded([row[1] for row in positive[:-1]])]])
    add(f'(ic:prepare-fit Source:MIL {tensor(malformed)} {region} ())',
        ['IC:SearchRefused','InvalidTask'])
    bad_version=encoded(['MIL.SourceTaskArchive',Text('MP0'),header(target,positive,negative,mode),
                         ['MIL.SourceLiteral',encoded([row[1] for row in positive])]])
    add(f'(ic:prepare-fit Source:MIL {tensor(bad_version)} {region} ())',
        ['IC:FitRefused','SourceTask'])
    empty=artifact(target,[],[],'MIL.Functional')
    add(f'(program-test:result {tensor(empty)} (ic:fit Source:MIL {tensor(empty)} {region} () 2048))',
        ['ProgramResult','true','4','0','0','RegionExhausted','IC:NoFault'])

    # The negative-only query is evaluated even though it has no positive row.
    pos=[(0,1.25)]*64;neg=[(9,19.0)]
    leak_doc=source_document('(= (ic-learned:leak $x) (superpose ((+ $x 1.25) (+ (* $x 2) 1))))')
    leak_bg=background_bytes(family([('ic-learned:leak','Number','Number')]),leak_doc)
    raw=artifact(target,pos,neg,'MIL.Relational')
    leak=artifact(target,pos,neg,'MIL.Relational',leak_bg,['MIL.SourceRef',0])
    assert len(leak)<len(raw)
    add(f'(ic:verify Source:MIL {tensor(raw)} {tensor(leak)})',['IC:SourceRejected','Reconstruction'])
    no_neg=artifact(target,pos,[],'MIL.Relational')
    no_neg_leak=artifact(target,pos,[],'MIL.Relational',leak_bg,['MIL.SourceRef',0])
    add(f'(program-test:verify {tensor(no_neg)} {tensor(no_neg_leak)})',
        ['ProgramVerified','true',str(len(no_neg)),str(len(no_neg_leak))])
    functional=artifact(target,pos,[],'MIL.Functional')
    functional_leak=artifact(target,pos,[],'MIL.Functional',leak_bg,['MIL.SourceRef',0])
    add(f'(ic:verify Source:MIL {tensor(functional)} {tensor(functional_leak)})',['IC:SourceRejected','Reconstruction'])

    # Effectful helpers still have their authored execution interface. They
    # are not admitted to an output-only model checker or speculatively run.
    effect_doc=source_document('(= (ic-learned:effect $x) (println! (Event $x "MP1-EFFECT-MUST-NOT-RUN")))')
    effect_bg=background_bytes(family([('ic-learned:effect','Number','Number')],[('Event',2)]),effect_doc)
    add(f'(ic:prepare-fit Source:MIL {tensor(original)} (MIL.SourceRegion {tensor(effect_bg)} ((MIL.Hole p))) ())',
        ['IC:FitRefused','Effects'])
    native_doc=source_document('(= (ic-learned:clock) (__cetta_lib_system_monotonic_ns ()))')
    native_bg=background_bytes(family([('ic-learned:clock','Unit','Number')],[('__cetta_lib_system_monotonic_ns',1)]),native_doc)
    add(f'(ic:prepare-background Source:MIL {tensor(native_bg)} ())',['MIL.SourceRefused','Family'])

    # Literal Error-shaped data is distinct from an actual raised error.
    data_value=['Error','origin',Text('literal')]
    data_doc=source_document('(= (ic-learned:data) (quote (Error origin "literal")))')
    data_bg=background_bytes(family([('ic-learned:data','Unit','Data')]),data_doc)
    dt=['MIL.Rel','Unit','Data'];dp=[(['MIL.Args'],data_value)]*64
    data_raw=artifact(dt,dp,[],'MIL.Functional');data_model=artifact(dt,dp,[],'MIL.Functional',data_bg,['MIL.SourceRef',0])
    add(f'(program-test:verify {tensor(data_raw)} {tensor(data_model)})',
        ['ProgramVerified','true',str(len(data_raw)),str(len(data_model))])
    error_doc=source_document('(= (ic-learned:divide $x) (/ $x 0))')
    error_bg=background_bytes(family([('ic-learned:divide','Number','Data')]),error_doc)
    ep=[(1,'Error')]*64;et=['MIL.Rel','Number','Data']
    error_raw=artifact(et,ep,[],'MIL.Relational');error_model=artifact(et,ep,[],'MIL.Relational',error_bg,['MIL.SourceRef',0])
    add(f'(ic:verify Source:MIL {tensor(error_raw)} {tensor(error_model)})',['IC:SourceRejected','Reconstruction'])
    # A ready identity returns hostile-looking data without invoking its head.
    identity_doc=source_document('(= (ic-learned:identity $x) $x)')
    identity_bg=background_bytes(family([('ic-learned:identity','Data','Data')]),identity_doc)
    datum=['println!',Text('MP1-DATA-MUST-NOT-RUN')]
    identity_type=['MIL.Rel','Data','Data'];identity_pos=[(datum,datum)]*64
    identity_raw=artifact(identity_type,identity_pos,[],'MIL.Functional')
    identity_model=artifact(identity_type,identity_pos,[],'MIL.Functional',identity_bg,['MIL.SourceRef',0])
    add(f'(program-test:verify {tensor(identity_raw)} {tensor(identity_model)})',
        ['ProgramVerified','true',str(len(identity_raw)),str(len(identity_model))])

    # A real extracted helper is composed with declared background predicates.
    # The source learner is checked separately against this same fixed witness;
    # these labels and model costs are computed by the independent evaluator.
    array_source=source_document((ROOT/'lib/petta/lib_ic_numeric_arrays.metta').read_text())
    first,second=criterion_seeds(array_source)
    extracted,_support,_pattern=source_candidate(array_source,'ic-learned:criterion',first,second)
    codec_dictionary=deepcopy(extracted);codec_dictionary[3]=codec_dictionary[3][:1]
    codec_dictionary[3].extend(source_document(
        '(= (ic-learned:exact $x) (== $x (IC:Some Numeric:Exact))) '
        '(= (ic-learned:leak $x) (superpose ((== $x $x) False)))')[3])
    codec_family=family([('ic-learned:criterion','TensorKind','Criterion'),
                         ('ic-learned:exact','Criterion','Bool'),('ic-learned:leak','Criterion','Bool')],
                        [('IC:Some',1),('IC:None',0),('Numeric:Exact',0)])
    codec_bg=background_bytes(codec_family,codec_dictionary)
    codec_type=['MIL.Rel',['MIL.List','TensorKind'],['MIL.List','Bool']]
    codec_pos=[([Text('BoolTensor'),Text('I64Tensor')],['true','true'])]*96
    codec_neg=[([Text('BoolTensor')],['false'])]
    codec_original=artifact(codec_type,codec_pos,codec_neg,'MIL.Relational')
    codec_program=['MIL.HO1','MIL.MapOp',['MIL.Chain',['MIL.SourceRef',0],['MIL.SourceRef',1]]]
    codec_bad=['MIL.HO1','MIL.MapOp',['MIL.Chain',['MIL.SourceRef',0],['MIL.SourceRef',2]]]
    codec_primitives=[lambda kind:[['IC:Some','Numeric:Exact'] if kind in ('BoolTensor','I64Tensor') else 'IC:None'],
                      lambda item:['true' if item==['IC:Some','Numeric:Exact'] else 'false'],
                      lambda _item:['true','false']]
    assert accepts(codec_program,codec_pos,codec_neg,'MIL.Relational',codec_primitives)
    assert not accepts(codec_bad,codec_pos,codec_neg,'MIL.Relational',codec_primitives)
    codec_best=artifact(codec_type,codec_pos,codec_neg,'MIL.Relational',codec_bg,codec_program)
    assert len(codec_best)<len(codec_original)
    codec_shape=['MIL.HO1','MIL.MapOp',['MIL.Chain',['MIL.Hole','classify'],['MIL.Hole','test']]]
    codec_region=f'(MIL.SourceRegion {tensor(codec_bg)} {construct([codec_shape])})'
    add(f'(program-test:result {tensor(codec_best)} '
        f'(ic:fit Source:MIL {tensor(codec_original)} {codec_region} () 4096))',
        ['ProgramResult','true','2','1','0','RegionExhausted','IC:NoFault'])
    new_inputs=[[Text('F64Tensor'),Text('Unknown'),Text('BoolTensor'),Text('I64Tensor'),Text('BoolTensor')]]
    add(f'(ic:predict Source:MIL {tensor(codec_best)} {construct(new_inputs)})',
        ['IC:Predictions',[[['false','false','true','true','true']]]])
    comparisons.append({'name':'real-array-codec-composition','source':'lib/petta/lib_ic_numeric_arrays.metta',
                        'support':_support,'original_bytes':len(codec_original),'best_bytes':len(codec_best),
                        'emitted':2,'accepted':1})

    # Mutation is last: it deliberately invalidates every retained source view.
    add(f'(program-test:changed-source {prepared})',['IC:ResumeRejected','ChangedDependencies'])
    source='\n'.join(code)+'\n';check_controls(source)
    check_controls((ROOT/'lib/petta/lib_ic_mil_program.metta').read_text())
    path=args.output/'source-programs.metta';path.write_text(source)
    run=subprocess.run([str(args.cetta.resolve()),'--lang','petta',str(path.resolve())],
                       cwd=ROOT,capture_output=True,text=True,timeout=120)
    (args.output/'stdout').write_text(run.stdout);(args.output/'stderr').write_text(run.stderr)
    actual=[row for row in parse_forms(run.stdout) if isinstance(row,list) and row[:1]==['ProgramCase']]
    indexed={row[1]:row for row in actual}
    differences=[{'case':i,'expected':want,'actual':indexed.get(str(i))}
                 for i,want in enumerate(expected) if want!=indexed.get(str(i))]
    inert=not any('MP1-EFFECT-MUST-NOT-RUN' in line or line.strip()=='"MP1-DATA-MUST-NOT-RUN"'
                  for line in run.stdout.splitlines())
    report={'passed':run.returncode==0 and not run.stderr and not differences and len(actual)==len(expected) and inert,
            'checks':len(expected),'observations':len(actual),'differences':differences,
            'costs':comparisons,'effectful_fitting_not_executed':inert,
            'scope':'independent complete MP1 bytes and typed finite-composition enumeration, fixed original examples, resumed common search and new-input prediction; no universal generalization claim'}
    (args.output/'receipt.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({**report,'differences':[{'case':row['case']} for row in differences]}))
    if not report['passed']:raise SystemExit(1)


if __name__=='__main__':main()
