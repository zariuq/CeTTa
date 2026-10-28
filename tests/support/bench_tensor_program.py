#!/usr/bin/env python3
"""Interleaved whole-process typed-DAG timing with exact output controls."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import statistics
import subprocess
import time

from check_binary_codec import ROOT, parse_forms, tensor as byte_tensor
from check_tensor_program import array, canonical, encoded, FAMILY, records, source_array, Text


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cetta",type=Path,required=True)
    parser.add_argument("--output",type=Path,required=True)
    parser.add_argument("--size",type=int,default=2048)
    parser.add_argument("--pairs",type=int,default=5)
    args=parser.parse_args()
    if args.size<1 or args.pairs<1: parser.error("size and pairs must be positive")
    args.output.mkdir(parents=True,exist_ok=True)
    rows=[('input',0,'F64Tensor'),('op',2,[],[0,0])]
    roots, projected=canonical([1],records(rows))
    blob=encoded(["TP:Program",Text("TP1"),FAMILY,["Dag:Forest",roots,projected]])
    values=[float(i%17-8) for i in range(args.size)]
    source=f'''!(import! &self (library lib_tensor_program))
!(case (tensor-program:run {byte_tensor(blob)} ({source_array(array((args.size,),values))}))
   (((TP:Outputs ((TP:Value "F64Tensor" $value)))
     (println! (ProgramBench (== $value {source_array(array((args.size,),[x*x for x in values]))}))))
    ($other (println! (ProgramBenchFault $other)))))
'''
    path=args.output/'run.metta';path.write_text(source)
    binary=args.cetta.resolve(); samples=[]
    # The same source, validation, imports and construction are charged on
    # both routes. Only the existing scalar-map reference switch differs.
    for pair in range(args.pairs):
        for reference in (("0","1") if pair%2==0 else ("1","0")):
            env=dict(os.environ,CETTA_TENSOR_MAP_REFERENCE=reference)
            start=time.perf_counter()
            result=subprocess.run([str(binary),'--lang','petta',str(path.resolve())],cwd=ROOT,
                                  env=env,text=True,capture_output=True,timeout=120)
            elapsed=time.perf_counter()-start
            observations=[form for form in parse_forms(result.stdout)
                          if isinstance(form,list) and form and form[0] in ('ProgramBench','ProgramBenchFault')]
            if result.returncode or observations != [['ProgramBench','true']]:
                raise AssertionError((result.returncode,observations,result.stderr[-1000:]))
            samples.append({'pair':pair,'route':'authored' if reference=='1' else 'packed','seconds':elapsed})
    medians={route:statistics.median(row['seconds'] for row in samples if row['route']==route)
             for route in ('packed','authored')}
    result={'size':args.size,'samples':samples,'median_seconds':medians,
            'authored_over_packed':medians['authored']/medians['packed'],
            'binary_sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),
            'scope':'whole process including imports, input/expected construction, program decode/typecheck, execution and exact output comparison; not kernel-only or WILLIAM comparison'}
    (args.output/'receipt.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result))


if __name__=='__main__':main()
