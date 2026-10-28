#!/usr/bin/env python3
"""Constructor labels/arity are ranked identity, not callable arguments."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess

from check_binary_codec import ROOT, parse_forms
from check_binary_tree import wire
from check_ic_search import check_controls


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cetta', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    check_controls((ROOT/'lib/petta/lib_ic_tree_archive.metta').read_text())
    code = ['!(import! &self (library lib_ic_tree_archive))', '''
(= (ConstructorLabel $x) (println! "CONSTRUCTOR-LABEL-MUST-NOT-RUN"))
(= (constructor-test:roundtrip $trees)
   (case (ic:prepare-trees Constructors $trees)
     (((IC:ConstructorArchive $archive)
       (== (ic:restore-trees Constructors $archive) (IC:Some $trees))) ($failure $failure))))
(= (constructor-test:seed $trees)
   (case (ic:prepare-trees Constructors $trees)
     (((IC:ConstructorArchive $archive)
       (case (ic-archive:prepare $archive)
         (((IC:PreparedArchive $tree (IC:Catalog $v $o $signature $heads $literals)
              ((IC:Packet $slots $first) (IC:Packet $same $second)))
           (case (ic-template:generalize $signature $slots $first $second)
             (((IC:Generalized $patterns $left $right) $patterns) ($failure $failure))))
          ($bad-archive $bad-archive)))) ($bad-task $bad-task))))
''']
    expected = []

    def add(call, wanted):
        index=len(expected)
        code.append(f'!(println! (ConstructorCase {index} {call}))')
        expected.append(['ConstructorCase', str(index), wanted])

    leaf = lambda value: ('leaf', value)
    node = lambda tag, children: ('node', tag, children)
    fixtures = [[], [leaf(b'x')], [node(b'f',[])], [leaf(b'f'),node(b'f',[])],
                [node(b'ConstructorLabel',[leaf(b'x')]),node(b'g',[leaf(b'x')])],
                [node('λ●\0□'.encode(),[leaf(b'\0'),leaf(b'\0')])],
                [node(b'f',[leaf(b'x')]),node(b'f',[leaf(b'x'),leaf(b'x')])]]
    for trees in fixtures:
        add('(constructor-test:roundtrip ('+' '.join(map(wire,trees))+'))','true')
    hole=[['IC:Hole','Root','0']]
    for trees in (fixtures[3],fixtures[4],fixtures[6]):
        add('(constructor-test:seed ('+' '.join(map(wire,trees))+'))',hole)
    add('(constructor-test:seed ('+wire(node(b'f',[leaf(b'x')]))+' '+wire(node(b'f',[leaf(b'y')]))+'))',
        [['IC:Node','1',[['IC:Hole','Value','0']]]])
    add('(ic:prepare-trees Constructors ('+wire(node(b'\xff',[]))+'))',['IC:TreeFault','InvalidArchive'])
    source='\n'.join(code)+'\n'
    check_controls(source)
    path=args.output/'constructors.metta';path.write_text(source)
    run=subprocess.run([str(args.cetta.resolve()),'--lang','petta',str(path.resolve())],cwd=ROOT,
                       text=True,capture_output=True,timeout=45)
    (args.output/'constructors.log').write_text(run.stdout+run.stderr)
    actual=[f for f in parse_forms(run.stdout) if isinstance(f,list) and f and f[0]=='ConstructorCase']
    found={f[1]:f for f in actual}
    wrong=[{'case':i,'expected':value,'actual':found.get(str(i))} for i,value in enumerate(expected)
           if found.get(str(i))!=value]
    receipt={'passed':run.returncode==0 and len(actual)==len(expected) and not wrong
             and 'CONSTRUCTOR-LABEL-MUST-NOT-RUN' not in run.stdout,
             'checks':len(expected),'answers':len(actual),'mismatches':wrong,
             'scope':'constructor-aware IC1 tree view, tags/arity, opaque labels and generalization shape'}
    (args.output/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print(json.dumps({**receipt,'mismatches':[row['case'] for row in wrong]}))
    for row in wrong: print(json.dumps(row))
    return 0 if receipt['passed'] else 1


if __name__=='__main__': raise SystemExit(main())
