#!/usr/bin/env python3
"""Paired cost panel for an actual learned PLN arithmetic fragment."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import statistics
import subprocess
import time

from check_binary_codec import ROOT, parse_forms
from gen_semiring_fixtures import sexpr


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cetta',type=Path,required=True)
    parser.add_argument('--stats-cetta',type=Path)
    parser.add_argument('--library-source',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--rows',type=int,default=512)
    parser.add_argument('--pairs',type=int,default=5)
    parser.add_argument('--repeats',type=int,default=4)
    args=parser.parse_args()
    if min(args.rows,args.pairs,args.repeats)<1:parser.error('rows, pairs and repeats must be positive')
    args.output.mkdir(parents=True,exist_ok=True)
    forms=parse_forms(args.library_source.read_text());definition=forms[0]
    assert definition[0]=='=' and len(definition[1])==4
    name=definition[1][0]
    expected_pattern=['/', ['*', ['+', '1.0', ['/', '$p0', '$p1']], '$p2'], ['+', '1.0', '$p2']]
    assert definition[2]==expected_pattern, 'the selected learned fragment changed; inspect before measuring'
    x=[0.25+(i%13)/16.0 for i in range(args.rows)]
    y=[0.5+(i%7)/16.0 for i in range(args.rows)]
    z=[0.125+(i%11)/16.0 for i in range(args.rows)]
    expected=sum(((1.0+a/b)*c)/(1.0+c) for a,b,c in zip(x,y,z))
    ratio='(/ (* (+ 1.0 (/ $x $y)) $z) (+ 1.0 $z))'
    data='('+' '.join(f'({a!r} {b!r} {c!r})' for a,b,c in zip(x,y,z))+')'
    vector=lambda values:f'(tensor:from-values f64 ({len(values)}) ('+' '.join(map(repr,values))+'))'
    paths={}
    for variant in ('original','learned-raw','learned-prepared','bulk'):
        if variant=='bulk':
            inputs=f'({vector(x)} {vector(y)} {vector(z)})'
            body='(tensor:sum (tensor:zip-map f64 $inputs bench:bulk-ratio ()) sequential)'
            loop=''
        else:
            inputs=data
            expression=ratio if variant=='original' else f'({name} $x $y $z)'
            # An explicit ordered loop avoids the separate prepared-fold
            # compiler, which is not disabled by the closed-call reference
            # switch. The raw-helper sample must actually exercise calls.
            body='(bench:loop $inputs 0.0)'
            loop=f'''(= (bench:loop $inputs $total)
              (if (== $inputs ()) $total
                (let* (($row (car-atom $inputs)) ($x (index-atom $row 0))
                        ($y (index-atom $row 1)) ($z (index-atom $row 2)))
                   (bench:loop (cdr-atom $inputs) (+ $total {expression})))))'''
        helper=sexpr(definition) if variant.startswith('learned-') else ''
        bulk_definition=f'(= (bench:bulk-ratio $x $y $z) {ratio})' if variant=='bulk' else ''
        source=f'''!(import! &self (library system))
!(import! &self (library tensor))
{helper}
{bulk_definition}
{loop}
(= (bench:run $inputs) {body})
(= (bench:repeat $count $inputs)
   (if (== $count 0) 0.0 (+ (bench:run $inputs) (bench:repeat (- $count 1) $inputs))))
!(let* (($start (system:monotonic-ns)) ($inputs {inputs}) ($ready (system:monotonic-ns))
        ($first (bench:run $inputs)) ($first-end (system:monotonic-ns))
        ($repeated (bench:repeat {args.repeats} $inputs)) ($end (system:monotonic-ns)))
   (println! (LearnedCost (- $ready $start)
              (- $first-end $ready) $first (- $end $first-end) $repeated)))
'''
        path=args.output/f'{variant}.metta';path.write_text(source);paths[variant]=path
    def execute(binary, variant, run_id, instrumentation=False):
        environment=os.environ.copy()
        if variant=='learned-raw':environment['CETTA_PREPARED_PURE_REFERENCE']='1'
        else:environment.pop('CETTA_PREPARED_PURE_REFERENCE',None)
        output=args.output/f'{run_id}-{variant}.log';errors=args.output/f'{run_id}-{variant}.stderr'
        command=[str(binary.resolve()),'--lang','petta']
        if instrumentation:command.append('--emit-runtime-stats')
        command.append(str(paths[variant].resolve()))
        started=time.perf_counter()
        with output.open('w') as stdout,errors.open('w') as stderr:
            process=subprocess.Popen(command,cwd=ROOT,env=environment,stdout=stdout,stderr=stderr)
            _pid,status,usage=os.wait4(process.pid,0)
            process.returncode=os.waitstatus_to_exitcode(status)
        elapsed=time.perf_counter()-started
        observations=[form for form in parse_forms(output.read_text()) if isinstance(form,list) and form and form[0]=='LearnedCost']
        if process.returncode or len(observations)!=1:
            raise RuntimeError(f'{variant} did not complete: {errors.read_text()[-1500:]} {output.read_text()[-1500:]}')
        record=observations[0]
        assert len(record)==6,record
        first=float(record[3]);repeated=float(record[5])
        assert math.isclose(first,expected,rel_tol=1e-12,abs_tol=1e-12), (variant,first,expected)
        assert math.isclose(repeated,expected*args.repeats,rel_tol=1e-12,abs_tol=1e-12)
        counters={}
        for line in errors.read_text().splitlines():
            if line.startswith('runtime-counter '):
                _prefix,key,value=line.split();counters[key]=int(value)
        if instrumentation and not any(counters.values()):raise RuntimeError('runtime statistics were not enabled')
        if instrumentation and variant=='learned-raw':
            assert counters.get('prepared-pure-call-commit',0)==0, 'the reference sample was prepared'
        if instrumentation and variant=='learned-prepared':
            assert counters.get('prepared-pure-call-commit',0)>0, 'ordinary preparation did not admit the helper loop'
        return {'variant':variant,'sample':run_id,'wall_seconds':elapsed,'input_preparation_ns':int(record[1]),
                'first_query_ns':int(record[2]),'repeated_query_ns':int(record[4])/args.repeats,
                'peak_rss_kib':usage.ru_maxrss,'user_seconds':usage.ru_utime,'system_seconds':usage.ru_stime,
                'instrumented':instrumentation,'counters':counters}
    samples=[]
    variants=list(paths)
    for pair in range(args.pairs):
        for variant in variants if pair%2==0 else reversed(variants):
            samples.append(execute(args.cetta,variant,f'pair-{pair}'))
    instrumented=[execute(args.stats_cetta,variant,'counters',True) for variant in variants] if args.stats_cetta else []
    summary={variant:{metric:statistics.median(row[metric] for row in samples if row['variant']==variant)
                      for metric in ('wall_seconds','input_preparation_ns','first_query_ns','repeated_query_ns','peak_rss_kib')}
             for variant in variants}
    paired_ratios={'prepared_over_inline':[],'raw_over_prepared':[],'bulk_over_inline':[]}
    for pair in range(args.pairs):
        values={row['variant']:row['repeated_query_ns'] for row in samples if row['sample']==f'pair-{pair}'}
        paired_ratios['prepared_over_inline'].append(values['learned-prepared']/values['original'])
        paired_ratios['raw_over_prepared'].append(values['learned-raw']/values['learned-prepared'])
        paired_ratios['bulk_over_inline'].append(values['bulk']/values['original'])
    result={'passed':True,'rows':args.rows,'pairs':args.pairs,'repeats':args.repeats,'expected':expected,
            'medians':summary,'samples':samples,'instrumented':instrumented,'paired_repeated_ratios':paired_ratios,
            'binary_sha256':hashlib.sha256(args.cetta.read_bytes()).hexdigest(),
            'stats_binary_sha256':hashlib.sha256(args.stats_cetta.read_bytes()).hexdigest() if args.stats_cetta else None,
            'scope':'one extracted real-source fragment; original production, raw helper, ordinarily prepared helper and packed bulk',
            'retention_scope':'arena ledger gauges at completed execution before engine cleanup, not total retained native heap',
            'timing_note':'instrumented counters are separate observations and not included in wall-time medians; imports and process/load costs remain charged in whole wall time, not independently isolated',
            'learning_cost_included':False,'held_out_program_reuse_qualified':False}
    (args.output/'receipt.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({'passed':True,'medians':summary,'instrumented':bool(instrumented)}))


if __name__=='__main__': raise SystemExit(main())
