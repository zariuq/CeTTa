#!/usr/bin/env python3
"""Paired original/learned costs for the actual finite-parameter selector."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import statistics
import subprocess
import time

from check_binary_codec import ROOT, parse_forms


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cetta', type=Path, required=True)
    parser.add_argument('--learned-source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--calls', type=int, default=128)
    parser.add_argument('--repeats', type=int, default=3)
    parser.add_argument('--pairs', type=int, default=5)
    args = parser.parse_args()
    if min(args.calls,args.repeats,args.pairs) < 1:
        parser.error('calls, repeats and pairs must be positive')
    args.output.mkdir(parents=True,exist_ok=True)
    original = ROOT/'lib/petta/lib_tensor_program_parameters.metta'
    learned = args.learned_source.resolve(strict=True)
    if parse_forms(learned.read_text())[0] != ['=', ['ic-learned:expr?', '$p0'],
                                               ['==', ['get-metatype', '$p0'], 'Expression']]:
        raise AssertionError('inspect the learned definition before measuring')
    paths = {}
    for name in ('original-default','original-reference','learned-reference','learned-default'):
        source = original if name.startswith('original-') else learned
        module = os.path.relpath(source,ROOT)
        program = f'''!(import! &self {json.dumps(module)})
!(import! &self (library system))
(= (reuse-bench:run $count $rows $expected)
   (if (== $count 0) 0
     (case (tp-parameters:select $rows (0 0 0))
       (((TP:Some $values)
         (if (== $values $expected)
           (+ 3 (reuse-bench:run (- $count 1) $rows $expected)) -1)) ($bad-selection -1)))))
(= (reuse-bench:repeat $count $rows $expected)
   (if (== $count 0) 0
     (+ (reuse-bench:run {args.calls} $rows $expected)
        (reuse-bench:repeat (- $count 1) $rows $expected))))
!(let* (($start (system:monotonic-ns))
         ($value (tensor:from-values f64 () (2.0)))
         ($constant (tensor-program:constant "F64Tensor" $value))
         ($third (cons-atom (TP:ParameterChoices "c" "F64Tensor" ($constant)) ()))
         ($second (cons-atom (TP:ParameterChoices "b" "F64Tensor" ($constant)) $third))
         ($rows (cons-atom (TP:ParameterChoices "a" "F64Tensor" ($constant)) $second))
         ($expected ($constant $constant $constant))
         ($ready (system:monotonic-ns))
         ($first (reuse-bench:run {args.calls} $rows $expected))
         ($first-end (system:monotonic-ns))
         ($again (reuse-bench:repeat {args.repeats} $rows $expected))
         ($end (system:monotonic-ns)))
   (println! (ReuseCost (- $ready $start) (- $first-end $ready) $first
               (- $end $first-end) $again)))
'''
        path=args.output/(name+'.metta')
        path.write_text(program)
        paths[name]=path
    samples=[]
    names=list(paths)
    for pair in range(args.pairs):
        for name in names if pair%2==0 else reversed(names):
            env=os.environ.copy()
            if name.endswith('-reference'): env['CETTA_PREPARED_PURE_REFERENCE']='1'
            else: env.pop('CETTA_PREPARED_PURE_REFERENCE',None)
            log=args.output/f'{pair}-{name}.log'
            errors=args.output/f'{pair}-{name}.stderr'
            started=time.perf_counter()
            with log.open('w') as stdout, errors.open('w') as stderr:
                process=subprocess.Popen([str(args.cetta.resolve()),'--lang','petta',str(paths[name].resolve())],
                                         cwd=ROOT,env=env,stdout=stdout,stderr=stderr)
                _pid,status,usage=os.wait4(process.pid,0)
                process.returncode=os.waitstatus_to_exitcode(status)
            wall=time.perf_counter()-started
            forms=[row for row in parse_forms(log.read_text()) if isinstance(row,list) and row and row[0]=='ReuseCost']
            if process.returncode or len(forms)!=1:
                raise RuntimeError((name,process.returncode,errors.read_text()[-1000:]))
            row=forms[0]
            assert int(row[3])==3*args.calls and int(row[5])==3*args.calls*args.repeats, row
            samples.append({'pair':pair,'variant':name,'whole_process_seconds':wall,
                            'input_preparation_ns':int(row[1]),'first_execution_ns':int(row[2]),
                            'repeated_execution_ns':int(row[4])/args.repeats,
                            'child_peak_rss_kib':usage.ru_maxrss,'child_user_seconds':usage.ru_utime,
                            'child_system_seconds':usage.ru_stime})
    metrics=('whole_process_seconds','input_preparation_ns','first_execution_ns',
             'repeated_execution_ns','child_peak_rss_kib')
    medians={name:{metric:statistics.median(row[metric] for row in samples if row['variant']==name)
                   for metric in metrics} for name in names}
    ratios={'learned_over_original_default':[],'learned_over_original_reference':[],
            'learned_default_over_reference':[]}
    for pair in range(args.pairs):
        values={row['variant']:row['repeated_execution_ns'] for row in samples if row['pair']==pair}
        ratios['learned_over_original_default'].append(values['learned-default']/values['original-default'])
        ratios['learned_over_original_reference'].append(values['learned-reference']/values['original-reference'])
        ratios['learned_default_over_reference'].append(values['learned-default']/values['learned-reference'])
    receipt={'passed':True,'calls':args.calls,'repeats':args.repeats,'pairs':args.pairs,
             'samples':samples,'medians':medians,'paired_ratios':ratios,
             'binary_sha256':hashlib.sha256(args.cetta.read_bytes()).hexdigest(),
             'original_source_sha256':hashlib.sha256(original.read_bytes()).hexdigest(),
             'learned_source_sha256':hashlib.sha256(learned.read_bytes()).hexdigest(),
             'bulk_comparison':None,'bulk_omission':'no same-contract bulk selector interface is supplied',
             'retained_memory':None,'retention_omission':'child peak RSS is not retained heap accounting',
             'compiler_admission_verified':False,
             'scope':'whole actual selector, original and frozen learned library, default and authored-reference routes; no kernel speedup claim'}
    (args.output/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print(json.dumps({'passed':True,'medians':medians,'paired_ratios':ratios}))
    return 0


if __name__=='__main__':
    raise SystemExit(main())
