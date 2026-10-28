#!/usr/bin/env python3
"""Exact binary64 observations and integer division through the real worker."""
import argparse
import hashlib
import json
from pathlib import Path
import random
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from value_worker_client import Worker
from space_service_client import E,S,BigInt,FloatBits,encode


def scalar(value):
    return value if -(1<<63)<=value<(1<<63) else BigInt(str(value))


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('worker',type=Path)
    parser.add_argument('--cli',type=Path)
    parser.add_argument('--output',type=Path)
    args=parser.parse_args()
    worker=Worker(args.worker.resolve(),ROOT/'tests/support/binary64_parts_fixture.metta',
                  'binary64-fixture:call',language='petta')
    rng=random.Random(2710532);checks=0
    try:
        bits=[0,1,(1<<63),(1<<63)+1,0x000fffffffffffff,0x0010000000000000,
              0x3ff0000000000000,0x7fefffffffffffff,0x7ff0000000000000,
              0xfff0000000000000,0x7ff0000000000001,0xfff8000000000042]
        bits += [rng.getrandbits(64) for _ in range(4096)]
        for word in bits:
            sign=-1 if word>>63 else 1
            exponent=(word>>52)&2047;fraction=word&((1<<52)-1)
            if exponent==2047:
                expected=E('Binary64InfinityV1',sign) if fraction==0 else E('Binary64NaNV1',sign,fraction)
            else:
                expected=E('Binary64PartsV1',sign,fraction if exponent==0 else fraction+(1<<52),
                           -1074 if exponent==0 else exponent-1075)
            actual=worker.call(E('Parts',FloatBits(word)))
            if encode(actual)!=encode(expected):raise AssertionError(('parts',hex(word),actual,expected))
            checks+=1
        integers=[-(1<<63),-9,-1,0,1,9,(1<<63)-1,1<<63,-(1<<128),1<<256]
        for _ in range(500):
            integers.append(rng.randrange(-(1<<rng.randrange(1,1200)),1<<rng.randrange(1,1200)))
        for i,left in enumerate(integers):
            right=integers[(i*17+3)%len(integers)]
            if right==0:right=-7
            q,r=divmod(left,right)
            expected=E('IntegerDivmodV1',scalar(q),scalar(r))
            actual=worker.call(E('Divide',scalar(left),scalar(right)))
            if encode(actual)!=encode(expected):raise AssertionError(('integer-divmod',left,right,actual,expected))
            checks+=1
        for left,right,reason in [(1,0,'division-by-zero'),(FloatBits(0x3ff0000000000000),2,'expected-integers')]:
            if worker.call(E('Divide',left,right))!=E('IntegerDivmodRefusedV1',S(reason)):raise AssertionError(reason)
            checks+=1
        if worker.call(E('Parts',1))!=E('Binary64RefusedV1',S('expected-float')):raise AssertionError('integer coerced')
        checks+=1
        result=worker.call(E('Parallel',FloatBits(0x3ff0000000000000)))
        if sorted(encode(x) for x in result)!=sorted((encode(E('Binary64PartsV1',1,1<<52,-52)),
                                                     encode(E('IntegerDivmodV1',-3,3)))):
            raise AssertionError(('parallel numerical calls',result))
        checks+=1
    finally:worker.close()
    dialect_checks=0
    if args.cli:
        wanted=['(Binary64PartsV1 1 6755399441055744 -52)',
                '(IntegerDivmodV1 -3 3)',
                '(IntegerDivmodV1 -6148914691236517206 -1)']
        for dialect in ('petta','he','prime'):
            imported='(library math)' if dialect=='petta' else 'math'
            program='!(import! &self '+imported+')\n!(math:binary64-parts 1.5)\n'+\
                    '!(math:integer-divmod -9 4)\n!(math:integer-divmod 18446744073709551617 -3)\n'
            result=subprocess.run([str(args.cli.resolve()),'--lang',dialect,'-e',program],
                                  cwd=ROOT,capture_output=True,text=True,timeout=30,check=True)
            actual=[line.strip('[]') for line in result.stdout.splitlines()[1:]]
            if actual!=wanted:raise AssertionError(('dialect math boundary',dialect,actual,wanted,result.stderr))
            # The reserved native spelling alone does not activate a library.
            result=subprocess.run([str(args.cli.resolve()),'--lang',dialect,'-e',
                                   '!(__cetta_lib_math_binary64_parts 1.5)'],
                                  cwd=ROOT,capture_output=True,text=True,timeout=30,check=True)
            if result.stdout.strip().strip('[]')!='(__cetta_lib_math_binary64_parts 1.5)':
                raise AssertionError(('math activated without import',dialect,result.stdout))
            dialect_checks+=4
    result=dict(checks=checks,worker_sha256=hashlib.sha256(args.worker.read_bytes()).hexdigest(),
                scope='actual PeTTa/native numerical boundary; no database runtime')
    if args.cli:
        result.update(dialect_checks=dialect_checks,
                      cli_sha256=hashlib.sha256(args.cli.read_bytes()).hexdigest())
    if args.output:args.output.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))


if __name__=='__main__':main()
