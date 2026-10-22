#!/usr/bin/env python3
"""Matched public scalar pipelines: prepare, work, process time and peak RSS."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import statistics
import subprocess
import time

ROOT=Path(__file__).resolve().parents[2]

def measure(binary,program,reference):
    env=dict(os.environ,CETTA_PREPARED_PURE_SCALAR_TREE_REFERENCE='1' if reference else '0')
    started=time.perf_counter_ns()
    with program.with_suffix('.out').open('w') as out,program.with_suffix('.err').open('w') as err:
        child=subprocess.Popen([str(binary),'--lang','petta@1.0.6',str(program)],
            cwd=ROOT,env=env,stdout=out,stderr=err)
        peak=0
        while True:
            done,status,usage=os.wait4(child.pid,os.WNOHANG)
            if done: break
            try:
                if Path(os.readlink(f'/proc/{child.pid}/exe')).resolve()==binary:
                    status_text=Path(f'/proc/{child.pid}/status').read_text()
                    high=re.search(r'^VmHWM:\s+(\d+)\s+kB$',status_text,re.M)
                    if high: peak=max(peak,int(high[1]))
            except (FileNotFoundError,ProcessLookupError,PermissionError):
                pass
            time.sleep(0.001)
        child.returncode=os.waitstatus_to_exitcode(status)
    assert child.returncode==0,(child.returncode,program.with_suffix('.err').read_text())
    match=re.search(r'\(ScalarRegionBench (\d+) (\d+) (\d+) ([^)]+)\)',program.with_suffix('.out').read_text())
    assert match,program.with_suffix('.out').read_text()
    return dict(load_ns=int(match[1]),prepare_ns=int(match[2]),work_ns=int(match[3]),
        result=float(match[4]),whole_ns=time.perf_counter_ns()-started,
        sampled_peak_rss_kib=peak or None,launcher_inclusive_peak_rss_kib=usage.ru_maxrss,
        user_s=usage.ru_utime,system_s=usage.ru_stime)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cetta',type=Path,required=True)
    parser.add_argument('--before',type=Path)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--size',type=int,default=100000)
    parser.add_argument('--repeat',type=int,default=3)
    args=parser.parse_args()
    if min(args.size,args.repeat)<1: parser.error('positive size and repetitions required')
    args.output.mkdir(parents=True,exist_ok=False)
    values=[(i%128)/4 for i in range(args.size)]
    records=[{'score':x,'tag':i%8} for i,x in enumerate(values)]
    data=args.output/'records.json'
    data.write_text(json.dumps({'values':values,'records':records},separators=(',',':')))
    literals='(let (JsonArray $values) (json:parse-at $text ("values")) $values)'
    projection='(+ (* $x 0.5) 1.0)'
    rows=[]
    workloads=[
      ('float-sum',literals,'(list:sum-by scalar-projection $items)',sum(x*0.5+1.0 for x in values)),
      ('float-filter',literals,'(size-atom (filter-atom $items $x (> (+ (* $x 0.5) 1.0) 9.0)))',sum(x*0.5+1.0>9 for x in values)),
      ('json-filter','(let (JsonArray $records) (json:parse-at $text ("records")) $records)','(size-atom (filter-atom $items $row (> (+ (* (json:get $row "score") 0.5) 1.0) 9.0)))',sum(x['score']*0.5+1.0>9 for x in records)),
      ('float-recursion','()',f'(scalar-iterate {args.size} 0.0)',2.0 if args.size>60 else None),
      ('integer-recursion','()',f'(scalar-count {args.size} 0)',args.size),
    ]
    if workloads[-2][3] is None:
        value=0.0
        for _ in range(args.size): value=value*0.5+1.0
        workloads[-2]=(*workloads[-2][:3],value)
    variants={'fused':(args.cetta.resolve(),False),'unfused':(args.cetta.resolve(),True)}
    if args.before: variants={'before':(args.before.resolve(),False),**variants}
    for name,prepare,work,expected in workloads:
        program=args.output/(name+'.metta')
        program.write_text(f'''!(import! &self (library list))
!(import! &self json)
!(import! &self system)
!(import! &self fs)
(= (scalar-projection $x) {projection})
(= (scalar-iterate $n $x) (if (== $n 0) $x (scalar-iterate (- $n 1) (+ (* $x 0.5) 1.0))))
(= (scalar-count $n $x) (if (== $n 0) $x (scalar-count (- $n 1) (+ (* $x 1) 1))))
!(let* (($start (system:monotonic-ns)) ($text (fs:read-text {json.dumps(str(data.resolve()))}))
        ($loaded (system:monotonic-ns)) ($items {prepare})
        ($prepared (system:monotonic-ns)) ($result {work}) ($end (system:monotonic-ns)))
   (ScalarRegionBench (- $loaded $start) (- $prepared $loaded) (- $end $prepared) $result))
''')
        observations={key:[] for key in variants}
        for repetition in range(args.repeat):
            ordered=list(variants) if repetition%2==0 else list(reversed(variants))
            for key in ordered:
                binary,reference=variants[key]
                observation=measure(binary,program,reference)
                assert observation['result']==expected,(name,key,observation,expected)
                observations[key].append(observation)
        medians={key:{field:statistics.median(row[field] for row in samples)
            for field in ('load_ns','prepare_ns','work_ns','whole_ns')}
            for key,samples in observations.items()}
        row=dict(workload=name,size=args.size,expected=expected,all_results_equal=True,
            medians=medians,observations=observations)
        rows.append(row)
        print(json.dumps({'workload':name,'medians':medians}),flush=True)
    report={'binary_sha256':{key:hashlib.sha256(binary.read_bytes()).hexdigest()
                            for key,(binary,_) in variants.items()},
            'scope':'public PeTTa numerical and JSON pipelines; sequential dyadic fixtures',
            'size':args.size,'repetitions':args.repeat,'panels':rows}
    (args.output/'report.json').write_text(json.dumps(report,indent=2)+'\n')

if __name__=='__main__': main()
