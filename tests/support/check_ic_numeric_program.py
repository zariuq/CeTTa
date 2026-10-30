#!/usr/bin/env python3
"""Independent finite DAG discovery, full NC1 costs and held-out prediction."""

from __future__ import annotations

import argparse
from copy import deepcopy
import itertools
import json
from pathlib import Path
import subprocess

from check_binary_codec import ROOT, observation, parse_forms, tensor as byte_tensor
from check_binary_value import Text, construct, encoded
from check_ic_numeric import archive, ctn, vector
from check_ic_search import check_controls
from check_tensor_program import canonical, records
from check_tensor_sequences import normalized


def family(recipes):
    return ["TP:Family",Text("TF1"),[["TP:Operator",Text(label),[Text("F64Tensor")]*arity,
                                    Text("F64Tensor"),recipe] for label,recipe,arity in recipes]]


def programs(recipes, maximum):
    # Recursive expansion independently supplies the explicit-frontier
    # implementation's finite chronological region. No dead-node pruning.
    def descend(rows):
        for root in range(len(rows)):
            yield root, deepcopy(rows)
        if len(rows)-1 == maximum:
            return
        for index,(_name,recipe,arity) in enumerate(recipes):
            params=[0] if recipe=="TP:Scan" else []
            for children in itertools.product(range(len(rows)),repeat=arity):
                yield from descend(rows+[("op",index,params,list(children))])
    yield from descend([("input",0,"F64Tensor")])


def artifact(original_inputs, mask, tolerance, recipes, root, rows):
    selected, projected=canonical([root],records(rows))
    blob=encoded(["TP:Program",Text("TP1"),family(recipes),["Dag:Forest",selected,projected]])
    return encoded(["Numeric:Archive",Text("NC1"),ctn(original_inputs),ctn(list(map(int,mask)),"u8"),
                    ctn([tolerance]),["Numeric:Program",blob]])


def predict(recipes, root, rows, inputs):
    def visit(index):
        row=rows[index]
        if row[0]=="input": return inputs
        _kind, operation, _params, children=row
        values=[visit(child) for child in children]
        recipe=recipes[operation][1]
        if recipe=="TP:Scan":
            result=[]; total=None
            for value in values[0]:
                total=value if total is None else total+value
                result.append(total)
            return result
        if recipe=="TP:Add": return [x+y for x,y in zip(*values)]
        if recipe=="TP:Multiply": return [x*y for x,y in zip(*values)]
        raise AssertionError(recipe)
    return visit(root)


