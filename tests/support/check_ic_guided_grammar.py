#!/usr/bin/env python3
"""Guided ranked generation versus independent late-filtered full grammars."""

from __future__ import annotations

import argparse
from collections import Counter
import json
from pathlib import Path
import subprocess

from check_ic_archive import ROOT, syntax
from check_ic_grammar import enumerate_templates
from check_ic_search import check_controls
from check_ic_templates import node, parse_forms, reference


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cetta',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();args.output.mkdir(parents=True,exist_ok=True)
    check_controls((ROOT/'lib/petta/lib_ic_guided_grammar.metta').read_text())
    value=['IC:Slot','Value','T'];root=['IC:Slot','Root','T'];code_slot=['IC:Slot','Code','T']
    small=[['IC:Operator','T',[]],['IC:Operator','T',[value]],['IC:Operator','T',[value,value]]]
    cases=[([],[],[[]],0), (small,[root],[[node(0)]],0),
           (small,[root],[[node(1,node(0))]],3),
           (small,[root],[[node(1,node(0))],[node(1,node(0))]],3),
           (small,[root],[[node(2,node(0),node(0))],[node(2,node(0),node(1,node(0)))]],2),
           (small,[root],[[node(2,node(1,node(0)),node(0))],[node(2,node(0),node(1,node(0)))]],2),
           (small,[value]*3,[[node(0),node(0),node(0)],[node(0),node(1,node(0)),node(0)]],1),
           (small,[value,code_slot,value],[[node(0),node(0),node(0)]],1),
           (small,[root],[[node(0)],[node(1,node(0))]],3),
           (small,[root],[[node(2,node(0),node(0))]]*3,2)]
    source=['!(import! &self (library lib_ic_guided_grammar))'];expected=[];region_sizes=[]
    def add(expression,wanted,bag=False):
        i=len(expected);source.append(f'!(println! (GuidedCase {i} {expression}))');expected.append((wanted,bag))
    for signature,slots,guides,depth in cases:
        complete=enumerate_templates(signature,slots,depth)
        supported=[patterns for patterns in complete if all(reference(signature,slots,patterns,guide)!='IC:None'
                                                         for guide in guides)]
        region_sizes.append({'complete':len(complete),'supported':len(supported),'guides':len(guides)})
        add(f'(collapse (ic-grammar:guided {syntax(signature)} {syntax(slots)} {syntax(guides)} {depth}))',
            [['IC:Template',pattern] for pattern in supported],True)
    for signature,slots,guides,depth in [(small,[root],[],1),(small,[root],[[node(2,node(0))]],2),
                                        (small,[root],[[node(9)]],1),(small,[['IC:Slot','Bad','T']],[[node(0)]],1),
                                        (small,[root],[[node(0)]],-1)]:
        add(f'(ic-grammar:guided {syntax(signature)} {syntax(slots)} {syntax(guides)} {depth})',
            ['IC:GrammarFault','InvalidGuides'])
    add('(ic-grammar:guided () () 7 1)',['IC:GrammarFault','InvalidGuides'])
    add('(ic-guided:term () Value T 0 (IC:HoleState 9223372036854775807 ()) () ((IC:Node 0 ())))',
        ['IC:GrammarFault','Capacity'])
    fixture=args.output/'guided.metta';fixture.write_text('\n'.join(source)+'\n');check_controls(fixture.read_text())
    run=subprocess.run([str(args.cetta.resolve()),'--lang','petta',str(fixture.resolve())],cwd=ROOT,
                       capture_output=True,text=True,timeout=120)
    (args.output/'guided.log').write_text(run.stdout+run.stderr)
    answers=[row for row in parse_forms(run.stdout) if isinstance(row,list) and row and row[0]=='GuidedCase']
    errors=[]
    for i,(wanted,bag) in enumerate(expected):
        observed=[row[2] for row in answers if row[1]==str(i)]
        valid=observed==[wanted]
        if bag and len(observed)==1:
            valid=Counter(map(json.dumps,observed[0]))==Counter(map(json.dumps,wanted))
        if not valid:
            errors.append({'case':i,'expected':wanted,'actual':observed})
    result={'passed':run.returncode==0 and len(answers)==len(expected) and not errors,
            'checks':len(expected),'answers':len(answers),'errors':errors,'region_sizes':region_sizes,
            'scope':'one/multiple guide forests, independent generate-then-match region comparison; answer bags, not an assumed evaluator ordering'}
    (args.output/'receipt.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result))
    return 0 if result['passed'] else 1


if __name__=='__main__':raise SystemExit(main())
