#!/usr/bin/env python3
"""Compare bounded scalar regions with unfused and semantic-fuel execution."""
import argparse
import os
from pathlib import Path
import random
import re
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[2]

def run(binary,source,language,route):
    env=dict(os.environ)
    env.pop('CETTA_PREPARED_PURE_DEBUG',None)
    env['CETTA_PREPARED_PURE_SCALAR_TREE_REFERENCE']='1' if route=='unfused' else '0'
    env['CETTA_PREPARED_PURE_CODE_REFERENCE']='1' if route=='nodes' else '0'
    if route=='collecting': env['CETTA_GC_BUDGET_MB']='1'
    command=[str(binary),'--lang',language]
    if language=='he': command+=['--profile','extended']
    if route=='canonical': command+=['--fuel','10000000']
    completed=subprocess.run(command+[str(source)],cwd=ROOT,env=env,
        text=True,capture_output=True,timeout=120)
    return completed.returncode,completed.stdout,completed.stderr

def programs():
    rng=random.Random(1107)
    constants=('0.0','-0.0','0.5','1.25','-3.0','9007199254740992.0','1','-2')
    def expression(depth):
        if depth==0 or rng.randrange(4)==0: return rng.choice(('$x',*constants))
        return '('+rng.choice(('+','-','*'))+' '+expression(depth-1)+' '+expression(depth-1)+')'
    forms=[]
    count=0
    for i in range(64):
        forms.append(f'(= (scalar-region-{i} $x) {expression(3)})')
        for value in ('0.0','-0.0','1.5','-7','9007199254740993'):
            forms.append(f'!(scalar-region-{i} {value})'); count+=1
    forms += ['(= (scalar-reassociate $x $y $z) (+ (+ $x $y) $z))',
              '!(scalar-reassociate 1.0e16 -1.0e16 1.0)',
              '(= (scalar-right-association $x $y $z) (+ $x (+ $y $z)))',
              '!(scalar-right-association 1.0e16 -1.0e16 1.0)',
              '(= (scalar-exact-equality $x) (== (+ $x 1) 9007199254740992.0))',
              '!(scalar-exact-equality 9007199254740992)',
              '(= (scalar-neg-zero $x) (* (- $x 0.0) 1.0))',
              '!(scalar-neg-zero -0.0)']
    yield 'finite', '\n'.join(forms),count+4
    yield 'bigint', '''(= (scalar-overflow $x) (+ (* $x 2) 1))
!(scalar-overflow 9223372036854775807)
!(scalar-overflow -9223372036854775808)
(= (scalar-overflow-region $x) (+ (* $x 2) 0.0))
!(scalar-overflow-region 9223372036854775807)
!(scalar-overflow-region -9223372036854775808)
''',4
    yield 'rational-refusal', '''!(pragma! prefer-rationals True)
(= (scalar-rational-region $x) (+ (* $x 0.5) 1.0))
!(scalar-rational-region (/ 1 3))
!(scalar-rational-region (/ -7 11))
''',3
    bits=['!(import! &self tensor)', '!(import! &self str)']
    for i in range(12):
        # Include cancellation, non-dyadic rounding and subnormal operands.
        body=expression(3)
        bits.append(f'(= (scalar-bits-{i} $x) (+ (* {body} 0.1) 1.0e-307))')
        for value in ('0.1', '-0.0', '1.0e-308', '-7.1', '9007199254740992.0'):
            bits.append(f'!(let $value (scalar-bits-{i} {value}) '
                '(str:sha256 (tensor:encode (tensor:from-values f64 (1) ($value)))))')
    bits += ['(= (scalar-bits-zero $x) (* (- $x 0.0) 1.0))',
             '!(let $value (scalar-bits-zero 0.0) (str:sha256 (tensor:encode (tensor:from-values f64 (1) ($value)))))',
             '!(let $value (scalar-bits-zero -0.0) (str:sha256 (tensor:encode (tensor:from-values f64 (1) ($value)))))']
    yield 'float-bits','\n'.join(bits),64
    yield 'boolean-observation', '''(= (scalar-bool $x) (== (< $x 2.0) True))
!(scalar-bool 1.0)
!(scalar-bool 3.0)
(= (scalar-nested-bool $x) (== (== (+ $x 1.0) 2.0) True))
!(scalar-nested-bool 1.0)
!(scalar-nested-bool 2.0)
''',4
    yield 'choice-effects', '''(= (scalar-choice $x) (+ (* $x 0.5) 1.0))
(= (scalar-choice $x) (- (* $x 0.25) 2.0))
!(collapse (scalar-choice 8.0))
(= (scalar-effect $x) (let $_ (println! (ScalarEffect $x)) (+ (* $x 0.5) 1.0)))
!(scalar-effect 2.0)
(= (scalar-call $x) $x)
(= (scalar-boundary $x) (+ (scalar-call $x) (* $x 0.5)))
!(scalar-boundary 2.0)
''',4
    yield 'collection', '''!(import! &self (library list))
(= (scalar-projection $x) (+ (* $x 0.5) 1.0))
!(list:sum-by scalar-projection (0.0 1.0 2.0 3.0 1.0))
!(map-atom (0.0 1.0 2.0) $x (+ (* $x 0.5) 1.0))
!(filter-atom (0.0 1.0 2.0 3.0) $x (> (+ (* $x 0.5) 1.0) 1.5))
''',4
    yield 'liveness', '''(= (scalar-build $n) (if (== $n 0) End (Box $n (scalar-build (- $n 1)))))
(= (scalar-after-call $n $value)
   (let $large (scalar-build $n) (+ (* $value 0.5) 1.0)))
!(scalar-after-call 40 (/ 1.0 7.0))
''',1
    yield 'typed', '''(: scalar-typed (-> Number Number))
(= (scalar-typed $x) (+ (* $x 0.5) 1.0))
!(scalar-typed 2.0)
''',1
    yield 'bad-type', '''(= (scalar-invalid $x) (+ (* $x 0.5) 1.0))
!(scalar-invalid data)
''',None
    yield 'exceptional', '''(= (scalar-exception $x) (+ (* $x 0.0) 1.0))
!(scalar-exception (/ 1.0 0.0))
!(scalar-exception (/ 0.0 0.0))
''',None

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cetta',type=Path,required=True)
    parser.add_argument('--native',type=Path)
    args=parser.parse_args()
    checks=0
    if args.native:
        results=[]
        for reference in ('0','1'):
            env=dict(os.environ,CETTA_PREPARED_PURE_SCALAR_TREE_REFERENCE=reference)
            run_native=subprocess.run([str(args.native.resolve())],env=env,text=True,
                capture_output=True,check=True,timeout=30)
            match=re.fullmatch(r'PreparedListEquality observations=(\d+) nested-cells, shared-records, mismatches, opaque-quotes and improper-tails\n'
                               r'PreparedScalarTree mode=(fused|reference) iterations=1000 allocated=(\d+)\n',run_native.stdout)
            assert match and not run_native.stderr,run_native
            assert int(match[1])==20,run_native
            results.append(int(match[3])); checks+=1+int(match[1])
        assert 0<results[0]<results[1],results
        print(f'PreparedScalarAllocation fused={results[0]} reference={results[1]}')
        checks+=1
    with tempfile.TemporaryDirectory(prefix='prepared-scalar-tree-',dir=ROOT/'runtime') as work:
        source=Path(work)/'controls.metta'
        for name,text,outputs in programs():
            for language in ('petta','he','prime'):
                if name=='collection' and not language.startswith('petta'):
                    text_for_language=text.replace('(library list)','list')
                else: text_for_language=text
                source.write_text(text_for_language)
                expected=run(args.cetta.resolve(),source,language,'canonical')
                if outputs is not None:
                    assert expected[0]==0 and len(expected[1].splitlines())==outputs and not expected[2],(name,language,expected)
                elif name=='bad-type':
                    assert expected[0]!=0 or 'data' in expected[1] or 'Error' in expected[1],(name,language,expected)
                else:
                    assert expected[0]!=0 or (len(expected[1].splitlines())==2 and
                        any(word in expected[1].lower() for word in ('nan','inf','error'))),(name,language,expected)
                checks+=1
                for route in ('fused','unfused','nodes','collecting'):
                    actual=run(args.cetta.resolve(),source,language,route)
                    assert actual==expected,(name,language,route,actual,expected)
                    checks+=1
                if name=='finite':
                    lines=expected[1].splitlines()
                    assert expected[0]==0 and len(lines)==outputs,(language,expected)
                    # This distinguishes the authored left-associated schedule.
                    assert lines[-4] != lines[-3],(language,lines[-4:])
                    checks+=1
                if name=='float-bits':
                    lines=expected[1].splitlines()
                    assert lines[-2] != lines[-1],(language,lines[-2:])
                    assert all(re.search(r'[0-9a-f]{64}',line) for line in lines[2:]),(language,lines)
                    checks+=1
    print(f'PreparedScalarRegions {checks} 0')

if __name__=='__main__': main()