def reference(inputs,outputs,mask,tolerance,recipes,maximum):
    best=archive(inputs,outputs,mask,tolerance); accepted=0; seen=0
    for root,rows in programs(recipes,maximum):
        candidate=artifact(inputs,mask,tolerance,recipes,root,rows)
        actual=predict(recipes,root,rows,inputs); seen+=1
        if len(candidate)<len(best) and all(not keep or abs(a-b)<=tolerance
                                          for keep,a,b in zip(mask,actual,outputs)):
            best=candidate; accepted+=1
    return best,seen,accepted


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cetta",type=Path,required=True)
    parser.add_argument("--output",type=Path,required=True)
    args=parser.parse_args();args.output.mkdir(parents=True,exist_ok=True)
    for name in ("lib_tensor_program_search.metta","lib_ic_numeric_program.metta"):
        check_controls((ROOT/"lib/petta"/name).read_text())
    code=['!(import! &self (library lib_ic_numeric_program))', '''
(= (program-fit:view $result)
   (case $result
     (((IC:SearchResult $best (IC:Statistics $turns $seen $accepted $faults) $reason $fault $state)
       (ProgramFit (tensor:inspect $best) $seen $accepted $faults $reason)) ($other $other))))
(= (program-fit:pieces $state $remaining)
   (case (ic:resume $state 1)
     (((IC:SearchResult $best $stats BudgetExhausted $fault $next)
       (if (> $remaining 0) (program-fit:pieces $next (- $remaining 1))
         (let $closed (ic:release $next) (ProgramFitFault Budget)))) ($other $other))))
(= (program-fit:run $original $region $pieces)
   (case (ic:prepare-fit Numeric:Programs $original $region ())
     (((IC:PreparedSearch $state)
       (if $pieces (program-fit:pieces $state 2000) (ic:resume $state 2000))) ($other $other))))
(= (program-fit:record-view $record)
   (case $record
     (((Binary:Leaf $bytes)
       (case (binary-value:unpack $bytes)
         (((BinaryValue:Value (TP:Input $sort $index)) (Input $sort $index)) ($other $other))))
      ((Binary:Node $bytes $children)
       (case (binary-value:unpack $bytes)
         (((BinaryValue:Value (TP:Apply $index $parameters)) (Apply $index $parameters $children)) ($other $other)))))))
(= (program-fit:trace $proposal)
   (case $proposal
     (((IC:Proposal (TP:Blueprint $family (Dag:Forest $roots $records)))
       (Trace $roots (map-atom $records $record (program-fit:record-view $record)))))))
''']
    expected=[]; fits=[]
    def add(call,wanted):
        i=len(expected);code.append(f"!(println! (ProgramFitCase {i} {call}))");expected.append(["ProgramFitCase",str(i),wanted])
    n=256; inputs=[1.0]*n; mask=[True]*n
    varied=[float(i%7-3) for i in range(n)]
    cases=[(inputs,[float(i+1) for i in range(n)],[("prefix","TP:Scan",1)],1),
           (inputs,[float((i+1)*(i+2)//2) for i in range(n)],[("prefix","TP:Scan",1)],2),
           (varied,[x*x for x in varied],[("twice","TP:Add",2),("square","TP:Multiply",2)],1)]
    for training,outputs,recipes,maximum in cases:
        original=archive(training,outputs,mask,0.0)
        best,seen,accepted=reference(training,outputs,mask,0.0,recipes,maximum)
        domains=[["TP:Domain",i,[[0]] if row[1]=="TP:Scan" else [[]]] for i,row in enumerate(recipes)]
        region=f'(tensor-program:prepare-region {construct(family(recipes))} ("F64Tensor") () {construct(domains)} {maximum})'
        want=["ProgramFit",observation(best),str(seen),str(accepted),"0","RegionExhausted"]
        for pieces in ("False","True"):
            add(f"(program-fit:view (program-fit:run {byte_tensor(original)} {region} {pieces}))",want)
        fits.append({'recipes':[row[1] for row in recipes],'bound':maximum,'seen':seen,'accepted':accepted,
                     'original_bytes':len(original),'best_bytes':len(best)})
        if recipes[0][1]=="TP:Scan":
            # A different input length exercises shape-polymorphic reuse,
            # not a replay of the training vector.
            new=[2.0,3.0,4.0]
            predicted=[2.0,5.0,9.0] if maximum==1 else [2.0,7.0,16.0]
            add(f'''(case (ic:predict Numeric:Programs {byte_tensor(best)} {vector(new)})
               (((IC:Prediction $value) (== $value {vector(predicted)})) ($other $other)))''',"true")
        else:
            add(f'''(case (ic:predict Numeric:Programs {byte_tensor(best)} {vector([-2.0,3.0,0.0])})
               (((IC:Prediction $value) (== $value {vector([4.0,9.0,0.0])})) ($other $other)))''',"true")
    # Enumerate a genuinely branching two-operation depth-two region. Its
    # 53 proposals include compatible repeated references and unused nodes.
    recipes=[("twice","TP:Add",2),("square","TP:Multiply",2)]
    reg=f'(tensor-program:prepare-region {construct(family(recipes))} ("F64Tensor") () ((TP:Domain 0 (())) (TP:Domain 1 (()))) 2)'
    count=sum(1 for _ in programs(recipes,2));assert count==53
    add(f"(size-atom (collapse (tensor-program:proposals {reg})))",str(count))
    tape=[]
    for root,rows in programs(recipes,2):
        trace=[]
        for row in rows:
            trace.append(["Input",json.dumps(row[2]),str(row[1])] if row[0]=="input" else
                         ["Apply",str(row[1]),list(map(str,row[2])),list(map(str,row[3]))])
        tape.append(["Trace",[str(root)],trace])
    add(f'''(let* (($handle (work-new (tensor-program:proposals {reg})))
                   ($route (work-route $handle fifo)) ($quantum (work-budget $handle 1000000))
                   ($batch (work-take $handle 100)) ($closed (work-cancel $handle)))
       (case $batch
         (((taken $items complete) (map-atom $items $item (program-fit:trace $item))) ($other $other))))''',tape)
    # Invalid regions cannot grant producer or completion authority just by
    # using a prepared-value constructor.
    add('(tensor-program:region? (TP:Region (TP:Family "TF1" ()) () () () -1))',"false")
    add('(ic:prepare-fit Numeric:Programs NotAnArchive (TP:Region (TP:Family "TF1" ()) () () () -1) ())',
        ["IC:FitRefused","Region"])
    add('''(let $seed (tensor-program:input "F64Tensor" 0)
       (tensor-program:region? (TP:Region (TP:Family "TF1" ()) ($seed) ("BoolTensor") () 0)))''',"false")
    add('(tensor-program:prepare-region (TP:Family "TF1" ()) ("F64Tensor") () ((TP:Domain 99 ())) 1)',
        ["TP:Fault","Region"])
    add(f'''(let $region (tensor-program:prepare-region {construct(family([('prefix','TP:Scan',1)]))}
          ("F64Tensor") () ((TP:Domain 0 ((0) (0)))) 1)
          (size-atom (collapse (tensor-program:proposals $region))))''',"5")
    add('''(let $region (tensor-program:prepare-region (TP:Family "TF1" ()) ("F64Tensor")
          ((TP:ConstantSeed "F64Tensor" (tensor:from-values f64 () (2.0)))
           (TP:ConstantSeed "I64Tensor" (tensor:from-values i64 () (7)))) () 0)
          (ConstantSeeds (tensor-program:region? $region)
            (size-atom (collapse (tensor-program:proposals $region)))))''', ["ConstantSeeds","true","2"])
    original=archive(inputs,cases[0][1],mask,0.0)
    scan_family=family(cases[0][2]);region=f'(tensor-program:prepare-region {construct(scan_family)} ("F64Tensor") () ((TP:Domain 0 ((0)))) 1)'
    add(f'''(case (ic:prepare-fit Numeric:Programs {byte_tensor(original)} {region} ())
       (((IC:PreparedSearch $state)
         (let* (($changed (add-atom &self ProgramFitDependencyChanged))
                ($result (ic:resume $state 0)) ($closed (ic:release $state))) $result)) ($other $other)))''',
        ["IC:ResumeRejected","ChangedDependencies"])
    # A perfectly typed but observationally wrong model cannot pass the
    # shared original-task checker, even if its byte representation is small.
    wrong=artifact(inputs,mask,0.0,cases[0][2],0,[("input",0,"F64Tensor")])
    add(f'(ic-packed:check IC:NumericArchive {byte_tensor(original)} {byte_tensor(original)} {byte_tensor(wrong)})',
        ["IC:PackedRejected","Reconstruction"])
    source='\n'.join(code)+'\n';check_controls(source)
    path=args.output/'search.metta';path.write_text(source)
    result=subprocess.run([str(args.cetta.resolve()),'--lang','petta',str(path.resolve())],cwd=ROOT,
                          text=True,capture_output=True,timeout=120)
    (args.output/'search.log').write_text(result.stdout+result.stderr)
    actual=[form for form in parse_forms(result.stdout) if isinstance(form,list) and form and form[0]=='ProgramFitCase']
    found={form[1]:form for form in actual}
    wrong=[{'case':i,'expected':want,'actual':found.get(str(i))} for i,want in enumerate(expected)
           if normalized(found.get(str(i)))!=normalized(want)]
    receipt={'passed':result.returncode==0 and len(actual)==len(expected) and not wrong,'checks':len(expected),
             'answers':len(actual),'mismatches':wrong,'fits':fits,'branching_region_proposals':count,
             'scope':'bounded typed forward DAG discovery and NC1 original-task qualification; not inverse completeness'}
    (args.output/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print(json.dumps({**receipt,'mismatches':[row['case'] for row in wrong]}))
    for row in wrong[:3]: print(json.dumps(row)[:1800])
    if result.returncode: print(result.stderr[-1500:])
    return 0 if receipt['passed'] else 1


if __name__=='__main__':raise SystemExit(main())
