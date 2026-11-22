#!/usr/bin/env python3
"""Compare prepared rule execution with the standalone ordinary C matcher."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import random
import subprocess

ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--binary',type=Path,required=True)
p.add_argument('--reference',type=Path,required=True)
p.add_argument('--out',type=Path,required=True)
p.add_argument('--cases',type=int,default=200)
a=p.parse_args();binary=a.binary.resolve();reference=a.reference.resolve()
a.out.mkdir(parents=True,exist_ok=False);results=[]
identities={name:hashlib.sha256(path.read_bytes()).hexdigest() for name,path in [('binary',binary),('reference',reference)]}
for seed in range(a.cases):
    rng=random.Random(seed);patterns=['(p $x $y)']
    for k in range(rng.randrange(3)):
        patterns.append(patterns[-1] if rng.randrange(5)==0 else
            '('+' '.join([rng.choice(['p','q','$x','$y']),rng.choice(['a','b','$x','$y']),rng.choice(['a','b','$x','$y'])])+')')
    takes=rng.randrange(1,len(patterns)+1)
    source='(step one) (budget 40) (store '+rng.choice(['set','multiset'])+') (matches '+rng.choice(['keys','symmetric'])+')\n'
    source+='(p a a) (p a a) (p a b) (p b a) (p b b) (q a a) (q b b)\n'
    if seed%13==0:source+='(p $shared a) (q b $shared)\n'
    clauses=['(take '+' '.join(patterns[:takes])+')']
    if takes<len(patterns):clauses.append('(read '+' '.join(patterns[takes:])+')')
    if seed%11==0:clauses.append('(tell (= $x a)) (check (= $y b))')
    if seed%17==0:clauses.append('(absent (blocked $x))')
    clauses.append('(make (out $x $y)'+(' (echo $y)' if seed%3==0 else '')+')')
    source+='(rule test '+' '.join(clauses)+')\n'
    path=a.out/f'case-{seed:04d}.tcalc';path.write_text(source)
    expected=subprocess.run([str(reference),str(path.resolve())],cwd=ROOT,capture_output=True,timeout=60,check=True)
    for mode in ['c','x86','hybrid']:
        env=dict(os.environ)
        if seed%2:env.update(CETTA_GC='1',CETTA_GC_BUDGET_MB='1')
        got=subprocess.run([str(binary),'--kernel',mode,'--lang','tcalc',str(path.resolve())],cwd=ROOT,env=env,capture_output=True,timeout=60)
        ok=got.returncode==expected.returncode and got.stdout==expected.stdout and got.stderr==expected.stderr
        results.append({'seed':seed,'mode':mode,'passed':ok,'gc_requested':bool(seed%2)})
        if not ok:
            (a.out/f'case-{seed:04d}-{mode}.actual').write_bytes(got.stdout+got.stderr)
            (a.out/f'case-{seed:04d}.expected').write_bytes(expected.stdout+expected.stderr)
            print('FAIL',seed,mode,flush=True)
    if seed%25==24:print(seed+1,'programs checked',flush=True)
for name,path in [('binary',binary),('reference',reference)]:
    assert hashlib.sha256(path.read_bytes()).hexdigest()==identities[name],name+' changed during comparison'
passed=all(row['passed'] for row in results)
(a.out/'result.json').write_text(json.dumps({'passed':passed,'identities':identities,'results':results},indent=2)+'\n')
print(f'{len(results)} comparisons against ordinary C; '+('PASS' if passed else 'FAIL'))
raise SystemExit(0 if passed else 1)
